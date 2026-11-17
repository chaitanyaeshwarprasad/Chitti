#pragma once
// Full-body animation frames stored in the 'anim' flash partition (made by tools_anim.py).
#include <stdbool.h>
#include <stdint.h>

#include "anim_ids.h"

typedef struct {
    int16_t x0, y0;       // frame rectangle on screen
    uint16_t w, h;
    float hx, hy;         // head centre (screen px) for the live face
    float jx, jy;         // hover-jet position
} anim_frame_info_t;

bool anim_init(void);                 // false if the partition is missing or empty
int anim_count(int anim);             // frames in an animation
int anim_fps(int anim);
bool anim_loops(int anim);
// Decode frame `i` of `anim` into the full-screen RGB565 buffer `fb` (466x466, panel byte order).
// Only the frame rectangle is written; the caller clears the previous one.
bool anim_decode(int anim, int i, uint16_t *fb, anim_frame_info_t *info);
