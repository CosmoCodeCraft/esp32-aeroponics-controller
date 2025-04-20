#include <Arduino.h>
#include "driver/ledc.h"  // Новый API LEDC
#include <WiFi.h>
#include <WebServer.h>
#include <time.h>

// =======================
// Wi‑Fi настройки (режим точки доступа)
// =======================
const char* ssid = "ESP32_AP";
const char* apPassword = "12345678";  // Задайте пароль

WebServer server(80);

// =======================
// Пины устройств и PWM
// =======================
const int pumpPin      = 25;   // Насос
const int redLEDPin    = 26;   // Красная светодиодная лента
const int blueLEDPin   = 27;   // Синяя светодиодная лента
const int whiteLEDPin  = 14;   // Белая светодиодная лента

// Для трёх лент используем три канала PWM:
const ledc_channel_t blueChannel  = LEDC_CHANNEL_0; // Синяя лента
const ledc_channel_t redChannel   = LEDC_CHANNEL_1; // Красная лента
const ledc_channel_t whiteChannel = LEDC_CHANNEL_2; // Белая лента

const int pwmFreq       = 5000;               // Частота PWM (Гц)
const int pwmResolution = 8;                  // 8-битное разрешение (0–255)
const int maxDuty       = (1 << pwmResolution) - 1;  // 255

// В автоматическом режиме яркость задаётся в процентах:
const int redBrightnessAutoPercent   = 100;  // для красной ленты
const int blueBrightnessAutoPercent  = 25;   // для синей ленты
const int whiteBrightnessAutoPercent = 10;  // для белой ленты

// =======================
// Режимы работы
// =======================
enum Mode { MODE_AUTO, MODE_REGULATED, MODE_DIRECT };
volatile Mode currentMode = MODE_AUTO;

// В автоматическом режиме выбор ленты: false - red+blue, true - white
volatile bool autoUseWhite = false;

// =======================
// Параметры автоматики (автоматический режим)
// =======================
// Интервалы задаются в секундах
const unsigned long pumpIntervalAutoSec = 300;  // 300 сек = 5 минут
const unsigned long pumpOnDurationAutoSec = 10;   // 10 сек
unsigned long lastPumpActivationAuto = 0;
bool pumpActiveAuto = false;

// =======================
// Ведение времени (локальное время)
// =======================
time_t currentUnixTime = 0;            // Текущее время в секундах с эпохи
unsigned long lastTimeUpdateMillis = 0;  // Милисекундная отметка последнего обновления

// Функция обновления времени
void updateCurrentTime() {
  unsigned long delta = millis() - lastTimeUpdateMillis;
  if (delta >= 1000UL) {
    currentUnixTime += delta / 1000;
    lastTimeUpdateMillis = millis() - (delta % 1000);
  }
}

// =======================
// Функция парсинга строки времени
// =======================
time_t parseTimeString(String timeStr) {
  // Ожидаемый формат: "ГГГГ,ММ,ДД,ЧЧ,ММ,СС"
  int year = timeStr.substring(0,4).toInt();
  int month = timeStr.substring(5,7).toInt();
  int day = timeStr.substring(8,10).toInt();
  int hour = timeStr.substring(11,13).toInt();
  int minute = timeStr.substring(14,16).toInt();
  int second = timeStr.substring(17,19).toInt();
  struct tm t;
  t.tm_year = year - 1900;
  t.tm_mon  = month - 1;
  t.tm_mday = day;
  t.tm_hour = hour;
  t.tm_min  = minute;
  t.tm_sec  = second;
  t.tm_isdst = -1;
  return mktime(&t);
}

// =======================
// Параметры boot-периода
// =======================
const unsigned long bootLedDurationSec = 30;  // 30 сек (boot-период)
unsigned long bootLedStartTime = 0;

// =======================
// Параметры регулируемого режима
// =======================
// Яркость задаётся в процентах (0–100)
volatile int manualRedBrightness   = 100;  // для красной ленты
volatile int manualBlueBrightness  = 25;   // для синей ленты
volatile int manualWhiteBrightness = 100;  // для белой ленты
// Интервалы для насоса (в секундах)
volatile unsigned long manualPumpOnDuration  = 10;   // сек
volatile unsigned long manualPumpOffDuration = 300;  // сек

// =======================
// Параметры режима прямого управления
// =======================
volatile bool manualPumpState      = false;
volatile bool manualRedLEDState    = false;
volatile bool manualBlueLEDState   = false;
volatile bool manualWhiteLEDState  = false;

// =======================
// Параметры графика освещения (для автоматического режима)
// =======================
volatile int scheduleOffHour = 10;  // Отключение: 10:00
volatile int scheduleOnHour  = 22;  // Включение: 22:00

// =======================
// Настройка PWM для синей ленты
// =======================
void setupBlueLEDPWM() {
  ledc_timer_config_t ledc_timer = {
      .speed_mode      = LEDC_HIGH_SPEED_MODE,
      .duty_resolution = (ledc_timer_bit_t)pwmResolution,
      .timer_num       = LEDC_TIMER_0,
      .freq_hz         = pwmFreq,
      .clk_cfg         = LEDC_AUTO_CLK
  };
  ledc_timer_config(&ledc_timer);
  
  ledc_channel_config_t blue_channel = {
      .gpio_num   = blueLEDPin,
      .speed_mode = LEDC_HIGH_SPEED_MODE,
      .channel    = blueChannel,
      .intr_type  = LEDC_INTR_DISABLE,
      .timer_sel  = LEDC_TIMER_0,
      .duty       = 0,
      .hpoint     = 0
  };
  ledc_channel_config(&blue_channel);
}

// =======================
// Настройка PWM для красной ленты
// =======================
void setupRedLEDPWM() {
  ledc_channel_config_t red_channel = {
      .gpio_num   = redLEDPin,
      .speed_mode = LEDC_HIGH_SPEED_MODE,
      .channel    = redChannel,
      .intr_type  = LEDC_INTR_DISABLE,
      .timer_sel  = LEDC_TIMER_0,
      .duty       = 0,
      .hpoint     = 0
  };
  ledc_channel_config(&red_channel);
}

// =======================
// Настройка PWM для белой ленты
// =======================
void setupWhiteLEDPWM() {
  ledc_channel_config_t white_channel = {
      .gpio_num   = whiteLEDPin,
      .speed_mode = LEDC_HIGH_SPEED_MODE,
      .channel    = whiteChannel,
      .intr_type  = LEDC_INTR_DISABLE,
      .timer_sel  = LEDC_TIMER_0,
      .duty       = 0,
      .hpoint     = 0
  };
  ledc_channel_config(&white_channel);
}

// =======================
// Функции автоматического режима
// =======================
void handleAutoPump(unsigned long currentMillis) {
  if (!pumpActiveAuto && (currentMillis - lastPumpActivationAuto >= pumpIntervalAutoSec * 1000UL)) {
    digitalWrite(pumpPin, HIGH);
    pumpActiveAuto = true;
    lastPumpActivationAuto = currentMillis;
  }
  if (pumpActiveAuto && (currentMillis - lastPumpActivationAuto >= pumpOnDurationAutoSec * 1000UL)) {
    digitalWrite(pumpPin, LOW);
    pumpActiveAuto = false;
  }
}

void handleAutoLED() {
  bool ledShouldBeOn = false;
  if ((millis() - bootLedStartTime) < bootLedDurationSec * 1000UL) {
    ledShouldBeOn = true;
  } else {
    struct tm * timeinfo = localtime(&currentUnixTime);
    if ((timeinfo->tm_hour >= scheduleOnHour) || (timeinfo->tm_hour < scheduleOffHour)) {
      ledShouldBeOn = true;
    }
  }
  
  if (ledShouldBeOn) {
    if (autoUseWhite) {
      // Режим автоматической работы белой ленты:
      int whiteDuty = (whiteBrightnessAutoPercent * maxDuty) / 100;
      ledc_set_duty(LEDC_HIGH_SPEED_MODE, whiteChannel, whiteDuty);
      ledc_update_duty(LEDC_HIGH_SPEED_MODE, whiteChannel);
      // Остальные ленты выключаем:
      ledc_set_duty(LEDC_HIGH_SPEED_MODE, redChannel, 0);
      ledc_update_duty(LEDC_HIGH_SPEED_MODE, redChannel);
      ledc_set_duty(LEDC_HIGH_SPEED_MODE, blueChannel, 0);
      ledc_update_duty(LEDC_HIGH_SPEED_MODE, blueChannel);
    } else {
      // Режим автоматической работы красной и синей лент:
      int redDuty  = (redBrightnessAutoPercent * maxDuty) / 100;
      int blueDuty = (blueBrightnessAutoPercent * maxDuty) / 100;
      ledc_set_duty(LEDC_HIGH_SPEED_MODE, redChannel, redDuty);
      ledc_update_duty(LEDC_HIGH_SPEED_MODE, redChannel);
      ledc_set_duty(LEDC_HIGH_SPEED_MODE, blueChannel, blueDuty);
      ledc_update_duty(LEDC_HIGH_SPEED_MODE, blueChannel);
      // Белая лента выключена:
      ledc_set_duty(LEDC_HIGH_SPEED_MODE, whiteChannel, 0);
      ledc_update_duty(LEDC_HIGH_SPEED_MODE, whiteChannel);
    }
  } else {
    // Выключаем все ленты:
    ledc_set_duty(LEDC_HIGH_SPEED_MODE, redChannel, 0);
    ledc_update_duty(LEDC_HIGH_SPEED_MODE, redChannel);
    ledc_set_duty(LEDC_HIGH_SPEED_MODE, blueChannel, 0);
    ledc_update_duty(LEDC_HIGH_SPEED_MODE, blueChannel);
    ledc_set_duty(LEDC_HIGH_SPEED_MODE, whiteChannel, 0);
    ledc_update_duty(LEDC_HIGH_SPEED_MODE, whiteChannel);
  }
}

void handleAutoMode() {
  unsigned long now = millis();
  handleAutoPump(now);
  handleAutoLED();
}

// =======================
// Функции регулируемого режима
// =======================
void handleRegulatedMode() {
  static unsigned long lastPumpTime = 0;
  static bool pumpActive = false;
  unsigned long now = millis();
  
  if (!pumpActive && (now - lastPumpTime >= manualPumpOffDuration * 1000UL)) {
    digitalWrite(pumpPin, HIGH);
    pumpActive = true;
    lastPumpTime = now;
  }
  if (pumpActive && (now - lastPumpTime >= manualPumpOnDuration * 1000UL)) {
    digitalWrite(pumpPin, LOW);
    pumpActive = false;
    lastPumpTime = now;
  }
  
  int redDuty   = (manualRedBrightness * maxDuty) / 100;
  int blueDuty  = (manualBlueBrightness * maxDuty) / 100;
  int whiteDuty = (manualWhiteBrightness * maxDuty) / 100;
  ledc_set_duty(LEDC_HIGH_SPEED_MODE, redChannel, redDuty);
  ledc_update_duty(LEDC_HIGH_SPEED_MODE, redChannel);
  ledc_set_duty(LEDC_HIGH_SPEED_MODE, blueChannel, blueDuty);
  ledc_update_duty(LEDC_HIGH_SPEED_MODE, blueChannel);
  ledc_set_duty(LEDC_HIGH_SPEED_MODE, whiteChannel, whiteDuty);
  ledc_update_duty(LEDC_HIGH_SPEED_MODE, whiteChannel);
}

// =======================
// Функции режима прямого управления
// =======================
void handleDirectMode() {
  digitalWrite(pumpPin, manualPumpState ? HIGH : LOW);
  
  int redDuty   = manualRedLEDState   ? ((manualRedBrightness * maxDuty) / 100) : 0;
  int blueDuty  = manualBlueLEDState  ? ((manualBlueBrightness * maxDuty) / 100) : 0;
  int whiteDuty = manualWhiteLEDState ? ((manualWhiteBrightness * maxDuty) / 100) : 0;
  ledc_set_duty(LEDC_HIGH_SPEED_MODE, redChannel, redDuty);
  ledc_update_duty(LEDC_HIGH_SPEED_MODE, redChannel);
  ledc_set_duty(LEDC_HIGH_SPEED_MODE, blueChannel, blueDuty);
  ledc_update_duty(LEDC_HIGH_SPEED_MODE, blueChannel);
  ledc_set_duty(LEDC_HIGH_SPEED_MODE, whiteChannel, whiteDuty);
  ledc_update_duty(LEDC_HIGH_SPEED_MODE, whiteChannel);
}

// =======================
// Обработчики веб-запросов
// =======================

void handleRoot() {
  updateCurrentTime();
  
  String html = "<html><head><meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<meta charset='UTF-8'>";
  html += "<meta http-equiv='refresh' content='10'>";
  html += "<script>"
          "function setAndSendTime(){"
            "var now = new Date();"
            "var year = now.getFullYear();"
            "var month = ('0'+(now.getMonth()+1)).slice(-2);"
            "var day = ('0'+now.getDate()).slice(-2);"
            "var hour = ('0'+now.getHours()).slice(-2);"
            "var minute = ('0'+now.getMinutes()).slice(-2);"
            "var second = ('0'+now.getSeconds()).slice(-2);"
            "var timeStr = year+','+month+','+day+','+hour+','+minute+','+second;"
            "document.getElementById('timeInput').value = timeStr;"
            "document.getElementById('localTime').innerHTML = now.toLocaleTimeString();"
            "var xhr = new XMLHttpRequest();"
            "xhr.open('GET', '/setTimeLocal?t=' + timeStr, true);"
            "xhr.send();"
          "}"
          "setInterval(setAndSendTime, 10000);"
          "window.onload = setAndSendTime;"
          "</script>";
  html += "<title>Панель управления ESP32</title></head><body>";
  html += "<h1>Панель управления ESP32</h1>";
  
  struct tm * timeinfo = localtime(&currentUnixTime);
  html += "<h2>Текущее состояние</h2><ul>";
  html += "<li>Режим работы: ";
  if(currentMode == MODE_AUTO)      html += "Автоматический";
  else if(currentMode == MODE_REGULATED) html += "Регулируемый";
  else if(currentMode == MODE_DIRECT)    html += "Прямое управление";
  html += "</li>";
  html += "<li>Локальное время: " + String(timeinfo->tm_year + 1900) + "-" 
          + String(timeinfo->tm_mon + 1) + "-" + String(timeinfo->tm_mday) + " " 
          + String(timeinfo->tm_hour) + ":" + String(timeinfo->tm_min) + ":" + String(timeinfo->tm_sec) + "</li>";
  html += "<li>График освещения: отключение в " + String(scheduleOffHour) + ":00, включение в " + String(scheduleOnHour) + ":00</li>";
  
  if(currentMode == MODE_AUTO) {
    html += "<li>Параметры автоматики (насос): интервал " + String(pumpIntervalAutoSec) + " сек, длительность " + String(pumpOnDurationAutoSec) + " сек</li>";
    if(autoUseWhite) {
      html += "<li>Параметры автоматики (свет): работает белая лента с яркостью " + String(whiteBrightnessAutoPercent) + "%</li>";
    } else {
      html += "<li>Параметры автоматики (свет): красная яркость = " + String(redBrightnessAutoPercent) + "%, синяя яркость = " + String(blueBrightnessAutoPercent) + "%</li>";
    }
  }
  else if(currentMode == MODE_REGULATED) {
    html += "<li>Параметры регулируемого режима: красная яркость = " + String(manualRedBrightness) + "%, синяя яркость = " + String(manualBlueBrightness) + "%, белая яркость = " + String(manualWhiteBrightness) + "%, ";
    html += "Насос ON = " + String(manualPumpOnDuration) + " сек, OFF = " + String(manualPumpOffDuration) + " сек</li>";
  }
  
  bool pumpState = (digitalRead(pumpPin) == HIGH);
  bool redState, blueState, whiteState;
  if(currentMode == MODE_DIRECT) {
    redState   = manualRedLEDState;
    blueState  = manualBlueLEDState;
    whiteState = manualWhiteLEDState;
  } else {
    if(currentMode == MODE_AUTO) {
      if(autoUseWhite) {
        redState = blueState = false;
        // Определяем состояние белой ленты по времени
        struct tm * t = localtime(&currentUnixTime);
        whiteState = ((t->tm_hour >= scheduleOnHour) || (t->tm_hour < scheduleOffHour));
      } else {
        struct tm * t = localtime(&currentUnixTime);
        bool ledOn = ((t->tm_hour >= scheduleOnHour) || (t->tm_hour < scheduleOffHour));
        redState = blueState = ledOn;
        whiteState = false;
      }
    } else { // MODE_REGULATED
      redState   = (manualRedBrightness  > 0);
      blueState  = (manualBlueBrightness > 0);
      whiteState = (manualWhiteBrightness > 0);
    }
  }
  
  html += "<li>Состояния: Насос " + String(pumpState ? "ВКЛ" : "ВЫКЛ") 
          + ", Красная лента " + String(redState ? "ВКЛ" : "ВЫКЛ")
          + ", Синяя лента " + String(blueState ? "ВКЛ" : "ВЫКЛ")
          + ", Белая лента " + String(whiteState ? "ВКЛ" : "ВЫКЛ")
          + "</li>";
  html += "</ul><hr>";
  
  // Форма выбора режима работы
  html += "<form action='/setMode' method='GET'>";
  html += "Режим работы: <select name='mode'>";
  html += "<option value='auto'" + String(currentMode == MODE_AUTO ? " selected" : "") + ">Автоматический</option>";
  html += "<option value='regulated'" + String(currentMode == MODE_REGULATED ? " selected" : "") + ">Регулируемый</option>";
  html += "<option value='direct'" + String(currentMode == MODE_DIRECT ? " selected" : "") + ">Прямое управление</option>";
  html += "</select> ";
  html += "<input type='submit' value='Установить режим'>";
  html += "</form><hr>";
  
  // Форма выбора режима LED в автоматическом режиме
  if(currentMode == MODE_AUTO) {
    html += "<form action='/setAutoLED' method='GET'>";
    html += "Автоматический режим светодиодов: <select name='autoLED'>";
    html += "<option value='redblue'" + String(!autoUseWhite ? " selected" : "") + ">Красная+Синяя</option>";
    html += "<option value='white'"   + String(autoUseWhite  ? " selected" : "") + ">Белая</option>";
    html += "</select> ";
    html += "<input type='submit' value='Установить'>";
    html += "</form><hr>";
  }
  
  // Форма установки времени
  html += "<form action='/setTime' method='GET'>";
  html += "Установить текущее время (ГГГГ,ММ,ДД,ЧЧ,ММ,СС): ";
  html += "<input id='timeInput' name='time' type='text' placeholder='2025,01,01,12,00,00'> ";
  html += "<input type='submit' value='Установить время'>";
  html += "</form><hr>";
  
  // Форма графика работы освещения
  html += "<h2>График работы освещения</h2>";
  html += "<form action='/setSchedule' method='GET'>";
  html += "Время отключения (час, 0-23): <input name='offHour' type='number' value='" + String(scheduleOffHour) + "'><br>";
  html += "Время включения (час, 0-23): <input name='onHour' type='number' value='" + String(scheduleOnHour) + "'><br>";
  html += "<input type='submit' value='Установить график'>";
  html += "</form><hr>";
  
  // Форма для регулируемого режима
  if (currentMode == MODE_REGULATED) {
    html += "<h2>Параметры регулируемого режима</h2>";
    html += "<form action='/setManual' method='GET'>";
    html += "Красная лента (яркость в %): <input name='red' type='number' value='" + String(manualRedBrightness) + "'><br>";
    html += "Синяя лента (яркость в %): <input name='blue' type='number' value='" + String(manualBlueBrightness) + "'><br>";
    html += "Белая лента (яркость в %): <input name='white' type='number' value='" + String(manualWhiteBrightness) + "'><br>";
    html += "Насос: длительность ON (сек): <input name='pon' type='number' value='" + String(manualPumpOnDuration) + "'><br>";
    html += "Насос: длительность OFF (сек): <input name='poff' type='number' value='" + String(manualPumpOffDuration) + "'><br>";
    html += "<input type='submit' value='Установить параметры'>";
    html += "</form><hr>";
  }
  
  // Форма для прямого управления
  if (currentMode == MODE_DIRECT) {
    html += "<h2>Режим прямого управления</h2>";
    html += "<form action='/control' method='GET'>";
    html += "Управление насосом: <button name='pump' value='on'>ВКЛ</button> <button name='pump' value='off'>ВЫКЛ</button><br>";
    html += "Управление красной лентой: <button name='red' value='on'>ВКЛ</button> <button name='red' value='off'>ВЫКЛ</button><br>";
    html += "Управление синей лентой: <button name='blue' value='on'>ВКЛ</button> <button name='blue' value='off'>ВЫКЛ</button><br>";
    html += "Управление белой лентой: <button name='white' value='on'>ВКЛ</button> <button name='white' value='off'>ВЫКЛ</button>";
    html += "</form><hr>";
  }
  
  html += "</body></html>";
  server.send(200, "text/html", html);
}

void handleSetMode() {
  if (server.hasArg("mode")) {
    String mode = server.arg("mode");
    if (mode == "auto") {
      currentMode = MODE_AUTO;
    } else if (mode == "regulated") {
      currentMode = MODE_REGULATED;
    } else if (mode == "direct") {
      currentMode = MODE_DIRECT;
    }
  }
  server.sendHeader("Location", "/");
  server.send(303, "text/plain", "Режим установлен");
}

void handleSetAutoLED() {
  if (server.hasArg("autoLED")) {
    String ledMode = server.arg("autoLED");
    if (ledMode == "white") {
      autoUseWhite = true;
    } else {
      autoUseWhite = false;
    }
  }
  server.sendHeader("Location", "/");
  server.send(303, "text/plain", "Режим авто светодиодов установлен");
}

void handleSetTime() {
  if (server.hasArg("time")) {
    String timeStr = server.arg("time"); // Формат: "ГГГГ,ММ,ДД,ЧЧ,ММ,СС"
    timeStr.trim();
    currentUnixTime = parseTimeString(timeStr);
    lastTimeUpdateMillis = millis();
  }
  server.sendHeader("Location", "/");
  server.send(303, "text/plain", "Время установлено");
}

void handleSetSchedule() {
  if (server.hasArg("offHour")) {
    scheduleOffHour = server.arg("offHour").toInt();
  }
  if (server.hasArg("onHour")) {
    scheduleOnHour = server.arg("onHour").toInt();
  }
  server.sendHeader("Location", "/");
  server.send(303, "text/plain", "График работы освещения установлен");
}

void handleSetManual() {
  if (server.hasArg("red")) {
    manualRedBrightness = server.arg("red").toInt();
  }
  if (server.hasArg("blue")) {
    manualBlueBrightness = server.arg("blue").toInt();
  }
  if (server.hasArg("white")) {
    manualWhiteBrightness = server.arg("white").toInt();
  }
  if (server.hasArg("pon")) {
    manualPumpOnDuration = server.arg("pon").toInt();
  }
  if (server.hasArg("poff")) {
    manualPumpOffDuration = server.arg("poff").toInt();
  }
  server.sendHeader("Location", "/");
  server.send(303, "text/plain", "Параметры регулируемого режима установлены");
}

void handleControl() {
  if (server.hasArg("pump")) {
    String pumpCmd = server.arg("pump");
    if (pumpCmd == "on") {
      manualPumpState = true;
    } else if (pumpCmd == "off") {
      manualPumpState = false;
    }
  }
  if (server.hasArg("red")) {
    String redCmd = server.arg("red");
    if (redCmd == "on") {
      manualRedLEDState = true;
    } else if (redCmd == "off") {
      manualRedLEDState = false;
    }
  }
  if (server.hasArg("blue")) {
    String blueCmd = server.arg("blue");
    if (blueCmd == "on") {
      manualBlueLEDState = true;
    } else if (blueCmd == "off") {
      manualBlueLEDState = false;
    }
  }
  if (server.hasArg("white")) {
    String whiteCmd = server.arg("white");
    if (whiteCmd == "on") {
      manualWhiteLEDState = true;
    } else if (whiteCmd == "off") {
      manualWhiteLEDState = false;
    }
  }
  server.sendHeader("Location", "/");
  server.send(303, "text/plain", "Команда выполнена");
}

void handleSetTimeLocal() {
  if (server.hasArg("t")) {
    String timeStr = server.arg("t"); // Формат: "ГГГГ,ММ,ДД,ЧЧ,ММ,СС"
    currentUnixTime = parseTimeString(timeStr);
    lastTimeUpdateMillis = millis();
  }
  server.send(200, "text/plain", "Локальное время обновлено");
}

void setup() {
  Serial.begin(115200);
  
  pinMode(pumpPin, OUTPUT);
  digitalWrite(pumpPin, LOW);
  
  setupBlueLEDPWM();
  setupRedLEDPWM();
  setupWhiteLEDPWM();
  
  bootLedStartTime = millis();
  
  // Если время не установлено, устанавливаем значение по умолчанию (1 января 2021)
  currentUnixTime = 1609459200;
  lastTimeUpdateMillis = millis();
  
  WiFi.softAP(ssid, apPassword);
  Serial.print("AP IP: ");
  Serial.println(WiFi.softAPIP());
  
  server.on("/", handleRoot);
  server.on("/setMode", handleSetMode);
  server.on("/setAutoLED", handleSetAutoLED);
  server.on("/setTime", handleSetTime);
  server.on("/setSchedule", handleSetSchedule);
  server.on("/setManual", handleSetManual);
  server.on("/control", handleControl);
  server.on("/setTimeLocal", handleSetTimeLocal);
  server.begin();
  Serial.println("HTTP сервер запущен");
  
  Serial.println("Период загрузки: 30 секунд, светодиодные ленты включены");
}

void loop() {
  server.handleClient();
  updateCurrentTime();
  
  if (currentMode == MODE_AUTO) {
    handleAutoMode();
  } else if (currentMode == MODE_REGULATED) {
    handleRegulatedMode();
  } else if (currentMode == MODE_DIRECT) {
    handleDirectMode();
  }
  
  delay(10);
}
