#pragma once
// Waveshare ESP32-S3-Touch-AMOLED-1.75C hardware (see ../../docs/hardware.md)
#include <stdbool.h>
#include <stdint.h>
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define LCD_W 466
#define LCD_H 466

#define PIN_I2C_SDA GPIO_NUM_15
#define PIN_I2C_SCL GPIO_NUM_14
#define PIN_LCD_CS GPIO_NUM_12
#define PIN_LCD_CLK GPIO_NUM_38
#define PIN_LCD_D0 GPIO_NUM_4
#define PIN_LCD_D1 GPIO_NUM_5
#define PIN_LCD_D2 GPIO_NUM_6
#define PIN_LCD_D3 GPIO_NUM_7
#define PIN_LCD_RST GPIO_NUM_1
#define PIN_TP_RST GPIO_NUM_2
#define PIN_TP_INT GPIO_NUM_11
#define PIN_I2S_MCLK GPIO_NUM_16
#define PIN_I2S_BCLK GPIO_NUM_9
#define PIN_I2S_WS GPIO_NUM_45
#define PIN_I2S_DOUT GPIO_NUM_8
#define PIN_I2S_DIN GPIO_NUM_10
#define PIN_PA_EN GPIO_NUM_46
#define PIN_BOOT GPIO_NUM_0

void board_init(void);
i2c_master_bus_handle_t board_i2c(void);

esp_lcd_panel_handle_t board_panel(void);
// Given once per finished color transfer to the panel.
SemaphoreHandle_t board_lcd_done(void);
void board_set_brightness(uint8_t level);

bool board_touch_read(int *x, int *y);
bool board_boot_pressed(void);
// Acceleration in g. Returns false if the IMU is missing.
bool board_imu_read(float *ax, float *ay, float *az);
int board_battery_percent(void);  // -1 when no battery
bool board_charging(void);
int board_battery_mv(void);
float board_temp_c(void);          // ESP32-S3 chip temperature
