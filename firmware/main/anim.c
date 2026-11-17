#include "anim.h"

#include <stdlib.h>
#include <string.h>

#include "board.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_partition.h"

static const char *TAG = "anim";

typedef struct __attribute__((packed)) {
    char name[12];
    uint16_t first, count;
    uint8_t fps, loop;
    uint16_t pad;
} anim_rec_t;

typedef struct __attribute__((packed)) {
    uint32_t offset, size;
    int16_t x0, y0;
    uint16_t w, h;
    int16_t hx10, hy10, jx10, jy10;
} frame_rec_t;

static const esp_partition_t *s_part;
static anim_rec_t *s_anims;
static frame_rec_t *s_frames;
static int s_nanims, s_nframes;
static uint8_t *s_blob;
static size_t s_blob_cap;

bool anim_init(void) {
    s_part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, 0x40, "anim");
    if (!s_part) {
        ESP_LOGE(TAG, "no 'anim' partition");
        return false;
    }
    uint8_t head[8];
    if (esp_partition_read(s_part, 0, head, 8) != ESP_OK || memcmp(head, "CHAN", 4) != 0) {
        ESP_LOGE(TAG, "anim partition is empty: flash anim.bin (build.bat anim)");
        return false;
    }
    s_nanims = head[4] | head[5] << 8;
    s_nframes = head[6] | head[7] << 8;
    s_anims = malloc(sizeof(anim_rec_t) * s_nanims);
    s_frames = heap_caps_malloc(sizeof(frame_rec_t) * s_nframes, MALLOC_CAP_SPIRAM);
    if (!s_anims || !s_frames) return false;
    esp_partition_read(s_part, 8, s_anims, sizeof(anim_rec_t) * s_nanims);
    esp_partition_read(s_part, 8 + sizeof(anim_rec_t) * s_nanims, s_frames, sizeof(frame_rec_t) * s_nframes);
    for (int i = 0; i < s_nframes; i++)
        if (s_frames[i].size > s_blob_cap) s_blob_cap = s_frames[i].size;
    s_blob = heap_caps_malloc(s_blob_cap, MALLOC_CAP_SPIRAM);
    ESP_LOGI(TAG, "%d animations, %d frames", s_nanims, s_nframes);
    return s_blob != NULL;
}

static bool valid(int a) { return s_anims && a >= 0 && a < s_nanims; }
int anim_count(int a) { return valid(a) ? s_anims[a].count : 0; }
int anim_fps(int a) { return valid(a) && s_anims[a].fps ? s_anims[a].fps : 10; }
bool anim_loops(int a) { return valid(a) && s_anims[a].loop; }

bool anim_decode(int a, int i, uint16_t *fb, anim_frame_info_t *info) {
    if (!valid(a) || i < 0 || i >= s_anims[a].count) return false;
    const frame_rec_t *f = &s_frames[s_anims[a].first + i];
    if (esp_partition_read(s_part, f->offset, s_blob, f->size) != ESP_OK) return false;
    const uint16_t *pal = (const uint16_t *)s_blob;  // already in panel byte order
    const uint8_t *p = s_blob + 512, *end = s_blob + f->size;
    int x = 0, y = 0;
    uint16_t *row = fb + f->y0 * LCD_W + f->x0;
    while (p < end && y < f->h) {
        uint8_t c = *p++;
        int n = (c & 0x7F) + 1;
        bool run = c & 0x80;
        uint8_t v = run ? *p++ : 0;
        for (int k = 0; k < n && y < f->h; k++) {
            row[x] = pal[run ? v : *p++];
            if (++x == f->w) {
                x = 0, y++;
                row += LCD_W;
            }
        }
    }
    info->x0 = f->x0, info->y0 = f->y0, info->w = f->w, info->h = f->h;
    info->hx = f->hx10 / 10.0f, info->hy = f->hy10 / 10.0f;
    info->jx = f->jx10 / 10.0f, info->jy = f->jy10 / 10.0f;
    return true;
}
