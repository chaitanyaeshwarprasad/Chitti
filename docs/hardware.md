# ESP32-S3-Touch-AMOLED-1.75C — hardware map

*Part of Chitti by [Chaitanya Eshwar Prasad](https://chaitanyaeshwarprasad.com).*

Board used: [Waveshare ESP32-S3-Touch-AMOLED-1.75C](https://www.waveshare.com/product/esp32-related/displays/amoled/esp32-s3-touch-amoled-1.75c.htm). ESP32-S3, 8 MB octal PSRAM, 32 MB flash.

| Function | Part | Bus / pins |
|---|---|---|
| Display 466×466 AMOLED | CO5300 | QSPI on SPI2: CS 12, CLK 38, D0 4, D1 5, D2 6, D3 7, RST 1. 40 MHz, 32-bit cmd, column gap 6 |
| Touch | CST9217 | I2C0, INT 11, RST 2, mirror X and Y |
| I2C bus (shared) | — | SDA 15, SCL 14 |
| Power management | AXP2101 | I2C 0x34 |
| Speaker codec (DAC) | ES8311 | I2C (default addr), I2S DOUT 8 |
| Microphones (ADC, 2 mics + ref) | ES7210 | I2C (default addr), I2S DIN 10 |
| I2S clocks | — | MCLK 16, BCLK 9, WS 45 |
| Speaker amplifier enable | — | GPIO 46 |
| IMU 6-axis | QMI8658 | I2C |
| Buttons | BOOT / PWR | BOOT = GPIO 0, PWR via AXP2101 |

CO5300 init sequence (after reset): FE 20, 19 10, 1C A0, FE 00, C4 80, 3A 55 (RGB565), 35 00, 53 20, 51 FF (brightness), 63 FF, 2A 0006..01D7, 2B 0000..01D1, 11 (sleep out, 600 ms), 29 (display on).
Brightness = command 0x51 with one data byte.

AXP2101 setup used by the board vendor: DC1 = 3.3 V, ALDO1 = 3.3 V (microphones) enabled, charge 4.1 V / 400 mA.

Factory firmware: The setup wizard (or tools\Flash Chitti.bat) can save a backup to `backup\factory_backup.bin`. Restore it with `server\.venv\Scripts\python.exe -m esptool --chip esp32s3 write_flash 0x0 backup\factory_backup.bin`.
