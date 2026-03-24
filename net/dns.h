#ifndef NET_DNS_H
#define NET_DNS_H

#include <plantos/types.h>

/* Resolve a hostname to an IPv4 address (host byte order).
 * Returns 0 on success, -1 on failure. */
int dns_resolve(const char *hostname, uint32_t *ip_out);

/* Initialize DNS (set server IP) */
void dns_init(void);

/* Process incoming DNS reply (called from udp_rx) */
void dns_process_reply(const uint8_t *data, uint16_t len);

#endif
