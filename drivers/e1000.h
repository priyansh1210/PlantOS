#ifndef DRIVERS_E1000_H
#define DRIVERS_E1000_H

#include <plantos/types.h>

void     e1000_init(void);
int      e1000_send(const void *data, uint16_t len);
void     e1000_get_mac(uint8_t mac[6]);
bool     e1000_is_link_up(void);

#endif
