#include "net/arp.h"
#include "net/ethernet.h"
#include "net/net.h"
#include "lib/string.h"
#include "lib/printf.h"

#define ARP_CACHE_SIZE 16

struct arp_entry {
    uint32_t ip;
    uint8_t  mac[6];
    bool     valid;
};

static struct arp_entry arp_cache[ARP_CACHE_SIZE];

void arp_init(void) {
    memset(arp_cache, 0, sizeof(arp_cache));
}

static void arp_cache_put(uint32_t ip, const uint8_t *mac) {
    /* Update existing */
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (arp_cache[i].valid && arp_cache[i].ip == ip) {
            memcpy(arp_cache[i].mac, mac, 6);
            return;
        }
    }
    /* Find free slot */
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (!arp_cache[i].valid) {
            arp_cache[i].ip = ip;
            memcpy(arp_cache[i].mac, mac, 6);
            arp_cache[i].valid = true;
            return;
        }
    }
    /* Evict slot 0 */
    arp_cache[0].ip = ip;
    memcpy(arp_cache[0].mac, mac, 6);
    arp_cache[0].valid = true;
}

void arp_learn(uint32_t ip, const uint8_t mac[6]) {
    arp_cache_put(ip, mac);
}

int arp_resolve(uint32_t ip, uint8_t mac_out[6]) {
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (arp_cache[i].valid && arp_cache[i].ip == ip) {
            memcpy(mac_out, arp_cache[i].mac, 6);
            return 0;
        }
    }
    return -1;
}

void arp_rx(struct netbuf *nb) {
    if (nb->len < sizeof(struct arp_header)) {
        netbuf_free(nb);
        return;
    }

    struct arp_header *arp = (struct arp_header *)nb->data;
    uint16_t op = ntohs(arp->opcode);
    uint32_t sender_ip = ntohl(arp->sender_ip);
    uint32_t target_ip = ntohl(arp->target_ip);

    /* Cache the sender */
    arp_cache_put(sender_ip, arp->sender_mac);

    if (op == ARP_OP_REQUEST && target_ip == net_cfg.ip) {
        /* Send ARP reply */
        struct netbuf *reply = netbuf_alloc();
        if (!reply) { netbuf_free(nb); return; }

        struct arp_header *r = (struct arp_header *)netbuf_put(reply, sizeof(struct arp_header));
        r->hw_type    = htons(1);
        r->proto_type = htons(0x0800);
        r->hw_len     = 6;
        r->proto_len  = 4;
        r->opcode     = htons(ARP_OP_REPLY);
        memcpy(r->sender_mac, net_cfg.mac, 6);
        r->sender_ip  = htonl(net_cfg.ip);
        memcpy(r->target_mac, arp->sender_mac, 6);
        r->target_ip  = arp->sender_ip;

        eth_tx(reply, arp->sender_mac, ETH_TYPE_ARP);
    }

    netbuf_free(nb);
}

void arp_request(uint32_t target_ip) {
    struct netbuf *nb = netbuf_alloc();
    if (!nb) return;

    struct arp_header *arp = (struct arp_header *)netbuf_put(nb, sizeof(struct arp_header));
    arp->hw_type    = htons(1);
    arp->proto_type = htons(0x0800);
    arp->hw_len     = 6;
    arp->proto_len  = 4;
    arp->opcode     = htons(ARP_OP_REQUEST);
    memcpy(arp->sender_mac, net_cfg.mac, 6);
    arp->sender_ip  = htonl(net_cfg.ip);
    memset(arp->target_mac, 0, 6);
    arp->target_ip  = htonl(target_ip);

    eth_tx(nb, ETH_BROADCAST, ETH_TYPE_ARP);
}
