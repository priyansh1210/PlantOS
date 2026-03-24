#include "gui/wm.h"
#include "drivers/fb.h"
#include "mm/heap.h"
#include "lib/string.h"

/* Window storage */
static struct wm_window windows[WM_MAX_WINDOWS];

/* Z-order: z_order[0] = bottommost, z_order[z_count-1] = topmost */
static int z_order[WM_MAX_WINDOWS];
static int z_count = 0;

static uint32_t scr_w, scr_h;

/* Title bar colors */
#define TITLE_ACTIVE_BG   FB_RGB(0, 90, 160)
#define TITLE_INACTIVE_BG FB_RGB(80, 80, 100)
#define TITLE_FG          FB_WHITE
#define BORDER_COLOR      FB_RGB(60, 60, 80)
#define CLOSE_BG          FB_RGB(200, 50, 50)
#define CLOSE_HOVER       FB_RGB(240, 60, 60)
#define CLIENT_DEFAULT_BG FB_RGB(30, 30, 45)

void wm_init(uint32_t screen_w, uint32_t screen_h) {
    scr_w = screen_w;
    scr_h = screen_h;
    z_count = 0;
    for (int i = 0; i < WM_MAX_WINDOWS; i++) {
        windows[i].used = false;
        windows[i].pixels = NULL;
    }
}

int wm_create(const char *title, int32_t x, int32_t y, uint32_t cw, uint32_t ch) {
    int id = -1;
    for (int i = 0; i < WM_MAX_WINDOWS; i++) {
        if (!windows[i].used) { id = i; break; }
    }
    if (id < 0) return -1;

    uint32_t *buf = kmalloc(cw * ch * sizeof(uint32_t));
    if (!buf) return -1;

    /* Fill client area with default background */
    for (uint32_t i = 0; i < cw * ch; i++)
        buf[i] = CLIENT_DEFAULT_BG;

    struct wm_window *w = &windows[id];
    w->used = true;
    w->visible = true;
    w->x = x;
    w->y = y;
    w->cw = cw;
    w->ch = ch;
    w->pixels = buf;

    /* Copy title */
    int len = 0;
    while (title[len] && len < 31) {
        w->title[len] = title[len];
        len++;
    }
    w->title[len] = '\0';

    /* Add to top of z-order */
    z_order[z_count++] = id;

    return id;
}

void wm_destroy(int id) {
    if (id < 0 || id >= WM_MAX_WINDOWS || !windows[id].used) return;

    if (windows[id].pixels) {
        kfree(windows[id].pixels);
        windows[id].pixels = NULL;
    }
    windows[id].used = false;
    windows[id].visible = false;

    /* Remove from z-order */
    int pos = -1;
    for (int i = 0; i < z_count; i++) {
        if (z_order[i] == id) { pos = i; break; }
    }
    if (pos >= 0) {
        for (int i = pos; i < z_count - 1; i++)
            z_order[i] = z_order[i + 1];
        z_count--;
    }
}

void wm_destroy_all(void) {
    for (int i = 0; i < WM_MAX_WINDOWS; i++)
        wm_destroy(i);
    z_count = 0;
}

void wm_bring_to_front(int id) {
    if (id < 0 || id >= WM_MAX_WINDOWS || !windows[id].used) return;

    /* Find and remove from current position */
    int pos = -1;
    for (int i = 0; i < z_count; i++) {
        if (z_order[i] == id) { pos = i; break; }
    }
    if (pos < 0) return;

    /* Shift everything after it down */
    for (int i = pos; i < z_count - 1; i++)
        z_order[i] = z_order[i + 1];

    /* Place at top */
    z_order[z_count - 1] = id;
}

struct wm_window *wm_get(int id) {
    if (id < 0 || id >= WM_MAX_WINDOWS || !windows[id].used) return NULL;
    return &windows[id];
}

uint32_t wm_total_w(int id) {
    if (id < 0 || id >= WM_MAX_WINDOWS) return 0;
    return windows[id].cw + WM_BORDER_W * 2;
}

uint32_t wm_total_h(int id) {
    if (id < 0 || id >= WM_MAX_WINDOWS) return 0;
    return windows[id].ch + WM_TITLEBAR_H + WM_BORDER_W;
}

void wm_move(int id, int32_t x, int32_t y) {
    if (id < 0 || id >= WM_MAX_WINDOWS || !windows[id].used) return;
    windows[id].x = x;
    windows[id].y = y;
}

/* ---- Drawing into window client area ---- */

void wm_fill_rect(int id, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
    if (id < 0 || id >= WM_MAX_WINDOWS || !windows[id].used) return;
    struct wm_window *win = &windows[id];
    for (uint32_t j = y; j < y + h && j < win->ch; j++)
        for (uint32_t i = x; i < x + w && i < win->cw; i++)
            win->pixels[j * win->cw + i] = color;
}

static void draw_char_buf(uint32_t *buf, uint32_t buf_w, uint32_t buf_h,
                           uint32_t x, uint32_t y, char c, uint32_t fg, uint32_t bg) {
    if (c < 32 || c > 126) c = '?';
    const uint8_t *glyph = fb_font8x16[c - 32];
    for (int row = 0; row < 16; row++) {
        uint8_t bits = glyph[row];
        for (int col = 0; col < 8; col++) {
            uint32_t px = x + col, py = y + row;
            if (px < buf_w && py < buf_h) {
                uint32_t color = (bits & (0x80 >> col)) ? fg : bg;
                buf[py * buf_w + px] = color;
            }
        }
    }
}

void wm_draw_string(int id, uint32_t x, uint32_t y, const char *s,
                     uint32_t fg, uint32_t bg) {
    if (id < 0 || id >= WM_MAX_WINDOWS || !windows[id].used) return;
    struct wm_window *win = &windows[id];
    uint32_t cx = x;
    while (*s) {
        if (*s == '\n') {
            cx = x;
            y += 16;
        } else {
            draw_char_buf(win->pixels, win->cw, win->ch, cx, y, *s, fg, bg);
            cx += 8;
        }
        s++;
    }
}

/* ---- Additional drawing primitives ---- */

void wm_putpixel(int id, int32_t x, int32_t y, uint32_t color) {
    if (id < 0 || id >= WM_MAX_WINDOWS || !windows[id].used) return;
    struct wm_window *w = &windows[id];
    if (x >= 0 && x < (int32_t)w->cw && y >= 0 && y < (int32_t)w->ch)
        w->pixels[y * w->cw + x] = color;
}

void wm_draw_line(int id, int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t color) {
    /* Bresenham's line algorithm */
    int32_t dx = x1 - x0, dy = y1 - y0;
    int32_t sx = (dx > 0) ? 1 : -1;
    int32_t sy = (dy > 0) ? 1 : -1;
    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;

    int32_t err = dx - dy;
    while (1) {
        wm_putpixel(id, x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int32_t e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 <  dx) { err += dx; y0 += sy; }
    }
}

void wm_draw_circle(int id, int32_t cx, int32_t cy, int32_t r, uint32_t color) {
    /* Midpoint circle algorithm */
    int32_t x = r, y = 0, d = 1 - r;
    while (x >= y) {
        wm_putpixel(id, cx + x, cy + y, color);
        wm_putpixel(id, cx - x, cy + y, color);
        wm_putpixel(id, cx + x, cy - y, color);
        wm_putpixel(id, cx - x, cy - y, color);
        wm_putpixel(id, cx + y, cy + x, color);
        wm_putpixel(id, cx - y, cy + x, color);
        wm_putpixel(id, cx + y, cy - x, color);
        wm_putpixel(id, cx - y, cy - x, color);
        y++;
        if (d < 0) {
            d += 2 * y + 1;
        } else {
            x--;
            d += 2 * (y - x) + 1;
        }
    }
}

void wm_fill_circle(int id, int32_t cx, int32_t cy, int32_t r, uint32_t color) {
    for (int32_t y = -r; y <= r; y++) {
        int32_t half_w = 0;
        /* Integer sqrt: find max x where x*x + y*y <= r*r */
        int32_t rr = r * r - y * y;
        while ((half_w + 1) * (half_w + 1) <= rr) half_w++;
        for (int32_t x = -half_w; x <= half_w; x++)
            wm_putpixel(id, cx + x, cy + y, color);
    }
}

/* ---- Compositing ---- */

/* Draw a single window's chrome + client area onto the LFB */
static void composite_window(volatile uint32_t *lfb, uint32_t stride,
                               int id, bool is_top) {
    struct wm_window *w = &windows[id];
    if (!w->used || !w->visible) return;

    int32_t wx = w->x, wy = w->y;
    uint32_t tw = wm_total_w(id);
    uint32_t th = wm_total_h(id);

    uint32_t title_bg = is_top ? TITLE_ACTIVE_BG : TITLE_INACTIVE_BG;

    /* Draw border (1px) */
    for (uint32_t i = 0; i < tw; i++) {
        int32_t sx = wx + (int32_t)i;
        /* Top border */
        if (sx >= 0 && sx < (int32_t)scr_w && wy >= 0 && wy < (int32_t)scr_h)
            lfb[wy * stride + sx] = BORDER_COLOR;
        /* Bottom border */
        int32_t by = wy + (int32_t)th - 1;
        if (sx >= 0 && sx < (int32_t)scr_w && by >= 0 && by < (int32_t)scr_h)
            lfb[by * stride + sx] = BORDER_COLOR;
    }
    for (uint32_t j = 0; j < th; j++) {
        int32_t sy = wy + (int32_t)j;
        /* Left border */
        if (wx >= 0 && wx < (int32_t)scr_w && sy >= 0 && sy < (int32_t)scr_h)
            lfb[sy * stride + wx] = BORDER_COLOR;
        /* Right border */
        int32_t rx = wx + (int32_t)tw - 1;
        if (rx >= 0 && rx < (int32_t)scr_w && sy >= 0 && sy < (int32_t)scr_h)
            lfb[sy * stride + rx] = BORDER_COLOR;
    }

    /* Draw title bar */
    int32_t tx = wx + WM_BORDER_W;
    int32_t ty = wy + WM_BORDER_W;
    for (int32_t j = 0; j < WM_TITLEBAR_H - 1; j++) {
        int32_t sy = ty + j;
        if (sy < 0 || sy >= (int32_t)scr_h) continue;
        for (uint32_t i = 0; i < w->cw; i++) {
            int32_t sx = tx + (int32_t)i;
            if (sx >= 0 && sx < (int32_t)scr_w)
                lfb[sy * stride + sx] = title_bg;
        }
    }
    /* Title bar bottom separator */
    {
        int32_t sy = ty + WM_TITLEBAR_H - 1;
        if (sy >= 0 && sy < (int32_t)scr_h) {
            for (uint32_t i = 0; i < w->cw; i++) {
                int32_t sx = tx + (int32_t)i;
                if (sx >= 0 && sx < (int32_t)scr_w)
                    lfb[sy * stride + sx] = BORDER_COLOR;
            }
        }
    }

    /* Title text */
    int32_t text_x = tx + 6;
    int32_t text_y = ty + 3;
    for (int i = 0; w->title[i] && text_x + 8 <= tx + (int32_t)w->cw - WM_CLOSE_W; i++) {
        if (text_x >= 0 && text_x + 8 <= (int32_t)scr_w &&
            text_y >= 0 && text_y + 16 <= (int32_t)scr_h) {
            /* Draw directly to LFB */
            char c = w->title[i];
            if (c < 32 || c > 126) c = '?';
            const uint8_t *glyph = fb_font8x16[c - 32];
            for (int row = 0; row < 16; row++) {
                uint8_t bits = glyph[row];
                for (int col = 0; col < 8; col++) {
                    int32_t px = text_x + col, py = text_y + row;
                    if (px >= 0 && px < (int32_t)scr_w && py >= 0 && py < (int32_t)scr_h)
                        lfb[py * stride + px] = (bits & (0x80 >> col)) ? TITLE_FG : title_bg;
                }
            }
        }
        text_x += 8;
    }

    /* Close button [X] */
    int32_t close_x = tx + (int32_t)w->cw - WM_CLOSE_W;
    int32_t close_y = ty + 2;
    for (int32_t j = 0; j < WM_TITLEBAR_H - 4; j++) {
        int32_t sy = close_y + j;
        if (sy < 0 || sy >= (int32_t)scr_h) continue;
        for (int32_t i = 0; i < WM_CLOSE_W; i++) {
            int32_t sx = close_x + i;
            if (sx >= 0 && sx < (int32_t)scr_w)
                lfb[sy * stride + sx] = CLOSE_BG;
        }
    }
    /* Draw X in close button */
    {
        int32_t cx_mid = close_x + WM_CLOSE_W / 2;
        int32_t cy_mid = close_y + (WM_TITLEBAR_H - 4) / 2;
        if (cx_mid >= 4 && cy_mid >= 4) {
            const uint8_t *glyph = fb_font8x16['X' - 32];
            int32_t gx = cx_mid - 4, gy = cy_mid - 8;
            for (int row = 0; row < 16; row++) {
                uint8_t bits = glyph[row];
                for (int col = 0; col < 8; col++) {
                    if (bits & (0x80 >> col)) {
                        int32_t px = gx + col, py = gy + row;
                        if (px >= 0 && px < (int32_t)scr_w && py >= 0 && py < (int32_t)scr_h)
                            lfb[py * stride + px] = FB_WHITE;
                    }
                }
            }
        }
    }

    /* Draw client area */
    int32_t cx = tx;
    int32_t cy = ty + WM_TITLEBAR_H;
    for (uint32_t j = 0; j < w->ch; j++) {
        int32_t sy = cy + (int32_t)j;
        if (sy < 0 || sy >= (int32_t)scr_h) continue;
        for (uint32_t i = 0; i < w->cw; i++) {
            int32_t sx = cx + (int32_t)i;
            if (sx >= 0 && sx < (int32_t)scr_w)
                lfb[sy * stride + sx] = w->pixels[j * w->cw + i];
        }
    }
}

void wm_composite(volatile uint32_t *lfb, uint32_t stride,
                    const uint32_t *bg_buf) {
    /* Draw background */
    for (uint32_t y = 0; y < scr_h; y++)
        for (uint32_t x = 0; x < scr_w; x++)
            lfb[y * stride + x] = bg_buf[y * scr_w + x];

    /* Draw windows back-to-front */
    for (int i = 0; i < z_count; i++) {
        bool is_top = (i == z_count - 1);
        composite_window(lfb, stride, z_order[i], is_top);
    }
}

/* ---- Hit testing ---- */

int wm_window_at(int32_t mx, int32_t my) {
    /* Check front-to-back (topmost first) */
    for (int i = z_count - 1; i >= 0; i--) {
        int id = z_order[i];
        if (id < 0 || id >= WM_MAX_WINDOWS) continue;
        struct wm_window *w = &windows[id];
        if (!w->used || !w->visible) continue;
        uint32_t tw = wm_total_w(id);
        uint32_t th = wm_total_h(id);
        if (mx >= w->x && mx < w->x + (int32_t)tw &&
            my >= w->y && my < w->y + (int32_t)th)
            return id;
    }
    return -1;
}

int wm_hit_test(int id, int32_t mx, int32_t my) {
    if (id < 0 || id >= WM_MAX_WINDOWS || !windows[id].used)
        return WM_HIT_NONE;

    struct wm_window *w = &windows[id];
    int32_t rel_x = mx - w->x - WM_BORDER_W;
    int32_t rel_y = my - w->y - WM_BORDER_W;

    /* Title bar area */
    if (rel_y >= 0 && rel_y < WM_TITLEBAR_H) {
        /* Close button */
        if (rel_x >= (int32_t)w->cw - WM_CLOSE_W && rel_x < (int32_t)w->cw)
            return WM_HIT_CLOSE;
        return WM_HIT_TITLEBAR;
    }

    /* Client area */
    if (rel_x >= 0 && rel_x < (int32_t)w->cw &&
        rel_y >= WM_TITLEBAR_H && rel_y < WM_TITLEBAR_H + (int32_t)w->ch)
        return WM_HIT_CLIENT;

    return WM_HIT_NONE;
}
