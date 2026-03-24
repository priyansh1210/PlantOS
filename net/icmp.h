#ifndef NET_ICMP_H
#define NET_ICMP_H

#include <plantos/types.h>
#include "net/netbuf.h"

#define ICMP_ECHO_REPLY   0
#define ICMP_ECHO_REQUEST 8

struct icmp_header {
    uint8_t  type;
    uint8_t  code;
    uint16_t checksum;
    uint16_t id;
    uint16_t sequence;
} __attribute__((packed));

void icmp_rx(struct netbuf *nb, uint32_t src_ip);

/* Send a ping. Returns 0 on success (reply received), -1 on timeout. */
int icmp_ping(uint32_t target_ip, uint16_t seq, uint32_t timeout_ms);

#endif
