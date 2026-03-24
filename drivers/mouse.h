#ifndef DRIVERS_MOUSE_H
#define DRIVERS_MOUSE_H

#include <plantos/types.h>

struct mouse_state {
    int32_t  x, y;          /* Current position */
    bool     left, right, middle;
};

typedef void (*mouse_handler_t)(const struct mouse_state *state);

void mouse_init(void);
void mouse_set_handler(mouse_handler_t handler);
void mouse_get_state(struct mouse_state *out);
void mouse_set_bounds(int32_t max_x, int32_t max_y);
void mouse_poll(void);  /* Poll for mouse data (fallback if IRQ12 doesn't fire) */

#endif
