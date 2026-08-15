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

/* ---------- Палитра ---------- */
#define COLOR_BG          lv_color_hex(0x0B0F0E)
#define COLOR_CARD        lv_color_hex(0x141A18)
#define COLOR_GOLD        lv_color_hex(0xD4AF37)
#define COLOR_GREEN_LIGHT lv_color_hex(0x4FAE7C)
#define COLOR_TEXT        lv_color_hex(0xF5F0E6)
#define COLOR_TEXT_DIM    lv_color_hex(0x8A8F8C)

/* ---------- Тасбих ---------- */
static uint32_t tasbih_count = 0;
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
  lv_label_set_text_fmt(tasbih_label, "%lu", tasbih_count);
}

/* ---------- Экран 1: Тасбих ---------- */
static void build_tasbih_screen(lv_obj_t *tile) {
  lv_obj_set_style_bg_color(tile, COLOR_BG, 0);
  lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);

  lv_obj_t *arc = lv_arc_create(tile);
  tasbih_arc = arc;
  lv_obj_set_size(arc, 200, 200);
  lv_obj_center(arc);
  lv_arc_set_rotation(arc, 270);
  lv_arc_set_bg_angles(arc, 0, 360);
  lv_arc_set_range(arc, 0, 33);
  lv_arc_set_value(arc, 0);
  lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
  lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_arc_color(arc, COLOR_CARD, LV_PART_MAIN);
  lv_obj_set_style_arc_width(arc, 10, LV_PART_MAIN);
  lv_obj_set_style_arc_color(arc, COLOR_GOLD, LV_PART_INDICATOR);
  lv_obj_set_style_arc_width(arc, 10, LV_PART_INDICATOR);

  tasbih_label = lv_label_create(tile);
  lv_obj_set_style_text_color(tasbih_label, COLOR_TEXT, 0);
  lv_obj_set_style_text_font(tasbih_label, &lv_font_montserrat_48, 0);
  lv_label_set_text(tasbih_label, "0");
  lv_obj_center(tasbih_label);

  lv_obj_t *hint = lv_label_create(tile);
  lv_obj_set_style_text_color(hint, COLOR_TEXT_DIM, 0);
  lv_obj_set_style_text_font(hint, &lv_font_montserrat_14, 0);
  lv_label_set_text(hint, "Коснитесь для отсчёта");
  lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -20);

  lv_obj_add_flag(tile, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(tile, tasbih_tap_cb, LV_EVENT_CLICKED, NULL);
}

/* ---------- Экран 2: Молитвы на сегодня ---------- */
struct PrayerRow { const char *name; const char *time; };
static PrayerRow prayers[] = {
  {"Fajr",    "04:32"},
  {"Dhuhr",   "12:15"},
  {"Asr",     "15:47"},
  {"Maghrib", "18:52"},
  {"Isha",    "20:20"},
};

static void build_prayers_screen(lv_obj_t *tile) {
  lv_obj_set_style_bg_color(tile, COLOR_BG, 0);
  lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
  lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(tile, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(tile, 6, 0);
  lv_obj_set_style_pad_top(tile, 14, 0);
  lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *title = lv_label_create(tile);
  lv_obj_set_style_text_color(title, COLOR_GOLD, 0);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
  lv_label_set_text(title, "Молитвы сегодня");

  for (auto &p : prayers) {
    lv_obj_t *card = lv_obj_create(tile);
    lv_obj_set_size(card, 200, 30);
    lv_obj_set_style_bg_color(card, COLOR_CARD, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 10, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *name = lv_label_create(card);
    lv_obj_set_style_text_color(name, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(name, &lv_font_montserrat_12, 0);
    lv_label_set_text(name, p.name);
    lv_obj_align(name, LV_ALIGN_LEFT_MID, 10, 0);

    lv_obj_t *time = lv_label_create(card);
    lv_obj_set_style_text_color(time, COLOR_GOLD, 0);
    lv_obj_set_style_text_font(time, &lv_font_montserrat_12, 0);
    lv_label_set_text(time, p.time);
    lv_obj_align(time, LV_ALIGN_RIGHT_MID, -10, 0);
  }
}

/* ---------- Экран 3: Обратный отсчёт ---------- */
static void build_countdown_screen(lv_obj_t *tile) {
  lv_obj_set_style_bg_color(tile, COLOR_BG, 0);
  lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
  lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(tile, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(tile, 20, 0);
  lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *card1 = lv_obj_create(tile);
  lv_obj_set_size(card1, 200, 100);
  lv_obj_set_style_bg_color(card1, COLOR_CARD, 0);
  lv_obj_set_style_radius(card1, 16, 0);
  lv_obj_set_style_border_width(card1, 0, 0);
  lv_obj_clear_flag(card1, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *l1 = lv_label_create(card1);
  lv_obj_set_style_text_color(l1, COLOR_TEXT_DIM, 0);
  lv_obj_set_style_text_font(l1, &lv_font_montserrat_12, 0);
  lv_label_set_text(l1, "До следующей молитвы");
  lv_obj_align(l1, LV_ALIGN_TOP_MID, 0, 12);

  lv_obj_t *l2 = lv_label_create(card1);
  lv_obj_set_style_text_color(l2, COLOR_GOLD, 0);
  lv_obj_set_style_text_font(l2, &lv_font_montserrat_32, 0);
  lv_label_set_text(l2, "01:24:07");
  lv_obj_align(l2, LV_ALIGN_CENTER, 0, 8);

  lv_obj_t *card2 = lv_obj_create(tile);
  lv_obj_set_size(card2, 200, 80);
  lv_obj_set_style_bg_color(card2, COLOR_CARD, 0);
  lv_obj_set_style_radius(card2, 16, 0);
  lv_obj_set_style_border_width(card2, 0, 0);
  lv_obj_clear_flag(card2, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *l3 = lv_label_create(card2);
  lv_obj_set_style_text_color(l3, COLOR_TEXT_DIM, 0);
  lv_obj_set_style_text_font(l3, &lv_font_montserrat_12, 0);
  lv_label_set_text(l3, "До Рамадана");
  lv_obj_align(l3, LV_ALIGN_TOP_MID, 0, 10);

  lv_obj_t *l4 = lv_label_create(card2);
  lv_obj_set_style_text_color(l4, COLOR_GREEN_LIGHT, 0);
  lv_obj_set_style_text_font(l4, &lv_font_montserrat_22, 0);
  lv_label_set_text(l4, "142 дня");
  lv_obj_align(l4, LV_ALIGN_CENTER, 0, 8);
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

  lv_obj_t *tile1 = lv_tileview_add_tile(tv, 0, 0, LV_DIR_HOR);
  lv_obj_t *tile2 = lv_tileview_add_tile(tv, 1, 0, LV_DIR_HOR);
  lv_obj_t *tile3 = lv_tileview_add_tile(tv, 2, 0, LV_DIR_HOR);

  build_tasbih_screen(tile1);
  build_prayers_screen(tile2);
  build_countdown_screen(tile3);

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
