#include "net/icmp.h"
#include "net/ipv4.h"
#include "net/net.h"
#include "lib/string.h"
#include "lib/printf.h"
#include "drivers/pit.h"

/* Pending ping state */
static volatile bool     ping_received = false;
static volatile uint16_t ping_expect_seq = 0;
static volatile uint32_t ping_rtt_start = 0;
static volatile uint32_t ping_rtt = 0;

void icmp_rx(struct netbuf *nb, uint32_t src_ip) {
    if (nb->len < sizeof(struct icmp_header)) {
        netbuf_free(nb);
        return;
    }

    struct icmp_header *icmp = (struct icmp_header *)nb->data;

    if (icmp->type == ICMP_ECHO_REQUEST) {
        /* Reply to ping */
        uint16_t total = nb->len;
        uint8_t *payload = nb->data;

        struct netbuf *reply = netbuf_alloc();
        if (!reply) { netbuf_free(nb); return; }

        uint8_t *data = (uint8_t *)netbuf_put(reply, total);
        memcpy(data, payload, total);

        /* Fix type to reply and recompute checksum */
        struct icmp_header *rh = (struct icmp_header *)data;
        rh->type = ICMP_ECHO_REPLY;
        rh->checksum = 0;
        rh->checksum = htons(net_checksum(data, total));

        ipv4_tx(reply, src_ip, IP_PROTO_ICMP);
    } else if (icmp->type == ICMP_ECHO_REPLY) {
        uint16_t seq = ntohs(icmp->sequence);
        if (seq == ping_expect_seq) {
            ping_rtt = pit_get_ticks() - ping_rtt_start;
            ping_received = true;
        }
    }

    netbuf_free(nb);
}

int icmp_ping(uint32_t target_ip, uint16_t seq, uint32_t timeout_ms) {
    ping_received = false;
    ping_expect_seq = seq;
    ping_rtt_start = pit_get_ticks();

    /* Build ICMP echo request */
    struct netbuf *nb = netbuf_alloc();
    if (!nb) return -1;

    /* 8 bytes header + 32 bytes payload */
    uint16_t total = sizeof(struct icmp_header) + 32;
    uint8_t *data = (uint8_t *)netbuf_put(nb, total);
    memset(data, 0, total);

    struct icmp_header *icmp = (struct icmp_header *)data;
    icmp->type = ICMP_ECHO_REQUEST;
    icmp->code = 0;
    icmp->id = htons(0x1234);
    icmp->sequence = htons(seq);

    /* Fill payload with pattern */
    for (int i = 0; i < 32; i++)
        data[sizeof(struct icmp_header) + i] = (uint8_t)i;

    icmp->checksum = 0;
    icmp->checksum = htons(net_checksum(data, total));

    if (ipv4_tx(nb, target_ip, IP_PROTO_ICMP) < 0)
        return -1;

    /* Wait for reply */
    uint32_t start = pit_get_ticks();
    uint32_t timeout_ticks = timeout_ms; /* PIT runs at ~1000 Hz */
    while (!ping_received) {
        if (pit_get_ticks() - start > timeout_ticks)
            return -1; /* Timeout */
        __asm__ volatile ("hlt");
    }

    return (int)ping_rtt;
}
