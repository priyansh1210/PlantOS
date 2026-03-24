#ifndef NET_ARP_H
#define NET_ARP_H

#include <plantos/types.h>
#include "net/netbuf.h"

struct arp_header {
    uint16_t hw_type;       /* 1 = Ethernet */
    uint16_t proto_type;    /* 0x0800 = IPv4 */
    uint8_t  hw_len;        /* 6 */
    uint8_t  proto_len;     /* 4 */
    uint16_t opcode;        /* 1 = request, 2 = reply */
    uint8_t  sender_mac[6];
    uint32_t sender_ip;
    uint8_t  target_mac[6];
    uint32_t target_ip;
} __attribute__((packed));

#define ARP_OP_REQUEST 1
#define ARP_OP_REPLY   2

void arp_init(void);
void arp_rx(struct netbuf *nb);
int  arp_resolve(uint32_t ip, uint8_t mac_out[6]);  /* 0=found, -1=miss */
void arp_request(uint32_t target_ip);
void arp_learn(uint32_t ip, const uint8_t mac[6]);   /* Add/update cache entry */

#endif
