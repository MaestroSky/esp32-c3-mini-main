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
#include "ui/ui.h"
#include <BleKeyboard.h>
#include "esp_bt.h"
#include <Preferences.h>
#include "web_server.h"
#include "qrcode.h" // <-- ДОДАНО INCLUDE

String latest_version = "";
bool update_available = false;

Preferences preferences;

// Оголошення всіх ваших нових іконок погоди
LV_IMG_DECLARE(ui_img_blizzard_png);
LV_IMG_DECLARE(ui_img_blowingsnow_png);
LV_IMG_DECLARE(ui_img_clearnight_png);
LV_IMG_DECLARE(ui_img_cloudy_png);
LV_IMG_DECLARE(ui_img_cloudycleartimes_png);
LV_IMG_DECLARE(ui_img_cloudycleartimesnight_png);
LV_IMG_DECLARE(ui_img_drizzle_png);
LV_IMG_DECLARE(ui_img_drizzlenight_png);
LV_IMG_DECLARE(ui_img_drizzlesun_png);
LV_IMG_DECLARE(ui_img_fog_png);
LV_IMG_DECLARE(ui_img_hail_png);
LV_IMG_DECLARE(ui_img_heavyrain_png);
LV_IMG_DECLARE(ui_img_humidity_png);
LV_IMG_DECLARE(ui_img_partlycloudy_png);
LV_IMG_DECLARE(ui_img_partlycloudynight_png);
LV_IMG_DECLARE(ui_img_rain_png);
LV_IMG_DECLARE(ui_img_rainnight_png);
LV_IMG_DECLARE(ui_img_rainsun_png);
LV_IMG_DECLARE(ui_img_rainthunderstorm_png);
LV_IMG_DECLARE(ui_img_scatteradshowers_png);
LV_IMG_DECLARE(ui_img_scatteradshowersnight_png);
LV_IMG_DECLARE(ui_img_scatteradthunderstorm_png);
LV_IMG_DECLARE(ui_img_severthunderstorm_png);
LV_IMG_DECLARE(ui_img_sleet_png);
LV_IMG_DECLARE(ui_img_snow_png);
LV_IMG_DECLARE(ui_img_sunny_png);
LV_IMG_DECLARE(ui_img_wind_png);

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

// <<< ГЛОБАЛЬНІ НАЛАШТУВАННЯ ЗАЛИШАЮТЬСЯ ГОЛОВНИМИ ТУТ >>>
String ssid = "MaestroSkyHome";
String password = "maestro19";
String api_key = "f957aeaeed8744887eaa43ee1b5c43c6"; 
String city = "Kherson";
long gmtOffset_sec = 7200;
bool daylightOffset_enabled = true;
const int daylightOffset_sec = 3600;
byte macAddress[] = {0x58, 0x11, 0x22, 0xAF, 0x02, 0xC5};
String macStr = "58:11:22:AF:02:C5";
const char* ntpServer = "pool.ntp.org";

// Інші глобальні змінні
static lv_disp_draw_buf_t draw_buf;
static lv_color_t buf[2][240 * 10];
enum ScreenState { AWAKE, PARTIALLY_DIMMED, SCREENSAVER };
ScreenState currentScreenState = AWAKE;
const uint32_t PARTIAL_DIM_TIMEOUT_MS = 10000;
const uint32_t SCREENSAVER_TIMEOUT_MS = 15000;
static lv_obj_t *ui_screensaver_icon;
static lv_obj_t *ui_screensaver_temp;
static lv_style_t style_screensaver_temp;
BleKeyboard bleKeyboard("ESP32 Watch Control", "Misha Inc.", 100);
bool bleKeyboardStarted = false;
uint8_t currentBrightness = 255; 
int lastSnappedValue = -1; 
static lv_obj_t * version_label;
static lv_obj_t * qr_canvas;    // <-- ДОДАНО
static lv_color_t * qr_buf;     // <-- ДОДАНО

// --- Функції ---

void checkForUpdates() {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("Перевірка оновлень неможлива: немає Wi-Fi");
        return;
    }
    HTTPClient http;
    String url = "https://raw.githubusercontent.com/MaestroSky/esp32-c3-mini-main/main/version.txt";
    Serial.println("Перевірка наявності оновлень...");
    http.begin(url);
    int httpCode = http.GET();
    if (httpCode == HTTP_CODE_OK) {
        latest_version = http.getString();
        latest_version.trim();
        Serial.printf("Поточна версія: %s\n", FIRMWARE_VERSION);
        Serial.printf("Остання версія: %s\n", latest_version.c_str());
        if (latest_version != FIRMWARE_VERSION && latest_version != "") {
            update_available = true;
            Serial.println("Доступна нова версія прошивки!");
        } else {
            update_available = false;
            Serial.println("У вас остання версія прошивки.");
        }
    } else {
        Serial.printf("Помилка перевірки оновлень, код: %d\n", httpCode);
    }
    http.end();
}

// <<< ДОДАНО ФУНКЦІЮ ДЛЯ ГЕНЕРАЦІЇ QR-КОДУ >>>
void displayQrCode(lv_obj_t* parent) {
    if (WiFi.status() != WL_CONNECTED && WiFi.getMode() != WIFI_AP) return;

    String url = "http://";
    if(WiFi.getMode() == WIFI_AP) {
        url += WiFi.softAPIP().toString();
    } else {
        url += WiFi.localIP().toString();
    }
    
    // Створення QR коду
    QRCode qrcode;
    uint8_t qrcodeData[qrcode_getBufferSize(3)];
    qrcode_initText(&qrcode, qrcodeData, 3, ECC_LOW, url.c_str());

    // Розміри QR-коду (ширина і висота) та розмір пікселя
    int qr_size = qrcode.size;
    int pixel_size = 3; // Збільшуємо кожен піксель до 3x3
    int canvas_size = qr_size * pixel_size;

    // Створюємо буфер для LVGL канви
    qr_buf = (lv_color_t*)malloc(LV_CANVAS_BUF_SIZE_TRUE_COLOR(canvas_size, canvas_size));
    
    // Створюємо саму канву
    qr_canvas = lv_canvas_create(parent);
    lv_canvas_set_buffer(qr_canvas, qr_buf, canvas_size, canvas_size, LV_IMG_CF_TRUE_COLOR);
    lv_obj_align(qr_canvas, LV_ALIGN_CENTER, 0, 0);
    lv_canvas_fill_bg(qr_canvas, lv_color_white(), LV_OPA_COVER);

    // Малюємо QR-код на канві
    lv_draw_rect_dsc_t rect_dsc;
    lv_draw_rect_dsc_init(&rect_dsc);
    rect_dsc.bg_color = lv_color_black();
    
    for (int y = 0; y < qr_size; y++) {
        for (int x = 0; x < qr_size; x++) {
            if (qrcode_getModule(&qrcode, x, y)) {
                lv_canvas_draw_rect(qr_canvas, x * pixel_size, y * pixel_size, pixel_size, pixel_size, &rect_dsc);
            }
        }
    }
}


void loadSettings() {
  preferences.begin("watch-prefs", false);
  ssid = preferences.getString("ssid", ssid);
  password = preferences.getString("password", password);
  api_key = preferences.getString("api_key", api_key);
  city = preferences.getString("city", city);
  gmtOffset_sec = preferences.getLong("gmt_offset", gmtOffset_sec);
  daylightOffset_enabled = preferences.getBool("dst_enabled", daylightOffset_enabled);
  macStr = preferences.getString("mac_addr", macStr);
  sscanf(macStr.c_str(), "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx", 
         &macAddress[0], &macAddress[1], &macAddress[2], &macAddress[3], &macAddress[4], &macAddress[5]);
  preferences.end();
  Serial.println("Налаштування завантажено.");
}

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

static void wol_btn_event_cb(lv_event_t * e) {
    if(lv_event_get_code(e) == LV_EVENT_CLICKED) {
        send_wol_packet(macAddress);
    }
}

const lv_img_dsc_t* get_weather_icon_by_code(String icon_code) {
    if (icon_code == "01d") return &ui_img_sunny_png;
    if (icon_code == "01n") return &ui_img_clearnight_png;
    if (icon_code == "02d") return &ui_img_partlycloudy_png;
    if (icon_code == "02n") return &ui_img_partlycloudynight_png;
    if (icon_code == "03d" || icon_code == "03n") return &ui_img_cloudy_png;
    if (icon_code == "04d" || icon_code == "04n") return &ui_img_cloudy_png;
    if (icon_code == "09d" || icon_code == "09n") return &ui_img_drizzle_png;
    if (icon_code == "10d" || icon_code == "10n") return &ui_img_rain_png;
    if (icon_code == "11d" || icon_code == "11n") return &ui_img_rainthunderstorm_png;
    if (icon_code == "13d" || icon_code == "13n") return &ui_img_snow_png;
    if (icon_code == "50d" || icon_code == "50n") return &ui_img_fog_png;
    return &ui_img_cloudy_png; 
}

void arc_event_handler(lv_event_t * e) {
    int32_t currentValue = lv_arc_get_value(ui_ControlArc);
    int32_t snappedValue = round(currentValue / 2.0) * 2;
    if (snappedValue != lastSnappedValue) {
        lv_arc_set_value(ui_ControlArc, snappedValue);
        if (ui_SliderLabel) {
            lv_label_set_text_fmt(ui_SliderLabel, "%d", snappedValue);
        }
        bool is_volume_mode = lv_obj_has_state(ui_ModeSwitch, LV_STATE_CHECKED);
        if (!is_volume_mode) {
            currentBrightness = map(snappedValue, 0, 100, 0, 255);
            tft.setBrightness(currentBrightness);
        } else {
            if (bleKeyboard.isConnected()) {
                if (lastSnappedValue != -1) { 
                    if (snappedValue > lastSnappedValue) {
                        bleKeyboard.write(KEY_MEDIA_VOLUME_UP);
                    } else if (snappedValue < lastSnappedValue) {
                        bleKeyboard.write(KEY_MEDIA_VOLUME_DOWN);
                    }
                }
            }
        }
        lastSnappedValue = snappedValue;
    }
}

void switch_event_handler(lv_event_t * e) {
    bool is_volume_mode = lv_obj_has_state(ui_ModeSwitch, LV_STATE_CHECKED);
    if (is_volume_mode) {
        const int32_t val = 50;
        lv_arc_set_value(ui_ControlArc, val);
        if (ui_SliderLabel) {
            lv_label_set_text_fmt(ui_SliderLabel, "%d", val);
        }
        lastSnappedValue = val;
        if (bleKeyboard.isConnected()) {
            for (int i = 0; i < 50; i++) {
                bleKeyboard.write(KEY_MEDIA_VOLUME_UP);
                delay(10);
            }
            for (int i = 0; i < 25; i++) {
                bleKeyboard.write(KEY_MEDIA_VOLUME_DOWN);
                delay(10);
            }
        }
    } else {
        int brightness_percent = map(currentBrightness, 0, 255, 0, 100);
        int32_t val = (brightness_percent / 2) * 2;
        lv_arc_set_value(ui_ControlArc, val);
        if (ui_SliderLabel) {
            lv_label_set_text_fmt(ui_SliderLabel, "%d", val);
        }
        lastSnappedValue = val;
    }
}

void bluetooth_power_switch_event_cb(lv_event_t * e) {
    lv_obj_t* target_switch = lv_event_get_target(e);
    if (lv_obj_has_state(target_switch, LV_STATE_CHECKED)) {
        if (!bleKeyboardStarted) {
            bleKeyboard.begin();
            bleKeyboardStarted = true;
        }
    } else {
        if (bleKeyboardStarted) {
            bleKeyboard.end();
            bleKeyboardStarted = false;
        }
    }
}

void update_time_task(void *param) {
  struct tm timeinfo;
  while(1) {
    if (getLocalTime(&timeinfo)) {
      lv_label_set_text_fmt(ui_hour, "%02d", timeinfo.tm_hour);
      lv_label_set_text_fmt(ui_minute, "%02d", timeinfo.tm_min);
      lv_label_set_text_fmt(ui_second, "%02d", timeinfo.tm_sec);
      lv_label_set_text_fmt(ui_date, "%02d.%02d.%d", timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900);
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
        DynamicJsonDocument doc(2048);
        deserializeJson(doc, payload);
        if (!doc.isNull()) {
            lv_label_set_text(ui_city, doc["name"]);
            float temp = doc["main"]["temp"];
            String icon_code = doc["weather"][0]["icon"].as<String>();
            const lv_img_dsc_t* icon_src = get_weather_icon_by_code(icon_code);
            lv_label_set_text_fmt(ui_temperature, "%+d", (int)round(temp));
            lv_img_set_src(ui_WeatherIcon, icon_src);
            lv_label_set_text_fmt(ui_screensaver_temp, "%+d°", (int)round(temp));
            lv_img_set_src(ui_screensaver_icon, icon_src);
        }
    }
    http.end();
}

void update_wifi_status_icon() {
    if (WiFi.status() == WL_CONNECTED) {
        lv_obj_clear_flag(ui_WiFiON, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui_WIFIOFF, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(ui_WiFiON, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ui_WIFIOFF, LV_OBJ_FLAG_HIDDEN);
    }
}

static void check_inactivity_timer_cb(lv_timer_t * timer) {
    uint32_t inactive_time = lv_disp_get_inactive_time(NULL);
    if (inactive_time >= SCREENSAVER_TIMEOUT_MS && currentScreenState != SCREENSAVER) {
        lv_scr_load(ui_Screen1);
        tft.setBrightness(51); 
        if(ui_SettingsPanel) {
            lv_obj_set_y(ui_SettingsPanel, -210);
            if(ui_Down) lv_obj_clear_flag(ui_Down, LV_OBJ_FLAG_HIDDEN);
            if(ui_Upp) lv_obj_add_flag(ui_Upp, LV_OBJ_FLAG_HIDDEN);
        }
        lv_obj_add_flag(ui_fone, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui_WOLButton, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui_WeatherIcon, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui_temperature, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui_celsius, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui_date, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui_city, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui_WiFiON, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui_WIFIOFF, LV_OBJ_FLAG_HIDDEN);
        if(ui_SettingsPanel) lv_obj_add_flag(ui_SettingsPanel, LV_OBJ_FLAG_HIDDEN);
        if(ui_Down) lv_obj_add_flag(ui_Down, LV_OBJ_FLAG_HIDDEN);
        if(ui_Upp) lv_obj_add_flag(ui_Upp, LV_OBJ_FLAG_HIDDEN);
        if (ui_Screen2) {
            for (uint32_t i = 0; i < lv_obj_get_child_cnt(ui_Screen2); i++) {
                lv_obj_add_flag(lv_obj_get_child(ui_Screen2, i), LV_OBJ_FLAG_HIDDEN);
            }
        }
        lv_obj_set_style_bg_color(lv_scr_act(), lv_color_black(), 0);
        lv_obj_set_style_text_color(ui_hour, lv_color_white(), 0);
        lv_obj_set_style_text_color(ui_minute, lv_color_white(), 0);
        lv_obj_set_style_text_color(ui_second, lv_color_white(), 0);
        lv_obj_clear_flag(ui_screensaver_icon, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ui_screensaver_temp, LV_OBJ_FLAG_HIDDEN);
        currentScreenState = SCREENSAVER;
    }
    else if (inactive_time >= PARTIAL_DIM_TIMEOUT_MS && currentScreenState == AWAKE) {
        tft.setBrightness(currentBrightness / 2);
        currentScreenState = PARTIALLY_DIMMED;
    }
    else if (inactive_time < PARTIAL_DIM_TIMEOUT_MS && currentScreenState != AWAKE) {
        tft.setBrightness(currentBrightness);
        lv_obj_add_flag(ui_screensaver_icon, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui_screensaver_temp, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ui_fone, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ui_WOLButton, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ui_WeatherIcon, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ui_temperature, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ui_celsius, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ui_date, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ui_city, LV_OBJ_FLAG_HIDDEN);
        if(ui_SettingsPanel) {
            lv_obj_clear_flag(ui_SettingsPanel, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_y(ui_SettingsPanel, -210);
            if(ui_Down) lv_obj_clear_flag(ui_Down, LV_OBJ_FLAG_HIDDEN);
            if(ui_Upp) lv_obj_add_flag(ui_Upp, LV_OBJ_FLAG_HIDDEN);
        }
        if (ui_Screen2) {
            for (uint32_t i = 0; i < lv_obj_get_child_cnt(ui_Screen2); i++) {
                lv_obj_clear_flag(lv_obj_get_child(ui_Screen2, i), LV_OBJ_FLAG_HIDDEN);
            }
        }
        lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(0xFFFFFF), 0);
        if (ui_Screen1) lv_obj_set_style_bg_color(ui_Screen1, lv_color_hex(0xFFFFFF), 0);
        if (ui_Screen2) lv_obj_set_style_bg_color(ui_Screen2, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_color(ui_hour, lv_color_hex(0x000000), 0);
        lv_obj_set_style_text_color(ui_minute, lv_color_hex(0x000000), 0);
        lv_obj_set_style_text_color(ui_second, lv_color_hex(0x000000), 0);
        update_wifi_status_icon();
        currentScreenState = AWAKE;
    }
}

void setup() {
    Serial.begin(115200);

    loadSettings();
    btStop(); 
    
    tft.init(); tft.initDMA(); tft.startWrite();
    touch.begin(); 
    tft.setBrightness(currentBrightness);

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

    ui_init(); 
    
    ui_screensaver_icon = lv_img_create(lv_scr_act());
    lv_img_set_zoom(ui_screensaver_icon, 320); 
    lv_obj_align(ui_screensaver_icon, LV_ALIGN_CENTER, 0, 15);
    lv_obj_add_flag(ui_screensaver_icon, LV_OBJ_FLAG_HIDDEN);

    lv_style_init(&style_screensaver_temp);
    lv_style_set_text_font(&style_screensaver_temp, &lv_font_montserrat_28);
    lv_style_set_text_color(&style_screensaver_temp, lv_color_white());

    ui_screensaver_temp = lv_label_create(lv_scr_act());
    lv_obj_add_style(ui_screensaver_temp, &style_screensaver_temp, 0); 
    lv_obj_align_to(ui_screensaver_temp, ui_screensaver_icon, LV_ALIGN_OUT_BOTTOM_MID, 0, 15); 
    lv_obj_add_flag(ui_screensaver_temp, LV_OBJ_FLAG_HIDDEN);

    lv_obj_add_event_cb(ui_WOLButton, wol_btn_event_cb, LV_EVENT_CLICKED, NULL);

    if (ui_ControlArc) {
        lv_obj_add_event_cb(ui_ControlArc, arc_event_handler, LV_EVENT_VALUE_CHANGED, NULL);
        int initialArcValue = map(currentBrightness, 0, 255, 0, 100);
        int32_t val = round(initialArcValue / 2.0) * 2;
        lv_arc_set_value(ui_ControlArc, val);
        if(ui_SliderLabel) {
            lv_label_set_text_fmt(ui_SliderLabel, "%d", val);
        }
        lastSnappedValue = val;
    }
    
    if(ui_ModeSwitch) {
        lv_obj_add_event_cb(ui_ModeSwitch, switch_event_handler, LV_EVENT_VALUE_CHANGED, NULL);
    }
    
    if(ui_BluetoothSwitch) {
        lv_obj_add_event_cb(ui_BluetoothSwitch, bluetooth_power_switch_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    }
    
    update_wifi_status_icon();

    lv_label_set_text(ui_city, "Loading...");
    lv_label_set_text(ui_temperature, "--");
    
    WiFi.begin(ssid.c_str(), password.c_str());
    int wifi_retries = 20;
    while (WiFi.status() != WL_CONNECTED && wifi_retries > 0) { 
      delay(500); 
      Serial.print("."); 
      wifi_retries--;
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\nWiFi Connected!");
        Serial.print("IP Address: ");
        Serial.println(WiFi.localIP());

        setupWebServer();
        checkForUpdates();
        update_wifi_status_icon();
        update_weather();

        int dst = daylightOffset_enabled ? daylightOffset_sec : 0;
        configTime(gmtOffset_sec, dst, ntpServer);
        xTaskCreate(update_time_task, "time_task", 4096, NULL, 5, NULL);
    } else {
        Serial.println("\nCould not connect to WiFi. Starting AP mode.");
        lv_label_set_text(ui_city, "AP Mode");
        lv_label_set_text(ui_temperature, "Setup");
        WiFi.softAP("Watch_Setup", "12345678");
        Serial.print("AP IP address: ");
        Serial.println(WiFi.softAPIP());
        
        setupWebServer();
    }
    
    // <<< ІНТЕГРАЦІЯ ВЕРСІЇ ПРОШИВКИ ТА QR-КОДУ >>>
    if(ui_SettingsPanel) {
        // Оновлюємо текстову мітку 'Firmware', створену в SLS
        lv_label_set_text_fmt(ui_Firmware, "Ver: %s", FIRMWARE_VERSION);

        // Генеруємо і показуємо QR-код
        displayQrCode(ui_SettingsPanel);
    }
    
    lv_timer_create(check_inactivity_timer_cb, 500, NULL);
    lv_timer_create([](lv_timer_t* t){ if(WiFi.status() == WL_CONNECTED) update_weather(); }, 1800000, NULL);
}

void loop() {
    lv_timer_handler();

    handleWebServerClient();

    static uint32_t last_wifi_check = 0;
    if (millis() - last_wifi_check > 2000) {
        if (currentScreenState == AWAKE) { 
            update_wifi_status_icon();
        }
        last_wifi_check = millis();
    }
    
    delay(5);
}