#include "net/ethernet.h"
#include "net/net.h"
#include "net/arp.h"
#include "net/ipv4.h"
#include "drivers/e1000.h"
#include "lib/string.h"
#include "lib/printf.h"

const uint8_t ETH_BROADCAST[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

void eth_rx(struct netbuf *nb) {
    if (nb->len < ETH_HLEN) {
        netbuf_free(nb);
        return;
    }

    struct eth_header *hdr = (struct eth_header *)nb->data;
    uint16_t type = ntohs(hdr->ethertype);

    /* Learn source MAC from all incoming IP frames (helps server-side TCP) */
    if (type == ETH_TYPE_IP && nb->len >= ETH_HLEN + 20) {
        struct ipv4_header *ip = (struct ipv4_header *)(nb->data + ETH_HLEN);
        uint32_t src_ip = ntohl(ip->src_ip);
        if (src_ip != 0)
            arp_learn(src_ip, hdr->src);
    }

    /* Strip ethernet header */
    netbuf_pull(nb, ETH_HLEN);

    switch (type) {
    case ETH_TYPE_ARP:
        arp_rx(nb);
        break;
    case ETH_TYPE_IP:
        ipv4_rx(nb);
        break;
    default:
        netbuf_free(nb);
        break;
    }
}

int eth_tx(struct netbuf *nb, const uint8_t *dst_mac, uint16_t ethertype) {
    struct eth_header *hdr = (struct eth_header *)netbuf_push(nb, ETH_HLEN);
    if (!hdr) {
        netbuf_free(nb);
        return -1;
    }

    memcpy(hdr->dst, dst_mac, ETH_ALEN);
    memcpy(hdr->src, net_cfg.mac, ETH_ALEN);
    hdr->ethertype = htons(ethertype);

    int ret = e1000_send(nb->data, nb->len);
    netbuf_free(nb);
    return ret;
}
