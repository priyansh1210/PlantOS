#ifndef GUI_TERMINAL_H
#define GUI_TERMINAL_H

#include <plantos/types.h>

#define TERM_COLS  36
#define TERM_ROWS  12
#define TERM_CW    (TERM_COLS * 8)    /* 288 px */
#define TERM_CH    (TERM_ROWS * 16)   /* 192 px */
#define TERM_INPUT_MAX 80

/* Initialize terminal state (does NOT create the window) */
void term_init(void);

/* Create/destroy the terminal window via the window manager */
int  term_open(void);
void term_close(void);

/* Feed a character of keyboard input */
void term_key_input(char c);

/* Redraw the terminal contents into its window pixel buffer */
void term_render(void);

/* Returns the window manager ID of the terminal, or -1 */
int  term_get_wm_id(void);

#endif
