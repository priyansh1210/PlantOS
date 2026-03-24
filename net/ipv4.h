#ifndef NET_IPV4_H
#define NET_IPV4_H

#include <plantos/types.h>
#include "net/netbuf.h"

#define IP_PROTO_ICMP  1
#define IP_PROTO_TCP   6
#define IP_PROTO_UDP   17

struct ipv4_header {
    uint8_t  ver_ihl;
    uint8_t  tos;
    uint16_t total_len;
    uint16_t id;
    uint16_t flags_frag;
    uint8_t  ttl;
    uint8_t  protocol;
    uint16_t checksum;
    uint32_t src_ip;
    uint32_t dst_ip;
} __attribute__((packed));

/* Process a received IP packet */
void ipv4_rx(struct netbuf *nb);

/* Send an IP packet: prepends IP header, resolves next-hop MAC via ARP */
int ipv4_tx(struct netbuf *nb, uint32_t dst_ip, uint8_t protocol);

#endif
