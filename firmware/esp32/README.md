# ESP32 Firmware

## File

`ConsoleConnect.ino` is based on the latest supplied `ConsoleCrew_ESP32_LogoutFix.ino` firmware. It includes the logout flow that returns to player selection while retaining Wi-Fi, online player selection/login, and high-score integration.

## Hardware

- ESP32 DevKit board
- SSD1306 128×64 I²C OLED, address `0x3C`
- Analog joystick with push switch

### Pin mapping

| Component | ESP32 pin |
|---|---:|
| OLED SDA | GPIO 21 |
| OLED SCL | GPIO 22 |
| OLED VCC | 3V3 |
| OLED GND | GND |
| Joystick VRX | GPIO 34 |
| Joystick VRY | GPIO 35 |
| Joystick SW | GPIO 27 |
| Joystick VCC | 3V3 |
| Joystick GND | GND |

Check exact board wiring before powering. Do not connect 5 V to an ESP32 GPIO.

## Arduino IDE setup

1. Install ESP32 board support.
2. Install libraries required by the sketch, such as Adafruit GFX and Adafruit SSD1306; install other dependencies if the compiler reports them.
3. Open `ConsoleConnect.ino` and select your ESP32 board and serial port.
4. Replace `YOUR_WIFI_NAME`, `YOUR_WIFI_PASSWORD`, and `YOUR_SUPABASE_ANON_KEY` placeholders locally.
5. Keep `SUPABASE_API_URL` pointed at the deployed `console-api` URL.
6. Compile and upload.

The anon/publishable key is public client configuration. Never insert `SUPABASE_SERVICE_ROLE_KEY` into firmware.

## Notes

The Wi-Fi values are placeholders. A physical device's firmware can be extracted, so do not treat embedded Wi-Fi credentials or the four-direction password as strong secrets. This is prototype firmware, not production-grade authentication.
