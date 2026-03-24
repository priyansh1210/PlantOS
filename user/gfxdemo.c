#include "user/usyscall.h"
#include "user/libc/ulibc.h"

/* Direct pixel write to the linear framebuffer */
static volatile uint32_t *fb;
static uint32_t fb_w, fb_h, fb_stride; /* stride in pixels (pitch/4) */

static void putpixel(uint32_t x, uint32_t y, uint32_t color) {
    if (x < fb_w && y < fb_h)
        fb[y * fb_stride + x] = color;
}

static void fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t col) {
    for (uint32_t j = y; j < y + h && j < fb_h; j++)
        for (uint32_t i = x; i < x + w && i < fb_w; i++)
            fb[j * fb_stride + i] = col;
}

static uint32_t rgb(uint8_t r, uint8_t g, uint8_t b) {
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

/* Simple busy-wait delay */
static void delay(int n) {
    for (volatile int i = 0; i < n * 100000; i++);
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;

    /* Switch to 640x480 graphics mode */
    if (ufb_setmode(640, 480) < 0) {
        printf("gfxdemo: failed to set graphics mode\n");
        return 1;
    }

    /* Get framebuffer info */
    struct ufb_info info;
    if (ufb_getinfo(&info) < 0) {
        printf("gfxdemo: failed to get fb info\n");
        return 1;
    }

    fb = (volatile uint32_t *)info.framebuffer;
    fb_w = info.width;
    fb_h = info.height;
    fb_stride = info.pitch / 4; /* pitch is in bytes, stride is in pixels */

    /* === Demo 1: Color bars === */
    uint32_t bar_w = fb_w / 8;
    uint32_t colors[] = {
        rgb(255,   0,   0),   /* Red */
        rgb(255, 165,   0),   /* Orange */
        rgb(255, 255,   0),   /* Yellow */
        rgb(  0, 255,   0),   /* Green */
        rgb(  0, 255, 255),   /* Cyan */
        rgb(  0,   0, 255),   /* Blue */
        rgb(128,   0, 128),   /* Purple */
        rgb(255, 255, 255),   /* White */
    };
    for (int i = 0; i < 8; i++) {
        fill_rect(i * bar_w, 0, bar_w, fb_h, colors[i]);
    }
    delay(30);

    /* === Demo 2: Gradient fill === */
    for (uint32_t y = 0; y < fb_h; y++) {
        for (uint32_t x = 0; x < fb_w; x++) {
            uint8_t r = (uint8_t)((x * 255) / fb_w);
            uint8_t g = (uint8_t)((y * 255) / fb_h);
            uint8_t b = (uint8_t)(128);
            putpixel(x, y, rgb(r, g, b));
        }
    }
    delay(30);

    /* === Demo 3: Concentric rectangles === */
    /* Clear to dark blue */
    fill_rect(0, 0, fb_w, fb_h, rgb(0, 0, 32));

    for (int i = 0; i < 12; i++) {
        uint32_t margin = i * 20;
        uint32_t x = margin;
        uint32_t y = margin;
        uint32_t w = fb_w - 2 * margin;
        uint32_t h = fb_h - 2 * margin;
        if (w < 4 || h < 4) break;

        uint32_t col = rgb(
            (uint8_t)(i * 20 + 40),
            (uint8_t)(255 - i * 20),
            (uint8_t)(i * 15 + 100)
        );

        /* Draw rectangle outline (top, bottom, left, right) */
        fill_rect(x, y, w, 2, col);
        fill_rect(x, y + h - 2, w, 2, col);
        fill_rect(x, y, 2, h, col);
        fill_rect(x + w - 2, y, 2, h, col);
    }

    /* Draw title text using kernel's fb_draw_string (via manual pixel text) */
    /* Since we don't have the font in user space, just draw a simple banner */
    fill_rect(200, 200, 240, 40, rgb(0, 100, 0));
    fill_rect(204, 204, 232, 32, rgb(0, 60, 0));

    /* Draw a simple "P" letter pixel art as PlantOS logo (16x16 scaled 2x) */
    static const uint8_t logo[] = {
        0x00,0xFC,0x66,0x66,0x66,0x7C,0x60,0x60,
        0x60,0x60,0x60,0xF0,0x00,0x00,0x00,0x00
    };
    for (int row = 0; row < 16; row++) {
        uint8_t bits = logo[row];
        for (int col = 0; col < 8; col++) {
            if (bits & (0x80 >> col)) {
                fill_rect(220 + col * 3, 208 + row * 2, 3, 2, rgb(0, 255, 100));
            }
        }
    }

    delay(40);

    /* === Demo 4: Bouncing pixel trail === */
    fill_rect(0, 0, fb_w, fb_h, rgb(0, 0, 0));

    int bx = 100, by = 100;
    int dx = 3, dy = 2;
    for (int frame = 0; frame < 2000; frame++) {
        /* Draw a 6x6 colored square at current position */
        uint32_t trail_col = rgb(
            (uint8_t)(frame & 0xFF),
            (uint8_t)((frame * 3) & 0xFF),
            (uint8_t)((frame * 7) & 0xFF)
        );
        fill_rect((uint32_t)bx, (uint32_t)by, 6, 6, trail_col);

        bx += dx;
        by += dy;
        if (bx <= 0 || bx >= (int)fb_w - 6) dx = -dx;
        if (by <= 0 || by >= (int)fb_h - 6) dy = -dy;
    }

    delay(30);

    /* Switch back to text mode */
    ufb_textmode();

    printf("gfxdemo: done!\n");
    return 0;
}
