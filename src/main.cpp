#define LGFX_USE_V1
#include "Arduino.h"
#include <lvgl.h>
#include <LovyanGFX.hpp>
#include "CST816D.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "WakeOnLan.h"
#include "main.h"

// --- КОНФІГУРАЦІЯ LGFX (без змін) ---
class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_GC9A01 _panel_instance;
  lgfx::Light_PWM _light_instance;
  lgfx::Bus_SPI _bus_instance;
public:
  LGFX(void) {
    { auto cfg = _bus_instance.config();
      cfg.spi_host = SPI2_HOST; cfg.spi_mode = 0; cfg.freq_write = 80000000;
      cfg.pin_sclk = SCLK; cfg.pin_mosi = MOSI; cfg.pin_miso = MISO; cfg.pin_dc = DC;
      _bus_instance.config(cfg); _panel_instance.setBus(&_bus_instance); }
    { auto cfg = _panel_instance.config();
      cfg.pin_cs = CS; cfg.pin_rst = RST; cfg.pin_busy = -1;
      cfg.panel_width = 240; cfg.panel_height = 240; cfg.invert = true;
      _panel_instance.config(cfg); }
    { auto cfg = _light_instance.config();
      cfg.pin_bl = BL; cfg.invert = false; cfg.freq = 44100; cfg.pwm_channel = 1;
      _light_instance.config(cfg); _panel_instance.setLight(&_light_instance); }
    setPanel(&_panel_instance);
  }
};

LGFX tft;
CST816D touch(I2C_SDA, I2C_SCL, TP_RST, TP_INT);
const char* ssid = "MaestroSkyHome";
const char* password = "maestro19";
const byte macAddress[] = {0x58, 0x11, 0x22, 0xAF, 0x02, 0xC5};
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 7200;
const int   daylightOffset_sec = 3600;

String api_key = "f957aeaeed8744887eaa43ee1b5c43c6"; 
String city = "Kherson";

static lv_disp_draw_buf_t draw_buf;
static lv_color_t buf[2][240 * 10];
lv_obj_t *time_label;
lv_obj_t *btn;
lv_obj_t *city_label;
lv_obj_t *temp_label;

static bool is_dimmed = false;
const uint32_t INACTIVITY_TIMEOUT_MS = 5000;

void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
    if (tft.getStartCount() == 0) tft.endWrite();
    tft.pushImageDMA(area->x1, area->y1, (area->x2 - area->x1 + 1), (area->y2 - area->y1 + 1), (lgfx::swap565_t *)&color_p->full);
    lv_disp_flush_ready(disp);
}

void my_touchpad_read(lv_indev_drv_t *indev, lv_indev_data_t *data) {
    uint16_t touchX, touchY;
    uint8_t gesture;
    if (touch.getTouch(&touchX, &touchY, &gesture)) {
        data->state = LV_INDEV_STATE_PR; data->point.x = touchX; data->point.y = touchY;
    } else {
        data->state = LV_INDEV_STATE_REL;
    }
}

static void btn_event_cb(lv_event_t * e) {
    if(lv_event_get_code(e) == LV_EVENT_CLICKED) {
        send_wol_packet(macAddress);
        lv_obj_t* label = lv_obj_get_child((lv_obj_t*)lv_event_get_target(e), 0);
        lv_label_set_text(label, "Packet Sent!");
    }
}

void update_time_task(void *param) {
  struct tm timeinfo;
  while(1) {
    if (getLocalTime(&timeinfo)) {
      lv_label_set_text_fmt(time_label, "%02d:%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    }
    vTaskDelay(1000);
  }
}

void update_weather() {
    if (WiFi.status() != WL_CONNECTED) return;

    HTTPClient http;
    String url = "http://api.openweathermap.org/data/2.5/weather?q=" + city + "&appid=" + api_key + "&units=metric";
    http.begin(url);
    int httpCode = http.GET();

    if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();
        DynamicJsonDocument doc(1024);
        deserializeJson(doc, payload);

        if (!doc.isNull()) {
            const char* city_name = doc["name"];
            float temp = doc["main"]["temp"];
            
            lv_label_set_text(city_label, city_name);
            // =========================================================================
            // <<< ОНОВЛЕНО: Додано символ градуса >>>
            lv_label_set_text(temp_label, (String((int)round(temp)) + "°C").c_str());
            // =========================================================================
        }
    }
    http.end();
}

static void check_inactivity_timer_cb(lv_timer_t * timer) {
    uint32_t inactive_time = lv_disp_get_inactive_time(NULL);

    if (inactive_time >= INACTIVITY_TIMEOUT_MS && !is_dimmed) {
        lv_obj_set_style_bg_color(lv_scr_act(), lv_color_black(), 0);
        lv_obj_set_style_text_color(time_label, lv_color_white(), 0);
        lv_obj_set_style_text_color(city_label, lv_color_white(), 0);
        lv_obj_set_style_text_color(temp_label, lv_color_white(), 0);
        lv_obj_add_flag(btn, LV_OBJ_FLAG_HIDDEN);
        is_dimmed = true;
    }

    if (inactive_time < INACTIVITY_TIMEOUT_MS && is_dimmed) {
        lv_obj_set_style_bg_color(lv_scr_act(), lv_color_white(), 0);
        lv_obj_set_style_text_color(time_label, lv_color_black(), 0);
        lv_obj_set_style_text_color(city_label, lv_color_black(), 0);
        lv_obj_set_style_text_color(temp_label, lv_color_black(), 0);
        lv_obj_clear_flag(btn, LV_OBJ_FLAG_HIDDEN);
        is_dimmed = false;
    }
}

void setup() {
    Serial.begin(115200);
    tft.init(); tft.initDMA(); tft.startWrite();
    touch.begin(); tft.setBrightness(255);
    lv_init();
    lv_disp_draw_buf_init(&draw_buf, buf[0], buf[1], 240 * 10);
    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = 240; disp_drv.ver_res = 240; disp_drv.flush_cb = my_disp_flush;
    disp_drv.draw_buf = &draw_buf; lv_disp_drv_register(&disp_drv);
    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = my_touchpad_read;
    lv_indev_drv_register(&indev_drv);

    lv_obj_t* screen = lv_scr_act();
    lv_obj_set_style_bg_color(screen, lv_color_white(), 0);

    time_label = lv_label_create(screen);
    lv_obj_set_style_text_font(time_label, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(time_label, lv_color_black(), 0);
    lv_obj_align(time_label, LV_ALIGN_CENTER, 0, -40);
    lv_label_set_text(time_label, "Connecting...");

    btn = lv_btn_create(screen);
    lv_obj_add_event_cb(btn, btn_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_align(btn, LV_ALIGN_CENTER, 0, 40);
    lv_obj_set_size(btn, 200, 70);
    lv_obj_t * label = lv_label_create(btn);
    lv_label_set_text(label, "Turn On PC");
    lv_obj_center(label);
    
    city_label = lv_label_create(screen);
    lv_obj_set_style_text_font(city_label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(city_label, lv_color_black(), 0);
    lv_obj_align_to(city_label, time_label, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 10);
    lv_label_set_text(city_label, "Loading...");

    temp_label = lv_label_create(screen);
    lv_obj_set_style_text_font(temp_label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(temp_label, lv_color_black(), 0);
    lv_obj_align_to(temp_label, time_label, LV_ALIGN_OUT_BOTTOM_RIGHT, 0, 10);
    // =========================================================================
    // <<< ОНОВЛЕНО: Додано символ градуса >>>
    lv_label_set_text(temp_label, "--°C");
    // =========================================================================

    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
    Serial.println("\nWiFi Connected!");

    update_weather();
    lv_label_set_text(time_label, "Connected!");
    
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    xTaskCreate(update_time_task, "time_task", 4096, NULL, 5, NULL);
    
    lv_timer_create(check_inactivity_timer_cb, 500, NULL);
    lv_timer_create([](lv_timer_t* t){ update_weather(); }, 1800000, NULL);
}

void loop() {
    lv_timer_handler();
    delay(5);
}