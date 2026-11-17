#include "audio.h"

#include <math.h>
#include <string.h>

#include "board.h"
#include "driver/i2s_std.h"
#include "driver/i2s_tdm.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/stream_buffer.h"
#include "freertos/task.h"

static const char *TAG = "audio";

#define FRAME (AUDIO_RATE / 50)  // 20 ms
#define PLAY_BUF (512 * 1024)

static esp_codec_dev_handle_t s_out, s_in;
static audio_mic_cb_t s_mic_cb;
static volatile bool s_capture, s_play_active, s_play_eof, s_primed;
static volatile uint32_t s_underruns;  // times the speaker ran dry mid-reply (diagnostics)
#define PRIME_BYTES (AUDIO_RATE * 2 * 2 / 5)  // 400 ms in hand before speaking: no gaps if Wi-Fi hiccups
static StreamBufferHandle_t s_play;
static volatile float s_mic_level, s_out_level;
static int s_volume = 70;  // small speaker distorts near the top

static void i2s_init(i2s_chan_handle_t *tx, i2s_chan_handle_t *rx) {
    i2s_chan_config_t chan = {
        .id = I2S_NUM_0,
        .role = I2S_ROLE_MASTER,
        .dma_desc_num = 10,  // more slack for the mic
        .dma_frame_num = 240,
        .auto_clear_after_cb = true,
    };
    ESP_ERROR_CHECK(i2s_new_channel(&chan, tx, rx));

    i2s_std_config_t std = {
        .clk_cfg = {.sample_rate_hz = AUDIO_RATE, .clk_src = I2S_CLK_SRC_DEFAULT, .mclk_multiple = I2S_MCLK_MULTIPLE_256},
        .slot_cfg = {
            .data_bit_width = I2S_DATA_BIT_WIDTH_16BIT,
            .slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO,
            .slot_mode = I2S_SLOT_MODE_STEREO,
            .slot_mask = I2S_STD_SLOT_BOTH,
            .ws_width = I2S_DATA_BIT_WIDTH_16BIT,
            .bit_shift = true,
            .left_align = true,
        },
        .gpio_cfg = {.mclk = PIN_I2S_MCLK, .bclk = PIN_I2S_BCLK, .ws = PIN_I2S_WS, .dout = PIN_I2S_DOUT, .din = I2S_GPIO_UNUSED},
    };
    i2s_tdm_config_t tdm = {
        .clk_cfg = {.sample_rate_hz = AUDIO_RATE, .clk_src = I2S_CLK_SRC_DEFAULT, .mclk_multiple = I2S_MCLK_MULTIPLE_256, .bclk_div = 8},
        .slot_cfg = {
            .data_bit_width = I2S_DATA_BIT_WIDTH_16BIT,
            .slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO,
            .slot_mode = I2S_SLOT_MODE_STEREO,
            .slot_mask = I2S_TDM_SLOT0 | I2S_TDM_SLOT1 | I2S_TDM_SLOT2 | I2S_TDM_SLOT3,
            .ws_width = I2S_TDM_AUTO_WS_WIDTH,
            .bit_shift = true,
            .total_slot = I2S_TDM_AUTO_SLOT_NUM,
        },
        .gpio_cfg = {.mclk = PIN_I2S_MCLK, .bclk = PIN_I2S_BCLK, .ws = PIN_I2S_WS, .dout = I2S_GPIO_UNUSED, .din = PIN_I2S_DIN},
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(*tx, &std));
    ESP_ERROR_CHECK(i2s_channel_init_tdm_mode(*rx, &tdm));
    ESP_ERROR_CHECK(i2s_channel_enable(*tx));
    ESP_ERROR_CHECK(i2s_channel_enable(*rx));
}

static void codec_init(void) {
    i2s_chan_handle_t tx, rx;
    i2s_init(&tx, &rx);
    audio_codec_i2s_cfg_t i2s_cfg = {.port = I2S_NUM_0, .rx_handle = rx, .tx_handle = tx};
    const audio_codec_data_if_t *data_if = audio_codec_new_i2s_data(&i2s_cfg);
    const audio_codec_gpio_if_t *gpio_if = audio_codec_new_gpio();

    audio_codec_i2c_cfg_t i2c_cfg = {.port = I2C_NUM_0, .addr = ES8311_CODEC_DEFAULT_ADDR, .bus_handle = board_i2c()};
    const audio_codec_ctrl_if_t *out_ctrl = audio_codec_new_i2c_ctrl(&i2c_cfg);
    es8311_codec_cfg_t es8311 = {
        .ctrl_if = out_ctrl,
        .gpio_if = gpio_if,
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC,
        .pa_pin = PIN_PA_EN,
        .use_mclk = true,
        .hw_gain = {.pa_voltage = 5.0, .codec_dac_voltage = 3.3},
    };
    esp_codec_dev_cfg_t out_cfg = {.dev_type = ESP_CODEC_DEV_TYPE_OUT, .codec_if = es8311_codec_new(&es8311), .data_if = data_if};
    s_out = esp_codec_dev_new(&out_cfg);

    i2c_cfg.addr = ES7210_CODEC_DEFAULT_ADDR;
    es7210_codec_cfg_t es7210 = {
        .ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg),
        .mic_selected = ES7210_SEL_MIC1 | ES7210_SEL_MIC2 | ES7210_SEL_MIC3 | ES7210_SEL_MIC4,
    };
    esp_codec_dev_cfg_t in_cfg = {.dev_type = ESP_CODEC_DEV_TYPE_IN, .codec_if = es7210_codec_new(&es7210), .data_if = data_if};
    s_in = esp_codec_dev_new(&in_cfg);

    ESP_LOGI(TAG, "codecs ready (powered on only while listening / speaking)");
}

static float rms01(const int16_t *x, int n) {
    double acc = 0;
    for (int i = 0; i < n; i++) acc += (double)x[i] * x[i];
    float r = sqrtf(acc / n) / 6000.0f;
    return r > 1 ? 1 : r;
}

static bool s_in_open, s_out_open;
static int s_mic_settle;

static void mic_power(bool on) {
    if (on == s_in_open) return;
    if (on) {
        esp_codec_dev_sample_info_t fs = {
            .bits_per_sample = 16, .channel = 4, .channel_mask = ESP_CODEC_DEV_MAKE_CHANNEL_MASK(0), .sample_rate = AUDIO_RATE};
        if (esp_codec_dev_open(s_in, &fs) != ESP_CODEC_DEV_OK) return;
        esp_codec_dev_set_in_channel_gain(s_in, ESP_CODEC_DEV_MAKE_CHANNEL_MASK(0), 37.5);  // speech was too quiet at 33 dB
        s_mic_settle = 10;  // drop the first 200 ms: the mic "clicks" when it powers on
    } else {
        esp_codec_dev_close(s_in);
        s_mic_level = 0;
    }
    s_in_open = on;
}

static void speaker_power(bool on) {
    if (on == s_out_open) return;
    if (on) {
        esp_codec_dev_sample_info_t fs = {.bits_per_sample = 16, .channel = 1, .sample_rate = AUDIO_RATE};
        if (esp_codec_dev_open(s_out, &fs) != ESP_CODEC_DEV_OK) return;
        esp_codec_dev_set_out_vol(s_out, s_volume);
    } else {
        esp_codec_dev_close(s_out);  // also switches the speaker amplifier off
    }
    s_out_open = on;
}

static void mic_task(void *arg) {
    int16_t *buf = heap_caps_malloc(FRAME * sizeof(int16_t), MALLOC_CAP_INTERNAL);
    while (1) {
        mic_power(s_capture);  // microphones only run while Chitti is listening
        if (!s_in_open) {
            vTaskDelay(pdMS_TO_TICKS(40));
            continue;
        }
        if (esp_codec_dev_read(s_in, buf, FRAME * sizeof(int16_t)) != ESP_CODEC_DEV_OK) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        if (s_mic_settle > 0) {
            s_mic_settle--;
            continue;
        }
        float lvl = rms01(buf, FRAME);
        s_mic_level = s_mic_level * 0.6f + lvl * 0.4f;
        if (s_capture && s_mic_cb) s_mic_cb(buf, FRAME);
    }
}

static void play_task(void *arg) {
    int16_t *buf = heap_caps_malloc(FRAME * sizeof(int16_t), MALLOC_CAP_INTERNAL);
    int quiet_ms = 0;
    while (1) {
        if (s_play_active && !s_primed) {
            if (!s_play_eof && xStreamBufferBytesAvailable(s_play) < PRIME_BYTES) {
                vTaskDelay(pdMS_TO_TICKS(10));
                continue;
            }
            s_primed = true;
        }
        // Ran low mid-reply (Wi-Fi hiccup or next sentence not ready): don't play a half-empty chunk padded
        // with zeros - that chops the wave and crackles. Wait and rebuild the cushion instead.
        size_t avail = xStreamBufferBytesAvailable(s_play);
        if (s_play_active && !s_play_eof && avail < FRAME * sizeof(int16_t)) {
            if (avail > 0) s_underruns++;  // starved part-way through sound (not a clean gap between sentences)
            s_primed = false;
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        size_t got = xStreamBufferReceive(s_play, buf, FRAME * sizeof(int16_t), pdMS_TO_TICKS(20));
        if (got >= 2) {
            quiet_ms = 0;
            speaker_power(true);
            int n = got / 2;
            if (n < FRAME) memset(buf + n, 0, (FRAME - n) * 2);
            s_out_level = s_out_level * 0.5f + rms01(buf, n) * 0.5f;
            esp_codec_dev_write(s_out, buf, FRAME * sizeof(int16_t));
        } else {
            s_out_level *= 0.7f;
            quiet_ms += 20;
            if (quiet_ms > 1500 && !s_play_active) speaker_power(false);  // amp off after 1.5 s of silence
            if (s_play_active && s_play_eof && xStreamBufferIsEmpty(s_play)) {
                s_play_active = false;
                s_out_level = 0;
            }
        }
    }
}

void audio_init(audio_mic_cb_t mic_cb) {
    s_mic_cb = mic_cb;
    codec_init();
    s_play = xStreamBufferCreateWithCaps(PLAY_BUF, 2, MALLOC_CAP_SPIRAM);
    xTaskCreatePinnedToCore(mic_task, "mic", 4096, NULL, 6, NULL, 0);
    xTaskCreatePinnedToCore(play_task, "play", 4096, NULL, 7, NULL, 0);
}

void audio_capture(bool on) { s_capture = on; }

void audio_play_begin(void) {
    xStreamBufferReset(s_play);
    s_play_eof = false;
    s_primed = false;
    s_play_active = true;
}

void audio_play_push(const uint8_t *pcm, size_t bytes) {
    if (!s_play_active) return;
    xStreamBufferSend(s_play, pcm, bytes, pdMS_TO_TICKS(2000));
}

void audio_play_end(void) { s_play_eof = true; }

void audio_play_stop(void) {
    s_play_active = false;
    s_play_eof = true;
    xStreamBufferReset(s_play);
}

bool audio_playing(void) { return s_play_active; }
uint32_t audio_underruns(void) { return s_underruns; }
float audio_mic_level(void) { return s_mic_level; }
float audio_out_level(void) { return s_out_level; }

int audio_get_volume(void) { return s_volume; }

void audio_set_volume(int vol) {
    s_volume = vol;
    if (s_out_open) esp_codec_dev_set_out_vol(s_out, vol);
}

void audio_beep(int freq, int ms) {
    int n = AUDIO_RATE * ms / 1000;
    int16_t *tone = heap_caps_malloc(n * 2, MALLOC_CAP_SPIRAM);
    if (!tone) return;
    for (int i = 0; i < n; i++) {
        float env = fminf(1.0f, fminf(i, n - i) / 400.0f);
        tone[i] = (int16_t)(sinf(2 * M_PI * freq * i / AUDIO_RATE) * 5000 * env);
    }
    audio_play_begin();
    audio_play_push((uint8_t *)tone, n * 2);
    audio_play_end();
    free(tone);
}
