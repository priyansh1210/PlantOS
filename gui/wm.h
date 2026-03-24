#ifndef GUI_WM_H
#define GUI_WM_H

#include <plantos/types.h>

#define WM_MAX_WINDOWS  8
#define WM_TITLEBAR_H   22
#define WM_BORDER_W     1
#define WM_CLOSE_W      18

/* Hit test results */
#define WM_HIT_NONE     0
#define WM_HIT_CLIENT   1
#define WM_HIT_TITLEBAR 2
#define WM_HIT_CLOSE    3

struct wm_window {
    bool     used;
    bool     visible;
    int32_t  x, y;          /* Top-left of entire window (including border/titlebar) */
    uint32_t cw, ch;        /* Client area dimensions */
    char     title[32];
    uint32_t *pixels;       /* Client area pixel buffer (cw * ch) */
};

/* Initialize the window manager */
void wm_init(uint32_t screen_w, uint32_t screen_h);

/* Create a window, returns id (0..WM_MAX_WINDOWS-1) or -1 on failure */
int  wm_create(const char *title, int32_t x, int32_t y, uint32_t cw, uint32_t ch);
void wm_destroy(int id);
void wm_destroy_all(void);

/* Z-order */
void wm_bring_to_front(int id);

/* Get window properties */
struct wm_window *wm_get(int id);

/* Total window dimensions including chrome */
uint32_t wm_total_w(int id);
uint32_t wm_total_h(int id);

/* Drawing into a window's client area */
void wm_fill_rect(int id, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
void wm_draw_string(int id, uint32_t x, uint32_t y, const char *s,
                     uint32_t fg, uint32_t bg);
void wm_putpixel(int id, int32_t x, int32_t y, uint32_t color);
void wm_draw_line(int id, int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t color);
void wm_draw_circle(int id, int32_t cx, int32_t cy, int32_t r, uint32_t color);
void wm_fill_circle(int id, int32_t cx, int32_t cy, int32_t r, uint32_t color);

/* Composite all visible windows onto a framebuffer.
 * bg_buf is the desktop background (screen_w * screen_h pixels). */
void wm_composite(volatile uint32_t *lfb, uint32_t stride,
                   const uint32_t *bg_buf);

/* Hit testing — returns topmost window id at (mx,my), or -1 */
int  wm_window_at(int32_t mx, int32_t my);

/* Hit test within a specific window — returns WM_HIT_* */
int  wm_hit_test(int id, int32_t mx, int32_t my);

/* Move a window */
void wm_move(int id, int32_t x, int32_t y);

#endif
