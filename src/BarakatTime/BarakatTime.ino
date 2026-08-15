#include <lvgl.h>
#include <math.h>
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
#define COLOR_BG          lv_color_hex(0x0A130F)
#define COLOR_CARD        lv_color_hex(0x121C17)
#define COLOR_GOLD        lv_color_hex(0xD9AF56)
#define COLOR_GOLD_DIM    lv_color_hex(0x8A7038)
#define COLOR_GREEN_LIGHT lv_color_hex(0x4FAE7C)
#define COLOR_TEXT        lv_color_hex(0xF3EFE2)
#define COLOR_TEXT_DIM    lv_color_hex(0x7C857E)

/* ---------- Звёздный фон (общий для всех экранов) ---------- */
static lv_img_dsc_t star_img_dsc;
static lv_color_t *star_buf;

void init_starfield() {
  const int W = 240, H = 280;
  star_buf = (lv_color_t *)heap_caps_malloc(W * H * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
  for (int i = 0; i < W * H; i++) star_buf[i] = COLOR_BG;

  uint32_t seed = 12345;
  auto nextRand = [&]() { seed = seed * 1103515245u + 12345u; return (seed >> 16) & 0x7FFF; };

  for (int i = 0; i < 55; i++) {
    int x = nextRand() % W, y = nextRand() % H;
    uint8_t b = 110 + nextRand() % 140;
    star_buf[y * W + x] = lv_color_make(b, b, (uint8_t)(b * 0.85));
  }
  for (int i = 0; i < 6; i++) {
    int x = 6 + nextRand() % (W - 12), y = 6 + nextRand() % (H - 12);
    lv_color_t c = COLOR_GOLD;
    star_buf[y * W + x] = c;
    star_buf[y * W + x - 1] = c; star_buf[y * W + x + 1] = c;
    star_buf[(y - 1) * W + x] = c; star_buf[(y + 1) * W + x] = c;
  }

  star_img_dsc.header.always_zero = 0;
  star_img_dsc.header.w = W;
  star_img_dsc.header.h = H;
  star_img_dsc.header.cf = LV_IMG_CF_TRUE_COLOR;
  star_img_dsc.data_size = W * H * sizeof(lv_color_t);
  star_img_dsc.data = (const uint8_t *)star_buf;
}

static void add_bg(lv_obj_t *parent) {
  lv_obj_t *bg = lv_img_create(parent);
  lv_img_set_src(bg, &star_img_dsc);
  lv_obj_set_pos(bg, 0, 0);
  lv_obj_clear_flag(bg, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_move_background(bg);
}

/* ---------- Точки-индикатор страниц ---------- */
static void add_page_dots(lv_obj_t *parent, int total, int active_idx) {
  lv_obj_t *cont = lv_obj_create(parent);
  lv_obj_set_size(cont, LV_SIZE_CONTENT, 16);
  lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(cont, 0, 0);
  lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_align(cont, LV_ALIGN_BOTTOM_MID, 0, -8);
  lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(cont, 6, 0);
  for (int i = 0; i < total; i++) {
    lv_obj_t *dot = lv_obj_create(cont);
    lv_coord_t s = (i == active_idx) ? 7 : 5;
    lv_obj_set_size(dot, s, s);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dot, (i == active_idx) ? COLOR_TEXT : COLOR_TEXT_DIM, 0);
    lv_obj_set_style_bg_opa(dot, (i == active_idx) ? LV_OPA_COVER : LV_OPA_50, 0);
    lv_obj_set_style_border_width(dot, 0, 0);
    lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
  }
}

/* ---------- Стрелка "назад" ---------- */
static void add_back_chevron(lv_obj_t *parent) {
  lv_obj_t *l = lv_label_create(parent);
  lv_obj_set_style_text_color(l, COLOR_GOLD, 0);
  lv_obj_set_style_text_font(l, &lv_font_montserrat_20, 0);
  lv_label_set_text(l, "<");
  lv_obj_align(l, LV_ALIGN_TOP_LEFT, 10, 8);
}

/* ---------- Золотое кольцо (для мандалы) ---------- */
static lv_obj_t *create_ring(lv_obj_t *parent, lv_coord_t cx, lv_coord_t cy, lv_coord_t d, lv_color_t color, lv_coord_t width) {
  lv_obj_t *a = lv_arc_create(parent);
  lv_obj_set_size(a, d, d);
  lv_obj_set_pos(a, cx - d / 2, cy - d / 2);
  lv_arc_set_bg_angles(a, 0, 360);
  lv_arc_set_value(a, 0);
  lv_obj_remove_style(a, NULL, LV_PART_KNOB);
  lv_obj_clear_flag(a, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_bg_opa(a, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(a, 0, LV_PART_MAIN);
  lv_obj_set_style_arc_color(a, color, LV_PART_MAIN);
  lv_obj_set_style_arc_width(a, width, LV_PART_MAIN);
  lv_obj_set_style_arc_opa(a, LV_OPA_COVER, LV_PART_MAIN);
  return a;
}

static void create_flower(lv_obj_t *parent, lv_coord_t cx, lv_coord_t cy) {
  create_ring(parent, cx, cy, 66, COLOR_GOLD_DIM, 1);
  for (int i = 0; i < 6; i++) {
    float ang = i * (2 * (float)M_PI / 6);
    lv_coord_t x = cx + (lv_coord_t)(48 * cosf(ang));
    lv_coord_t y = cy + (lv_coord_t)(48 * sinf(ang));
    create_ring(parent, x, y, 66, COLOR_GOLD_DIM, 1);
  }
}

/* ---------- Тасбих ---------- */
static uint32_t tasbih_count = 0;
static lv_obj_t *tasbih_label;
static lv_obj_t *progress_ring;

static void tasbih_tap_cb(lv_event_t *e) {
  tasbih_count++;
  uint32_t pos = tasbih_count % 33;
  lv_arc_set_value(progress_ring, pos == 0 ? 33 : pos);
  lv_label_set_text_fmt(tasbih_label, "%lu", tasbih_count);
}

static void build_tasbih_screen(lv_obj_t *tile) {
  add_bg(tile);
  lv_coord_t cx = 120, cy = 130;

  create_flower(tile, cx, cy);

  progress_ring = lv_arc_create(tile);
  lv_obj_set_size(progress_ring, 190, 190);
  lv_obj_set_pos(progress_ring, cx - 95, cy - 95);
  lv_arc_set_rotation(progress_ring, 270);
  lv_arc_set_bg_angles(progress_ring, 0, 360);
  lv_arc_set_range(progress_ring, 0, 33);
  lv_arc_set_value(progress_ring, 0);
  lv_obj_remove_style(progress_ring, NULL, LV_PART_KNOB);
  lv_obj_clear_flag(progress_ring, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_bg_opa(progress_ring, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(progress_ring, 0, LV_PART_MAIN);
  lv_obj_set_style_arc_opa(progress_ring, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_arc_color(progress_ring, COLOR_GOLD, LV_PART_INDICATOR);
  lv_obj_set_style_arc_width(progress_ring, 4, LV_PART_INDICATOR);

  tasbih_label = lv_label_create(tile);
  lv_obj_set_style_text_color(tasbih_label, COLOR_TEXT, 0);
  lv_obj_set_style_text_font(tasbih_label, &lv_font_montserrat_48, 0);
  lv_label_set_text(tasbih_label, "0");
  lv_obj_align(tasbih_label, LV_ALIGN_TOP_MID, 0, cy - 24);

  add_page_dots(tile, 4, 0);

  lv_obj_add_flag(tile, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(tile, tasbih_tap_cb, LV_EVENT_CLICKED, NULL);
}

/* ---------- Ramadan (арка) ---------- */
static void build_ramadan_screen(lv_obj_t *tile) {
  add_bg(tile);

  static lv_point_t arch[] = {
    {30, 95}, {30, 62}, {58, 30}, {78, 55}, {95, 18},
    {120, 2}, {145, 18}, {162, 55}, {182, 30}, {210, 62}, {210, 95}
  };
  lv_obj_t *line = lv_line_create(tile);
  lv_line_set_points(line, arch, 11);
  lv_obj_set_style_line_color(line, COLOR_GOLD, 0);
  lv_obj_set_style_line_width(line, 3, 0);
  lv_obj_set_style_line_rounded(line, true, 0);
  lv_obj_clear_flag(line, LV_OBJ_FLAG_CLICKABLE);

  lv_obj_t *l1 = lv_label_create(tile);
  lv_obj_set_style_text_color(l1, COLOR_TEXT, 0);
  lv_obj_set_style_text_font(l1, &lv_font_montserrat_16, 0);
  lv_label_set_text(l1, "Ramadan —");
  lv_obj_align(l1, LV_ALIGN_TOP_MID, 0, 118);

  lv_obj_t *l2 = lv_label_create(tile);
  lv_obj_set_style_text_color(l2, COLOR_GOLD, 0);
  lv_obj_set_style_text_font(l2, &lv_font_montserrat_40, 0);
  lv_label_set_text(l2, "127 days");
  lv_obj_align(l2, LV_ALIGN_TOP_MID, 0, 142);

  lv_obj_t *bar = lv_bar_create(tile);
  lv_obj_set_size(bar, 180, 3);
  lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, 200);
  lv_obj_set_style_bg_color(bar, COLOR_CARD, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_bg_color(bar, COLOR_GOLD, LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_INDICATOR);
  lv_obj_set_style_radius(bar, 2, LV_PART_MAIN);
  lv_bar_set_range(bar, 0, 365);
  lv_bar_set_value(bar, 365 - 127, LV_ANIM_OFF);

  add_page_dots(tile, 4, 1);
}

/* ---------- Следующая молитва ---------- */
static void build_next_prayer_screen(lv_obj_t *tile) {
  add_bg(tile);
  add_back_chevron(tile);

  lv_obj_t *card = lv_obj_create(tile);
  lv_obj_set_size(card, 200, 90);
  lv_obj_center(card);
  lv_obj_set_style_bg_color(card, COLOR_CARD, 0);
  lv_obj_set_style_bg_opa(card, LV_OPA_90, 0);
  lv_obj_set_style_radius(card, 18, 0);
  lv_obj_set_style_border_width(card, 0, 0);
  lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *l1 = lv_label_create(card);
  lv_obj_set_style_text_color(l1, COLOR_TEXT_DIM, 0);
  lv_obj_set_style_text_font(l1, &lv_font_montserrat_12, 0);
  lv_label_set_text(l1, "Next Prayer — Maghrib");
  lv_obj_align(l1, LV_ALIGN_TOP_MID, 0, 14);

  lv_obj_t *l2 = lv_label_create(card);
  lv_obj_set_style_text_color(l2, COLOR_TEXT, 0);
  lv_obj_set_style_text_font(l2, &lv_font_montserrat_30, 0);
  lv_label_set_text(l2, "00:45:48");
  lv_obj_align(l2, LV_ALIGN_CENTER, 0, 10);

  add_page_dots(tile, 4, 2);
}

/* ---------- Список молитв ---------- */
struct PrayerRow { const char *name; const char *time; };
static PrayerRow prayers[] = {
  {"Fajr", "02:38"}, {"Dhuhr", "03:46"}, {"Asr", "01:36"},
  {"Maghrib", "02:49"}, {"Isha", "08:38"},
};

static void build_prayers_screen(lv_obj_t *tile) {
  add_bg(tile);
  add_back_chevron(tile);

  lv_obj_t *list = lv_obj_create(tile);
  lv_obj_set_size(list, 210, 240);
  lv_obj_align(list, LV_ALIGN_TOP_MID, 0, 34);
  lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(list, 0, 0);
  lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(list, 8, 0);
  lv_obj_clear_flag(list, LV_OBJ_FLAG_SCROLLABLE);

  for (auto &p : prayers) {
    lv_obj_t *row = lv_obj_create(list);
    lv_obj_set_size(row, 200, 42);
    lv_obj_set_style_bg_color(row, COLOR_CARD, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_80, 0);
    lv_obj_set_style_radius(row, 12, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *name = lv_label_create(row);
    lv_obj_set_style_text_color(name, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(name, &lv_font_montserrat_14, 0);
    lv_label_set_text(name, p.name);
    lv_obj_align(name, LV_ALIGN_LEFT_MID, 14, 0);

    lv_obj_t *time = lv_label_create(row);
    lv_obj_set_style_text_color(time, COLOR_GOLD, 0);
    lv_obj_set_style_text_font(time, &lv_font_montserrat_14, 0);
    lv_label_set_text(time, p.time);
    lv_obj_align(time, LV_ALIGN_RIGHT_MID, -14, 0);
  }

  add_page_dots(tile, 4, 3);
}

/* ---------- Заставка ---------- */
static lv_obj_t *g_tileview;

static void splash_timer_cb(lv_timer_t *t) {
  lv_scr_load_anim(g_tileview, LV_SCR_LOAD_ANIM_FADE_IN, 500, 0, true);
  lv_timer_del(t);
}

static lv_obj_t *build_splash_screen() {
  lv_obj_t *scr = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(scr, COLOR_BG, 0);
  add_bg(scr);

  create_flower(scr, 120, 120);

  lv_obj_t *title = lv_label_create(scr);
  lv_obj_set_style_text_color(title, COLOR_GOLD, 0);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_22, 0);
  lv_label_set_text(title, "BarakatTime");
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 190);

  return scr;
}

/* ---------- Дисплей / тач / LVGL ---------- */
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

  init_starfield();

  g_tileview = lv_tileview_create(NULL);
  lv_obj_set_style_bg_color(g_tileview, COLOR_BG, 0);

  lv_obj_t *t1 = lv_tileview_add_tile(g_tileview, 0, 0, LV_DIR_HOR);
  lv_obj_t *t2 = lv_tileview_add_tile(g_tileview, 1, 0, LV_DIR_HOR);
  lv_obj_t *t3 = lv_tileview_add_tile(g_tileview, 2, 0, LV_DIR_HOR);
  lv_obj_t *t4 = lv_tileview_add_tile(g_tileview, 3, 0, LV_DIR_HOR);

  build_tasbih_screen(t1);
  build_ramadan_screen(t2);
  build_next_prayer_screen(t3);
  build_prayers_screen(t4);

  lv_obj_t *splash = build_splash_screen();
  lv_scr_load(splash);
  lv_timer_create(splash_timer_cb, 1800, NULL);

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
