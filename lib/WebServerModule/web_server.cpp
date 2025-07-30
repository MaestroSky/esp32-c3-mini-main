#include "Arduino.h"
#include "web_server.h" // Підключаємо наш заголовочний файл
#include <Preferences.h>
#include "main.h"       // Підключаємо для доступу до #define FIRMWARE_VERSION

// Об'єкт сервера та переклади тепер "живуть" тут, ізольовано від main.
WebServer server(80);

// --- Структури та змінні для перекладу ---
struct Translations {
    const char* pageTitle;
    const char* header;
    const char* langUkrainian;
    const char* langEnglish;
    
    const char* h2_basicSettings;
    const char* label_ssid;
    const char* label_password;
    const char* label_apiKey;
    
    const char* h2_personalization;
    const char* label_city;
    const char* label_mac;
    const char* label_timezone;
    const char* label_dst;
    
    const char* h2_firmware;
    const char* label_current_version;
    const char* label_latest_version;
    const char* update_notification;
    const char* no_update_notification;

    const char* button_save;
};

// Ініціалізуємо тексти для української мови
const Translations t_uk = {
    "Налаштування годинника", "Налаштування годинника", "Українська", "English",
    "Базові налаштування", "Назва мережі Wi-Fi (SSID)", "Пароль Wi-Fi", "Ключ API для погоди",
    "Персоналізація", "Місто для погоди", "MAC-адреса для Wake-on-LAN", "Часовий пояс", "Автоматичний перехід на літній час",
    "Прошивка", "Поточна версія:", "Остання версія:", "<a href='https://github.com/MaestroSky/esp32-c3-mini-main/releases' target='_blank'>Доступне оновлення! Натисніть, щоб завантажити.</a>", "У вас остання версія.",
    "Зберегти і перезавантажити"
};

// Ініціалізуємо тексти для англійської мови
const Translations t_en = {
    "Watch Settings", "Watch Settings", "Українська", "English",
    "Basic Settings", "Wi-Fi Network Name (SSID)", "Wi-Fi Password", "Weather API Key",
    "Personalization", "City for Weather", "MAC Address for Wake-on-LAN", "Timezone", "Enable Daylight Saving Time",
    "Firmware", "Current Version:", "Latest Version:", "<a href='https://github.com/MaestroSky/esp32-c3-mini-main/releases' target='_blank'>Update available! Click to download.</a>", "You have the latest version.",
    "Save and Reboot"
};

String currentLanguage = "uk";
// -----------------------------------------

// Обробник збереження налаштувань з веб-форми
void handleSave() {
  Serial.println("Отримано запит на збереження налаштувань...");
  
  // Зберігаємо нові значення у глобальні змінні
  ssid = server.arg("ssid");
  password = server.arg("password");
  api_key = server.arg("api_key");
  city = server.arg("city");
  gmtOffset_sec = server.arg("gmt_offset").toInt();
  daylightOffset_enabled = server.hasArg("dst_enabled");
  macStr = server.arg("mac_addr");

  // Зберігаємо змінні у пам'ять
  Preferences preferences;
  preferences.begin("watch-prefs", false);
  preferences.putString("ssid", ssid);
  preferences.putString("password", password);
  preferences.putString("api_key", api_key);
  preferences.putString("city", city);
  preferences.putLong("gmt_offset", gmtOffset_sec);
  preferences.putBool("dst_enabled", daylightOffset_enabled);
  preferences.putString("mac_addr", macStr);
  preferences.end();
  
  Serial.println("Налаштування збережено. Перезавантаження...");

  // Відправляємо сторінку з повідомленням про успіх
  String html = "<!DOCTYPE html><html><head><title>Збережено</title><meta charset='UTF-8'>";
  html += "<style>body{font-family: sans-serif; text-align: center; padding-top: 50px; background: #f0f0f0;}</style>";
  html += "</head><body><h1>Налаштування збережено!</h1><p>Пристрій перезавантажиться за 3 секунди, щоб застосувати зміни.</p></body></html>";
  server.send(200, "text/html; charset=utf-8", html);

  // Перезавантажуємо ESP, щоб застосувати нові налаштування Wi-Fi
  delay(3000);
  ESP.restart();
}

// Обробник головної сторінки, який генерує HTML
void handleRoot() {
  // Перевірка мови
  if (server.hasArg("lang")) {
    String lang = server.arg("lang");
    if (lang == "en" || lang == "uk") {
      currentLanguage = lang;
    }
  }
  const Translations* t;
  if (currentLanguage == "uk") {
    t = &t_uk;
  } else {
    t = &t_en;
  }

  // Початок HTML
  String html = "<!DOCTYPE html><html><head><title>";
  html += t->pageTitle;
  html += "</title><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1'>";
  // Стилі для гарного вигляду
  html += "<style>"
          "body{font-family: sans-serif; background: #f0f0f0; margin: 0;}"
          "a{color: #007bff; text-decoration: none;}"
          ".container{max-width: 800px; margin: 20px auto; padding: 20px; background: #fff; border-radius: 8px; box-shadow: 0 2px 4px rgba(0,0,0,0.1);}"
          "h1,h2{text-align: center; color: #333; border-bottom: 1px solid #eee; padding-bottom: 10px; margin-top: 0;}"
          "form{display: flex; flex-direction: column; gap: 15px;}"
          ".form-group{display: flex; flex-direction: column;}"
          "label{margin-bottom: 5px; font-weight: bold; color: #555;}"
          "input, select{padding: 10px; border: 1px solid #ccc; border-radius: 4px; font-size: 1em;}"
          ".cb-label{display: flex; align-items: center;}"
          "input[type=checkbox]{width: auto; margin-right: 10px; transform: scale(1.2);}"
          "button{padding: 12px; background: #007bff; color: white; border: none; border-radius: 4px; font-size: 1.1em; cursor: pointer; margin-top: 10px;}"
          "button:hover{background: #0056b3;}"
          ".lang-switch{text-align: center; margin-bottom: 20px;}"
          "</style>";
  html += "</head><body><div class='container'>";
  
  html += "<h1>" + String(t->header) + "</h1>";
  
  // Перемикач мови
  html += "<div class='lang-switch'><a href='/?lang=uk'>" + String(t->langUkrainian) + "</a> | <a href='/?lang=en'>" + String(t->langEnglish) + "</a></div>";
  
  // Форма для налаштувань
  html += "<form action='/save' method='POST'>";

  // --- Секція 1: Базові налаштування ---
  html += "<h2>" + String(t->h2_basicSettings) + "</h2>";
  html += "<div class='form-group'><label for='ssid'>" + String(t->label_ssid) + "</label><input type='text' id='ssid' name='ssid' value='" + ssid + "'></div>";
  html += "<div class='form-group'><label for='password'>" + String(t->label_password) + "</label><input type='password' id='password' name='password' value='" + password + "'></div>";
  html += "<div class='form-group'><label for='api_key'>" + String(t->label_apiKey) + "</label><input type='text' id='api_key' name='api_key' value='" + api_key + "'></div>";
  
  // --- Секція 2: Персоналізація ---
  html += "<h2>" + String(t->h2_personalization) + "</h2>";
  html += "<div class='form-group'><label for='city'>" + String(t->label_city) + "</label><input type='text' id='city' name='city' value='" + city + "'></div>";
  html += "<div class='form-group'><label for='mac_addr'>" + String(t->label_mac) + "</label><input type='text' id='mac_addr' name='mac_addr' value='" + macStr + "'></div>";
  
  html += "<div class='form-group'><label for='gmt_offset'>" + String(t->label_timezone) + "</label><select id='gmt_offset' name='gmt_offset'>";
  int offsets[] = {-43200, -39600, -36000, -28800, -25200, -21600, -18000, -14400, -10800, -3600, 0, 3600, 7200, 10800, 14400, 18000, 21600, 25200, 28800, 32400, 34200, 39600, 46800};
  String zones[] = {"(GMT-12)","(GMT-11)","(GMT-10) Hawaii","(GMT-8) Los Angeles","(GMT-7) Denver","(GMT-6) Chicago","(GMT-5) New York","(GMT-4)","(GMT-3) Buenos Aires","(GMT-1)","(GMT 0) London","(GMT+1) Berlin","(GMT+2) Kyiv","(GMT+3) Moscow","(GMT+4)","(GMT+5)","(GMT+6)","(GMT+7)","(GMT+8) Hong Kong","(GMT+9) Tokyo","(GMT+9:30)","(GMT+11)","(GMT+13)"};
  for(unsigned int i=0; i < sizeof(offsets)/sizeof(int); i++){
    html += "<option value='" + String(offsets[i]) + "'";
    if(offsets[i] == gmtOffset_sec) html += " selected";
    html += ">" + zones[i] + "</option>";
  }
  html += "</select></div>";
  
  html += "<div class='form-group'><label class='cb-label'><input type='checkbox' name='dst_enabled'";
  if(daylightOffset_enabled) html += " checked";
  html += "> " + String(t->label_dst) + "</label></div>";

  // --- Секція 3: Прошивка ---
  html += "<h2>" + String(t->h2_firmware) + "</h2>";
  html += "<p>" + String(t->label_current_version) + " " + String(FIRMWARE_VERSION) + "</p>";
  if (update_available) {
      html += "<p>" + String(t->label_latest_version) + " " + latest_version + "</p>";
      html += "<p style='color:green; font-weight:bold;'>" + String(t->update_notification) + "</p>";
  } else {
      html += "<p style='color:grey;'>" + String(t->no_update_notification) + "</p>";
  }
  
  // --- Кнопка Зберегти ---
  html += "<button type='submit'>" + String(t->button_save) + "</button>";
  
  html += "</form>";
  html += "</div></body></html>";

  server.send(200, "text/html; charset=utf-8", html);
}

// Реалізація наших публічних функцій
void setupWebServer() {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.begin();
  Serial.println("Веб-сервер запущено.");
}

void handleWebServerClient() {
  server.handleClient();
}