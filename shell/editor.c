#include "shell/editor.h"
#include "drivers/vga.h"
#include "drivers/keyboard.h"
#include "cpu/ports.h"
#include "lib/printf.h"
#include "lib/string.h"
#include "fs/vfs.h"
#include "kernel/env.h"
#include "lib/util.h"

/* Editor layout:
 * Row 0:       title bar
 * Rows 1-22:   text area (22 lines visible)
 * Row 23:      status bar
 * Row 24:      help bar
 */

#define ED_TEXT_ROWS   22
#define ED_TEXT_COLS   80
#define ED_MAX_LINES   512
#define ED_LINE_MAX    256

/* Text buffer: array of line strings */
static char lines[ED_MAX_LINES][ED_LINE_MAX];
static int  num_lines = 0;

/* Cursor position in the document */
static int cx = 0;  /* column */
static int cy = 0;  /* line (row in document) */

/* Scroll offset */
static int scroll_y = 0;

/* File path */
static char filepath[256];
static bool modified = false;

/* Editor running flag */
static volatile bool ed_running = false;

/* Saved keyboard handler */
static key_handler_t saved_handler = NULL;

/* ---- Line helpers ---- */

static int line_len(int line) {
    if (line < 0 || line >= num_lines) return 0;
    return strlen(lines[line]);
}

static void insert_line(int at) {
    if (num_lines >= ED_MAX_LINES) return;
    /* Shift lines down */
    for (int i = num_lines; i > at; i--)
        memcpy(lines[i], lines[i - 1], ED_LINE_MAX);
    lines[at][0] = '\0';
    num_lines++;
}

static void delete_line(int at) {
    if (at < 0 || at >= num_lines) return;
    for (int i = at; i < num_lines - 1; i++)
        memcpy(lines[i], lines[i + 1], ED_LINE_MAX);
    num_lines--;
    if (num_lines == 0) {
        lines[0][0] = '\0';
        num_lines = 1;
    }
}

static void insert_char_at(int line, int col, char c) {
    int len = line_len(line);
    if (len >= ED_LINE_MAX - 1) return;
    /* Shift right */
    for (int i = len + 1; i > col; i--)
        lines[line][i] = lines[line][i - 1];
    lines[line][col] = c;
}

static void delete_char_at(int line, int col) {
    int len = line_len(line);
    if (col >= len) return;
    for (int i = col; i < len; i++)
        lines[line][i] = lines[line][i + 1];
}

/* ---- File I/O ---- */

static void load_file(const char *path) {
    num_lines = 0;
    lines[0][0] = '\0';

    int fd = vfs_open(path, 0);
    if (fd < 0) {
        /* New file */
        num_lines = 1;
        return;
    }

    char buf[512];
    int cur_line = 0;
    int cur_col = 0;
    lines[0][0] = '\0';
    num_lines = 1;

    int n;
    while ((n = vfs_read(fd, buf, sizeof(buf))) > 0) {
        for (int i = 0; i < n; i++) {
            if (buf[i] == '\n') {
                lines[cur_line][cur_col] = '\0';
                cur_line++;
                cur_col = 0;
                if (cur_line >= ED_MAX_LINES) goto done;
                lines[cur_line][0] = '\0';
                num_lines = cur_line + 1;
            } else if (buf[i] != '\r') {
                if (cur_col < ED_LINE_MAX - 1) {
                    lines[cur_line][cur_col++] = buf[i];
                    lines[cur_line][cur_col] = '\0';
                }
            }
        }
    }
done:
    vfs_close(fd);
    if (num_lines == 0) {
        num_lines = 1;
        lines[0][0] = '\0';
    }
}

static int save_file(const char *path) {
    int fd = vfs_open(path, VFS_O_CREATE);
    if (fd < 0) return -1;

    /* VFS write appends to offset, but we want to overwrite.
     * Since we opened with CREATE on ramfs, offset starts at 0.
     * For a proper solution we'd need truncate, but this works for ramfs. */
    for (int i = 0; i < num_lines; i++) {
        int len = line_len(i);
        if (len > 0)
            vfs_write(fd, lines[i], len);
        if (i < num_lines - 1)
            vfs_write(fd, "\n", 1);
    }
    vfs_close(fd);
    return 0;
}

/* ---- Rendering ---- */

static void draw_title_bar(void) {
    vga_set_color(VGA_BLACK, VGA_WHITE);
    vga_set_cursor(0, 0);

    char title[81];
    int pos = 0;
    const char *prefix = " EDIT: ";
    while (*prefix && pos < 70) title[pos++] = *prefix++;
    const char *fp = filepath;
    while (*fp && pos < 70) title[pos++] = *fp++;
    if (modified) {
        const char *mod = " [modified]";
        while (*mod && pos < 78) title[pos++] = *mod++;
    }
    while (pos < 80) title[pos++] = ' ';
    title[80] = '\0';

    for (int i = 0; i < 80; i++)
        vga_put_at(0, i, title[i], VGA_BLACK, VGA_WHITE);
}

static void draw_status_bar(void) {
    char status[81];
    int pos = 0;
    const char *s = " Ln ";
    while (*s) status[pos++] = *s++;

    /* Line number */
    char num[12];
    utoa(cy + 1, num, 10);
    for (int i = 0; num[i]; i++) status[pos++] = num[i];

    s = ", Col ";
    while (*s) status[pos++] = *s++;
    utoa(cx + 1, num, 10);
    for (int i = 0; num[i]; i++) status[pos++] = num[i];

    s = "  | ";
    while (*s) status[pos++] = *s++;
    utoa(num_lines, num, 10);
    for (int i = 0; num[i]; i++) status[pos++] = num[i];
    s = " lines";
    while (*s) status[pos++] = *s++;

    while (pos < 80) status[pos++] = ' ';
    status[80] = '\0';

    for (int i = 0; i < 80; i++)
        vga_put_at(23, i, status[i], VGA_BLACK, VGA_LIGHT_GREY);
}

static void draw_help_bar(void) {
    const char *help = " ^X Exit  ^S Save  ^G Goto  Arrow keys to navigate";
    int i = 0;
    while (help[i] && i < 80) {
        /* Highlight shortcuts */
        uint8_t fg = VGA_BLACK;
        if (help[i] == '^' && help[i+1]) {
            vga_put_at(24, i, help[i], VGA_WHITE, VGA_DARK_GREY);
            i++;
            vga_put_at(24, i, help[i], VGA_WHITE, VGA_DARK_GREY);
            i++;
            continue;
        }
        vga_put_at(24, i, help[i], fg, VGA_DARK_GREY);
        i++;
    }
    while (i < 80) {
        vga_put_at(24, i, ' ', VGA_BLACK, VGA_DARK_GREY);
        i++;
    }
}

static void draw_text_area(void) {
    for (int row = 0; row < ED_TEXT_ROWS; row++) {
        int doc_line = scroll_y + row;
        int scr_row = row + 1; /* offset by title bar */

        if (doc_line < num_lines) {
            int len = line_len(doc_line);
            int col = 0;
            /* Draw line content */
            for (; col < len && col < ED_TEXT_COLS; col++) {
                vga_put_at(scr_row, col, lines[doc_line][col],
                           VGA_LIGHT_GREY, VGA_BLACK);
            }
            /* Clear rest of line */
            for (; col < ED_TEXT_COLS; col++) {
                vga_put_at(scr_row, col, ' ', VGA_LIGHT_GREY, VGA_BLACK);
            }
        } else {
            /* Empty line — show tilde */
            vga_put_at(scr_row, 0, '~', VGA_DARK_GREY, VGA_BLACK);
            for (int col = 1; col < ED_TEXT_COLS; col++)
                vga_put_at(scr_row, col, ' ', VGA_DARK_GREY, VGA_BLACK);
        }
    }
}

static void editor_refresh(void) {
    /* Adjust scroll to keep cursor visible */
    if (cy < scroll_y)
        scroll_y = cy;
    if (cy >= scroll_y + ED_TEXT_ROWS)
        scroll_y = cy - ED_TEXT_ROWS + 1;

    /* Clamp cx to line length */
    int len = line_len(cy);
    if (cx > len) cx = len;

    draw_title_bar();
    draw_text_area();
    draw_status_bar();
    draw_help_bar();

    /* Position hardware cursor */
    int scr_row = cy - scroll_y + 1;
    int scr_col = cx;
    if (scr_col >= ED_TEXT_COLS) scr_col = ED_TEXT_COLS - 1;
    vga_set_cursor(scr_row, scr_col);
}

/* ---- Key handling ---- */

static void editor_key_handler(char c) {
    unsigned char uc = (unsigned char)c;

    /* Ctrl+X = exit */
    if (c == 0x18) { /* Ctrl+X */
        ed_running = false;
        return;
    }

    /* Ctrl+S = save */
    if (c == 0x13) { /* Ctrl+S */
        char resolved[256];
        path_resolve(filepath, resolved, sizeof(resolved));
        if (save_file(resolved) == 0) {
            modified = false;
            /* Flash status */
            for (int i = 0; i < 80; i++)
                vga_put_at(23, i, ' ', VGA_BLACK, VGA_LIGHT_GREEN);
            const char *msg = " Saved!";
            for (int i = 0; msg[i]; i++)
                vga_put_at(23, i, msg[i], VGA_BLACK, VGA_LIGHT_GREEN);
            /* Brief delay then refresh */
            for (volatile int i = 0; i < 5000000; i++);
        } else {
            for (int i = 0; i < 80; i++)
                vga_put_at(23, i, ' ', VGA_WHITE, VGA_RED);
            const char *msg = " Save failed!";
            for (int i = 0; msg[i]; i++)
                vga_put_at(23, i, msg[i], VGA_WHITE, VGA_RED);
            for (volatile int i = 0; i < 5000000; i++);
        }
        editor_refresh();
        return;
    }

    /* Arrow keys */
    if (uc == KEY_UP) {
        if (cy > 0) cy--;
        editor_refresh();
        return;
    }
    if (uc == KEY_DOWN) {
        if (cy < num_lines - 1) cy++;
        editor_refresh();
        return;
    }
    if (uc == KEY_LEFT) {
        if (cx > 0) {
            cx--;
        } else if (cy > 0) {
            cy--;
            cx = line_len(cy);
        }
        editor_refresh();
        return;
    }
    if (uc == KEY_RIGHT) {
        if (cx < line_len(cy)) {
            cx++;
        } else if (cy < num_lines - 1) {
            cy++;
            cx = 0;
        }
        editor_refresh();
        return;
    }
    if (uc == KEY_HOME) {
        cx = 0;
        editor_refresh();
        return;
    }
    if (uc == KEY_END) {
        cx = line_len(cy);
        editor_refresh();
        return;
    }
    if (uc == KEY_PGUP) {
        cy -= ED_TEXT_ROWS;
        if (cy < 0) cy = 0;
        editor_refresh();
        return;
    }
    if (uc == KEY_PGDN) {
        cy += ED_TEXT_ROWS;
        if (cy >= num_lines) cy = num_lines - 1;
        editor_refresh();
        return;
    }
    if (uc == KEY_DELETE) {
        int len = line_len(cy);
        if (cx < len) {
            delete_char_at(cy, cx);
            modified = true;
        } else if (cy < num_lines - 1) {
            /* Join with next line */
            int next_len = line_len(cy + 1);
            if (len + next_len < ED_LINE_MAX - 1) {
                strcat(lines[cy], lines[cy + 1]);
                delete_line(cy + 1);
                modified = true;
            }
        }
        editor_refresh();
        return;
    }

    /* Ignore other special keys */
    if (uc >= 0x80) return;

    /* Enter — split line */
    if (c == '\n') {
        if (num_lines >= ED_MAX_LINES) return;
        int len = line_len(cy);
        /* Insert new line after current */
        insert_line(cy + 1);
        /* Move text after cursor to new line */
        if (cx < len) {
            strcpy(lines[cy + 1], &lines[cy][cx]);
            lines[cy][cx] = '\0';
        }
        cy++;
        cx = 0;
        modified = true;
        editor_refresh();
        return;
    }

    /* Backspace */
    if (c == '\b') {
        if (cx > 0) {
            delete_char_at(cy, cx - 1);
            cx--;
            modified = true;
        } else if (cy > 0) {
            /* Join with previous line */
            int prev_len = line_len(cy - 1);
            int cur_len = line_len(cy);
            if (prev_len + cur_len < ED_LINE_MAX - 1) {
                strcat(lines[cy - 1], lines[cy]);
                delete_line(cy);
                cy--;
                cx = prev_len;
                modified = true;
            }
        }
        editor_refresh();
        return;
    }

    /* Tab — insert spaces */
    if (c == '\t') {
        int spaces = 4 - (cx % 4);
        for (int i = 0; i < spaces; i++) {
            if (line_len(cy) < ED_LINE_MAX - 1) {
                insert_char_at(cy, cx, ' ');
                cx++;
            }
        }
        modified = true;
        editor_refresh();
        return;
    }

    /* Regular printable character */
    if (c >= 32 && c <= 126) {
        insert_char_at(cy, cx, c);
        cx++;
        modified = true;
        editor_refresh();
    }
}

/* ---- Public API ---- */

void editor_open(const char *path) {
    /* Resolve path */
    path_resolve(path, filepath, sizeof(filepath));

    /* Load file */
    load_file(filepath);
    cx = 0;
    cy = 0;
    scroll_y = 0;
    modified = false;

    /* Save and replace keyboard handler */
    /* We need to take over the keyboard — but the shell uses IRQ-driven input.
     * We'll replace the handler and use the editor's own handler. */
    ed_running = true;

    /* Save current VGA state */
    int saved_row, saved_col;
    vga_get_cursor(&saved_row, &saved_col);

    /* Save current handler and set editor keyboard handler */
    saved_handler = keyboard_get_handler();
    keyboard_set_handler(editor_key_handler);

    /* Initial draw */
    editor_refresh();

    /* Editor main loop — wait for keys via IRQ */
    while (ed_running) {
        __asm__ volatile ("hlt");
    }

    /* Restore shell */
    vga_clear();
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    kprintf("Editor closed.\n");

    /* Restore previous keyboard handler */
    keyboard_set_handler(saved_handler);
    saved_handler = NULL;
}
