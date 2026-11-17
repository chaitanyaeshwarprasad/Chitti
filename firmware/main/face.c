// Chitti's face: glowing robot eyes, mouth and hands drawn as signed-distance shapes.
// Only the area that changed is redrawn, in horizontal bands, straight to the AMOLED.
#include "face.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "audio.h"
#include "board.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_pm.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "font.h"
#include "font_small.h"
#include "anim.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "face";

#define PI_F 3.14159265f
#define CX 233.0f
#define CY 233.0f
#define BAND 24
#define LAYERS 3  // 0 = mood glow color, 1 = pink accent, 2 = white (hands, text)
#define GLOW 16.0f
#define MAXP 48

typedef enum { P_RRECT, P_CAPS, P_ARC, P_CIRC } pkind_t;

typedef struct {
    uint8_t kind, layer;
    float a, b, c, d, e, f, g, h;  // shape parameters (see sd_prim())
    float ring;              // >0: outline of this thickness instead of a filled shape
    float glow;              // glow strength 0..1
    float alpha;
    bool clip;
    float nx, ny, nc;        // clip: remove where nx*x + ny*y > nc
} prim_t;

typedef struct { int x0, y0, x1, y1; } rect_t;

// ---------------------------------------------------------------- mood table
enum { E_PILL, E_HAPPY, E_LINE, E_CHEVRON, E_WINK };
enum { M_SMILE, M_OPEN, M_O, M_FLAT, M_TEETH, M_TONGUE, M_FROWN, M_TALK };
enum { H_NONE, H_WAVE, H_FISTS, H_LAUGH, H_CHIN };
enum { X_NONE, X_STARS, X_ZZZ, X_DOTS, X_BARS };

typedef struct {
    uint8_t eye;
    float ew, eh, lid, angry, sad;
    uint8_t mouth;
    float r, g, b;
    uint8_t hands, extra;
    float lookx, looky;
} mood_cfg_t;

static const mood_cfg_t MOODS[MOOD_COUNT] = {
    [MOOD_NEUTRAL] = {E_PILL, 64, 96, 0, 0, 0, M_SMILE, 80, 240, 255, H_NONE, X_NONE, 0, 0},
    [MOOD_HAPPY] = {E_HAPPY, 64, 96, 0, 0, 0, M_OPEN, 80, 240, 255, H_NONE, X_NONE, 0, 0},
    [MOOD_EXCITED] = {E_PILL, 76, 112, 0, 0, 0, M_OPEN, 90, 245, 255, H_WAVE, X_STARS, 0, 0},
    [MOOD_CALM] = {E_PILL, 66, 90, 0.38f, 0, 0, M_SMILE, 90, 220, 255, H_NONE, X_NONE, 0, 0},
    [MOOD_THINKING] = {E_PILL, 58, 80, 0.12f, 0, 0, M_FLAT, 80, 240, 255, H_CHIN, X_DOTS, -14, -14},
    [MOOD_SURPRISED] = {E_PILL, 82, 86, 0, 0, 0, M_O, 90, 245, 255, H_NONE, X_NONE, 0, 0},
    [MOOD_SAD] = {E_PILL, 60, 84, 0, 0, 1, M_FROWN, 90, 160, 255, H_NONE, X_NONE, 0, 6},
    [MOOD_SLEEPY] = {E_LINE, 64, 96, 0, 0, 0, M_O, 70, 170, 220, H_NONE, X_ZZZ, 0, 0},
    [MOOD_PLAYFUL] = {E_WINK, 64, 96, 0, 0, 0, M_TONGUE, 80, 240, 255, H_NONE, X_NONE, 0, 0},
    [MOOD_ANGRY] = {E_PILL, 70, 90, 0, 1, 0, M_TEETH, 255, 70, 45, H_FISTS, X_NONE, 0, 0},
    [MOOD_LAUGHING] = {E_CHEVRON, 64, 96, 0, 0, 0, M_OPEN, 80, 240, 255, H_LAUGH, X_NONE, 0, 0},
    [MOOD_DANCING] = {E_HAPPY, 64, 96, 0, 0, 0, M_OPEN, 90, 245, 255, H_NONE, X_STARS, 0, 0},
    [MOOD_DRIVING] = {E_PILL, 70, 100, 0, 0, 0, M_OPEN, 90, 245, 255, H_NONE, X_NONE, 6, 0},
};

static const char *MOOD_NAMES[MOOD_COUNT] = {"neutral", "happy", "excited", "calm", "thinking", "surprised",
                                             "sad", "sleepy", "playful", "angry", "laughing", "dancing", "driving"};

// ---------------------------------------------------------------- shared state
static volatile mood_t s_mood = MOOD_NEUTRAL;
static volatile activity_t s_act = ACT_IDLE;
static volatile int64_t s_look_until;
static volatile float s_touch_x, s_touch_y;
static volatile float s_pop;
static volatile bool s_dim, s_rest, s_night;
static char s_text[2][32];
static volatile bool s_text_dirty;

// smoothed parameters
static struct {
    float ew, eh, lid, angry, sad, r, g, b, lx, ly, open;
} P = {64, 96, 0, 0, 0, 80, 240, 255, 0, 0, 0};

static prim_t s_prims[MAXP];
static int s_np;
static rect_t s_prev_box = {0, 0, 0, 0};

static uint8_t *s_layers;      // LAYERS * LCD_W * BAND
static uint16_t *s_buf[2];     // DMA band buffers
static bool s_pending[2];
static float s_bars[5];
static int64_t s_t_wait, s_t_render, s_t_build, s_px, s_t_raster, s_t_draw;  // profiling
static esp_pm_lock_handle_t s_cpu_lock;
// Static artwork (helmet, hair, ear pods, visor) from docs/design/face_static.html, RGB565 big-endian.
static uint16_t *s_fb;  // full-screen RGB565 frame (panel byte order): the current body animation frame
#define s_bg ((const uint16_t *)s_fb)
static float s_hx = 238.2f, s_hy = 182.8f;  // head centre of the current frame
static float s_jx = 238.2f, s_jy = 358.4f;  // hover jet of the current frame
static bool s_away = true;                  // off screen (asleep); starts true so she flies in at boot
#define PANEL_BG 0xE508  // dark navy #0c1c2c behind status text (RGB565 0x08E5, byte-swapped)
static rect_t s_panel;   // area of the text panel, empty when no text
static prim_t s_prev[MAXP];
static int s_prev_np = -1;
static int s_prev_col[3];

static bool near(float a, float b, float tol) { return fabsf(a - b) <= tol; }

// Same shape within a fraction of a pixel: nothing visible changed.
static bool prim_same(const prim_t *p, const prim_t *q) {
    if (p->kind != q->kind || p->layer != q->layer || p->clip != q->clip) return false;
    const float tol = 0.15f;
    return near(p->a, q->a, tol) && near(p->b, q->b, tol) && near(p->c, q->c, tol) && near(p->d, q->d, 0.002f) &&
           near(p->e, q->e, p->kind == P_ARC ? 0.002f : tol) && near(p->f, q->f, tol) && near(p->g, q->g, 0.002f) &&
           near(p->h, q->h, 0.002f) && near(p->ring, q->ring, tol) && near(p->alpha, q->alpha, 0.02f) &&
           (!p->clip || (near(p->nx, q->nx, 0.002f) && near(p->ny, q->ny, 0.002f) && near(p->nc, q->nc, tol)));
}

// ---------------------------------------------------------------- public API
void face_set_mood(mood_t m) {
    if (m < MOOD_COUNT) s_mood = m;
}

mood_t face_mood_from_name(const char *name) {
    for (int i = 0; i < MOOD_COUNT; i++)
        if (strcmp(name, MOOD_NAMES[i]) == 0) return (mood_t)i;
    return MOOD_HAPPY;
}

void face_set_activity(activity_t a) { s_act = a; }

void face_set_text(const char *l1, const char *l2) {
    snprintf(s_text[0], sizeof(s_text[0]), "%s", l1 ? l1 : "");
    snprintf(s_text[1], sizeof(s_text[1]), "%s", l2 ? l2 : "");
    s_text_dirty = true;
}

void face_look_at(float x, float y) {
    s_touch_x = x;
    s_touch_y = y;
    s_look_until = esp_timer_get_time() + 1500000;
}

void face_poke(void) { s_pop = 1.0f; }
void face_set_dim(bool dim) { s_dim = dim; }
void face_set_rest(bool rest) { s_rest = rest; }
void face_set_night(bool night) { s_night = night; }

static volatile int s_batt = -1;      // percent, -1 = no battery
static volatile bool s_charging;
void face_set_battery(int percent, bool charging) {
    s_batt = percent;
    s_charging = charging;
}

// ---------------------------------------------------------------- primitive builders
static prim_t *add(pkind_t k, int layer) {
    if (s_np >= MAXP) return &s_prims[MAXP - 1];
    prim_t *p = &s_prims[s_np++];
    memset(p, 0, sizeof(*p));
    p->kind = k;
    p->layer = layer;
    p->alpha = 1;
    p->glow = layer == 2 ? 0.15f : 0.38f;
    return p;
}

static prim_t *rrect(float cx, float cy, float hw, float hh, float r, int layer) {
    prim_t *p = add(P_RRECT, layer);
    p->a = cx, p->b = cy, p->c = hw, p->d = hh, p->e = fminf(r, fminf(hw, hh));
    return p;
}

static prim_t *caps(float ax, float ay, float bx, float by, float r, int layer) {
    prim_t *p = add(P_CAPS, layer);
    p->a = ax, p->b = ay, p->c = bx, p->d = by, p->e = r;
    return p;
}

// Arc from angle a0 to a1 (radians, screen coordinates). Stored as mid-direction and half-aperture
// so the distance needs no trigonometry per pixel.
static prim_t *arc(float cx, float cy, float R, float a0, float a1, float r, int layer) {
    prim_t *p = add(P_ARC, layer);
    float mid = (a0 + a1) * 0.5f, half = (a1 - a0) * 0.5f;
    p->a = cx, p->b = cy, p->c = R, p->f = r;
    p->d = cosf(mid), p->e = sinf(mid), p->g = sinf(half), p->h = cosf(half);
    return p;
}

static prim_t *circ(float cx, float cy, float r, int layer) {
    prim_t *p = add(P_CIRC, layer);
    p->a = cx, p->b = cy, p->c = r;
    return p;
}

// Remove the side of line A->B that contains point O (outside).
static void cut(prim_t *p, float ax, float ay, float bx, float by, float ox, float oy) {
    float nx = by - ay, ny = -(bx - ax);
    float len = sqrtf(nx * nx + ny * ny);
    if (len < 1e-3f) return;
    nx /= len, ny /= len;
    float c = nx * ax + ny * ay;
    if (nx * ox + ny * oy - c < 0) nx = -nx, ny = -ny, c = -c;
    p->clip = true, p->nx = nx, p->ny = ny, p->nc = c;
}

// ---------------------------------------------------------------- signed distance
static inline float sd_prim(const prim_t *p, float x, float y) {
    float d;
    switch (p->kind) {
        case P_RRECT: {
            float qx = fabsf(x - p->a) - p->c + p->e, qy = fabsf(y - p->b) - p->d + p->e;
            float mx = fmaxf(qx, 0), my = fmaxf(qy, 0);
            d = sqrtf(mx * mx + my * my) + fminf(fmaxf(qx, qy), 0) - p->e;
            break;
        }
        case P_CAPS: {
            float pax = x - p->a, pay = y - p->b, bax = p->c - p->a, bay = p->d - p->b;
            float bb = bax * bax + bay * bay;
            float h = bb > 0 ? fminf(fmaxf((pax * bax + pay * bay) / bb, 0), 1) : 0;
            float dx = pax - bax * h, dy = pay - bay * h;
            d = sqrtf(dx * dx + dy * dy) - p->e;
            break;
        }
        case P_ARC: {
            float dx = x - p->a, dy = y - p->b;
            float ly = dx * p->d + dy * p->e;            // along the arc's middle direction
            float lx = fabsf(dy * p->d - dx * p->e);     // across it (arc is symmetric)
            if (p->h * lx > p->g * ly) {                 // past the arc's end: distance to the end cap
                float ex = lx - p->g * p->c, ey = ly - p->h * p->c;
                d = sqrtf(ex * ex + ey * ey) - p->f;
            } else {
                d = fabsf(sqrtf(lx * lx + ly * ly) - p->c) - p->f;
            }
            break;
        }
        default: {
            float dx = x - p->a, dy = y - p->b;
            d = sqrtf(dx * dx + dy * dy) - p->c;
        }
    }
    if (p->ring > 0) d = fabsf(d) - p->ring;
    if (p->clip) d = fmaxf(d, p->nx * x + p->ny * y - p->nc);
    return d;
}

static rect_t prim_box(const prim_t *p) {
    float m = (p->glow > 0 ? GLOW : 0) + p->ring + 2, x0, y0, x1, y1;
    switch (p->kind) {
        case P_RRECT: x0 = p->a - p->c, x1 = p->a + p->c, y0 = p->b - p->d, y1 = p->b + p->d; break;
        case P_CAPS:
            x0 = fminf(p->a, p->c) - p->e, x1 = fmaxf(p->a, p->c) + p->e;
            y0 = fminf(p->b, p->d) - p->e, y1 = fmaxf(p->b, p->d) + p->e;
            break;
        case P_ARC: x0 = p->a - p->c - p->f, x1 = p->a + p->c + p->f, y0 = p->b - p->c - p->f, y1 = p->b + p->c + p->f; break;
        default: x0 = p->a - p->c, x1 = p->a + p->c, y0 = p->b - p->c, y1 = p->b + p->c;
    }
    rect_t r = {(int)floorf(x0 - m), (int)floorf(y0 - m), (int)ceilf(x1 + m), (int)ceilf(y1 + m)};
    return r;
}

static rect_t rect_union(rect_t a, rect_t b) {
    if (a.x1 <= a.x0) return b;
    if (b.x1 <= b.x0) return a;
    rect_t r = {a.x0 < b.x0 ? a.x0 : b.x0, a.y0 < b.y0 ? a.y0 : b.y0, a.x1 > b.x1 ? a.x1 : b.x1, a.y1 > b.y1 ? a.y1 : b.y1};
    return r;
}

// ---------------------------------------------------------------- scene
// Ease toward b; snaps when close so a settled face stops changing (and stops being redrawn).
static float lerp(float a, float b, float k) {
    float v = a + (b - a) * k;
    return fabsf(b - v) < 0.05f ? b : v;
}
static float frand(void) { return (esp_random() & 0xFFFF) / 65535.0f; }

static void zee(float x, float y, float s, float alpha) {
    prim_t *p;
    p = caps(x, y, x + s, y, 2.5f, 0), p->alpha = alpha;
    p = caps(x + s, y, x, y + s, 2.5f, 0), p->alpha = alpha;
    p = caps(x, y + s, x + s, y + s, 2.5f, 0), p->alpha = alpha;
}

static void sparkle(float x, float y, float s, float alpha) {
    prim_t *p;
    p = caps(x - s, y, x + s, y, 2.0f, 0), p->alpha = alpha;
    p = caps(x, y - s, x, y + s, 2.0f, 0), p->alpha = alpha;
}

static void hashmark(float x, float y) {
    caps(x - 7, y - 4, x + 7, y - 4, 1.6f, 0), caps(x - 7, y + 4, x + 7, y + 4, 1.6f, 0);
    caps(x - 2, y - 9, x - 5, y + 9, 1.6f, 0), caps(x + 5, y - 9, x + 2, y + 9, 1.6f, 0);
}

// Layout of the glowing parts on top of the static artwork (docs/design/face_static.html:
// head centre (240, 252), scale 1.85). Visor spans x 125..355, y 178..348.
#define FX 240.0f
#define EYE_Y 248.0f
#define EYE_DX 50.0f
#define MOUTH_Y 300.0f
#define K 0.63f  // mood table sizes -> visor sizes
#define EAR_DX 148.0f
#define EAR_Y 263.0f
#define ANT_X 319.5f
#define ANT_Y 61.5f


// The face is designed in close-up coordinates (head centre 240,252 at 1.85 px per design unit).
// The full-body view (docs/design/face_full.html) draws the same head at 238.2,182.8 and 1.045 px/unit,
// so every glowing shape is mapped across with one scale + offset.
#define FULL_HX s_hx
#define FULL_HY s_hy
#define FULL_F (1.045f / 1.85f)

static void map_to_full_view(void) {
    const float f = FULL_F;
    for (int i = 0; i < s_np; i++) {
        prim_t *p = &s_prims[i];
        float ox = p->a, oy = p->b;
        p->a = FULL_HX + (ox - FX) * f, p->b = FULL_HY + (oy - 252.0f) * f;
        switch (p->kind) {
            case P_RRECT: p->c *= f, p->d *= f, p->e *= f; break;
            case P_CAPS:
                p->c = FULL_HX + (p->c - FX) * f, p->d = FULL_HY + (p->d - 252.0f) * f, p->e *= f;
                break;
            case P_ARC: p->c *= f, p->f *= f; break;
            default: p->c *= f;
        }
        p->ring *= f;
        if (p->clip) p->nc = p->nx * FULL_HX + p->ny * FULL_HY + f * (p->nc - p->nx * FX - p->ny * 252.0f);
    }
}

static void build_scene(float t, float blink) {
    s_np = 0;
    if (s_away) return;  // off screen: nothing to draw
    mood_t mood = s_mood;
    activity_t act = s_act;
    if (act == ACT_THINKING) mood = MOOD_THINKING;
    const mood_cfg_t *m = &MOODS[mood];

    float ew = m->ew, eh = m->eh;
    if (act == ACT_LISTENING) ew += 6, eh += 10;
    float pop = s_pop;
    float squash = 1 + 0.18f * pop * cosf((1 - pop) * 14);

    float k = 0.22f;
    P.ew = lerp(P.ew, ew, k), P.eh = lerp(P.eh, eh, k);
    P.lid = lerp(P.lid, m->lid, k), P.angry = lerp(P.angry, m->angry, k), P.sad = lerp(P.sad, m->sad, k);
    P.r = lerp(P.r, m->r, 0.12f), P.g = lerp(P.g, m->g, 0.12f), P.b = lerp(P.b, m->b, 0.12f);

    static float wx, wy;
    static int64_t next_wander;
    int64_t now = esp_timer_get_time();
    if (now > next_wander) {
        wx = (frand() - 0.5f) * 14, wy = (frand() - 0.5f) * 6;
        next_wander = now + 1500000 + (int64_t)(frand() * 2500000);
    }
    float tx = m->lookx * 0.6f, ty = m->looky * 0.6f;
    if (now < s_look_until) tx = s_touch_x * 14, ty = s_touch_y * 8;
    else if (act == ACT_IDLE && mood == MOOD_NEUTRAL) tx = wx, ty = wy;
    P.lx = lerp(P.lx, tx, 0.15f), P.ly = lerp(P.ly, ty, 0.15f);

    float shake = mood == MOOD_ANGRY ? sinf(t * 70) * 1.5f : 0;
    float bounce = mood == MOOD_LAUGHING ? -fabsf(sinf(t * 11)) * 4 : 0;
    float lx = P.lx + shake, ly = P.ly + bounce;
    float eye_y = EYE_Y + ly;
    float EW = P.ew * K, EH = P.eh * K;

    // ---- antenna light: soft always, pulsing while listening / thinking
    {
        bool busy = act == ACT_LISTENING || act == ACT_THINKING;
        float pulse = busy ? 0.5f + 0.5f * sinf(floorf(t * 15) / 15.0f * 7) : 0.35f;
        prim_t *a = circ(ANT_X, ANT_Y, 10, 0);
        a->alpha = 0.35f + 0.55f * pulse;
        a->glow = 0.5f;
    }

    // ---- eyes
    for (int s = -1; s <= 1; s += 2) {
        float cx = FX + s * EYE_DX + lx;
        int style = m->eye;
        if (style == E_WINK) style = (s < 0) ? E_HAPPY : E_PILL;
        if (style == E_PILL) {
            float hw = EW * 0.5f * squash, hh = EH * 0.5f * blink / squash;
            prim_t *e = rrect(cx, eye_y, hw, fmaxf(hh, 2.5f), hw, 0);
            float top = eye_y - EH * 0.5f;
            if (P.angry > 0.02f) {
                cut(e, cx - s * hw * 1.2f, top + EH * (0.02f + 0.18f * P.angry), cx + s * hw * 1.2f,
                    top + EH * (0.02f + 0.55f * P.angry), cx, top - 40);
            } else if (P.sad > 0.02f) {
                cut(e, cx + s * hw * 1.2f, top + EH * (0.02f + 0.15f * P.sad), cx - s * hw * 1.2f,
                    top + EH * (0.02f + 0.5f * P.sad), cx, top - 40);
            } else if (P.lid > 0.02f) {
                cut(e, cx - 40, top + EH * P.lid, cx + 40, top + EH * P.lid, cx, top - 40);
            }
            if (hh > 8 && P.angry < 0.3f) {  // glossy highlight
                prim_t *h = circ(cx - hw * 0.35f, eye_y - hh * 0.4f, hw * 0.28f, 2);
                h->glow = 0, h->alpha = 0.85f;
            }
            if (P.angry > 0.5f) {
                prim_t *brow = caps(cx - s * 26, top - 16, cx + s * 18, top + 1, 4, 0);
                brow->alpha = (P.angry - 0.5f) * 2;
            } else if (mood == MOOD_NEUTRAL || mood == MOOD_EXCITED || mood == MOOD_CALM) {
                arc(cx, top + 2, 17, -PI_F + 0.95f, -0.95f, 2.2f, 0);  // small eyebrow
            }
        } else if (style == E_HAPPY) {
            arc(cx, eye_y + 12, 19, -PI_F + 0.45f, -0.45f, 5, 0);
        } else if (style == E_LINE) {
            arc(cx, eye_y - 12, 18, 0.5f, PI_F - 0.5f, 3.5f, 0);
        } else if (style == E_CHEVRON) {
            caps(cx - s * 15, eye_y - 17, cx + s * 11, eye_y, 5, 0);
            caps(cx + s * 11, eye_y, cx - s * 15, eye_y + 17, 5, 0);
        }
    }
    if (mood == MOOD_HAPPY || mood == MOOD_EXCITED) {
        hashmark(FX - 86 + lx, 286 + ly);
        hashmark(FX + 86 + lx, 286 + ly);
    }

    // ---- mouth
    float mx = FX + lx * 0.5f, my = MOUTH_Y + ly * 0.6f;
    int mouth = m->mouth;
    if (act == ACT_SPEAKING && mouth != M_TEETH) mouth = (mood == MOOD_LAUGHING) ? M_OPEN : M_TALK;
    if (act == ACT_LISTENING) mouth = M_O;
    P.open = lerp(P.open, fminf(1, audio_out_level() * 2.2f), 0.5f);
    switch (mouth) {
        case M_SMILE: arc(mx, my - 20, 26, 0.62f, PI_F - 0.62f, 4.5f, 0); break;
        case M_FROWN: arc(mx, my + 22, 22, -PI_F + 0.75f, -0.75f, 4.5f, 0); break;
        case M_O: {
            prim_t *o = circ(mx, my, act == ACT_LISTENING ? 6 + audio_mic_level() * 7 : 7, 0);
            o->ring = 3.2f;
            break;
        }
        case M_FLAT: caps(mx - 7, my + 1, mx + 17, my - 2, 3.6f, 0); break;
        case M_OPEN: {
            float r = mood == MOOD_LAUGHING ? 30 + 3 * fabsf(sinf(t * 11)) : 26;
            prim_t *o = circ(mx, my - 11, r, 0);
            cut(o, mx - 60, my - 7, mx + 60, my - 7, mx, my - 50);
            prim_t *tg = circ(mx + 5, my + r * 0.55f - 3, r * 0.38f, 1);
            cut(tg, mx - 60, my - 4, mx + 60, my - 4, mx, my - 50);
            tg->glow = 0.2f;
            break;
        }
        case M_TONGUE: {
            arc(mx, my - 20, 26, 0.62f, PI_F - 0.62f, 4.5f, 0);
            prim_t *tg = circ(mx + 9, my + 9, 8, 1);
            cut(tg, mx - 60, my + 3, mx + 60, my + 3, mx, my - 50);
            break;
        }
        case M_TEETH: {
            float hh = 10 * (act == ACT_SPEAKING ? 0.7f + P.open * 0.6f : 1);
            prim_t *b = rrect(mx + shake, my, 32, hh, 7, 0);
            b->ring = 2.2f;
            caps(mx - 30 + shake, my, mx + 30 + shake, my, 1.6f, 0);
            for (int i = -1; i <= 1; i++)
                caps(mx + i * 16 + shake, my - hh + 2, mx + i * 16 + shake, my + hh - 2, 1.6f, 0);
            break;
        }
        case M_TALK: {
            float hh = 3 + 14 * P.open, hw = 17 - 4 * P.open;
            rrect(mx, my, hw, hh, fminf(hw, hh), 0);
            break;
        }
    }

    // ---- ear pods: ripples while listening, steam when angry (15 fps is enough for these)
    float t15 = floorf(t * 15) / 15.0f;
    if (act == ACT_LISTENING) {
        float lvl = audio_mic_level();
        for (int s = -1; s <= 1; s += 2)
            for (int i = 0; i < 2; i++) {
                float ph = fmodf(t15 * 1.2f + i * 0.5f, 1.0f);
                prim_t *r = circ(FX + s * EAR_DX, EAR_Y, 30 + ph * 26, 0);
                r->ring = 1.8f, r->glow = 0.25f, r->alpha = fminf(1, (1 - ph) * (0.4f + lvl));
            }
    }
    if (mood == MOOD_ANGRY) {
        for (int s = -1; s <= 1; s += 2)
            for (int i = 0; i < 3; i++) {
                float ph = fmodf(t15 * 0.9f + i / 3.0f, 1.0f);
                prim_t *p = circ(FX + s * (165 + ph * 18), 220 - ph * 70, 6 + ph * 9, 2);
                p->glow = 0, p->alpha = (1 - ph) * 0.7f;
            }
    }

    // ---- extras in the black space around the head
    int extra = m->extra;
    if (act == ACT_THINKING) extra = X_DOTS;
    switch (extra) {
        case X_DOTS:
            for (int i = 0; i < 3; i++) {
                float b = fmaxf(0, sinf(t15 * 6 - i * 0.9f));
                prim_t *d = circ(348 + i * 15, 104 - i * 15 - b * 4, 4 + i, 0);
                d->alpha = 0.4f + 0.6f * b;
            }
            break;
        case X_ZZZ:
            for (int i = 0; i < 3; i++) {
                float ph = fmodf(floorf(t * 10) / 30.0f + i / 3.0f, 1.0f);
                zee(352 + ph * 26, 150 - ph * 60, 10 + 6 * ph, sinf(ph * PI_F));
            }
            break;
        case X_STARS: {
            static const float pos[4][2] = {{70, 130}, {405, 150}, {55, 320}, {410, 320}};
            for (int i = 0; i < 4; i++) {
                float tw = fabsf(sinf(t15 * 3.3f + i * 1.7f));
                sparkle(pos[i][0], pos[i][1], 5 + 8 * tw, tw);
            }
            break;
        }
    }
    map_to_full_view();

    // hover jet under the body: flickers like a tiny thruster
    float fl = sinf(floorf(t * 6) * 1.7f);  // 6 updates a second: lively but cheap
    // battery icon at the top (only when a battery is fitted)
    int pct = s_batt;
    if (pct >= 0) {
        bool low = pct <= 15 && !s_charging;
        bool show = !low || ((int)(t * 2) & 1);  // blink when low
        if (show) {
            prim_t *body = rrect(233, 22, 13, 6.5f, 2.5f, low ? 1 : 2);
            body->ring = 1.2f, body->glow = 0;
            prim_t *nub = rrect(248, 22, 1.5f, 3, 1, low ? 1 : 2);
            nub->glow = 0;
            float w = fmaxf(1.0f, 21.0f * pct / 100.0f);
            prim_t *fill = rrect(233 - 10.5f + w / 2, 22, w / 2, 4, 1.5f, low ? 1 : (s_charging ? 0 : 2));
            fill->glow = s_charging ? 0.3f : 0, fill->alpha = 0.9f;
        }
    }
    if (s_jx > 0) {  // no jet while sitting in the car
        prim_t *jet = circ(s_jx, s_jy + 2 + fl * 1.5f, 9 + fl * 1.2f, 0);
        jet->glow = 0.9f, jet->alpha = 0.85f;
    }
}

// ---------------------------------------------------------------- text
#define TEXT_Y 362

static void text_box(int line, rect_t *r) {
    int len = strlen(s_text[line]);
    int w = len * FONT_W;
    r->x0 = (LCD_W - w) / 2, r->x1 = r->x0 + w;
    r->y0 = TEXT_Y + line * (FONT_H + 4), r->y1 = r->y0 + FONT_H;
}

static void draw_text_band(int line, int bx0, int bw, int by0, int bh) {
    rect_t r;
    text_box(line, &r);
    if (r.x1 <= r.x0 || r.y1 <= by0 || r.y0 >= by0 + bh) return;
    uint8_t *L = s_layers + 2 * LCD_W * BAND;
    const char *s = s_text[line];
    for (int y = (r.y0 > by0 ? r.y0 : by0); y < r.y1 && y < by0 + bh; y++) {
        int gy = y - r.y0;
        for (int i = 0; s[i]; i++) {
            int c = (unsigned char)s[i];
            if (c < FONT_FIRST || c >= FONT_FIRST + FONT_COUNT) c = '?';
            const uint8_t *row = font_data[c - FONT_FIRST][gy];
            for (int gx = 0; gx < FONT_W; gx++) {
                int x = r.x0 + i * FONT_W + gx - bx0;
                if (x < 0 || x >= bw || !row[gx]) continue;
                uint8_t *px = &L[(y - by0) * bw + x];
                if (row[gx] > *px) *px = row[gx];
            }
        }
    }
}

// ---------------------------------------------------------------- subtitles
// Up to 3 short lines under Chitti, narrower toward the bottom of the round screen.
#define SUB_Y 380
#define SUB_LINES 3
static const int SUB_CAP[SUB_LINES] = {34, 30, 24};  // characters that fit each line's width
static char s_sub[SUB_LINES][40];
static volatile bool s_sub_dirty;

void face_set_subtitle(const char *text) {
    char lines[SUB_LINES][40] = {{0}};
    if (text) {
        int li = 0;
        const char *w = text;
        while (*w && li < SUB_LINES) {
            while (*w == ' ') w++;
            int wl = strcspn(w, " ");
            if (!wl) break;
            int cur = strlen(lines[li]);
            int need = cur ? wl + 1 : wl;
            if (cur + need > SUB_CAP[li]) {
                if (!cur) wl = SUB_CAP[li];  // single long word: cut it
                else {
                    li++;
                    continue;
                }
            }
            if (cur) strcat(lines[li], " ");
            strncat(lines[li], w, wl);
            w += wl;
        }
        if (*w && li >= SUB_LINES - 1) {  // didn't fit: end with "..."
            char *last = lines[SUB_LINES - 1];
            int n = strlen(last);
            if (n > SUB_CAP[SUB_LINES - 1] - 3) n = SUB_CAP[SUB_LINES - 1] - 3;
            strcpy(last + n, "...");
        }
    }
    memcpy(s_sub, lines, sizeof(s_sub));
    s_sub_dirty = true;
}

static void sub_box(int line, rect_t *r) {
    int w = strlen(s_sub[line]) * SUB_W;
    r->x0 = (LCD_W - w) / 2, r->x1 = r->x0 + w;
    r->y0 = SUB_Y + line * (SUB_H + 1), r->y1 = r->y0 + SUB_H;
}

static void draw_sub_band(int line, int bx0, int bw, int by0, int bh) {
    rect_t r;
    sub_box(line, &r);
    if (r.x1 <= r.x0 || r.y1 <= by0 || r.y0 >= by0 + bh) return;
    uint8_t *L = s_layers + 2 * LCD_W * BAND;
    const char *s = s_sub[line];
    for (int y = (r.y0 > by0 ? r.y0 : by0); y < r.y1 && y < by0 + bh; y++) {
        int gy = y - r.y0;
        for (int i = 0; s[i]; i++) {
            int c = (unsigned char)s[i];
            if (c < SUB_FIRST || c >= SUB_FIRST + SUB_COUNT) c = '?';
            const uint8_t *row = sub_data[c - SUB_FIRST][gy];
            for (int gx = 0; gx < SUB_W; gx++) {
                int x = r.x0 + i * SUB_W + gx - bx0;
                if (x < 0 || x >= bw || !row[gx]) continue;
                uint8_t *px = &L[(y - by0) * bw + x];
                if (row[gx] > *px) *px = row[gx];
            }
        }
    }
}

// ---------------------------------------------------------------- rasterizer
static void render(rect_t box) {
    if (box.x0 < 0) box.x0 = 0;
    if (box.y0 < 0) box.y0 = 0;
    if (box.x1 > LCD_W) box.x1 = LCD_W;
    if (box.y1 > LCD_H) box.y1 = LCD_H;
    box.x0 &= ~1, box.y0 &= ~1;
    box.x1 = (box.x1 + 1) & ~1, box.y1 = (box.y1 + 1) & ~1;  // panel needs even start and size
    if (box.x1 > LCD_W) box.x1 = LCD_W;
    if (box.y1 > LCD_H) box.y1 = LCD_H;
    int bw = box.x1 - box.x0;
    if (bw <= 0 || box.y1 <= box.y0) return;

    s_px += (int64_t)bw * (box.y1 - box.y0);
    const int cr0 = (int)P.r, cg0 = (int)P.g, cb0 = (int)P.b;  // layer colors: mood, pink accent, white
    static int which;

    for (int y0 = box.y0; y0 < box.y1; y0 += BAND) {
        int bh = box.y1 - y0 < BAND ? box.y1 - y0 : BAND;
        int64_t tr = esp_timer_get_time();
        memset(s_layers, 0, LAYERS * LCD_W * BAND);
        for (int i = 0; i < s_np; i++) {
            const prim_t *p = &s_prims[i];
            rect_t r = prim_box(p);
            int ys = r.y0 > y0 ? r.y0 : y0, ye = r.y1 < y0 + bh ? r.y1 : y0 + bh;
            int xs = r.x0 > box.x0 ? r.x0 : box.x0, xe = r.x1 < box.x1 ? r.x1 : box.x1;
            if (ys >= ye || xs >= xe) continue;
            uint8_t *L = s_layers + p->layer * LCD_W * BAND;
            float glow = p->glow, alpha = p->alpha * 255.0f;
            for (int y = ys; y < ye; y++) {
                uint8_t *row = L + (y - y0) * bw;
                for (int x = xs; x < xe; x++) {
                    float d = sd_prim(p, x + 0.5f, y + 0.5f);
                    float I = d < -0.5f ? 1.0f : (d < 0.5f ? 0.5f - d : 0.0f);
                    if (glow > 0 && d < GLOW && I < 1) {
                        float g = 1 - fmaxf(d, 0) / GLOW;
                        g = g * g * glow;
                        if (g > I) I = g;
                    }
                    int v = (int)(I * alpha);
                    uint8_t *px = &row[x - box.x0];
                    if (v > *px) *px = v > 255 ? 255 : v;
                }
            }
        }
        s_t_raster += esp_timer_get_time() - tr;
        if (s_text[0][0]) draw_text_band(0, box.x0, bw, y0, bh);
        if (s_text[1][0]) draw_text_band(1, box.x0, bw, y0, bh);
        for (int l = 0; l < SUB_LINES; l++)
            if (s_sub[l][0]) draw_sub_band(l, box.x0, bw, y0, bh);

        // compose to RGB565 (big-endian for the panel)
        int64_t tw = esp_timer_get_time();
        if (s_pending[which]) xSemaphoreTake(board_lcd_done(), portMAX_DELAY);
        s_t_wait += esp_timer_get_time() - tw;
        uint16_t *out = s_buf[which];
        const uint8_t *L0 = s_layers, *L1 = s_layers + LCD_W * BAND, *L2 = s_layers + 2 * LCD_W * BAND;
        for (int yy = 0; yy < bh; yy++) {
            int y = y0 + yy;
            const uint16_t *bgrow = s_bg + y * LCD_W + box.x0;
            bool panel_row = s_panel.x1 > s_panel.x0 && y >= s_panel.y0 && y < s_panel.y1;
            for (int xx = 0; xx < bw; xx++) {
                int i = yy * bw + xx;
                uint16_t base = bgrow[xx];
                if (panel_row && box.x0 + xx >= s_panel.x0 && box.x0 + xx < s_panel.x1) base = PANEL_BG;
                int a = L0[i], b = L1[i], c = L2[i];
                if ((a | b | c) == 0) {
                    out[i] = base;
                    continue;
                }
                uint16_t v = (base >> 8) | (base << 8);  // panel byte order -> native RGB565
                int R = ((v >> 8) & 0xF8) + ((cr0 * a + 255 * b + 235 * c) >> 8);
                int G = ((v >> 3) & 0xFC) + ((cg0 * a + 105 * b + 240 * c) >> 8);
                int B = ((v << 3) & 0xF8) + ((cb0 * a + 165 * b + 250 * c) >> 8);
                if (R > 255) R = 255;
                if (G > 255) G = 255;
                if (B > 255) B = 255;
                v = ((R & 0xF8) << 8) | ((G & 0xFC) << 3) | (B >> 3);
                out[i] = (v >> 8) | (v << 8);
            }
        }
        int64_t td = esp_timer_get_time();
        esp_lcd_panel_draw_bitmap(board_panel(), box.x0, y0, box.x1, y0 + bh, out);
        s_t_draw += esp_timer_get_time() - td;
        s_pending[which] = true;
        which ^= 1;
    }
}

static void clear_screen(void) {
    int which = 0;
    for (int y = 0; y < LCD_H; y += BAND) {
        int bh = LCD_H - y < BAND ? LCD_H - y : BAND;
        if (s_pending[which]) xSemaphoreTake(board_lcd_done(), portMAX_DELAY);
        memcpy(s_buf[which], s_bg + y * LCD_W, LCD_W * bh * 2);
        esp_lcd_panel_draw_bitmap(board_panel(), 0, y, LCD_W, y + bh, s_buf[which]);
        s_pending[which] = true;
        which ^= 1;
    }
}

// ---------------------------------------------------------------- body animation
static int s_anim = -1, s_aframe = -1;
static int64_t s_anext, s_greet_until;
static rect_t s_abox;  // rectangle of the body frame on screen now

// Which body animation fits Chitti's state right now.
static int choose_anim(int64_t now) {
    if (s_dim) return ANIM_CARLEAVE;                                                     // falls asleep: drives away
    if (s_anim == ANIM_CARENTER && s_aframe < anim_count(ANIM_CARENTER) - 1) return ANIM_CARENTER;  // arriving
    if (s_away) return ANIM_CARENTER;                                                    // woke up: drives back
    if (now < s_greet_until) return ANIM_WAVE;                                           // just arrived: waves hi
    mood_t m = s_mood;
    activity_t a = s_act;
    if (a == ACT_THINKING) return ANIM_THINK;
    if (a == ACT_LISTENING) return ANIM_IDLE;
    // Chitti acts out the feeling of what she says (and keeps it a few seconds after)
    switch (m) {
        case MOOD_ANGRY: return ANIM_ANGRY;
        case MOOD_LAUGHING: return ANIM_LAUGH;
        case MOOD_DANCING: return ANIM_DANCE;
        case MOOD_DRIVING: return ANIM_DRIVE;
        case MOOD_HAPPY: return ANIM_HAPPY;
        case MOOD_EXCITED: return ANIM_WAVE;
        case MOOD_SAD: return ANIM_SAD;
        case MOOD_SURPRISED: return ANIM_SURPRISED;
        case MOOD_PLAYFUL: return ANIM_PLAYFUL;
        case MOOD_SLEEPY: return a == ACT_SPEAKING ? ANIM_YAWN : ANIM_SLEEP;
        default: break;
    }
    if (a == ACT_SPEAKING) return ANIM_TALK;
    return ANIM_IDLE;
}

static void clear_rect(rect_t r) {
    for (int y = r.y0; y < r.y1; y++) memset(s_fb + y * LCD_W + r.x0, 0, (r.x1 - r.x0) * 2);
}

// Advances the body animation; returns the screen area that changed (empty if none).
static rect_t step_anim(int64_t now) {
    rect_t none = {0, 0, 0, 0};
    int want = choose_anim(now);
    if (want != s_anim) {
        s_anim = want, s_aframe = -1, s_anext = 0;
        if (want == ANIM_CARENTER) s_away = false;  // visible again as she drives in
    }
    if (s_away || now < s_anext) return none;
    if (s_rest && !s_dim && s_aframe >= 0) return none;  // resting: body holds its pose, only the face moves
    int n = anim_count(s_anim);
    if (n <= 0) return none;
    int next = s_aframe + 1;
    if (next >= n) {
        if (anim_loops(s_anim)) {
            next = 0;
        } else {
            if (s_anim == ANIM_CARLEAVE) {  // drove off screen: go dark
                rect_t gone = s_abox;
                clear_rect(gone);
                s_abox = none;
                s_away = true;
                return gone;
            }
            return none;  // hold the last frame
        }
    }
    if (n == 1 && s_aframe == 0) return none;  // still pose already shown
    s_anext = now + 1000000 / anim_fps(s_anim);
    rect_t old = s_abox;
    clear_rect(old);
    anim_frame_info_t fi;
    if (!anim_decode(s_anim, next, s_fb, &fi)) return old;
    s_aframe = next;
    s_abox = (rect_t){fi.x0, fi.y0, fi.x0 + fi.w, fi.y0 + fi.h};
    s_hx = fi.hx, s_hy = fi.hy, s_jx = fi.jx, s_jy = fi.jy;
    if (s_anim == ANIM_CARENTER && next == n - 1) s_greet_until = now + 2500000;
    return rect_union(old, s_abox);
}

static void face_task(void *arg) {
    clear_screen();
    float blink = 1;
    int64_t next_blink = esp_timer_get_time() + 2000000, blink_start = 0;
    int frames = 0, drawn = 0;
    int bright_applied = -1;  // forces the first brightness write
    TickType_t wake = xTaskGetTickCount();
    while (1) {
        int64_t now = esp_timer_get_time();
        float t = now / 1e6f;
        if (now > next_blink) {
            blink_start = now;
            next_blink = now + 2500000 + (int64_t)(frand() * 3000000);
        }
        float bp = (now - blink_start) / 160000.0f;
        blink = bp < 1 ? fmaxf(0.08f, fabsf(cosf(bp * PI_F))) : 1;
        s_pop *= 0.9f;

        rect_t anim_dirty = step_anim(now);
        int64_t tb0 = esp_timer_get_time();
        build_scene(t, blink);
        s_t_build += esp_timer_get_time() - tb0;
        // Redraw only what changed since the last frame.
        rect_t box = {0, 0, 0, 0};
        for (int i = 0; i < s_np; i++) box = rect_union(box, prim_box(&s_prims[i]));
        int col[3] = {(int)P.r, (int)P.g, (int)P.b};
        rect_t draw = {0, 0, 0, 0};
        if (s_np != s_prev_np || memcmp(col, s_prev_col, sizeof(col))) {
            draw = rect_union(box, s_prev_box);
        } else {
            for (int i = 0; i < s_np; i++)
                if (!prim_same(&s_prims[i], &s_prev[i]))
                    draw = rect_union(draw, rect_union(prim_box(&s_prims[i]), prim_box(&s_prev[i])));
        }
        s_panel = (rect_t){0, 0, 0, 0};
        for (int l = 0; l < 2; l++)
            if (s_text[l][0]) {
                rect_t tb;
                text_box(l, &tb);
                tb.x0 -= 12, tb.x1 += 12, tb.y0 -= 6, tb.y1 += 6;
                s_panel = rect_union(s_panel, tb);
            }
        for (int l = 0; l < SUB_LINES; l++)
            if (s_sub[l][0]) {
                rect_t sb;
                sub_box(l, &sb);
                sb.x0 -= 10, sb.x1 += 10, sb.y0 -= 4, sb.y1 += 4;
                s_panel = rect_union(s_panel, sb);
            }
        if (anim_dirty.x1 > anim_dirty.x0) draw = rect_union(draw, anim_dirty);
        if (s_sub_dirty) {
            draw = rect_union(draw, (rect_t){40, SUB_Y - 6, LCD_W - 40, SUB_Y + SUB_LINES * (SUB_H + 1) + 6});
            s_sub_dirty = false;
        }
        if (s_text_dirty) {
            draw = rect_union(draw, (rect_t){20, TEXT_Y - 12, LCD_W - 20, TEXT_Y + 2 * FONT_H + 18});
            s_text_dirty = false;
        }
        memcpy(s_prev, s_prims, sizeof(prim_t) * s_np);
        memcpy(s_prev_col, col, sizeof(col));
        s_prev_np = s_np;
        s_prev_box = box;

        if (draw.x1 > draw.x0) {
            int64_t tr0 = esp_timer_get_time();
            esp_pm_lock_acquire(s_cpu_lock);  // full speed only while drawing, low clock otherwise
            render(draw);
            esp_pm_lock_release(s_cpu_lock);
            s_t_render += esp_timer_get_time() - tr0;
            drawn++;
        }

        // brightness is set here so all panel traffic stays on this task
        // awake 120 (night 60), resting 60 (night 30), falling asleep 30, gone (asleep) 0 = panel dark
        // awake 100 (night 50), resting 40 (night 25), falling asleep 30, asleep 0 = panel dark (less heat)
        int bright = s_dim ? (s_away ? 0 : 30) : s_rest ? (s_night ? 25 : 40) : (s_night ? 50 : 100);
        if (bright != bright_applied) {
            bright_applied = bright;
            board_set_brightness(bright);
        }

        if (++frames == 600) {
            int dd = drawn ? drawn : 1;
            ESP_LOGI(TAG, "chip %.1f C | %d/%d frames redrawn, avg render %lld us, %lld px", board_temp_c(),
                     drawn, frames, s_t_render / dd, s_px / dd);
            s_t_build = s_t_render = s_t_wait = s_px = s_t_raster = s_t_draw = 0;
            frames = drawn = 0;
        }
        // 30 fps talking/listening, 20 fps idle, 10 fps resting, 5 fps asleep: fewer frames = less heat
        int period = s_act != ACT_IDLE ? 33 : s_away ? 250 : s_rest ? 200 : 66;  // 30 / 15 / 5 / 4 fps
        TickType_t next = wake + pdMS_TO_TICKS(period), cur = xTaskGetTickCount();
        if ((int32_t)(next - cur) < 4) {
            vTaskDelay(pdMS_TO_TICKS(4));
            wake = xTaskGetTickCount();
        } else {
            vTaskDelayUntil(&wake, pdMS_TO_TICKS(period));
        }
    }
}

void face_init(void) {
    s_layers = heap_caps_malloc(LAYERS * LCD_W * BAND, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    for (int i = 0; i < 2; i++) s_buf[i] = heap_caps_malloc(LCD_W * BAND * 2, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    assert(s_layers && s_buf[0] && s_buf[1]);
    esp_pm_lock_create(ESP_PM_CPU_FREQ_MAX, 0, "face", &s_cpu_lock);
    s_fb = heap_caps_calloc(LCD_W * LCD_H, 2, MALLOC_CAP_SPIRAM);
    assert(s_fb);
    if (!anim_init()) ESP_LOGE(TAG, "no body animations: only the face will show");
    xTaskCreatePinnedToCore(face_task, "face", 6144, NULL, 5, NULL, 1);
}
