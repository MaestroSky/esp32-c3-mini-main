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
#include "ui/ui.h" // Підключаємо головний файл UI

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

static bool is_dimmed = false;
const uint32_t INACTIVITY_TIMEOUT_MS = 5000; // Змінено на 5 сек згідно з запитом

// <<< ДОДАНО: Глобальні змінні для об'єктів режиму енергозбереження >>>
static lv_obj_t *ui_screensaver_icon;
static lv_obj_t *ui_screensaver_temp;
static lv_style_t style_screensaver_temp;


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

// <<< ЗМІНЕНО: Функція оновлення погоди тепер також оновлює дані для заставки >>>
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

            // Оновлюємо основний екран
            lv_label_set_text_fmt(ui_temperature, "%+d", (int)round(temp));
            lv_img_set_src(ui_WeatherIcon, icon_src);
            
            // Оновлюємо дані для екрану енергозбереження
            // Додаємо знак градуса ° для кращого вигляду
            lv_label_set_text_fmt(ui_screensaver_temp, "%+d°", (int)round(temp));
            lv_img_set_src(ui_screensaver_icon, icon_src);
        }
    }
    http.end();
}

// <<< ЗМІНЕНО: Додано керування яскравістю екрана >>>
static void check_inactivity_timer_cb(lv_timer_t * timer) {
    uint32_t inactive_time = lv_disp_get_inactive_time(NULL);

    // --- БЛОК АКТИВАЦІЇ ЕНЕРГОЗБЕРЕЖЕННЯ ---
    if (inactive_time >= INACTIVITY_TIMEOUT_MS && !is_dimmed) {
        // <<< ДОДАНО: Зменшуємо яскравість екрана до 20% >>>
        tft.setBrightness(25.5); // 255 * 0.1 = 25.5

        // 1. Ховаємо всі елементи основного інтерфейсу
        lv_obj_add_flag(ui_fone, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui_WOLButton, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui_WeatherIcon, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui_temperature, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui_celsius, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui_date, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui_city, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui_WiFiON, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui_WIFIOFF, LV_OBJ_FLAG_HIDDEN);

        // 2. Робимо фон екрана чорним
        lv_obj_set_style_bg_color(lv_scr_act(), lv_color_black(), 0);
        
        // 3. Робимо текст годинника білим
        lv_obj_set_style_text_color(ui_hour, lv_color_white(), 0);
        lv_obj_set_style_text_color(ui_minute, lv_color_white(), 0);
        lv_obj_set_style_text_color(ui_second, lv_color_white(), 0);

        // 4. Показуємо спеціальні елементи для режиму енергозбереження
        lv_obj_clear_flag(ui_screensaver_icon, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ui_screensaver_temp, LV_OBJ_FLAG_HIDDEN);

        is_dimmed = true;
    }

    // --- БЛОК "ПРОБУДЖЕННЯ" ЕКРАНА ---
    if (inactive_time < INACTIVITY_TIMEOUT_MS && is_dimmed) {
        // <<< ДОДАНО: Повертаємо яскравість екрана на максимум >>>
        tft.setBrightness(255);

        // 1. Ховаємо елементи режиму енергозбереження
        lv_obj_add_flag(ui_screensaver_icon, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui_screensaver_temp, LV_OBJ_FLAG_HIDDEN);

        // 2. Показуємо всі елементи основного інтерфейсу
        lv_obj_clear_flag(ui_fone, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ui_WOLButton, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ui_WeatherIcon, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ui_temperature, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ui_celsius, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ui_date, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ui_city, LV_OBJ_FLAG_HIDDEN);
        
        // 3. Повертаємо стилі, які були задані в SquareLine
        lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_color(ui_hour, lv_color_hex(0x000000), 0);
        lv_obj_set_style_text_color(ui_minute, lv_color_hex(0x000000), 0);
        lv_obj_set_style_text_color(ui_second, lv_color_hex(0x000000), 0);

        is_dimmed = false;
    }
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

    ui_init(); 
    
    // <<< ДОДАНО: Створення та налаштування об'єктів для екрану енергозбереження >>>
    // 1. Створюємо іконку погоди для заставки
    ui_screensaver_icon = lv_img_create(lv_scr_act());
    lv_img_set_zoom(ui_screensaver_icon, 320); // Збільшуємо на ~25% (320/256)
    lv_obj_align(ui_screensaver_icon, LV_ALIGN_CENTER, 0, 15); // Центруємо і трохи зміщуємо вниз від центру екрана
    lv_obj_add_flag(ui_screensaver_icon, LV_OBJ_FLAG_HIDDEN); // Ховаємо початково

    // 2. Створюємо стиль для температури на заставці (великий, жирний шрифт)
    lv_style_init(&style_screensaver_temp);
    // ВАЖЛИВО: Переконайтеся, що шрифт lv_font_montserrat_28_bold увімкнений у вашому файлі lv_conf.h
    // Якщо його немає, ви можете обрати інший великий шрифт, наприклад lv_font_montserrat_28 або згенерувати новий
    lv_style_set_text_font(&style_screensaver_temp, &lv_font_montserrat_28);
    lv_style_set_text_color(&style_screensaver_temp, lv_color_white());

    // 3. Створюємо напис температури для заставки
    ui_screensaver_temp = lv_label_create(lv_scr_act());
    lv_obj_add_style(ui_screensaver_temp, &style_screensaver_temp, 0); // Застосовуємо створений стиль
    lv_obj_align_to(ui_screensaver_temp, ui_screensaver_icon, LV_ALIGN_OUT_BOTTOM_MID, 0, 15); // Розміщуємо під іконкою
    lv_obj_add_flag(ui_screensaver_temp, LV_OBJ_FLAG_HIDDEN); // Ховаємо початково

    lv_obj_add_event_cb(ui_WOLButton, wol_btn_event_cb, LV_EVENT_CLICKED, NULL);

    update_wifi_status_icon();

    lv_label_set_text(ui_city, "Loading...");
    lv_label_set_text(ui_temperature, "--");

    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
    Serial.println("\nWiFi Connected!");
    
    update_wifi_status_icon();
    update_weather();
    
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    xTaskCreate(update_time_task, "time_task", 4096, NULL, 5, NULL);
    
    lv_timer_create(check_inactivity_timer_cb, 500, NULL);
    lv_timer_create([](lv_timer_t* t){ update_weather(); }, 1800000, NULL);
}

void loop() {
    lv_timer_handler();

    static uint32_t last_wifi_check = 0;
    if (millis() - last_wifi_check > 2000) {
        if (!is_dimmed) {
            update_wifi_status_icon();
        }
        last_wifi_check = millis();
    }
    
    delay(5);
}