# ESP32 Aeroponics Controller

## О проекте
ESP32‑контроллер для аэропонной фермы с:
- Тремя светодиодными лентами (красная, синяя, белая)
- Насосом для распыления питательного раствора на корни растений
- Режимами: автоматический, регулируемый, прямое управление
- Веб‑панелью для управления (точка доступа + встроенный HTTP‑сервер)

## Структура репозитория
esp32-aeroponics-controller/
- ├── LICENSE
- ├── README.md
- ├── CHANGELOG.md
- ├── CONTRIBUTING.md
- ├── .gitignore
- ├── docs/
- │   └── web-ui-screenshot.png
- └── src/
- └── main.ino

## Быстрый старт
1. Клонировать репозиторий:
   ```bash
   git clone https://github.com/CosmoCodeCraft/esp32-aeroponics-controller.git
   cd esp32-aeroponics-controller
2. Открыть src/main.ino в Arduino IDE или PlatformIO.
3. Загрузить на плату ESP32.
4. Подключиться к Wi‑Fi SSID ESP32_AP, пароль 12345678.
5. Открыть в браузере http://192.168.4.1/

## Пины и настройки
В проекте используются данные, но можете заменить на свои:
   ```bash
   const int pumpPin      = 25;
   const int redLEDPin    = 26;
   const int blueLEDPin   = 27;
   const int whiteLEDPin  = 14;
   //PWM (ШИМ): частота 5 kHz, разрешение 8 бит
   ```
## Режимы работы
- Автоматический: интервалы полива, расписание включения лент.
- Регулируемый: яркость и интервалы полива задаются вручную.
- Прямое управление: прямые команды ВКЛ/ВЫКЛ через кнопки веб‑интерфейса.
