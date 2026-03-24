#include "net/udp.h"
#include "net/net.h"
#include "net/ipv4.h"
#include "net/dns.h"
#include "lib/string.h"
#include "lib/printf.h"
#include "drivers/pit.h"

/* Socket table */
static struct udp_socket sockets[UDP_MAX_SOCKETS];

/* Ephemeral port counter */
static uint16_t udp_next_ephemeral = 50000;

/* ---- Socket API ---- */

int udp_bind(uint16_t port) {
    /* Check for duplicate */
    for (int i = 0; i < UDP_MAX_SOCKETS; i++) {
        if (sockets[i].used && sockets[i].local_port == port)
            return -1;
    }
    /* Allocate socket */
    for (int i = 0; i < UDP_MAX_SOCKETS; i++) {
        if (!sockets[i].used) {
            memset(&sockets[i], 0, sizeof(struct udp_socket));
            sockets[i].used = true;
            sockets[i].local_port = port;
            kprintf("[UDP] Bound socket %d to port %u\n", i, port);
            return i;
        }
    }
    return -1;
}

int udp_sendto(int sock, uint32_t dst_ip, uint16_t dst_port,
               const void *data, uint16_t len) {
    if (sock < 0 || sock >= UDP_MAX_SOCKETS || !sockets[sock].used)
        return -1;
    return udp_send(dst_ip, dst_port, sockets[sock].local_port, data, len);
}

int udp_recvfrom(int sock, void *buf, uint16_t len,
                 uint32_t *src_ip, uint16_t *src_port) {
    if (sock < 0 || sock >= UDP_MAX_SOCKETS || !sockets[sock].used)
        return -1;

    struct udp_socket *s = &sockets[sock];

    /* Block until data arrives (use sti;hlt like TCP) */
    uint32_t deadline = pit_get_ticks() + 1000; /* 10 second timeout */
    while (!s->data_ready && pit_get_ticks() < deadline) {
        __asm__ volatile("sti; hlt");
    }

    if (!s->data_ready || s->rx_count == 0)
        return 0; /* Timeout */

    /* Read from ring buffer */
    uint16_t n = 0;
    uint8_t *dst = (uint8_t *)buf;
    while (n < len && s->rx_count > 0) {
        dst[n++] = s->rxbuf[s->rx_tail];
        s->rx_tail = (s->rx_tail + 1) % UDP_RXBUF_SIZE;
        s->rx_count--;
    }

    /* Return sender info */
    if (src_ip)   *src_ip = s->rx_src_ip;
    if (src_port) *src_port = s->rx_src_port;

    if (s->rx_count == 0)
        s->data_ready = false;

    return n;
}

void udp_sock_close(int sock) {
    if (sock < 0 || sock >= UDP_MAX_SOCKETS) return;
    sockets[sock].used = false;
    kprintf("[UDP] Closed socket %d\n", sock);
}

/* ---- Receive path ---- */

void udp_rx(struct netbuf *nb, uint32_t src_ip) {
    if (nb->len < sizeof(struct udp_header)) {
        netbuf_free(nb);
        return;
    }

    struct udp_header *udp = (struct udp_header *)nb->data;
    uint16_t s_port = ntohs(udp->src_port);
    uint16_t dst_port = ntohs(udp->dst_port);
    uint16_t udp_len = ntohs(udp->length);

    /* Strip UDP header */
    uint8_t *payload = nb->data + sizeof(struct udp_header);
    uint16_t payload_len = udp_len - sizeof(struct udp_header);
    if (payload_len > nb->len - sizeof(struct udp_header))
        payload_len = nb->len - sizeof(struct udp_header);

    /* DNS reply from port 53 */
    if (s_port == 53 && dst_port == 1053) {
        dns_process_reply(payload, payload_len);
        netbuf_free(nb);
        return;
    }

    /* Deliver to bound socket */
    for (int i = 0; i < UDP_MAX_SOCKETS; i++) {
        if (sockets[i].used && sockets[i].local_port == dst_port) {
            struct udp_socket *s = &sockets[i];
            /* Write payload into ring buffer */
            for (uint16_t j = 0; j < payload_len && s->rx_count < UDP_RXBUF_SIZE; j++) {
                s->rxbuf[s->rx_head] = payload[j];
                s->rx_head = (s->rx_head + 1) % UDP_RXBUF_SIZE;
                s->rx_count++;
            }
            s->rx_src_ip = src_ip;
            s->rx_src_port = s_port;
            s->data_ready = true;
            netbuf_free(nb);
            return;
        }
    }

    netbuf_free(nb);
}

/* ---- Send ---- */

int udp_send(uint32_t dst_ip, uint16_t dst_port, uint16_t src_port,
             const void *data, uint16_t len) {
    struct netbuf *nb = netbuf_alloc();
    if (!nb) return -1;

    /* Copy payload */
    void *payload = netbuf_put(nb, len);
    if (!payload) { netbuf_free(nb); return -1; }
    memcpy(payload, data, len);

    /* Prepend UDP header */
    struct udp_header *udp = (struct udp_header *)netbuf_push(nb, sizeof(struct udp_header));
    if (!udp) { netbuf_free(nb); return -1; }

    udp->src_port = htons(src_port);
    udp->dst_port = htons(dst_port);
    udp->length   = htons(sizeof(struct udp_header) + len);
    udp->checksum  = 0; /* Optional for IPv4 */

    return ipv4_tx(nb, dst_ip, IP_PROTO_UDP);
}
