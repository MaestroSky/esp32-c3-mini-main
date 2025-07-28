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

// <<< ВАЖЛИВО: Оголошуємо іконки, які будемо використовувати динамічно >>>
// SquareLine оголошує лише ті іконки, які використовуються в проєкті при експорті (хмара).
// Інші (сонце, дощ і т.д.) потрібно оголосити вручну після конвертації.
LV_IMG_DECLARE(ui_img_cloud_png); // Ця іконка вже є у вашому проєкті
// LV_IMG_DECLARE(ui_img_sun_png);   // Додайте цю, коли сконвертуєте іконку сонця
// LV_IMG_DECLARE(ui_img_rain_png);  // Додайте цю, коли сконвертуєте іконку дощу


// --- КОНФІГУРАЦІЯ LGFX (без змін) ---
class LGFX : public lgfx::LGFX_Device {
  // ... (ваш код конфігурації LGFX залишається без змін) ...
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
const uint32_t INACTIVITY_TIMEOUT_MS = 10000; // Збільшимо час до 10 сек

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

// Обробник події для кнопки Wake-on-LAN
static void wol_btn_event_cb(lv_event_t * e) {
    if(lv_event_get_code(e) == LV_EVENT_CLICKED) {
        send_wol_packet(macAddress);
        // Візуальний відгук (анімація) має бути налаштований у SquareLine Studio
    }
}

// Функція для вибору іконки погоди за кодом з API
const lv_img_dsc_t* get_weather_icon_by_code(String icon_code) {
    if (icon_code.startsWith("02") || icon_code.startsWith("03") || icon_code.startsWith("04")) {
        return &ui_img_cloud_png; 
    }
    // if (icon_code.startsWith("01")) return &ui_img_sun_png; // Розкоментуйте, коли додасте іконку сонця
    // ... додайте інші умови для дощу, снігу, грози і т.д.
    
    return &ui_img_cloud_png; // Іконка за замовчуванням
}

// Оновлення дати й часу для окремих полів
void update_time_task(void *param) {
  struct tm timeinfo;
  while(1) {
    if (getLocalTime(&timeinfo)) {
      // !!! Перевірте імена ui_hour, ui_minute і т.д. у файлі src/ui/ui_Screen1.h !!!
      lv_label_set_text_fmt(ui_hour, "%02d", timeinfo.tm_hour);
      lv_label_set_text_fmt(ui_minute, "%02d", timeinfo.tm_min);
      lv_label_set_text_fmt(ui_second, "%02d", timeinfo.tm_sec);
      lv_label_set_text_fmt(ui_date, "%02d.%02d.%d", timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900);
    }
    vTaskDelay(1000);
  }
}

// Оновлення погоди та іконки
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
            lv_label_set_text_fmt(ui_temperature, "%+d", (int)round(temp));
            String icon_code = doc["weather"][0]["icon"].as<String>();
            lv_img_set_src(ui_WeatherIcon, get_weather_icon_by_code(icon_code));
        }
    }
    http.end();
}

// Режим затемнення екрана
static void check_inactivity_timer_cb(lv_timer_t * timer) {
    uint32_t inactive_time = lv_disp_get_inactive_time(NULL);

    if (inactive_time >= INACTIVITY_TIMEOUT_MS && !is_dimmed) {
        // Затемнюємо: ховаємо непотрібні елементи
        lv_obj_add_flag(ui_WOLButton, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui_WeatherIcon, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui_temperature, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui_celsius, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui_date, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui_city, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui_fone, LV_OBJ_FLAG_HIDDEN); // Ховаємо фонове зображення
        is_dimmed = true;
    }

    if (inactive_time < INACTIVITY_TIMEOUT_MS && is_dimmed) {
        // Повертаємо до життя: показуємо всі елементи
        lv_obj_clear_flag(ui_WOLButton, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ui_WeatherIcon, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ui_temperature, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ui_celsius, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ui_date, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ui_city, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ui_fone, LV_OBJ_FLAG_HIDDEN); // Показуємо фонове зображення
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

    // Створюємо весь інтерфейс з SquareLine.
    // Анімації, які ви налаштували в студії, запустяться автоматично.
    ui_init(); 
    
    // Прив'язуємо подію до кнопки
    // !!! Перевірте ім'я 'ui_WOLButton' у файлі 'src/ui/ui_Screen1.h' !!!
    lv_obj_add_event_cb(ui_WOLButton, wol_btn_event_cb, LV_EVENT_CLICKED, NULL);

    // Встановлюємо початковий текст
    lv_label_set_text(ui_city, "Loading...");
    lv_label_set_text(ui_temperature, "--");

    // Запускаємо логіку
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
    Serial.println("\nWiFi Connected!");
    
    update_weather();
    
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    xTaskCreate(update_time_task, "time_task", 4096, NULL, 5, NULL);
    
    lv_timer_create(check_inactivity_timer_cb, 500, NULL);
    lv_timer_create([](lv_timer_t* t){ update_weather(); }, 1800000, NULL);
}

void loop() {
    lv_timer_handler();
    delay(5);
}