#include "board.h"

#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_lcd_co5300.h"
#include "esp_lcd_touch_cst9217.h"
#include "esp_log.h"
#include "driver/temperature_sensor.h"

static const char *TAG = "board";

static i2c_master_bus_handle_t s_i2c;
static i2c_master_dev_handle_t s_pmic, s_imu;
static esp_lcd_panel_io_handle_t s_io;
static esp_lcd_panel_handle_t s_panel;
static esp_lcd_touch_handle_t s_touch;
static SemaphoreHandle_t s_lcd_done;

i2c_master_bus_handle_t board_i2c(void) { return s_i2c; }
esp_lcd_panel_handle_t board_panel(void) { return s_panel; }
SemaphoreHandle_t board_lcd_done(void) { return s_lcd_done; }

// ---------------------------------------------------------------- I2C helpers
static esp_err_t reg_write(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t val) {
    uint8_t buf[2] = {reg, val};
    return i2c_master_transmit(dev, buf, 2, 100);
}

static esp_err_t reg_read(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t *out, size_t n) {
    return i2c_master_transmit_receive(dev, &reg, 1, out, n, 100);
}

static i2c_master_dev_handle_t add_dev(uint8_t addr, uint32_t hz) {
    i2c_device_config_t cfg = {.dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = addr, .scl_speed_hz = hz};
    i2c_master_dev_handle_t dev = NULL;
    ESP_ERROR_CHECK(i2c_master_bus_add_device(s_i2c, &cfg, &dev));
    return dev;
}

// ---------------------------------------------------------------- power (AXP2101)
static void pmic_init(void) {
    s_pmic = add_dev(0x34, 400000);
    reg_write(s_pmic, 0x22, 0x06);               // PWRON > OFFLEVEL as power-off source
    reg_write(s_pmic, 0x27, 0x10);               // hold 4 s to power off
    reg_write(s_pmic, 0x80, 0x01);               // DCDC1 on
    reg_write(s_pmic, 0x90, 0x00);               // all LDOs off
    reg_write(s_pmic, 0x91, 0x00);
    reg_write(s_pmic, 0x82, (3300 - 1500) / 100);  // DC1 3.3 V
    reg_write(s_pmic, 0x92, (3300 - 500) / 100);   // ALDO1 3.3 V
    reg_write(s_pmic, 0x90, 0x01);               // ALDO1 on (microphones)
    reg_write(s_pmic, 0x64, 0x02);               // charge to 4.1 V
    reg_write(s_pmic, 0x61, 0x02);               // precharge 50 mA
    reg_write(s_pmic, 0x62, 0x06);               // charge 150 mA: gentle on the 500 mAh cell, less charger heat
    reg_write(s_pmic, 0x63, 0x01);               // termination 25 mA
    reg_write(s_pmic, 0x30, 0x0F);               // ADC: battery, temperature on
    uint8_t v = 0;
    if (reg_read(s_pmic, 0x18, &v, 1) == ESP_OK) reg_write(s_pmic, 0x18, v | 0x08);  // fuel gauge on (battery %)
    if (reg_read(s_pmic, 0x68, &v, 1) == ESP_OK) reg_write(s_pmic, 0x68, v | 0x01);  // battery detection on
    ESP_LOGI(TAG, "AXP2101 ready");
}

bool board_charging(void) {
    uint8_t st = 0;
    if (reg_read(s_pmic, 0x01, &st, 1) != ESP_OK) return false;
    return ((st >> 5) & 0x03) == 0x01;  // battery current direction: 01 = charging
}

int board_battery_mv(void) {
    uint8_t d[2];
    if (reg_read(s_pmic, 0x34, d, 2) != ESP_OK) return 0;
    return ((d[0] & 0x3F) << 8) | d[1];
}

int board_battery_percent(void) {
    uint8_t status = 0, pct = 0;
    if (reg_read(s_pmic, 0x00, &status, 1) != ESP_OK || !(status & 0x08)) return -1;  // no battery
    if (reg_read(s_pmic, 0xA4, &pct, 1) != ESP_OK) return -1;
    return pct > 100 ? 100 : pct;
}

// ---------------------------------------------------------------- display (CO5300 over QSPI)
static const co5300_lcd_init_cmd_t s_lcd_init[] = {
    {0xFE, (uint8_t[]){0x20}, 1, 0},
    {0x19, (uint8_t[]){0x10}, 1, 0},
    {0x1C, (uint8_t[]){0xA0}, 1, 0},
    {0xFE, (uint8_t[]){0x00}, 1, 0},
    {0xC4, (uint8_t[]){0x80}, 1, 0},
    {0x3A, (uint8_t[]){0x55}, 1, 0},
    {0x35, (uint8_t[]){0x00}, 1, 0},
    {0x53, (uint8_t[]){0x20}, 1, 0},
    {0x51, (uint8_t[]){0xFF}, 1, 0},
    {0x63, (uint8_t[]){0xFF}, 1, 0},
    {0x2A, (uint8_t[]){0x00, 0x06, 0x01, 0xD7}, 4, 0},
    {0x2B, (uint8_t[]){0x00, 0x00, 0x01, 0xD1}, 4, 600},
    {0x11, NULL, 0, 600},
    {0x29, NULL, 0, 0},
};

static bool IRAM_ATTR on_color_done(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *e, void *ctx) {
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(s_lcd_done, &woken);
    return woken == pdTRUE;
}

static void display_init(void) {
    spi_bus_config_t bus = {
        .data0_io_num = PIN_LCD_D0,
        .data1_io_num = PIN_LCD_D1,
        .sclk_io_num = PIN_LCD_CLK,
        .data2_io_num = PIN_LCD_D2,
        .data3_io_num = PIN_LCD_D3,
        .max_transfer_sz = LCD_W * 48 * 2 + 64,
        .flags = SPICOMMON_BUSFLAG_QUAD,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO));

    s_lcd_done = xSemaphoreCreateCounting(8, 0);
    esp_lcd_panel_io_spi_config_t io = {
        .cs_gpio_num = PIN_LCD_CS,
        .dc_gpio_num = GPIO_NUM_NC,
        .spi_mode = 0,
        .pclk_hz = 40 * 1000 * 1000,
        .trans_queue_depth = 8,
        .on_color_trans_done = on_color_done,
        .lcd_cmd_bits = 32,
        .lcd_param_bits = 8,
        .flags = {.quad_mode = true},
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI2_HOST, &io, &s_io));

    co5300_vendor_config_t vendor = {
        .init_cmds = s_lcd_init,
        .init_cmds_size = sizeof(s_lcd_init) / sizeof(s_lcd_init[0]),
        .flags = {.use_qspi_interface = 1},
    };
    esp_lcd_panel_dev_config_t panel = {
        .reset_gpio_num = PIN_LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
        .vendor_config = &vendor,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_co5300(s_io, &panel, &s_panel));
    esp_lcd_panel_set_gap(s_panel, 0x06, 0);
    esp_lcd_panel_reset(s_panel);
    esp_lcd_panel_init(s_panel);
    esp_lcd_panel_invert_color(s_panel, false);
    esp_lcd_panel_disp_on_off(s_panel, true);
    ESP_LOGI(TAG, "display ready");
}

void board_set_brightness(uint8_t level) {
    uint32_t cmd = (0x02UL << 24) | (0x51UL << 8);
    esp_lcd_panel_io_tx_param(s_io, cmd, &level, 1);
}

// ---------------------------------------------------------------- touch (CST9217)
static void touch_init(void) {
    esp_lcd_panel_io_handle_t tp_io = NULL;
    esp_lcd_panel_io_i2c_config_t tp_io_cfg = ESP_LCD_TOUCH_IO_I2C_CST9217_CONFIG();
    tp_io_cfg.scl_speed_hz = 400000;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(s_i2c, &tp_io_cfg, &tp_io));
    esp_lcd_touch_config_t cfg = {
        .x_max = LCD_W - 1,
        .y_max = LCD_H - 1,
        .rst_gpio_num = PIN_TP_RST,
        .int_gpio_num = PIN_TP_INT,
        .levels = {.reset = 0, .interrupt = 0},
        .flags = {.swap_xy = 0, .mirror_x = 1, .mirror_y = 1},
    };
    if (esp_lcd_touch_new_i2c_cst9217(tp_io, &cfg, &s_touch) != ESP_OK) {
        ESP_LOGE(TAG, "touch init failed");
        s_touch = NULL;
    }
}

bool board_touch_read(int *x, int *y) {
    if (!s_touch) return false;
    uint16_t tx[1], ty[1], strength[1];
    uint8_t n = 0;
    esp_lcd_touch_read_data(s_touch);
    bool pressed = esp_lcd_touch_get_coordinates(s_touch, tx, ty, strength, &n, 1);
    if (pressed && n > 0) {
        *x = tx[0];
        *y = ty[0];
        return true;
    }
    return false;
}

bool board_boot_pressed(void) { return gpio_get_level(PIN_BOOT) == 0; }

// ---------------------------------------------------------------- IMU (QMI8658)
static void imu_init(void) {
    const uint8_t addrs[] = {0x6B, 0x6A};
    for (int i = 0; i < 2; i++) {
        if (i2c_master_probe(s_i2c, addrs[i], 50) != ESP_OK) continue;
        i2c_master_dev_handle_t dev = add_dev(addrs[i], 400000);
        uint8_t who = 0;
        if (reg_read(dev, 0x00, &who, 1) == ESP_OK && who == 0x05) {
            s_imu = dev;
            reg_write(s_imu, 0x02, 0x40);  // CTRL1: address auto-increment
            reg_write(s_imu, 0x03, 0x15);  // CTRL2: accel ±4 g, 250 Hz
            reg_write(s_imu, 0x08, 0x01);  // CTRL7: accel on
            ESP_LOGI(TAG, "QMI8658 at 0x%02X", addrs[i]);
            return;
        }
        i2c_master_bus_rm_device(dev);
    }
    ESP_LOGW(TAG, "IMU not found");
}

bool board_imu_read(float *ax, float *ay, float *az) {
    if (!s_imu) return false;
    uint8_t d[6];
    if (reg_read(s_imu, 0x35, d, 6) != ESP_OK) return false;
    const float k = 1.0f / 8192.0f;
    *ax = (int16_t)(d[0] | d[1] << 8) * k;
    *ay = (int16_t)(d[2] | d[3] << 8) * k;
    *az = (int16_t)(d[4] | d[5] << 8) * k;
    return true;
}

// ---------------------------------------------------------------- chip temperature
static temperature_sensor_handle_t s_tsens;

float board_temp_c(void) {
    float c = 0;
    if (!s_tsens) {
        temperature_sensor_config_t cfg = TEMPERATURE_SENSOR_CONFIG_DEFAULT(10, 80);
        if (temperature_sensor_install(&cfg, &s_tsens) != ESP_OK) return 0;
        temperature_sensor_enable(s_tsens);
    }
    temperature_sensor_get_celsius(s_tsens, &c);
    return c;
}

// ---------------------------------------------------------------- init
void board_init(void) {
    i2c_master_bus_config_t bus = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = PIN_I2C_SDA,
        .scl_io_num = PIN_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags = {.enable_internal_pullup = 1},
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus, &s_i2c));

    gpio_config_t boot = {.pin_bit_mask = 1ULL << PIN_BOOT, .mode = GPIO_MODE_INPUT, .pull_up_en = 1};
    gpio_config(&boot);

    pmic_init();
    display_init();
    touch_init();
    imu_init();
}
