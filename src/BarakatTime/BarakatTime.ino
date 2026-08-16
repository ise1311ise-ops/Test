#include <lvgl.h>
#include "Arduino_GFX_Library.h"
#include "Arduino_DriveBus_Library.h"
#include "pin_config.h"
#include "lv_conf.h"
#include "HWCDC.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <time.h>
#include <math.h>

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

/* ---------- Palette ---------- */
#define COLOR_BG          lv_color_hex(0x0B0F0E)
#define COLOR_CARD        lv_color_hex(0x141A18)
#define COLOR_GOLD        lv_color_hex(0xD4AF37)
#define COLOR_GREEN_LIGHT lv_color_hex(0x4FAE7C)
#define COLOR_TEXT        lv_color_hex(0xF5F0E6)
#define COLOR_TEXT_DIM    lv_color_hex(0x8A8F8C)

/* ================================================================
   ZIKR (tasbih phrases)
   ================================================================ */
#define ZIKR_COUNT 5
const char *zikr_names[ZIKR_COUNT] = {
  "SubhanAllah", "Alhamdulillah", "Allahu Akbar", "La ilaha illallah", "Astaghfirullah"
};
static int zikr_selected = 0;
static uint32_t zikr_counts[ZIKR_COUNT] = {0, 0, 0, 0, 0};

static lv_obj_t *tasbih_label;
static lv_obj_t *tasbih_arc;
static lv_obj_t *zikr_name_label;
static lv_obj_t *stats_list_labels[ZIKR_COUNT];

void zikr_save_counts() {
  Preferences p;
  p.begin("zikr", false);
  char key[8];
  for (int i = 0; i < ZIKR_COUNT; i++) {
    snprintf(key, sizeof(key), "c%d", i);
    p.putUInt(key, zikr_counts[i]);
  }
  p.putInt("sel", zikr_selected);
  p.end();
}

void zikr_load_counts() {
  Preferences p;
  p.begin("zikr", true);
  char key[8];
  for (int i = 0; i < ZIKR_COUNT; i++) {
    snprintf(key, sizeof(key), "c%d", i);
    zikr_counts[i] = p.getUInt(key, 0);
  }
  zikr_selected = p.getInt("sel", 0);
  p.end();
}

/* ---------- WiFi / setup ---------- */
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
int cfg_method = 3;      // 2=ISNA 3=MWL 4=UmmAlQura 5=Egypt 13=Turkey
float cfg_lat = 0;
float cfg_lng = 0;
float cfg_utc_offset = 0;
bool cfg_has_coords = false;

/* ---------- Prayer data ---------- */
const char *prayer_names[5]  = {"Fajr", "Dhuhr", "Asr", "Maghrib", "Isha"};
int prayer_minutes[5]        = {0, 0, 0, 0, 0};   // minutes-from-midnight, local time
String prayer_times_str[5]   = {"--:--", "--:--", "--:--", "--:--", "--:--"};
String next_prayer_name = "-";
String next_prayer_time = "--:--";
int next_prayer_idx = -1;
int ramadan_days = -1;

bool time_synced = false;
time_t next_prayer_epoch = 0;
static lv_obj_t *countdown_time_label = NULL;

/* last day we computed prayer_minutes[] for (to know when to recompute) */
static int last_calc_yday = -1;

/* ---------- Buzzer ---------- */
bool prayer_alerted_today[5] = {false, false, false, false, false};
static int last_alert_yday = -1;

void buzzer_beep(int times, int on_ms, int off_ms) {
  for (int i = 0; i < times; i++) {
    digitalWrite(BUZZER_PIN, HIGH);
    delay(on_ms);
    digitalWrite(BUZZER_PIN, LOW);
    if (i < times - 1) delay(off_ms);
  }
}

/* ================================================================
   ON-DEVICE PRAYER TIME CALCULATION (no external API needed)
   ================================================================ */

double deg2rad(double d) { return d * M_PI / 180.0; }
double rad2deg(double r) { return r * 180.0 / M_PI; }

double fix_hour(double h) {
  h = fmod(h, 24.0);
  if (h < 0) h += 24.0;
  return h;
}

double julian_date(int y, int m, int d) {
  if (m <= 2) { y -= 1; m += 12; }
  double A = floor(y / 100.0);
  double B = 2 - A + floor(A / 4.0);
  return floor(365.25 * (y + 4716)) + floor(30.6001 * (m + 1)) + d + B - 1524.5;
}

void sun_position(double JD, double *decl, double *eqt) {
  double D = JD - 2451545.0;
  double g = deg2rad(fmod(357.529 + 0.98560028 * D, 360.0));
  double q = fmod(280.459 + 0.98564736 * D, 360.0);
  double L = deg2rad(fmod(q + 1.915 * sin(g) + 0.020 * sin(2 * g), 360.0));
  double e = deg2rad(23.439 - 0.00000036 * D);

  double RA = rad2deg(atan2(cos(e) * sin(L), cos(L))) / 15.0;
  RA = fix_hour(RA);
  double eqt_h = q / 15.0 - RA;
  if (eqt_h > 12) eqt_h -= 24;
  if (eqt_h < -12) eqt_h += 24;

  *decl = rad2deg(asin(sin(e) * sin(L)));
  *eqt = eqt_h;
}

double hour_angle(double angle, double lat, double decl) {
  double latR = deg2rad(lat);
  double declR = deg2rad(decl);
  double val = (-sin(deg2rad(angle)) - sin(latR) * sin(declR)) / (cos(latR) * cos(declR));
  if (val > 1) val = 1;
  if (val < -1) val = -1;
  return rad2deg(acos(val)) / 15.0;
}

double asr_hour_angle(double t, double lat, double decl) {
  double latR = deg2rad(lat);
  double declR = deg2rad(decl);
  double angle = -rad2deg(atan(1.0 / (t + tan(fabs(latR - declR)))));
  double val = (sin(deg2rad(angle)) - sin(latR) * sin(declR)) / (cos(latR) * cos(declR));
  if (val > 1) val = 1;
  if (val < -1) val = -1;
  return rad2deg(acos(val)) / 15.0;
}

void calc_prayer_times(int year, int month, int day, double lat, double lng, double tz, int method) {
  double fajr_angle, isha_angle;
  double isha_minutes_after_maghrib = -1;

  switch (method) {
    case 2:  fajr_angle = 15.0; isha_angle = 15.0; break;
    case 4:  fajr_angle = 18.5; isha_minutes_after_maghrib = 90; isha_angle = 0; break;
    case 5:  fajr_angle = 19.5; isha_angle = 17.5; break;
    case 13: fajr_angle = 18.0; isha_angle = 17.0; break;
    default: fajr_angle = 18.0; isha_angle = 17.0; break;
  }

  double JD = julian_date(year, month, day) - lng / (15.0 * 24.0);
  double decl, eqt;
  sun_position(JD, &decl, &eqt);

  double dhuhr = fix_hour(12 + tz - eqt);

  double fajr    = dhuhr - hour_angle(fajr_angle, lat, decl);
  double asr     = dhuhr + asr_hour_angle(1.0, lat, decl);
  double sunset  = dhuhr + hour_angle(0.833, lat, decl);
  double maghrib = sunset;
  double isha;
  if (isha_minutes_after_maghrib > 0) {
    isha = maghrib + isha_minutes_after_maghrib / 60.0;
  } else {
    isha = dhuhr + hour_angle(isha_angle, lat, decl);
  }

  double times_h[5] = {fajr, dhuhr, asr, maghrib, isha};
  for (int i = 0; i < 5; i++) {
    double h = fix_hour(times_h[i]);
    int hh = (int)h;
    int mm = (int)round((h - hh) * 60.0);
    if (mm == 60) { mm = 0; hh = (hh + 1) % 24; }
    prayer_minutes[i] = hh * 60 + mm;
    char buf[6];
    snprintf(buf, sizeof(buf), "%02d:%02d", hh, mm);
    prayer_times_str[i] = String(buf);
  }
}

long days_to_ramadan(time_t now) {
  struct tm ref = {0};
  ref.tm_year = 2025 - 1900; ref.tm_mon = 5; ref.tm_mday = 26; // 1 Muharram 1447 ~ 2025-06-26
  time_t ref_epoch = timegm(&ref);
  double days_since_ref = difftime(now, ref_epoch) / 86400.0;
  double hijri_year_len = 354.367;
  double year_progress = fmod(days_since_ref, hijri_year_len);
  if (year_progress < 0) year_progress += hijri_year_len;
  double ramadan_start_day = 8 * 29.53;
  double diff = ramadan_start_day - year_progress;
  if (diff < 0) diff += hijri_year_len;
  return (long)round(diff);
}

/* ================================================================
   Geocoding (city/country -> lat/lng), Open-Meteo, one-time only
   ================================================================ */
bool geocode_city() {
  HTTPClient http;
  http.setTimeout(6000);
  String url = "https://geocoding-api.open-meteo.com/v1/search?name=" + cfg_city + "&count=1&language=en&format=json";
  http.begin(url);
  int code = http.GET();
  if (code != 200) { http.end(); return false; }
  String payload = http.getString();
  http.end();

  int latIdx = payload.indexOf("\"latitude\":");
  int lngIdx = payload.indexOf("\"longitude\":");
  if (latIdx == -1 || lngIdx == -1) return false;

  cfg_lat = payload.substring(latIdx + 11, payload.indexOf(",", latIdx)).toFloat();
  cfg_lng = payload.substring(lngIdx + 12, payload.indexOf(",", lngIdx)).toFloat();
  cfg_has_coords = true;
  return true;
}

/* ================================================================
   NTP time sync (short timeout, never blocks forever)
   ================================================================ */
bool sync_time_ntp() {
  configTime((long)(cfg_utc_offset * 3600), 0, "pool.ntp.org", "ru.pool.ntp.org", "time.google.com");
  struct tm now_tm;
  int retry = 0;
  while (!getLocalTime(&now_tm, 1000) && retry < 6) {
    retry++;
  }
  if (retry >= 6) return false;
  time_synced = true;

  time_t now = time(nullptr);
  Preferences p;
  p.begin("clock", false);
  p.putULong("last_sync", (uint32_t)now);
  p.end();
  return true;
}

void recompute_schedule() {
  time_t now = time(nullptr);
  struct tm *t = localtime(&now);
  if (t->tm_yday == last_calc_yday && last_calc_yday != -1) return;

  calc_prayer_times(t->tm_year + 1900, t->tm_mon + 1, t->tm_mday, cfg_lat, cfg_lng, cfg_utc_offset, cfg_method);
  last_calc_yday = t->tm_yday;
  ramadan_days = (int)days_to_ramadan(now);

  int now_minutes = t->tm_hour * 60 + t->tm_min;
  next_prayer_idx = -1;
  for (int i = 0; i < 5; i++) {
    if (prayer_minutes[i] > now_minutes) { next_prayer_idx = i; break; }
  }
  if (next_prayer_idx == -1) next_prayer_idx = 0;

  next_prayer_name = prayer_names[next_prayer_idx];
  next_prayer_time = prayer_times_str[next_prayer_idx];

  struct tm target_tm = *t;
  target_tm.tm_hour = prayer_minutes[next_prayer_idx] / 60;
  target_tm.tm_min = prayer_minutes[next_prayer_idx] % 60;
  target_tm.tm_sec = 0;
  time_t target = mktime(&target_tm);
  if (target <= now) target += 86400;
  next_prayer_epoch = target;

  if (t->tm_yday != last_alert_yday) {
    for (int i = 0; i < 5; i++) prayer_alerted_today[i] = false;
    last_alert_yday = t->tm_yday;
  }
}

void check_prayer_alerts() {
  if (!time_synced) return;
  time_t now = time(nullptr);
  struct tm *t = localtime(&now);
  int now_minutes = t->tm_hour * 60 + t->tm_min;
  for (int i = 0; i < 5; i++) {
    if (!prayer_alerted_today[i] && now_minutes == prayer_minutes[i]) {
      prayer_alerted_today[i] = true;
      buzzer_beep(3, 250, 150);
    }
  }
}

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

/* ---------- Tasbih tap ---------- */
static void tasbih_tap_cb(lv_event_t *e) {
  zikr_counts[zikr_selected]++;
  uint32_t pos = zikr_counts[zikr_selected] % 33;
  lv_arc_set_value(tasbih_arc, pos == 0 ? 33 : pos);
  lv_label_set_text_fmt(tasbih_label, "%lu", (unsigned long)zikr_counts[zikr_selected]);
  zikr_save_counts();
}

static void tasbih_longpress_cb(lv_event_t *e) {
  zikr_selected = (zikr_selected + 1) % ZIKR_COUNT;
  lv_label_set_text(zikr_name_label, zikr_names[zikr_selected]);
  uint32_t pos = zikr_counts[zikr_selected] % 33;
  lv_arc_set_value(tasbih_arc, pos == 0 ? 33 : pos);
  lv_label_set_text_fmt(tasbih_label, "%lu", (unsigned long)zikr_counts[zikr_selected]);
  zikr_save_counts();
}

/* ---------- Screen 1: Tasbih ---------- */
static void build_tasbih_screen(lv_obj_t *tile) {
  lv_obj_set_style_bg_color(tile, COLOR_BG, 0);
  lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);

  zikr_name_label = lv_label_create(tile);
  lv_obj_set_style_text_color(zikr_name_label, COLOR_GOLD, 0);
  lv_obj_set_style_text_font(zikr_name_label, &lv_font_montserrat_16, 0);
  lv_label_set_text(zikr_name_label, zikr_names[zikr_selected]);
  lv_obj_align(zikr_name_label, LV_ALIGN_TOP_MID, 0, 16);

  lv_obj_t *arc = lv_arc_create(tile);
  tasbih_arc = arc;
  lv_obj_set_size(arc, 190, 190);
  lv_obj_center(arc);
  lv_arc_set_rotation(arc, 270);
  lv_arc_set_bg_angles(arc, 0, 360);
  lv_arc_set_range(arc, 0, 33);
  lv_arc_set_value(arc, zikr_counts[zikr_selected] % 33);
  lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
  lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_arc_color(arc, COLOR_CARD, LV_PART_MAIN);
  lv_obj_set_style_arc_width(arc, 10, LV_PART_MAIN);
  lv_obj_set_style_arc_color(arc, COLOR_GOLD, LV_PART_INDICATOR);
  lv_obj_set_style_arc_width(arc, 10, LV_PART_INDICATOR);

  tasbih_label = lv_label_create(tile);
  lv_obj_set_style_text_color(tasbih_label, COLOR_TEXT, 0);
  lv_obj_set_style_text_font(tasbih_label, &lv_font_montserrat_48, 0);
  lv_label_set_text_fmt(tasbih_label, "%lu", (unsigned long)zikr_counts[zikr_selected]);
  lv_obj_center(tasbih_label);

  lv_obj_t *hint = lv_label_create(tile);
  lv_obj_set_style_text_color(hint, COLOR_TEXT_DIM, 0);
  lv_obj_set_style_text_font(hint, &lv_font_montserrat_14, 0);
  lv_label_set_text(hint, "Tap to count - Hold to change zikr");
  lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -16);

  lv_obj_add_flag(tile, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(tile, tasbih_tap_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_add_event_cb(tile, tasbih_longpress_cb, LV_EVENT_LONG_PRESSED, NULL);
}

/* ---------- Screen 2: Prayers today ---------- */
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
  lv_label_set_text(title, "Today's Prayers");

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
    lv_label_set_text(name, prayer_names[i]);
    lv_obj_align(name, LV_ALIGN_LEFT_MID, 10, 0);

    lv_obj_t *time_lbl = lv_label_create(card);
    lv_obj_set_style_text_color(time_lbl, COLOR_GOLD, 0);
    lv_obj_set_style_text_font(time_lbl, &lv_font_montserrat_12, 0);
    lv_label_set_text(time_lbl, prayer_times_str[i].c_str());
    lv_obj_align(time_lbl, LV_ALIGN_RIGHT_MID, -10, 0);
  }
}

/* ---------- Screen 3: Countdown ---------- */
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
  lv_label_set_text_fmt(l1, "Until %s", next_prayer_name.c_str());
  lv_obj_align(l1, LV_ALIGN_TOP_MID, 0, 12);

  lv_obj_t *l2 = lv_label_create(card1);
  lv_obj_set_style_text_color(l2, COLOR_GOLD, 0);
  lv_obj_set_style_text_font(l2, &lv_font_montserrat_32, 0);
  lv_label_set_text(l2, "--:--:--");
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
  lv_label_set_text(l3, "Until Ramadan");
  lv_obj_align(l3, LV_ALIGN_TOP_MID, 0, 10);

  lv_obj_t *l4 = lv_label_create(card2);
  lv_obj_set_style_text_color(l4, COLOR_GREEN_LIGHT, 0);
  lv_obj_set_style_text_font(l4, &lv_font_montserrat_22, 0);
  lv_label_set_text_fmt(l4, "%d days", ramadan_days);
  lv_obj_align(l4, LV_ALIGN_CENTER, 0, 8);

  lv_timer_create(countdown_tick_cb, 1000, NULL);
}

/* ---------- Screen 4: Zikr stats ---------- */
static void build_stats_screen(lv_obj_t *tile) {
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
  lv_label_set_text(title, "Zikr Stats");

  for (int i = 0; i < ZIKR_COUNT; i++) {
    lv_obj_t *card = lv_obj_create(tile);
    lv_obj_set_size(card, 200, 28);
    lv_obj_set_style_bg_color(card, COLOR_CARD, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 10, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *name = lv_label_create(card);
    lv_obj_set_style_text_color(name, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(name, &lv_font_montserrat_12, 0);
    lv_label_set_text(name, zikr_names[i]);
    lv_obj_align(name, LV_ALIGN_LEFT_MID, 10, 0);

    lv_obj_t *cnt = lv_label_create(card);
    lv_obj_set_style_text_color(cnt, COLOR_GOLD, 0);
    lv_obj_set_style_text_font(cnt, &lv_font_montserrat_12, 0);
    lv_label_set_text_fmt(cnt, "%lu", (unsigned long)zikr_counts[i]);
    lv_obj_align(cnt, LV_ALIGN_RIGHT_MID, -10, 0);
    stats_list_labels[i] = cnt;
  }
}

/* ---------- Boot splash overlay ---------- */
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

/* ---------- Config load/save ---------- */
bool load_config() {
  prefs.begin("barakat", true);
  bool configured = prefs.getBool("configured", false);
  cfg_ssid = prefs.getString("ssid", "");
  cfg_pass = prefs.getString("pass", "");
  cfg_city = prefs.getString("city", "");
  cfg_country = prefs.getString("country", "");
  cfg_method = prefs.getInt("method", 3);
  cfg_lat = prefs.getFloat("lat", 0);
  cfg_lng = prefs.getFloat("lng", 0);
  cfg_utc_offset = prefs.getFloat("utc", 0);
  cfg_has_coords = prefs.getBool("has_coords", false);
  prefs.end();
  return configured;
}

/* ---------- Setup-mode screen on the display ---------- */
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
  lv_label_set_text(l1, "Connect to WiFi:");
  lv_obj_align(l1, LV_ALIGN_CENTER, 0, -30);

  lv_obj_t *l2 = lv_label_create(scr);
  lv_obj_set_style_text_color(l2, COLOR_GOLD, 0);
  lv_obj_set_style_text_font(l2, &lv_font_montserrat_16, 0);
  lv_label_set_text(l2, "BarakatTime-Setup");
  lv_obj_align(l2, LV_ALIGN_CENTER, 0, 0);

  lv_obj_t *l3 = lv_label_create(scr);
  lv_obj_set_style_text_color(l3, COLOR_TEXT_DIM, 0);
  lv_obj_set_style_text_font(l3, &lv_font_montserrat_14, 0);
  lv_label_set_text(l3, "Open: 192.168.4.1");
  lv_obj_align(l3, LV_ALIGN_CENTER, 0, 40);
}

/* ---------- Captive portal web pages ---------- */
void handle_root() {
  String html = "<!DOCTYPE html><html><head><meta charset='utf-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<title>BarakatTime Setup</title>";
  html += "<style>body{font-family:sans-serif;background:#0B0F0E;color:#F5F0E6;padding:24px;}";
  html += "input,select{width:100%;padding:10px;margin:8px 0;border-radius:8px;border:none;background:#141A18;color:#F5F0E6;font-size:16px;box-sizing:border-box;}";
  html += "button{width:100%;padding:12px;margin-top:16px;border-radius:8px;border:none;background:#D4AF37;color:#0B0F0E;font-weight:bold;font-size:16px;}";
  html += "a{color:#D4AF37;}h2{color:#D4AF37;}label{font-size:13px;color:#8A8F8C;}</style></head><body>";
  html += "<h2>BarakatTime Setup</h2>";
  html += "<form action='/save' method='POST'>";
  html += "<label>WiFi SSID</label><input name='ssid' value='" + cfg_ssid + "' required>";
  html += "<label>WiFi Password</label><input name='pass' type='password' value=''>";
  html += "<label>City</label><input name='city' value='" + cfg_city + "' required>";
  html += "<label>Country</label><input name='country' value='" + cfg_country + "' required>";
  html += "<label>UTC offset (e.g. 3 or -5)</label><input name='utc' type='number' step='0.5' value='" + String(cfg_utc_offset) + "' required>";
  html += "<label>Calculation method</label><select name='method'>";
  html += "<option value='2'" + String(cfg_method == 2 ? " selected" : "") + ">ISNA (North America)</option>";
  html += "<option value='3'" + String(cfg_method == 3 ? " selected" : "") + ">MWL (Muslim World League)</option>";
  html += "<option value='4'" + String(cfg_method == 4 ? " selected" : "") + ">Umm al-Qura (Makkah)</option>";
  html += "<option value='5'" + String(cfg_method == 5 ? " selected" : "") + ">Egyptian</option>";
  html += "<option value='13'" + String(cfg_method == 13 ? " selected" : "") + ">Turkey (Diyanet)</option>";
  html += "</select>";
  html += "<button type='submit'>Save</button>";
  html += "</form><p><a href='/stats'>View zikr stats</a></p></body></html>";
  server.send(200, "text/html", html);
}

void handle_stats() {
  String html = "<!DOCTYPE html><html><head><meta charset='utf-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<title>Zikr Stats</title>";
  html += "<style>body{font-family:sans-serif;background:#0B0F0E;color:#F5F0E6;padding:24px;}";
  html += "table{width:100%;border-collapse:collapse;}td{padding:10px;border-bottom:1px solid #141A18;}";
  html += "h2{color:#D4AF37;}a{color:#D4AF37;}</style></head><body>";
  html += "<h2>Zikr Stats</h2><table>";
  uint32_t total = 0;
  for (int i = 0; i < ZIKR_COUNT; i++) {
    html += "<tr><td>" + String(zikr_names[i]) + "</td><td style='color:#D4AF37;text-align:right'>" + String(zikr_counts[i]) + "</td></tr>";
    total += zikr_counts[i];
  }
  html += "<tr><td><b>Total</b></td><td style='text-align:right'><b>" + String(total) + "</b></td></tr>";
  html += "</table><p><a href='/'>Back to setup</a></p></body></html>";
  server.send(200, "text/html", html);
}

void handle_save() {
  bool city_changed = false;
  if (server.hasArg("ssid")) cfg_ssid = server.arg("ssid");
  if (server.hasArg("pass")) cfg_pass = server.arg("pass");
  if (server.hasArg("city") && server.arg("city") != cfg_city) { cfg_city = server.arg("city"); city_changed = true; }
  if (server.hasArg("country") && server.arg("country") != cfg_country) { cfg_country = server.arg("country"); city_changed = true; }
  if (server.hasArg("method")) cfg_method = server.arg("method").toInt();
  if (server.hasArg("utc")) cfg_utc_offset = server.arg("utc").toFloat();

  prefs.begin("barakat", false);
  prefs.putString("ssid", cfg_ssid);
  prefs.putString("pass", cfg_pass);
  prefs.putString("city", cfg_city);
  prefs.putString("country", cfg_country);
  prefs.putInt("method", cfg_method);
  prefs.putFloat("utc", cfg_utc_offset);
  prefs.putBool("configured", true);
  if (city_changed) prefs.putBool("has_coords", false);
  prefs.end();

  String html = "<html><body style='font-family:sans-serif;background:#0B0F0E;color:#F5F0E6;padding:24px;text-align:center;'>";
  html += "<h2 style='color:#D4AF37;'>Saved!</h2><p>Restarting...</p></body></html>";
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
  server.on("/stats", handle_stats);
  server.onNotFound(handle_root);
  server.begin();
}

/* ---------- WiFi STA (used at boot for NTP, and for one-time geocoding) ---------- */
bool connect_wifi_sta(unsigned long timeout_ms) {
  WiFi.mode(WIFI_STA);
  WiFi.begin(cfg_ssid.c_str(), cfg_pass.c_str());
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < timeout_ms) {
    delay(200);
  }
  return WiFi.status() == WL_CONNECTED;
}

/* ---------- BOOT button: long-press triggers AP setup mode ---------- */
static unsigned long boot_press_start = 0;
static bool boot_ap_triggered = false;

void check_boot_button() {
  if (digitalRead(BOOT_PIN) == LOW) {
    if (boot_press_start == 0) boot_press_start = millis();
    if (!boot_ap_triggered && millis() - boot_press_start > 2000) {
      boot_ap_triggered = true;
      zikr_save_counts();
      prefs.begin("barakat", false);
      prefs.putBool("configured", false);
      prefs.end();
      ESP.restart();
    }
  } else {
    boot_press_start = 0;
  }
}

void setup() {
  USBSerial.begin(115200);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
  pinMode(BOOT_PIN, INPUT_PULLUP);

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

  zikr_load_counts();
  bool is_configured = load_config();

  if (!is_configured) {
    build_setup_ap_screen();
    start_ap_setup_mode();
    return;
  }

  bool online = connect_wifi_sta(10000);
  if (online) {
    if (!cfg_has_coords) {
      if (geocode_city()) {
        prefs.begin("barakat", false);
        prefs.putFloat("lat", cfg_lat);
        prefs.putFloat("lng", cfg_lng);
        prefs.putBool("has_coords", true);
        prefs.end();
      }
    }
    sync_time_ntp();
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
  }

  if (cfg_has_coords) {
    recompute_schedule();
  }

  lv_obj_t *tv = lv_tileview_create(lv_scr_act());
  lv_obj_set_style_bg_color(tv, COLOR_BG, 0);

  lv_obj_t *tile1 = lv_tileview_add_tile(tv, 0, 0, LV_DIR_HOR);
  lv_obj_t *tile2 = lv_tileview_add_tile(tv, 1, 0, LV_DIR_HOR);
  lv_obj_t *tile3 = lv_tileview_add_tile(tv, 2, 0, LV_DIR_HOR);
  lv_obj_t *tile4 = lv_tileview_add_tile(tv, 3, 0, LV_DIR_HOR);

  build_tasbih_screen(tile1);
  build_prayers_screen(tile2);
  build_countdown_screen(tile3);
  build_stats_screen(tile4);

  const esp_timer_create_args_t lvgl_tick_timer_args = {
    .callback = &example_increase_lvgl_tick,
    .name = "lvgl_tick"
  };
  esp_timer_handle_t lvgl_tick_timer = NULL;
  esp_timer_create(&lvgl_tick_timer_args, &lvgl_tick_timer);
  esp_timer_start_periodic(lvgl_tick_timer, EXAMPLE_LVGL_TICK_PERIOD_MS * 1000);

  build_splash_screen(lv_layer_top());
}

unsigned long last_alert_check = 0;
unsigned long last_weekly_check = 0;

void loop() {
  if (ap_mode_active) {
    dnsServer.processNextRequest();
    server.handleClient();
    lv_timer_handler();
    delay(5);
    return;
  }

  check_boot_button();
  lv_timer_handler();

  unsigned long now_ms = millis();

  if (now_ms - last_alert_check > 10000) {
    last_alert_check = now_ms;
    check_prayer_alerts();
  }

  if (now_ms - last_weekly_check > 60000) {
    last_weekly_check = now_ms;
    if (cfg_has_coords) recompute_schedule();

    Preferences p;
    p.begin("clock", true);
    uint32_t last_sync = p.getULong("last_sync", 0);
    p.end();
    time_t now = time(nullptr);
    if (!time_synced || (uint32_t)now - last_sync > 7UL * 24 * 3600) {
      if (connect_wifi_sta(8000)) {
        sync_time_ntp();
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
      }
    }
  }

  delay(5);
}
