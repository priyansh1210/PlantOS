#include "gui/terminal.h"
#include "gui/wm.h"
#include "drivers/fb.h"
#include "drivers/keyboard.h"
#include "lib/printf.h"
#include "lib/string.h"
#include "shell/commands.h"

#define TERM_BG   FB_RGB(10, 10, 25)
#define TERM_FG   FB_RGB(200, 200, 200)
#define PROMPT_FG FB_RGB(0, 220, 100)
#define CURSOR_FG FB_RGB(180, 180, 180)

/* Text buffer — stores visible lines */
static char text_buf[TERM_ROWS][TERM_COLS + 1];
static int  cursor_col;   /* Current column in bottom line */
static int  cursor_row;   /* Current row (for output writing) */

/* Input line buffer */
static char input_buf[TERM_INPUT_MAX];
static int  input_len;

/* Prompt */
static const char *prompt_str = "$ ";
static const int   prompt_len = 2;

/* Window manager id */
static int wm_id = -1;

/* State */
static bool initialized = false;

/* Command history */
#define TERM_HIST_SIZE 16
static char term_history[TERM_HIST_SIZE][TERM_INPUT_MAX];
static int  term_hist_count = 0;
static int  term_hist_write = 0;
static int  term_hist_browse = -1;
static char term_saved_input[TERM_INPUT_MAX];

static void term_hist_add(const char *line) {
    if (strlen(line) == 0) return;
    int prev = (term_hist_write - 1 + TERM_HIST_SIZE) % TERM_HIST_SIZE;
    if (term_hist_count > 0 && strcmp(term_history[prev], line) == 0) return;
    strncpy(term_history[term_hist_write], line, TERM_INPUT_MAX - 1);
    term_history[term_hist_write][TERM_INPUT_MAX - 1] = '\0';
    term_hist_write = (term_hist_write + 1) % TERM_HIST_SIZE;
    if (term_hist_count < TERM_HIST_SIZE) term_hist_count++;
}

/* ---- Scrolling ---- */

static void scroll_up(void) {
    for (int r = 0; r < TERM_ROWS - 1; r++)
        memcpy(text_buf[r], text_buf[r + 1], TERM_COLS + 1);
    memset(text_buf[TERM_ROWS - 1], 0, TERM_COLS + 1);
    if (cursor_row > 0) cursor_row--;
}

/* ---- Output capture callback ---- */

static void term_capture_char(char c) {
    if (c == '\n') {
        cursor_col = 0;
        cursor_row++;
        if (cursor_row >= TERM_ROWS) {
            scroll_up();
            cursor_row = TERM_ROWS - 1;
        }
        return;
    }
    if (c == '\r')
        return;
    if (c == '\b') {
        if (cursor_col > 0) cursor_col--;
        return;
    }
    if (cursor_col >= TERM_COLS) {
        /* Wrap */
        cursor_col = 0;
        cursor_row++;
        if (cursor_row >= TERM_ROWS) {
            scroll_up();
            cursor_row = TERM_ROWS - 1;
        }
    }
    text_buf[cursor_row][cursor_col++] = c;
}

/* ---- Write a string into the terminal text buffer ---- */

static void term_puts(const char *s) {
    while (*s)
        term_capture_char(*s++);
}

/* ---- Show the prompt ---- */

static void term_show_prompt(void) {
    term_puts(prompt_str);
}

/* ---- Initialize ---- */

void term_init(void) {
    memset(text_buf, 0, sizeof(text_buf));
    cursor_col = 0;
    cursor_row = 0;
    input_len = 0;
    input_buf[0] = '\0';
    initialized = true;
}

/* ---- Open / close ---- */

int term_open(void) {
    if (wm_id >= 0) {
        /* Already open — bring to front */
        wm_bring_to_front(wm_id);
        return wm_id;
    }
    if (!initialized) term_init();

    wm_id = wm_create("Terminal", 40, 60, TERM_CW, TERM_CH);
    if (wm_id < 0) return -1;

    /* Welcome message + prompt */
    term_puts("PlantOS Terminal\n");
    term_show_prompt();
    term_render();

    return wm_id;
}

void term_close(void) {
    if (wm_id >= 0) {
        wm_destroy(wm_id);
        wm_id = -1;
    }
    initialized = false;
}

int term_get_wm_id(void) {
    return wm_id;
}

/* ---- Execute a command ---- */

static void term_execute(const char *line) {
    if (strlen(line) == 0) {
        term_show_prompt();
        return;
    }

    /* Reject commands that interfere with desktop mode */
    if (strcmp(line, "desktop") == 0 || strcmp(line, "gfxtest") == 0 ||
        strcmp(line, "mousetest") == 0) {
        term_puts("Not available in desktop\n");
        term_show_prompt();
        return;
    }

    /* Handle 'clear' specially */
    if (strcmp(line, "clear") == 0) {
        memset(text_buf, 0, sizeof(text_buf));
        cursor_col = 0;
        cursor_row = 0;
        term_show_prompt();
        return;
    }

    /* Parse command line into argc/argv */
    static char cmdbuf[TERM_INPUT_MAX];
    memcpy(cmdbuf, line, strlen(line) + 1);

    char *argv[16];
    int argc = 0;
    char *p = cmdbuf;

    while (*p && argc < 15) {
        /* Skip whitespace */
        while (*p == ' ') p++;
        if (!*p) break;
        argv[argc++] = p;
        while (*p && *p != ' ') p++;
        if (*p) *p++ = '\0';
    }
    argv[argc] = NULL;

    if (argc == 0) {
        term_show_prompt();
        return;
    }

    /* Redirect kprintf output to terminal */
    kprintf_set_capture(term_capture_char);
    commands_execute(argc, argv);
    kprintf_clear_capture();

    term_show_prompt();
}

/* ---- Line editing helpers ---- */

/* Erase the current input from the text buffer display */
static void term_erase_input(void) {
    while (input_len > 0) {
        input_len--;
        if (cursor_col > 0) {
            cursor_col--;
            text_buf[cursor_row][cursor_col] = ' ';
        }
    }
}

/* Set input to a new string and display it */
static void term_set_input(const char *s) {
    term_erase_input();
    int len = strlen(s);
    if (len >= TERM_INPUT_MAX) len = TERM_INPUT_MAX - 1;
    memcpy(input_buf, s, len);
    input_buf[len] = '\0';
    input_len = len;
    for (int i = 0; i < len; i++)
        term_capture_char(input_buf[i]);
    term_render();
}

/* ---- Keyboard input ---- */

void term_key_input(char c) {
    if (wm_id < 0 || !initialized) return;
    unsigned char uc = (unsigned char)c;

    if (c == '\n') {
        /* Echo newline */
        term_capture_char('\n');
        /* Execute */
        input_buf[input_len] = '\0';
        char line_copy[TERM_INPUT_MAX];
        memcpy(line_copy, input_buf, input_len + 1);
        /* Add to history */
        term_hist_add(line_copy);
        term_hist_browse = -1;
        input_len = 0;
        term_execute(line_copy);
        term_render();
        return;
    }

    if (c == '\b') {
        if (input_len > 0) {
            input_len--;
            /* Erase character from display */
            if (cursor_col > 0) {
                cursor_col--;
                text_buf[cursor_row][cursor_col] = ' ';
            }
            term_render();
        }
        return;
    }

    /* Arrow keys for history */
    if (uc == KEY_UP) {
        if (term_hist_count == 0) return;
        if (term_hist_browse == -1) {
            input_buf[input_len] = '\0';
            strcpy(term_saved_input, input_buf);
            term_hist_browse = (term_hist_write - 1 + TERM_HIST_SIZE) % TERM_HIST_SIZE;
        } else {
            int oldest = (term_hist_write - term_hist_count + TERM_HIST_SIZE) % TERM_HIST_SIZE;
            if (term_hist_browse == oldest) return;
            term_hist_browse = (term_hist_browse - 1 + TERM_HIST_SIZE) % TERM_HIST_SIZE;
        }
        term_set_input(term_history[term_hist_browse]);
        return;
    }

    if (uc == KEY_DOWN) {
        if (term_hist_browse == -1) return;
        int newest = (term_hist_write - 1 + TERM_HIST_SIZE) % TERM_HIST_SIZE;
        if (term_hist_browse == newest) {
            term_hist_browse = -1;
            term_set_input(term_saved_input);
        } else {
            term_hist_browse = (term_hist_browse + 1) % TERM_HIST_SIZE;
            term_set_input(term_history[term_hist_browse]);
        }
        return;
    }

    /* Ignore other special keys */
    if (uc >= 0x80) return;

    /* Filter non-printable characters */
    if (c < 32 || c > 126) return;

    /* Regular character */
    if (input_len < TERM_INPUT_MAX - 1) {
        input_buf[input_len++] = c;
        term_capture_char(c);
        term_render();
    }
}

/* ---- Rendering ---- */

void term_render(void) {
    if (wm_id < 0) return;

    /* Clear client area */
    wm_fill_rect(wm_id, 0, 0, TERM_CW, TERM_CH, TERM_BG);

    /* Draw each line of text */
    for (int row = 0; row < TERM_ROWS; row++) {
        uint32_t y = (uint32_t)(row * 16);
        for (int col = 0; col < TERM_COLS; col++) {
            char c = text_buf[row][col];
            if (c == '\0' || c == ' ') continue;
            uint32_t x = (uint32_t)(col * 8);

            /* Color the prompt green */
            uint32_t fg = TERM_FG;
            if (row == cursor_row && col < prompt_len && input_len == 0) {
                /* This is a heuristic — prompt chars at line start */
            }

            /* Simple approach: just use default FG for all text */
            wm_draw_string(wm_id, x, y, (char[]){c, '\0'}, fg, TERM_BG);
        }
    }

    /* Draw blinking cursor (always on for simplicity) */
    uint32_t cx = (uint32_t)(cursor_col * 8);
    uint32_t cy = (uint32_t)(cursor_row * 16);
    if (cx + 8 <= TERM_CW && cy + 16 <= TERM_CH)
        wm_fill_rect(wm_id, cx, cy + 14, 8, 2, CURSOR_FG);
}
