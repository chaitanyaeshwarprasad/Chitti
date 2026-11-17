// Chitti firmware by Chaitanya Eshwar Prasad (https://chaitanyaeshwarprasad.com). MIT licence.
// Chitti: AI companion firmware for the Waveshare ESP32-S3-Touch-AMOLED-1.75C.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio.h"
#include "board.h"
#include "cJSON.h"
#include "esp_log.h"
#include "esp_pm.h"
#include "esp_timer.h"
#include "face.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/stream_buffer.h"
#include "esp_heap_caps.h"
#include "net.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "chitti";

static volatile activity_t s_act = ACT_IDLE;
static volatile bool s_tts_done;       // server finished sending speech
static volatile int64_t s_mood_until;  // when a temporary mood returns to neutral
static volatile int64_t s_last_input;
static volatile uint32_t s_mic_dropped;  // mic bytes lost because the send buffer was full
static volatile int64_t s_sub_clear_at;  // when to hide the subtitle after speaking

static volatile bool s_night;             // server says it's late night: dimmer, sleeps sooner
static volatile int64_t s_text_clear_at;  // when to hide a short on-screen note (volume)

static int64_t now_us(void) { return esp_timer_get_time(); }

// ---------------------------------------------------------------- volume (kept across restarts)
// Codec volume 40..100 (shown as 0..100 %). Default 88: the old 70 was too quiet on the small speaker.
#define VOL_MIN 40
#define VOL_MAX 100
#define VOL_DEFAULT 88
static volatile bool s_vol_changed;  // tell the server (dashboard slider) on the next battery report

static int vol_percent(int v) { return (v - VOL_MIN) * 100 / (VOL_MAX - VOL_MIN); }

static void volume_load(void) {
    nvs_handle_t h;
    int32_t v = VOL_DEFAULT;
    if (nvs_open("chitti", NVS_READONLY, &h) == ESP_OK) {
        nvs_get_i32(h, "vol2", &v);  // new key: older, too-quiet saved values are ignored
        nvs_close(h);
    }
    audio_set_volume(v < VOL_MIN ? VOL_MIN : v > VOL_MAX ? VOL_MAX : v);
}

static void volume_set(int v, bool show) {
    v = v < VOL_MIN ? VOL_MIN : v > VOL_MAX ? VOL_MAX : v;
    audio_set_volume(v);
    nvs_handle_t h;
    if (nvs_open("chitti", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_i32(h, "vol2", v);
        nvs_commit(h);
        nvs_close(h);
    }
    s_vol_changed = true;
    if (show) {
        char line[24];
        snprintf(line, sizeof(line), "Volume %d%%", vol_percent(v));
        face_set_text(line, NULL);
        s_text_clear_at = now_us() + 1500000;
    }
    if (!audio_playing()) audio_beep(880, 90);
}

static void volume_step(int delta) { volume_set(audio_get_volume() + delta, true); }

static void set_activity(activity_t a) {
    s_act = a;
    face_set_activity(a);
    // Radio fully awake whenever Chitti is busy (listening, thinking, speaking); it naps only when idle.
    // Napping while speaking made the voice arrive in bursts and the radio's power spikes buzzed in the speaker.
    net_fast_mode(a != ACT_IDLE);
}

static void mood_for(mood_t m, int ms) {
    face_set_mood(m);
    s_mood_until = ms > 0 ? now_us() + ms * 1000LL : 0;
}

// ---------------------------------------------------------------- network callbacks
static void on_status(const char *a, const char *b) {
    face_set_text(a, b);
    if (!net_ready()) face_set_mood(MOOD_SLEEPY);
}

static void on_connected(void) {
    face_set_text(NULL, NULL);
    net_send_text("{\"type\":\"hello\",\"device\":\"chitti\",\"version\":1,\"rate\":24000}");
    mood_for(MOOD_HAPPY, 3000);
    face_poke();
}

static void on_disconnected(void) {
    audio_capture(false);
    audio_play_stop();
    set_activity(ACT_IDLE);
    face_set_mood(MOOD_SLEEPY);
}

static void on_text(const char *json, size_t len) {
    cJSON *msg = cJSON_ParseWithLength(json, len);
    if (!msg) return;
    const char *type = cJSON_GetStringValue(cJSON_GetObjectItem(msg, "type"));
    const char *state = cJSON_GetStringValue(cJSON_GetObjectItem(msg, "state"));
    if (!type) goto done;

    if (!strcmp(type, "state") && state) {
        if (!strcmp(state, "listening")) set_activity(ACT_LISTENING);
        else if (!strcmp(state, "thinking")) set_activity(ACT_THINKING);
        else if (!strcmp(state, "speaking")) set_activity(ACT_SPEAKING);
        else if (!strcmp(state, "idle") && !audio_playing() && s_act != ACT_LISTENING) set_activity(ACT_IDLE);
    } else if (!strcmp(type, "listen") && state && !strcmp(state, "stop")) {
        audio_capture(false);
        if (s_mic_dropped) {
            ESP_LOGW(TAG, "mic audio dropped: %lu ms (Wi-Fi too slow)", (unsigned long)(s_mic_dropped / (AUDIO_RATE * 2 / 1000)));
            s_mic_dropped = 0;
        }
        set_activity(ACT_THINKING);
    } else if (!strcmp(type, "mood")) {
        const char *m = cJSON_GetStringValue(cJSON_GetObjectItem(msg, "mood"));
        if (m) mood_for(face_mood_from_name(m), 0);
    } else if (!strcmp(type, "tts") && state) {
        if (!strcmp(state, "start")) {
            audio_capture(false);
            s_tts_done = false;
            audio_play_begin();
            set_activity(ACT_SPEAKING);
        } else {
            s_tts_done = true;
            audio_play_end();
        }
    } else if (!strcmp(type, "stt")) {
        ESP_LOGI(TAG, "heard: %s", cJSON_GetStringValue(cJSON_GetObjectItem(msg, "text")));
    } else if (!strcmp(type, "volume")) {  // dashboard slider, 0..100 %
        cJSON *v = cJSON_GetObjectItem(msg, "value");
        if (cJSON_IsNumber(v)) volume_set(VOL_MIN + (int)v->valuedouble * (VOL_MAX - VOL_MIN) / 100, true);
    } else if (!strcmp(type, "night")) {
        s_night = cJSON_IsTrue(cJSON_GetObjectItem(msg, "on"));
        face_set_night(s_night);
    } else if (!strcmp(type, "say")) {
        const char *en = cJSON_GetStringValue(cJSON_GetObjectItem(msg, "en"));
        face_set_subtitle(en && en[0] ? en : NULL);
        s_sub_clear_at = 0;
    }
done:
    cJSON_Delete(msg);
}

static void on_audio(const uint8_t *pcm, size_t len) { audio_play_push(pcm, len); }

// Microphone -> 3 s buffer -> sender task. Recording never waits for Wi-Fi, so a slow moment
// on the network can't make the mic drop pieces of your voice (that made long sentences garbled).
#define MIC_BUF_BYTES (AUDIO_RATE * 2 * 3)
#define MIC_SEND_BYTES (AUDIO_RATE * 2 / 10)  // send in 100 ms pieces
static StreamBufferHandle_t s_mic_sb;

static void on_mic(const int16_t *pcm, size_t samples) {
    size_t n = samples * 2;
    if (xStreamBufferSend(s_mic_sb, pcm, n, 0) != n) s_mic_dropped += n;  // only if 3 s behind
}

static void mic_sender_task(void *arg) {
    uint8_t *chunk = heap_caps_malloc(MIC_SEND_BYTES, MALLOC_CAP_INTERNAL);
    while (1) {
        size_t got = xStreamBufferReceive(s_mic_sb, chunk, MIC_SEND_BYTES, pdMS_TO_TICKS(100));
        if (got) net_send_audio((const int16_t *)chunk, got / 2);
    }
}

// ---------------------------------------------------------------- user actions
// held_us: how long the finger was down (0 for the BOOT button, which is always trusted)
static void tap(int64_t held_us, bool button) {
    s_last_input = now_us();
    face_set_dim(false);
    if (!net_ready()) {
        face_poke();
        mood_for(MOOD_SAD, 2000);
        return;
    }
    switch (s_act) {
        case ACT_IDLE:
            audio_play_stop();
            face_set_subtitle(NULL);
            net_send_text("{\"type\":\"listen\",\"state\":\"start\"}");
            audio_capture(true);
            set_activity(ACT_LISTENING);
            face_set_mood(MOOD_NEUTRAL);
            break;
        case ACT_LISTENING:
            // Ignore taps while listening: she ends the turn herself when you go quiet.
            // (On battery, holding her can cause phantom touches that cut you off mid-sentence.)
            break;
        case ACT_THINKING:
            if (!button) break;  // phantom touches while carrying her must not cancel your question
            // fall through
        default:  // speaking: interrupt (screen needs a firm 0.15 s press)
            if (!button && held_us < 150000) break;
            net_send_text("{\"type\":\"abort\"}");
            audio_capture(false);
            audio_play_stop();
            set_activity(ACT_IDLE);
            mood_for(MOOD_SURPRISED, 1200);
            break;
    }
}

static void poke(void) {
    s_last_input = now_us();
    face_poke();
    if (s_act == ACT_IDLE) mood_for(MOOD_ANGRY, 2500);
    net_send_text("{\"type\":\"event\",\"name\":\"poke\"}");
}

// ---------------------------------------------------------------- input loop
static void input_task(void *arg) {
    bool down = false, was_boot = false, long_fired = false;
    int64_t down_at = 0;
    int sx = 0, sy = 0, lx = 0, ly = 0;
    float g0x = 0, g0y = 0, g0z = 0, prev_mag = 1;
    bool have_g0 = false;
    float shake = 0;
    int64_t face_down_since = 0;
    bool asleep = false;
    int tick = 0;
    s_last_input = now_us();

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(20));
        tick++;
        int64_t now = now_us();

        // touch: tap = talk / stop, long press = poke, drag = look around
        int x, y;
        bool pressed = board_touch_read(&x, &y);
        if (pressed && !down) {
            down = true, long_fired = false, down_at = now, sx = x, sy = y;
        }
        if (pressed) {
            lx = x, ly = y;
            face_look_at((x - 233) / 233.0f, (y - 233) / 233.0f);
            if (!long_fired && now - down_at > 1200000 && abs(x - sx) < 30 && abs(y - sy) < 30 &&
                s_act != ACT_LISTENING) {
                long_fired = true;
                poke();
            }
        } else if (down) {
            down = false;
            int64_t held = now - down_at;
            int moved = abs(lx - sx) + abs(ly - sy);
            // a real tap: 0.1-0.5 s and not sliding. Phantom touches (common on battery) are blips or drifts.
            bool ok = !long_fired && held >= 100000 && held <= 500000 && moved < 25;
            int dx = lx - sx, dy = ly - sy;
            // quick vertical swipe = volume (up = louder); slow sideways stroke = a pat on the head
            bool swipe = !long_fired && held < 900000 && abs(dy) > 120 && abs(dx) < 70;
            bool pat = !long_fired && held > 400000 && abs(dx) > 70 && abs(dy) < abs(dx);
            ESP_LOGI(TAG, "touch %lld ms at %d,%d moved %d,%d -> %s", held / 1000, sx, sy, dx, dy,
                     ok ? "tap" : swipe ? "volume" : pat ? "pat" : "ignored");
            if (ok) {
                tap(held, false);
            } else if (swipe) {
                s_last_input = now;
                volume_step(dy < 0 ? 10 : -10);
            } else if (pat && s_act == ACT_IDLE) {
                s_last_input = now;
                mood_for(MOOD_HAPPY, 2500);
                face_poke();
                net_send_text("{\"type\":\"event\",\"name\":\"pat\"}");
            }
        }

        bool boot = board_boot_pressed();
        if (boot && !was_boot) tap(0, true);  // physical button: always reliable
        was_boot = boot;

        // motion: shake = dizzy laugh, face down = sleep
        float ax, ay, az;
        if ((tick & 1) && board_imu_read(&ax, &ay, &az)) {
            float mag = sqrtf(ax * ax + ay * ay + az * az);
            shake = shake * 0.85f + fabsf(mag - prev_mag);
            prev_mag = mag;
            if (!have_g0 && fabsf(mag - 1) < 0.1f) g0x = ax, g0y = ay, g0z = az, have_g0 = true;
            // picked up or moved: wake up (and tell the server if she was asleep)
            if (shake > 0.35f && now - s_last_input > 2000000) {
                if (asleep) net_send_text("{\"type\":\"event\",\"name\":\"pickup\"}");
                s_last_input = now;
            }
            if (shake > 2.8f && s_act == ACT_IDLE) {  // a real shake, not walking around
                shake = 0;
                s_last_input = now;
                mood_for(MOOD_LAUGHING, 2500);
                face_poke();
                net_send_text("{\"type\":\"event\",\"name\":\"shake\"}");
            }
            bool face_down = have_g0 && (ax * g0x + ay * g0y + az * g0z) / fmaxf(mag, 0.1f) < -0.7f;
            if (face_down) {
                if (!face_down_since) face_down_since = now;
            } else {
                face_down_since = 0;
            }
        }

        // rest (dim, body still) after 20 s; sleep (drives away, screen dark) after 3 min or face down.
        // At night: 10 s and 45 s. Any touch, button, motion or reply wakes her.
        int64_t quiet = now - s_last_input;
        bool should_rest = s_act == ACT_IDLE && quiet > (s_night ? 10000000 : 20000000);
        bool should_sleep = s_act == ACT_IDLE && ((face_down_since && now - face_down_since > 1500000) ||
                                                  quiet > (s_night ? 45000000LL : 180000000LL));
        face_set_rest(should_rest);
        if (should_sleep && !asleep) {
            asleep = true;
            face_set_mood(MOOD_SLEEPY);
            face_set_dim(true);
        } else if (!should_sleep && asleep) {
            asleep = false;
            face_set_dim(false);
            mood_for(MOOD_SURPRISED, 1000);
            net_send_text("{\"type\":\"event\",\"name\":\"wake\"}");  // morning hello, if it's morning
        }
        if (s_text_clear_at && now > s_text_clear_at) {
            s_text_clear_at = 0;
            face_set_text(NULL, NULL);
        }

        // battery: icon on screen, log every minute, warning when nearly empty
        static int64_t next_batt, next_log;
        static bool warned;
        if (now > next_batt) {
            next_batt = now + 5000000;
            int pct = board_battery_percent();
            bool chg = board_charging();
            face_set_battery(pct, chg);
            // tell the PC (dashboard battery gauge) when it changes, and at least every 30 s
            static int sent_pct = -2;
            static bool sent_chg;
            static int64_t next_send;
            if (net_ready() && (pct != sent_pct || chg != sent_chg || now > next_send || s_vol_changed)) {
                char msg[128];
                snprintf(msg, sizeof(msg), "{\"type\":\"battery\",\"pct\":%d,\"chg\":%s,\"vol\":%d,\"und\":%lu,\"temp\":%.1f}",
                         pct, chg ? "true" : "false", vol_percent(audio_get_volume()), (unsigned long)audio_underruns(),
                         board_temp_c());
                net_send_text(msg);
                sent_pct = pct, sent_chg = chg, next_send = now + 30000000, s_vol_changed = false;
            }
            if (now > next_log) {
                next_log = now + 60000000;
                ESP_LOGI(TAG, "battery %d%% %s, %d mV", pct, chg ? "charging" : "not charging", board_battery_mv());
            }
            if (pct >= 0 && pct <= 8 && !chg && !warned) {
                warned = true;
                face_set_text("Low battery", "Please charge me");
                mood_for(MOOD_SLEEPY, 4000);
            } else if (warned && (chg || pct > 12)) {
                warned = false;
                face_set_text(NULL, NULL);
            }
        }

        // reply finished playing
        if (s_act == ACT_SPEAKING && s_tts_done && !audio_playing()) {
            set_activity(ACT_IDLE);
            s_last_input = now;
            if (!s_mood_until) s_mood_until = now + 5000000;
            s_sub_clear_at = now + 2500000;  // keep the last subtitle readable for a moment
        }
        if (s_sub_clear_at && now > s_sub_clear_at) {
            s_sub_clear_at = 0;
            face_set_subtitle(NULL);
        }
        if (s_mood_until && now > s_mood_until && s_act == ACT_IDLE && !asleep) {
            s_mood_until = 0;
            face_set_mood(net_ready() ? MOOD_NEUTRAL : MOOD_SLEEPY);
        }
    }
}

void app_main(void) {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }
    esp_pm_config_t pm = {.max_freq_mhz = 240, .min_freq_mhz = 80, .light_sleep_enable = false};
    if (esp_pm_configure(&pm) != ESP_OK) ESP_LOGW(TAG, "power management not available");
    board_init();
    board_temp_c();  // start the chip temperature sensor once, before the tasks that read it
    face_init();
    face_set_text("Hi, I'm Chitti", "Waking up...");
    s_mic_sb = xStreamBufferCreateWithCaps(MIC_BUF_BYTES, MIC_SEND_BYTES, MALLOC_CAP_SPIRAM);
    xStreamBufferSetTriggerLevel(s_mic_sb, MIC_SEND_BYTES);
    xTaskCreatePinnedToCore(mic_sender_task, "mic_send", 4096, NULL, 5, NULL, 0);
    audio_init(on_mic);
    volume_load();
    net_callbacks_t cb = {
        .on_status = on_status,
        .on_connected = on_connected,
        .on_disconnected = on_disconnected,
        .on_text = on_text,
        .on_audio = on_audio,
    };
    net_init(&cb);
    usb_setup_init();
    xTaskCreatePinnedToCore(input_task, "input", 4096, NULL, 5, NULL, 0);
    ESP_LOGI(TAG, "Chitti is awake");
}
