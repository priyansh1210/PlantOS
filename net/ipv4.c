#include "net/ipv4.h"
#include "net/net.h"
#include "net/ethernet.h"
#include "net/arp.h"
#include "net/icmp.h"
#include "net/udp.h"
#include "net/tcp.h"
#include "lib/string.h"
#include "lib/printf.h"

static uint16_t ip_id_counter = 1;

void ipv4_rx(struct netbuf *nb) {
    if (nb->len < sizeof(struct ipv4_header)) {
        netbuf_free(nb);
        return;
    }

    struct ipv4_header *ip = (struct ipv4_header *)nb->data;

    /* Basic validation */
    uint8_t version = (ip->ver_ihl >> 4) & 0xF;
    uint8_t ihl = ip->ver_ihl & 0xF;
    if (version != 4 || ihl < 5) {
        netbuf_free(nb);
        return;
    }

    uint16_t hdr_len = ihl * 4;
    uint32_t src_ip = ntohl(ip->src_ip);
    uint32_t dst_ip = ntohl(ip->dst_ip);
    uint8_t proto = ip->protocol;

    /* Strip IP header */
    netbuf_pull(nb, hdr_len);

    /* Trim to IP total_len (remove ethernet padding) */
    uint16_t total_len = ntohs(ip->total_len);
    if (total_len > hdr_len)
        nb->len = total_len - hdr_len;

    switch (proto) {
    case IP_PROTO_ICMP:
        icmp_rx(nb, src_ip);
        break;
    case IP_PROTO_TCP:
        tcp_rx(nb, src_ip, dst_ip);
        break;
    case IP_PROTO_UDP:
        udp_rx(nb, src_ip);
        break;
    default:
        netbuf_free(nb);
        break;
    }
}

int ipv4_tx(struct netbuf *nb, uint32_t dst_ip, uint8_t protocol) {
    uint16_t payload_len = nb->len;

    /* Prepend IP header */
    struct ipv4_header *ip = (struct ipv4_header *)netbuf_push(nb, sizeof(struct ipv4_header));
    if (!ip) {
        netbuf_free(nb);
        return -1;
    }

    ip->ver_ihl    = 0x45;  /* IPv4, 5 32-bit words */
    ip->tos        = 0;
    ip->total_len  = htons(sizeof(struct ipv4_header) + payload_len);
    ip->id         = htons(ip_id_counter++);
    ip->flags_frag = 0;
    ip->ttl        = 64;
    ip->protocol   = protocol;
    ip->checksum   = 0;
    ip->src_ip     = htonl(net_cfg.ip);
    ip->dst_ip     = htonl(dst_ip);

    /* Compute header checksum */
    ip->checksum   = htons(net_checksum(ip, sizeof(struct ipv4_header)));

    /* Determine next-hop: on-link or gateway */
    uint32_t next_hop = dst_ip;
    if ((dst_ip & net_cfg.netmask) != (net_cfg.ip & net_cfg.netmask))
        next_hop = net_cfg.gateway;

    /* ARP resolve */
    uint8_t dst_mac[6];
    if (arp_resolve(next_hop, dst_mac) < 0) {
        /* Send ARP request and drop this packet (caller can retry) */
        arp_request(next_hop);
        netbuf_free(nb);
        return -1;
    }

    return eth_tx(nb, dst_mac, ETH_TYPE_IP);
}
