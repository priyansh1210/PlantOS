#ifndef DRIVERS_FB_H
#define DRIVERS_FB_H

#include <plantos/types.h>

/* Framebuffer info (passed to user space) */
struct fb_info {
    uint32_t width;
    uint32_t height;
    uint32_t pitch;      /* Bytes per scanline */
    uint32_t bpp;        /* Bits per pixel (32) */
    uint64_t framebuffer; /* Physical/virtual address of LFB */
};

/* Color: 0xAARRGGBB (alpha ignored) */
#define FB_RGB(r, g, b) ((uint32_t)(((r) << 16) | ((g) << 8) | (b)))

#define FB_BLACK    FB_RGB(0, 0, 0)
#define FB_WHITE    FB_RGB(255, 255, 255)
#define FB_RED      FB_RGB(255, 0, 0)
#define FB_GREEN    FB_RGB(0, 255, 0)
#define FB_BLUE     FB_RGB(0, 0, 255)
#define FB_YELLOW   FB_RGB(255, 255, 0)
#define FB_CYAN     FB_RGB(0, 255, 255)
#define FB_MAGENTA  FB_RGB(255, 0, 255)
#define FB_GREY     FB_RGB(128, 128, 128)
#define FB_ORANGE   FB_RGB(255, 165, 0)

void fb_init(void);
bool fb_is_available(void);

/* Switch to graphics mode (width x height, 32bpp). Returns 0 on success. */
int fb_set_mode(uint32_t width, uint32_t height);

/* Switch back to VGA text mode */
void fb_text_mode(void);

/* Get framebuffer info */
void fb_get_info(struct fb_info *info);

/* Drawing primitives (kernel-side) */
void fb_putpixel(uint32_t x, uint32_t y, uint32_t color);
void fb_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
void fb_clear(uint32_t color);
void fb_draw_char(uint32_t x, uint32_t y, char c, uint32_t fg, uint32_t bg);
void fb_draw_string(uint32_t x, uint32_t y, const char *s, uint32_t fg, uint32_t bg);

/* Access the built-in 8x16 bitmap font (95 glyphs, ASCII 32-126) */
extern const uint8_t fb_font8x16[95][16];

#endif
