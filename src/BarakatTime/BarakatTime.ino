#include <lvgl.h>
#include "Arduino_GFX_Library.h"
#include "Arduino_DriveBus_Library.h"
#include "pin_config.h"
#include "lv_conf.h"
#include "HWCDC.h"
#include "image.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <time.h>

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

/* ---------- WiFi / настройка ---------- */
Preferences prefs;
WebServer server(80);
DNSServer dnsServer;
const byte DNS_PORT = 53;
IPAddress apIP(192, 168, 4, 1);
bool ap_mode_active = false;

String cfg_ssid = "";
String cfg_pass = "";
String cfg_city = "";
String cfg_country = "";
int cfg_method = 3;

/* ---------- Данные молитв (реальные, с Aladhan, либо заглушки) ---------- */
String prayer_names[5]     = {"Fajr", "Dhuhr", "Asr", "Maghrib", "Isha"};
String prayer_times_str[5] = {"04:32", "12:15", "15:47", "18:52", "20:20"};
String next_prayer_name = "Maghrib";
String next_prayer_time = "18:52";
int ramadan_days = 142;

bool time_synced = false;
time_t next_prayer_epoch = 0;
static lv_obj_t *countdown_time_label = NULL;

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

  for (int i = 0; i < 5; i++) {
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
    lv_label_set_text(name, prayer_names[i].c_str());
    lv_obj_align(name, LV_ALIGN_LEFT_MID, 10, 0);

    lv_obj_t *time = lv_label_create(card);
    lv_obj_set_style_text_color(time, COLOR_GOLD, 0);
    lv_obj_set_style_text_font(time, &lv_font_montserrat_12, 0);
    lv_label_set_text(time, prayer_times_str[i].c_str());
    lv_obj_align(time, LV_ALIGN_RIGHT_MID, -10, 0);
  }
}

/* ---------- Экран 3: Обратный отсчёт ---------- */
static void countdown_tick_cb(lv_timer_t *t) {
  if (!time_synced || countdown_time_label == NULL) return;
  time_t now = time(nullptr);
  long diff = (long)(next_prayer_epoch - now);
  if (diff < 0) diff = 0;
  int hh = diff / 3600;
  int mm = (diff % 3600) / 60;
  int ss = diff % 60;
  lv_label_set_text_fmt(countdown_time_label, "%02d:%02d:%02d", hh, mm, ss);
}

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
  lv_label_set_text_fmt(l1, "До %s", next_prayer_name.c_str());
  lv_obj_align(l1, LV_ALIGN_TOP_MID, 0, 12);

  lv_obj_t *l2 = lv_label_create(card1);
  lv_obj_set_style_text_color(l2, COLOR_GOLD, 0);
  lv_obj_set_style_text_font(l2, &lv_font_montserrat_32, 0);
  lv_label_set_text(l2, time_synced ? "--:--:--" : next_prayer_time.c_str());
  lv_obj_align(l2, LV_ALIGN_CENTER, 0, 8);
  countdown_time_label = l2;

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
  lv_label_set_text_fmt(l4, "%d дня", ramadan_days);
  lv_obj_align(l4, LV_ALIGN_CENTER, 0, 8);

  if (time_synced) {
    lv_timer_create(countdown_tick_cb, 1000, NULL);
  }
}

/* ---------- Boot-заставка (анимированная, поверх интерфейса) ---------- */
static lv_obj_t *splash_scr;

static void splash_opa_anim_cb(void *var, int32_t v) {
  lv_obj_set_style_opa((lv_obj_t *)var, (lv_opa_t)v, 0);
}

static void splash_fade_out_ready_cb(lv_anim_t *a) {
  lv_obj_del(splash_scr);
  splash_scr = NULL;
}

static void splash_start_fade_out(lv_timer_t *t) {
  lv_timer_del(t);
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, splash_scr);
  lv_anim_set_values(&a, 255, 0);
  lv_anim_set_time(&a, 600);
  lv_anim_set_exec_cb(&a, splash_opa_anim_cb);
  lv_anim_set_ready_cb(&a, splash_fade_out_ready_cb);
  lv_anim_start(&a);
}

static void build_splash_screen(lv_obj_t *parent_layer) {
  splash_scr = lv_obj_create(parent_layer);
  lv_obj_remove_style_all(splash_scr);
  lv_obj_set_size(splash_scr, LCD_WIDTH, LCD_HEIGHT);
  lv_obj_set_pos(splash_scr, 0, 0);
  lv_obj_set_style_bg_color(splash_scr, COLOR_BG, 0);
  lv_obj_set_style_bg_opa(splash_scr, LV_OPA_COVER, 0);
  lv_obj_clear_flag(splash_scr, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_opa(splash_scr, LV_OPA_TRANSP, 0);

  lv_obj_t *title = lv_label_create(splash_scr);
  lv_obj_set_style_text_color(title, COLOR_GOLD, 0);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_22, 0);
  lv_label_set_text(title, "BarakatTime");
  lv_obj_center(title);

  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, splash_scr);
  lv_anim_set_values(&a, 0, 255);
  lv_anim_set_time(&a, 600);
  lv_anim_set_exec_cb(&a, splash_opa_anim_cb);
  lv_anim_start(&a);

  lv_timer_t *hold_timer = lv_timer_create(splash_start_fade_out, 1600, NULL);
  lv_timer_set_repeat_count(hold_timer, 1);
}

/* ---------- Настройки: чтение сохранённой конфигурации ---------- */
bool load_config() {
  prefs.begin("barakat", true);
  bool configured = prefs.getBool("configured", false);
  cfg_ssid = prefs.getString("ssid", "");
  cfg_pass = prefs.getString("pass", "");
  cfg_city = prefs.getString("city", "");
  cfg_country = prefs.getString("country", "");
  cfg_method = prefs.getInt("method", 3);
  prefs.end();
  return configured;
}

/* ---------- Экран "режим настройки" на дисплее ---------- */
static void build_setup_ap_screen() {
  lv_obj_t *scr = lv_scr_act();
  lv_obj_set_style_bg_color(scr, COLOR_BG, 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

  lv_obj_t *title = lv_label_create(scr);
  lv_obj_set_style_text_color(title, COLOR_GOLD, 0);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_22, 0);
  lv_label_set_text(title, "Setup Mode");
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 40);

  lv_obj_t *l1 = lv_label_create(scr);
  lv_obj_set_style_text_color(l1, COLOR_TEXT, 0);
  lv_obj_set_style_text_font(l1, &lv_font_montserrat_14, 0);
  lv_label_set_text(l1, "Подключитесь к WiFi:");
  lv_obj_align(l1, LV_ALIGN_CENTER, 0, -30);

  lv_obj_t *l2 = lv_label_create(scr);
  lv_obj_set_style_text_color(l2, COLOR_GOLD, 0);
  lv_obj_set_style_text_font(l2, &lv_font_montserrat_16, 0);
  lv_label_set_text(l2, "BarakatTime-Setup");
  lv_obj_align(l2, LV_ALIGN_CENTER, 0, 0);

  lv_obj_t *l3 = lv_label_create(scr);
  lv_obj_set_style_text_color(l3, COLOR_TEXT_DIM, 0);
  lv_obj_set_style_text_font(l3, &lv_font_montserrat_14, 0);
  lv_label_set_text(l3, "Откройте: 192.168.4.1");
  lv_obj_align(l3, LV_ALIGN_CENTER, 0, 40);
}

/* ---------- Веб-страница настройки (captive portal) ---------- */
void handle_root() {
  String html = "<!DOCTYPE html><html><head><meta charset='utf-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<title>BarakatTime Setup</title>";
  html += "<style>body{font-family:sans-serif;background:#0B0F0E;color:#F5F0E6;padding:24px;}";
  html += "input,select{width:100%;padding:10px;margin:8px 0;border-radius:8px;border:none;background:#141A18;color:#F5F0E6;font-size:16px;box-sizing:border-box;}";
  html += "button{width:100%;padding:12px;margin-top:16px;border-radius:8px;border:none;background:#D4AF37;color:#0B0F0E;font-weight:bold;font-size:16px;}";
  html += "h2{color:#D4AF37;}label{font-size:13px;color:#8A8F8C;}</style></head><body>";
  html += "<h2>BarakatTime — Настройка</h2>";
  html += "<form action='/save' method='POST'>";
  html += "<label>WiFi (SSID)</label><input name='ssid' value='" + cfg_ssid + "' required>";
  html += "<label>Пароль WiFi</label><input name='pass' type='password' value=''>";
  html += "<label>Город</label><input name='city' value='" + cfg_city + "' required>";
  html += "<label>Страна</label><input name='country' value='" + cfg_country + "' required>";
  html += "<label>Метод расчёта</label><select name='method'>";
  html += "<option value='2'" + String(cfg_method == 2 ? " selected" : "") + ">ISNA (Северная Америка)</option>";
  html += "<option value='3'" + String(cfg_method == 3 ? " selected" : "") + ">MWL (Мусульманская лига)</option>";
  html += "<option value='4'" + String(cfg_method == 4 ? " selected" : "") + ">Умм аль-Кура (Мекка)</option>";
  html += "<option value='5'" + String(cfg_method == 5 ? " selected" : "") + ">Египет</option>";
  html += "<option value='13'" + String(cfg_method == 13 ? " selected" : "") + ">Турция (Diyanet)</option>";
  html += "</select>";
  html += "<button type='submit'>Сохранить</button>";
  html += "</form></body></html>";
  server.send(200, "text/html", html);
}

void handle_save() {
  if (server.hasArg("ssid")) cfg_ssid = server.arg("ssid");
  if (server.hasArg("pass")) cfg_pass = server.arg("pass");
  if (server.hasArg("city")) cfg_city = server.arg("city");
  if (server.hasArg("country")) cfg_country = server.arg("country");
  if (server.hasArg("method")) cfg_method = server.arg("method").toInt();

  prefs.begin("barakat", false);
  prefs.putString("ssid", cfg_ssid);
  prefs.putString("pass", cfg_pass);
  prefs.putString("city", cfg_city);
  prefs.putString("country", cfg_country);
  prefs.putInt("method", cfg_method);
  prefs.putBool("configured", true);
  prefs.end();

  String html = "<html><body style='font-family:sans-serif;background:#0B0F0E;color:#F5F0E6;padding:24px;text-align:center;'>";
  html += "<h2 style='color:#D4AF37;'>Готово!</h2><p>Настройки сохранены. Перезагрузка...</p></body></html>";
  server.send(200, "text/html", html);

  delay(1500);
  ESP.restart();
}

void start_ap_setup_mode() {
  ap_mode_active = true;
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  WiFi.softAP("BarakatTime-Setup");

  dnsServer.start(DNS_PORT, "*", apIP);

  server.on("/", handle_root);
  server.on("/save", HTTP_POST, handle_save);
  server.onNotFound(handle_root);
  server.begin();
}

/* ---------- WiFi STA + Aladhan API ---------- */
bool connect_wifi_sta() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(cfg_ssid.c_str(), cfg_pass.c_str());
  int retries = 0;
  while (WiFi.status() != WL_CONNECTED && retries < 30) {
    delay(500);
    retries++;
  }
  return WiFi.status() == WL_CONNECTED;
}

String extract_str_from(const String &json, const String &key, int fromIndex) {
  String pattern = "\"" + key + "\":\"";
  int idx = json.indexOf(pattern, fromIndex);
  if (idx == -1) return "";
  idx += pattern.length();
  int end = json.indexOf("\"", idx);
  if (end == -1) return "";
  return json.substring(idx, end);
}

int extract_int_from(const String &json, const String &key, int fromIndex) {
  String pattern = "\"" + key + "\":";
  int idx = json.indexOf(pattern, fromIndex);
  if (idx == -1) return -1;
  idx += pattern.length();
  int end = idx;
  while (end < (int)json.length() && isDigit(json[end])) end++;
  if (end == idx) return -1;
  return json.substring(idx, end).toInt();
}

bool fetch_prayer_times() {
  HTTPClient http;
  String url = "http://api.aladhan.com/v1/timingsByCity?city=" + cfg_city +
               "&country=" + cfg_country + "&method=" + String(cfg_method);
  http.begin(url);
  int code = http.GET();
  if (code != 200) {
    http.end();
    return false;
  }
  String payload = http.getString();
  http.end();

  const char *keys[5] = {"Fajr", "Dhuhr", "Asr", "Maghrib", "Isha"};
  String fajr_full = "";
  for (int i = 0; i < 5; i++) {
    String t = extract_str_from(payload, keys[i], 0);
    if (i == 0) fajr_full = t;
    if (t.length() >= 5) prayer_times_str[i] = t.substring(0, 5);
  }

  /* Дни до Рамадана — приближённо, по хиджри-месяцу/дню */
  int hijri_idx = payload.indexOf("\"hijri\"");
  int hijri_month = extract_int_from(payload, "number", hijri_idx);
  String hijri_day_str = extract_str_from(payload, "day", hijri_idx);
  int hijri_day = hijri_day_str.toInt();
  if (hijri_month > 0 && hijri_day > 0) {
    int months_to_ramadan = (hijri_month <= 9) ? (9 - hijri_month) : (9 + 12 - hijri_month);
    float days_f = months_to_ramadan * 29.53 - hijri_day;
    if (days_f < 0) days_f = 0;
    ramadan_days = (int)(days_f + 0.5);
  }

  /* Часовой пояс города берём из "(+04)" в строке Fajr */
  int gmt_offset_sec = 0;
  int paren = fajr_full.indexOf('(');
  if (paren != -1) {
    int close = fajr_full.indexOf(')', paren);
    if (close != -1) {
      String offs = fajr_full.substring(paren + 1, close);
      gmt_offset_sec = offs.toInt() * 3600;
    }
  }

  configTime(gmt_offset_sec, 0, "pool.ntp.org", "time.nist.gov");
  struct tm now_tm;
  int retry = 0;
  while (!getLocalTime(&now_tm) && retry < 10) {
    delay(300);
    retry++;
  }
  if (retry >= 10) return true; /* времена дня получены, но live-таймер не заведём */

  time_synced = true;
  int now_minutes = now_tm.tm_hour * 60 + now_tm.tm_min;

  int next_idx = -1;
  for (int i = 0; i < 5; i++) {
    int hh = prayer_times_str[i].substring(0, 2).toInt();
    int mm = prayer_times_str[i].substring(3, 5).toInt();
    if (hh * 60 + mm > now_minutes) { next_idx = i; break; }
  }
  if (next_idx == -1) next_idx = 0;

  next_prayer_name = prayer_names[next_idx];
  next_prayer_time = prayer_times_str[next_idx];

  struct tm target_tm = now_tm;
  target_tm.tm_hour = prayer_times_str[next_idx].substring(0, 2).toInt();
  target_tm.tm_min = prayer_times_str[next_idx].substring(3, 5).toInt();
  target_tm.tm_sec = 0;
  time_t target = mktime(&target_tm);
  time_t now = time(nullptr);
  if (target <= now) target += 86400;
  next_prayer_epoch = target;

  return true;
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

  bool is_configured = load_config();

  if (!is_configured) {
    build_setup_ap_screen();
    start_ap_setup_mode();
    return;
  }

  bool online = connect_wifi_sta();
  if (online) {
    fetch_prayer_times();
  }

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

  build_splash_screen(lv_layer_top());
}

void loop() {
  if (ap_mode_active) {
    dnsServer.processNextRequest();
    server.handleClient();
  }
  lv_timer_handler();
  delay(5);
}
