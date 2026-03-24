#ifndef NET_ETHERNET_H
#define NET_ETHERNET_H

#include <plantos/types.h>
#include "net/netbuf.h"

#define ETH_ALEN      6
#define ETH_HLEN      14
#define ETH_TYPE_ARP  0x0806
#define ETH_TYPE_IP   0x0800

struct eth_header {
    uint8_t  dst[ETH_ALEN];
    uint8_t  src[ETH_ALEN];
    uint16_t ethertype;     /* Big-endian */
} __attribute__((packed));

/* Process a received Ethernet frame */
void eth_rx(struct netbuf *nb);

/* Send a frame: prepends eth header, calls e1000_send */
int eth_tx(struct netbuf *nb, const uint8_t *dst_mac, uint16_t ethertype);

/* Broadcast MAC */
extern const uint8_t ETH_BROADCAST[6];

#endif
