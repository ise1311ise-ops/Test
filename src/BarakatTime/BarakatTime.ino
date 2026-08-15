#include <lvgl.h>
#include "Arduino_GFX_Library.h"
#include "Arduino_DriveBus_Library.h"
#include "pin_config.h"
#include "lv_conf.h"
#include "HWCDC.h"
#include "image.h"

HWCDC USBSerial;

Arduino_DataBus *bus = new Arduino_ESP32SPI(LCD_DC, LCD_CS, LCD_SCK, LCD_MOSI);
Arduino_GFX *gfx = new Arduino_ST7789(bus, LCD_RST, 0, true, LCD_WIDTH, LCD_HEIGHT, 0, 20, 0, 0);

std::shared_ptr<Arduino_IIC_DriveBus> IIC_Bus =
  std::make_shared<Arduino_HWIIC>(IIC_SDA, IIC_SCL, &Wire);

void Arduino_IIC_Touch_Interrupt(void);

std::unique_ptr<Arduino_IIC> CST816T(new Arduino_CST816x(IIC_Bus, CST816T_DEVICE_ADDRESS,
                                                         TP_RST, TP_INT, Arduino_IIC_Touch_Interrupt));

void Arduino_IIC_Touch_Interrupt(void) {
  CST816T->IIC_Interrupt_Flag = true;
}

#define EXAMPLE_LVGL_TICK_PERIOD_MS 2

uint32_t screenWidth;
uint32_t screenHeight;
static lv_disp_draw_buf_t draw_buf;

/* ---------- Точная палитра с макета ---------- */
#define COLOR_BG          lv_color_hex(0x0B0F0E)
#define COLOR_CARD        lv_color_hex(0x141A18)
#define COLOR_GOLD        lv_color_hex(0xD4AF37)
#define COLOR_GREEN_LIGHT lv_color_hex(0x4FAE7C)
#define COLOR_TEXT        lv_color_hex(0xF5F0E6)
#define COLOR_TEXT_DIM    lv_color_hex(0x8A8F8C)

/* ---------- Тасбих ---------- */
static uint32_t tasbih_count = 33;
static lv_obj_t *tasbih_label;
static lv_obj_t *tasbih_arc;

void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
  uint32_t w = (area->x2 - area->x1 + 1);
  uint32_t h = (area->y2 - area->y1 + 1);
#if (LV_COLOR_16_SWAP != 0)
  gfx->draw16bitBeRGBBitmap(area->x1, area->y1, (uint16_t *)&color_p->full, w, h);
#else
  gfx->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t *)&color_p->full, w, h);
#endif
  lv_disp_flush_ready(disp);
}

void example_increase_lvgl_tick(void *arg) {
  lv_tick_inc(EXAMPLE_LVGL_TICK_PERIOD_MS);
}

void my_touchpad_read(lv_indev_drv_t *indev_driver, lv_indev_data_t *data) {
  int32_t touchX = CST816T->IIC_Read_Device_Value(CST816T->Arduino_IIC_Touch::Value_Information::TOUCH_COORDINATE_X);
  int32_t touchY = CST816T->IIC_Read_Device_Value(CST816T->Arduino_IIC_Touch::Value_Information::TOUCH_COORDINATE_Y);

  if (CST816T->IIC_Interrupt_Flag == true) {
    CST816T->IIC_Interrupt_Flag = false;
    data->state = LV_INDEV_STATE_PR;
    if (touchX >= 0 && touchY >= 0) {
      data->point.x = touchX;
      data->point.y = touchY;
    }
  } else {
    data->state = LV_INDEV_STATE_REL;
  }
}

/* ---------- Тап по тасбиху ---------- */
static void tasbih_tap_cb(lv_event_t *e) {
  tasbih_count++;
  uint32_t pos = tasbih_count % 33;
  lv_arc_set_value(tasbih_arc, pos == 0 ? 33 : pos);
  lv_label_set_text_fmt(tasbih_label, "%lu", pos == 0 ? 33 : pos);
}

/* Общий фон экрана */
static void set_screen_background(lv_obj_t *tile) {
  lv_obj_set_style_bg_color(tile, COLOR_BG, 0);
  lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
}

/* Создание точек пагинации внизу */
static void add_pagination_dots(lv_obj_t *tile, int active_index) {
  lv_obj_t *dots_cont = lv_obj_create(tile);
  lv_obj_set_size(dots_cont, 80, 15);
  lv_obj_align(dots_cont, LV_ALIGN_BOTTOM_MID, 0, -8);
  lv_obj_set_style_bg_opa(dots_cont, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(dots_cont, 0, 0);
  lv_obj_clear_flag(dots_cont, LV_OBJ_FLAG_SCROLLABLE);

  for (int i = 0; i < 5; i++) {
    lv_obj_t *dot = lv_obj_create(dots_cont);
    lv_obj_set_size(dot, 5, 5);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(dot, 0, 0);
    lv_obj_set_style_bg_color(dot, (i == active_index) ? COLOR_TEXT : COLOR_TEXT_DIM, 0);
    lv_obj_align(dot, LV_ALIGN_LEFT_MID, i * 14 + 5, 0);
  }
}

/* ---------- Экран 1: Главная (BarakatTime) ---------- */
static void build_home_screen(lv_obj_t *tile) {
  set_screen_background(tile);

  lv_obj_t *title = lv_label_create(tile);
  lv_obj_set_style_text_color(title, COLOR_TEXT, 0);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_22, 0);
  lv_label_set_text(title, "BarakatTime");
  lv_obj_align(title, LV_ALIGN_CENTER, 0, 25);

  add_pagination_dots(tile, 0);
}

/* ---------- Экран 2: Тасбих ---------- */
static void build_tasbih_screen(lv_obj_t *tile) {
  set_screen_background(tile);

  lv_obj_t *arc = lv_arc_create(tile);
  tasbih_arc = arc;
  lv_obj_set_size(arc, 200, 200);
  lv_obj_center(arc);
  lv_arc_set_rotation(arc, 270);
  lv_arc_set_bg_angles(arc, 0, 360);
  lv_arc_set_range(arc, 0, 33);
  lv_arc_set_value(arc, 33);
  lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
  lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_arc_color(arc, COLOR_CARD, LV_PART_MAIN);
  lv_obj_set_style_arc_width(arc, 2, LV_PART_MAIN);
  lv_obj_set_style_arc_color(arc, COLOR_GOLD, LV_PART_INDICATOR);
  lv_obj_set_style_arc_width(arc, 3, LV_PART_INDICATOR);

  tasbih_label = lv_label_create(tile);
  lv_obj_set_style_text_color(tasbih_label, COLOR_TEXT, 0);
  lv_obj_set_style_text_font(tasbih_label, &lv_font_montserrat_48, 0);
  lv_label_set_text(tasbih_label, "33");
  lv_obj_center(tasbih_label);

  lv_obj_add_flag(tile, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(tile, tasbih_tap_cb, LV_EVENT_CLICKED, NULL);

  add_pagination_dots(tile, 1);
}

/* ---------- Экран 3: Рамадан ---------- */
static void build_countdown_screen(lv_obj_t *tile) {
  set_screen_background(tile);

  lv_obj_t *card = lv_obj_create(tile);
  lv_obj_set_size(card, 210, 160);
  lv_obj_center(card);
  lv_obj_set_style_bg_color(card, COLOR_CARD, 0);
  lv_obj_set_style_bg_opa(card, LV_OPA_60, 0);
  lv_obj_set_style_radius(card, 16, 0);
  lv_obj_set_style_border_width(card, 0, 0);
  lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *l_title = lv_label_create(card);
  lv_obj_set_style_text_color(l_title, COLOR_TEXT_DIM, 0);
  lv_obj_set_style_text_font(l_title, &lv_font_montserrat_14, 0);
  lv_label_set_text(l_title, "Ramadan —");
  lv_obj_align(l_title, LV_ALIGN_TOP_MID, 0, 25);

  lv_obj_t *l_days = lv_label_create(card);
  lv_obj_set_style_text_color(l_days, COLOR_GOLD, 0);
  lv_obj_set_style_text_font(l_days, &lv_font_montserrat_32, 0);
  lv_label_set_text(l_days, "127 days");
  lv_obj_align(l_days, LV_ALIGN_CENTER, 0, -5);

  lv_obj_t *bar = lv_bar_create(card);
  lv_obj_set_size(bar, 170, 6);
  lv_obj_align(bar, LV_ALIGN_BOTTOM_MID, 0, -18);
  lv_bar_set_value(bar, 60, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(bar, COLOR_BG, LV_PART_MAIN);
  lv_obj_set_style_bg_color(bar, COLOR_GOLD, LV_PART_INDICATOR);

  add_pagination_dots(tile, 2);
}

/* ---------- Экран 4: Следующая молитва ---------- */
static void build_next_prayer_screen(lv_obj_t *tile) {
  set_screen_background(tile);

  // Кнопка назад (стрелка)
  lv_obj_t *back_btn = lv_label_create(tile);
  lv_obj_set_style_text_color(back_btn, COLOR_GOLD, 0);
  lv_obj_set_style_text_font(back_btn, &lv_font_montserrat_16, 0);
  lv_label_set_text(back_btn, "<");
  lv_obj_align(back_btn, LV_ALIGN_TOP_LEFT, 20, 15);

  lv_obj_t *card = lv_obj_create(tile);
  lv_obj_set_size(card, 215, 115);
  lv_obj_center(card);
  lv_obj_set_style_bg_color(card, COLOR_CARD, 0);
  lv_obj_set_style_bg_opa(card, LV_OPA_90, 0);
  lv_obj_set_style_radius(card, 16, 0);
  lv_obj_set_style_border_width(card, 0, 0);
  lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *l1 = lv_label_create(card);
  lv_obj_set_style_text_color(l1, COLOR_TEXT, 0);
  lv_obj_set_style_text_font(l1, &lv_font_montserrat_14, 0);
  lv_label_set_text(l1, "Next Prayer — Maghrib");
  lv_obj_align(l1, LV_ALIGN_TOP_MID, 0, 12);

  lv_obj_t *l2 = lv_label_create(card);
  lv_obj_set_style_text_color(l2, COLOR_TEXT, 0);
  lv_obj_set_style_text_font(l2, &lv_font_montserrat_28, 0);
  lv_label_set_text(l2, "00:45:48");
  lv_obj_align(l2, LV_ALIGN_CENTER, 0, 10);

  add_pagination_dots(tile, 3);
}

/* ---------- Экран 5: Список молитв ---------- */
struct PrayerRow { const char *name; const char *time; };
static PrayerRow prayers[] = {
  {"Fajr",    "02:38"},
  {"Dhuhr",   "03:46"},
  {"Asr",     "01:36"},
  {"Maghrib", "02:49"},
  {"Isha",    "08:38"},
};

static void build_prayers_screen(lv_obj_t *tile) {
  set_screen_background(tile);

  lv_obj_t *back_btn = lv_label_create(tile);
  lv_obj_set_style_text_color(back_btn, COLOR_GOLD, 0);
  lv_obj_set_style_text_font(back_btn, &lv_font_montserrat_16, 0);
  lv_label_set_text(back_btn, "<");
  lv_obj_align(back_btn, LV_ALIGN_TOP_LEFT, 20, 15);

  lv_obj_t *card = lv_obj_create(tile);
  lv_obj_set_size(card, 215, 185);
  lv_obj_center(card);
  lv_obj_set_style_bg_color(card, COLOR_CARD, 0);
  lv_obj_set_style_bg_opa(card, LV_OPA_90, 0);
  lv_obj_set_style_radius(card, 16, 0);
  lv_obj_set_style_border_width(card, 0, 0);
  lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(card, 6, 0);
  lv_obj_set_style_pad_all(card, 10, 0);
  lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

  for (auto &p : prayers) {
    lv_obj_t *row = lv_obj_create(card);
    lv_obj_set_size(row, 195, 26);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *name = lv_label_create(row);
    lv_obj_set_style_text_color(name, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(name, &lv_font_montserrat_14, 0);
    lv_label_set_text(name, p.name);
    lv_obj_align(name, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_t *time = lv_label_create(row);
    lv_obj_set_style_text_color(time, COLOR_GOLD, 0);
    lv_obj_set_style_text_font(time, &lv_font_montserrat_14, 0);
    lv_label_set_text(time, p.time);
    lv_obj_align(time, LV_ALIGN_RIGHT_MID, 0, 0);
  }

  add_pagination_dots(tile, 4);
}

void setup() {
  USBSerial.begin(115200);

  while (CST816T->begin() == false) {
    USBSerial.println("CST816T initialization fail");
    delay(2000);
  }

  CST816T->IIC_Write_Device_State(CST816T->Arduino_IIC_Touch::Device::TOUCH_DEVICE_INTERRUPT_MODE,
                                  CST816T->Arduino_IIC_Touch::Device_Mode::TOUCH_DEVICE_INTERRUPT_PERIODIC);

  gfx->begin();
  pinMode(LCD_BL, OUTPUT);
  digitalWrite(LCD_BL, HIGH);

  screenWidth = gfx->width();
  screenHeight = gfx->height();

  lv_init();

  lv_color_t *buf1 = (lv_color_t *)heap_caps_malloc(screenWidth * screenHeight / 4 * sizeof(lv_color_t), MALLOC_CAP_DMA);
  lv_color_t *buf2 = (lv_color_t *)heap_caps_malloc(screenWidth * screenHeight / 4 * sizeof(lv_color_t), MALLOC_CAP_DMA);

  lv_disp_draw_buf_init(&draw_buf, buf1, buf2, screenWidth * screenHeight / 4);

  static lv_disp_drv_t disp_drv;
  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res = screenWidth;
  disp_drv.ver_res = screenHeight;
  disp_drv.flush_cb = my_disp_flush;
  disp_drv.draw_buf = &draw_buf;
  lv_disp_drv_register(&disp_drv);

  static lv_indev_drv_t indev_drv;
  lv_indev_drv_init(&indev_drv);
  indev_drv.type = LV_INDEV_TYPE_POINTER;
  indev_drv.read_cb = my_touchpad_read;
  lv_indev_drv_register(&indev_drv);

  lv_obj_t *tv = lv_tileview_create(lv_scr_act());
  lv_obj_set_style_bg_color(tv, COLOR_BG, 0);

  lv_obj_t *tile0 = lv_tileview_add_tile(tv, 0, 0, LV_DIR_HOR);
  lv_obj_t *tile1 = lv_tileview_add_tile(tv, 1, 0, LV_DIR_HOR);
  lv_obj_t *tile2 = lv_tileview_add_tile(tv, 2, 0, LV_DIR_HOR);
  lv_obj_t *tile3 = lv_tileview_add_tile(tv, 3, 0, LV_DIR_HOR);
  lv_obj_t *tile4 = lv_tileview_add_tile(tv, 4, 0, LV_DIR_HOR);

  build_home_screen(tile0);
  build_tasbih_screen(tile1);
  build_countdown_screen(tile2);
  build_next_prayer_screen(tile3);
  build_prayers_screen(tile4);

  const esp_timer_create_args_t lvgl_tick_timer_args = {
    .callback = &example_increase_lvgl_tick,
    .name = "lvgl_tick"
  };
  esp_timer_handle_t lvgl_tick_timer = NULL;
  esp_timer_create(&lvgl_tick_timer_args, &lvgl_tick_timer);
  esp_timer_start_periodic(lvgl_tick_timer, EXAMPLE_LVGL_TICK_PERIOD_MS * 1000);
}

void loop() {
  lv_timer_handler();
  delay(5);
}
