#include "net/tcp.h"
#include "net/net.h"
#include "net/ipv4.h"
#include "net/arp.h"
#include "lib/string.h"
#include "lib/printf.h"
#include "drivers/pit.h"
#include "task/task.h"

/* Connection table */
static struct tcp_conn conns[TCP_MAX_CONNS];

/* Ephemeral port counter */
static uint16_t next_ephemeral = 49152;

/* Pseudo-header for TCP checksum */
struct tcp_pseudo {
    uint32_t src_ip;
    uint32_t dst_ip;
    uint8_t  zero;
    uint8_t  protocol;
    uint16_t tcp_len;
} __attribute__((packed));

/* ---- Helpers ---- */

static uint32_t tcp_gen_iss(void) {
    /* Simple ISN based on ticks */
    return (uint32_t)(pit_get_ticks() * 64017 + 12345);
}

static uint16_t alloc_ephemeral(void) {
    return next_ephemeral++;
}

static int find_conn(uint32_t local_ip, uint16_t local_port,
                     uint32_t remote_ip, uint16_t remote_port) {
    /* Exact match first */
    for (int i = 0; i < TCP_MAX_CONNS; i++) {
        if (!conns[i].used) continue;
        if (conns[i].local_port == local_port &&
            conns[i].remote_port == remote_port &&
            conns[i].remote_ip == remote_ip)
            return i;
    }
    return -1;
}

static int find_listener(uint16_t local_port) {
    for (int i = 0; i < TCP_MAX_CONNS; i++) {
        if (!conns[i].used) continue;
        if (conns[i].state == TCP_LISTEN && conns[i].local_port == local_port)
            return i;
    }
    return -1;
}

static int alloc_conn(void) {
    for (int i = 0; i < TCP_MAX_CONNS; i++) {
        if (!conns[i].used) {
            memset(&conns[i], 0, sizeof(struct tcp_conn));
            conns[i].used = true;
            conns[i].rto = TCP_RTO_INIT;
            conns[i].rcv_wnd = TCP_RXBUF_SIZE;
            return i;
        }
    }
    return -1;
}

static void free_conn(int idx) {
    conns[idx].used = false;
    conns[idx].state = TCP_CLOSED;
    conns[idx].closed = true;
}

/* Sequence number comparison (handles wraparound) */
static inline bool seq_lt(uint32_t a, uint32_t b) {
    return (int32_t)(a - b) < 0;
}

static inline bool seq_le(uint32_t a, uint32_t b) {
    return (int32_t)(a - b) <= 0;
}

/* Receive buffer operations */
static uint16_t rxbuf_free(struct tcp_conn *c) {
    return TCP_RXBUF_SIZE - c->rx_count;
}

static void rxbuf_write(struct tcp_conn *c, const uint8_t *data, uint16_t len) {
    for (uint16_t i = 0; i < len && c->rx_count < TCP_RXBUF_SIZE; i++) {
        c->rxbuf[c->rx_head] = data[i];
        c->rx_head = (c->rx_head + 1) % TCP_RXBUF_SIZE;
        c->rx_count++;
    }
}

static uint16_t rxbuf_read(struct tcp_conn *c, uint8_t *buf, uint16_t len) {
    uint16_t n = 0;
    while (n < len && c->rx_count > 0) {
        buf[n++] = c->rxbuf[c->rx_tail];
        c->rx_tail = (c->rx_tail + 1) % TCP_RXBUF_SIZE;
        c->rx_count--;
    }
    return n;
}

/* ---- TCP checksum ---- */

static uint16_t tcp_checksum(uint32_t src_ip, uint32_t dst_ip,
                             const void *tcp_seg, uint16_t tcp_len) {
    struct tcp_pseudo pseudo;
    pseudo.src_ip   = htonl(src_ip);
    pseudo.dst_ip   = htonl(dst_ip);
    pseudo.zero     = 0;
    pseudo.protocol = IP_PROTO_TCP;
    pseudo.tcp_len  = htons(tcp_len);

    /* Compute over pseudo + tcp segment */
    uint32_t sum = 0;
    const uint8_t *p;

    /* Pseudo-header */
    p = (const uint8_t *)&pseudo;
    for (int i = 0; i < (int)sizeof(pseudo); i += 2)
        sum += ((uint16_t)p[i] << 8) | p[i + 1];

    /* TCP segment */
    p = (const uint8_t *)tcp_seg;
    int len = tcp_len;
    while (len > 1) {
        sum += ((uint16_t)p[0] << 8) | p[1];
        p += 2;
        len -= 2;
    }
    if (len == 1)
        sum += (uint16_t)p[0] << 8;

    while (sum >> 16)
        sum = (sum & 0xFFFF) + (sum >> 16);

    return (uint16_t)~sum;
}

/* ---- Send a TCP segment ---- */

static int tcp_send_segment(struct tcp_conn *c, uint8_t flags,
                            uint32_t seq, const void *data, uint16_t len) {
    struct netbuf *nb = netbuf_alloc();
    if (!nb) return -1;

    /* Payload */
    if (data && len > 0) {
        uint8_t *payload = (uint8_t *)netbuf_put(nb, len);
        if (!payload) { netbuf_free(nb); return -1; }
        memcpy(payload, data, len);
    }

    /* TCP header */
    struct tcp_header *tcp = (struct tcp_header *)netbuf_push(nb, sizeof(struct tcp_header));
    if (!tcp) { netbuf_free(nb); return -1; }

    memset(tcp, 0, sizeof(struct tcp_header));
    tcp->src_port  = htons(c->local_port);
    tcp->dst_port  = htons(c->remote_port);
    tcp->seq_num   = htonl(seq);
    tcp->ack_num   = htonl(c->rcv_nxt);
    tcp->data_off  = (5 << 4);  /* 20 bytes, no options */
    tcp->flags     = flags;
    tcp->window    = htons(rxbuf_free(c));
    tcp->checksum  = 0;
    tcp->urgent_ptr = 0;

    /* Checksum */
    uint16_t tcp_total = sizeof(struct tcp_header) + len;
    tcp->checksum = htons(tcp_checksum(c->local_ip, c->remote_ip,
                                       tcp, tcp_total));

    /* Record send time for retransmission */
    c->retx_time = pit_get_ticks();

    return ipv4_tx(nb, c->remote_ip, IP_PROTO_TCP);
}

static int tcp_send_rst(uint32_t src_ip, uint32_t dst_ip,
                        uint16_t src_port, uint16_t dst_port,
                        uint32_t seq, uint32_t ack, uint8_t flags) {
    struct netbuf *nb = netbuf_alloc();
    if (!nb) return -1;

    struct tcp_header *tcp = (struct tcp_header *)netbuf_push(nb, sizeof(struct tcp_header));
    if (!tcp) { netbuf_free(nb); return -1; }

    memset(tcp, 0, sizeof(struct tcp_header));
    tcp->src_port  = htons(src_port);
    tcp->dst_port  = htons(dst_port);
    tcp->data_off  = (5 << 4);

    if (flags & TCP_ACK) {
        /* RST in response to ACK: seq = ack of incoming */
        tcp->seq_num = htonl(ack);
        tcp->flags   = TCP_RST;
    } else {
        /* RST+ACK in response to non-ACK */
        tcp->seq_num = 0;
        tcp->ack_num = htonl(seq + 1);
        tcp->flags   = TCP_RST | TCP_ACK;
    }

    tcp->window = 0;
    tcp->checksum = 0;
    uint16_t tcp_total = sizeof(struct tcp_header);
    tcp->checksum = htons(tcp_checksum(src_ip, dst_ip, tcp, tcp_total));

    return ipv4_tx(nb, dst_ip, IP_PROTO_TCP);
}

/* ---- TCP state machine ---- */

void tcp_init(void) {
    memset(conns, 0, sizeof(conns));
}

void tcp_rx(struct netbuf *nb, uint32_t src_ip, uint32_t dst_ip) {
    if (nb->len < sizeof(struct tcp_header)) {
        netbuf_free(nb);
        return;
    }

    struct tcp_header *tcp = (struct tcp_header *)nb->data;
    uint16_t src_port = ntohs(tcp->src_port);
    uint16_t dst_port = ntohs(tcp->dst_port);
    uint32_t seq = ntohl(tcp->seq_num);
    uint32_t ack = ntohl(tcp->ack_num);
    uint8_t  flags = tcp->flags;
    uint16_t hdr_len = (tcp->data_off >> 4) * 4;
    uint16_t payload_len = nb->len - hdr_len;

    /* Find matching connection */
    int idx = find_conn(dst_ip, dst_port, src_ip, src_port);

    /* If no match, check for listener */
    if (idx < 0 && (flags & TCP_SYN) && !(flags & TCP_ACK)) {
        int li = find_listener(dst_port);
        if (li >= 0) {
            /* Create new connection for incoming SYN */
            idx = alloc_conn();
            if (idx < 0) {
                netbuf_free(nb);
                return;
            }
            struct tcp_conn *c = &conns[idx];
            c->local_ip    = dst_ip;
            c->local_port  = dst_port;
            c->remote_ip   = src_ip;
            c->remote_port = src_port;
            c->irs         = seq;
            c->rcv_nxt     = seq + 1;
            c->iss         = tcp_gen_iss();
            c->snd_una     = c->iss;
            c->snd_nxt     = c->iss + 1;
            c->snd_wnd     = ntohs(tcp->window);
            c->state       = TCP_SYN_RECEIVED;

            /* Send SYN+ACK */
            tcp_send_segment(c, TCP_SYN | TCP_ACK, c->iss, NULL, 0);

            /* Signal listener that a connection arrived */
            conns[li].data_ready = true;

            netbuf_free(nb);
            return;
        }
    }

    if (idx < 0) {
        /* No connection — send RST */
        if (!(flags & TCP_RST)) {
            tcp_send_rst(dst_ip, src_ip, dst_port, src_port, seq, ack, flags);
        }
        netbuf_free(nb);
        return;
    }

    struct tcp_conn *c = &conns[idx];
    uint8_t *payload = nb->data + hdr_len;

    /* Handle RST */
    if (flags & TCP_RST) {
        c->state = TCP_CLOSED;
        c->closed = true;
        c->data_ready = true;  /* Wake any blocked reader */
        netbuf_free(nb);
        return;
    }

    switch (c->state) {
    case TCP_SYN_SENT:
        /* Expecting SYN+ACK */
        if ((flags & (TCP_SYN | TCP_ACK)) == (TCP_SYN | TCP_ACK)) {
            if (ack == c->snd_nxt) {
                c->irs     = seq;
                c->rcv_nxt = seq + 1;
                c->snd_una = ack;
                c->snd_wnd = ntohs(tcp->window);
                c->state   = TCP_ESTABLISHED;
                c->established = true;
                c->retx_count = 0;

                /* Send ACK */
                tcp_send_segment(c, TCP_ACK, c->snd_nxt, NULL, 0);
            }
        } else if (flags & TCP_SYN) {
            /* Simultaneous open (rare) */
            c->irs     = seq;
            c->rcv_nxt = seq + 1;
            c->state   = TCP_SYN_RECEIVED;
            tcp_send_segment(c, TCP_SYN | TCP_ACK, c->iss, NULL, 0);
        }
        break;

    case TCP_SYN_RECEIVED:
        if (flags & TCP_ACK) {
            if (ack == c->snd_nxt) {
                c->snd_una = ack;
                c->snd_wnd = ntohs(tcp->window);
                c->state   = TCP_ESTABLISHED;
                c->established = true;
                c->retx_count = 0;
            }
        }
        break;

    case TCP_ESTABLISHED:
        /* Process ACK */
        if (flags & TCP_ACK) {
            if (seq_lt(c->snd_una, ack) && seq_le(ack, c->snd_nxt)) {
                /* Advance send window */
                uint32_t acked = ack - c->snd_una;
                c->snd_una = ack;
                c->snd_wnd = ntohs(tcp->window);
                c->retx_count = 0;
                c->rto = TCP_RTO_INIT;

                /* Remove acknowledged data from txbuf */
                if (acked > 0 && acked <= c->tx_len) {
                    memmove(c->txbuf, c->txbuf + acked, c->tx_len - acked);
                    c->tx_len -= (uint16_t)acked;
                    c->tx_seq += acked;
                } else if (acked > 0) {
                    c->tx_len = 0;
                }
            }
        }

        /* Process incoming data */
        if (payload_len > 0 && seq == c->rcv_nxt) {
            uint16_t space = rxbuf_free(c);
            uint16_t accept = payload_len < space ? payload_len : space;
            if (accept > 0) {
                rxbuf_write(c, payload, accept);
                c->rcv_nxt += accept;
                c->data_ready = true;
            }
            /* ACK the data */
            tcp_send_segment(c, TCP_ACK, c->snd_nxt, NULL, 0);
        }

        /* FIN received */
        if (flags & TCP_FIN) {
            c->rcv_nxt = seq + payload_len + 1;
            c->state = TCP_CLOSE_WAIT;
            c->rx_fin = true;
            c->data_ready = true;  /* Wake blocked reader */
            tcp_send_segment(c, TCP_ACK, c->snd_nxt, NULL, 0);
        }
        break;

    case TCP_FIN_WAIT_1:
        if (flags & TCP_ACK) {
            if (ack == c->snd_nxt) {
                c->snd_una = ack;
                if (flags & TCP_FIN) {
                    /* FIN+ACK: both sides closing */
                    c->rcv_nxt = seq + payload_len + 1;
                    c->state = TCP_TIME_WAIT;
                    c->tw_expiry = pit_get_ticks() + 400; /* ~4s */
                    tcp_send_segment(c, TCP_ACK, c->snd_nxt, NULL, 0);
                } else {
                    c->state = TCP_FIN_WAIT_2;
                }
            }
        }
        if (c->state == TCP_FIN_WAIT_1 && (flags & TCP_FIN)) {
            /* Simultaneous close */
            c->rcv_nxt = seq + payload_len + 1;
            c->state = TCP_CLOSING;
            tcp_send_segment(c, TCP_ACK, c->snd_nxt, NULL, 0);
        }
        /* Process any data in FIN_WAIT_1 */
        if (payload_len > 0 && seq == c->rcv_nxt) {
            uint16_t space = rxbuf_free(c);
            uint16_t accept = payload_len < space ? payload_len : space;
            if (accept > 0) {
                rxbuf_write(c, payload, accept);
                c->rcv_nxt += accept;
                c->data_ready = true;
            }
        }
        break;

    case TCP_FIN_WAIT_2:
        /* Process data */
        if (payload_len > 0 && seq == c->rcv_nxt) {
            uint16_t space = rxbuf_free(c);
            uint16_t accept = payload_len < space ? payload_len : space;
            if (accept > 0) {
                rxbuf_write(c, payload, accept);
                c->rcv_nxt += accept;
                c->data_ready = true;
            }
        }
        if (flags & TCP_FIN) {
            c->rcv_nxt = seq + payload_len + 1;
            c->state = TCP_TIME_WAIT;
            c->tw_expiry = pit_get_ticks() + 400;
            c->rx_fin = true;
            c->data_ready = true;
            tcp_send_segment(c, TCP_ACK, c->snd_nxt, NULL, 0);
        }
        break;

    case TCP_CLOSING:
        if ((flags & TCP_ACK) && ack == c->snd_nxt) {
            c->state = TCP_TIME_WAIT;
            c->tw_expiry = pit_get_ticks() + 400;
        }
        break;

    case TCP_LAST_ACK:
        if ((flags & TCP_ACK) && ack == c->snd_nxt) {
            free_conn(idx);
        }
        break;

    case TCP_CLOSE_WAIT:
        /* ACKs for any remaining data */
        if (flags & TCP_ACK) {
            if (seq_lt(c->snd_una, ack) && seq_le(ack, c->snd_nxt))
                c->snd_una = ack;
        }
        break;

    case TCP_TIME_WAIT:
        /* Re-ACK any FIN retransmission */
        if (flags & TCP_FIN) {
            tcp_send_segment(c, TCP_ACK, c->snd_nxt, NULL, 0);
            c->tw_expiry = pit_get_ticks() + 400;
        }
        break;

    default:
        break;
    }

    netbuf_free(nb);
}

/* ---- Timer: retransmissions and TIME_WAIT cleanup ---- */

void tcp_timer(void) {
    uint32_t now = pit_get_ticks();

    for (int i = 0; i < TCP_MAX_CONNS; i++) {
        if (!conns[i].used) continue;
        struct tcp_conn *c = &conns[i];

        /* TIME_WAIT expiry */
        if (c->state == TCP_TIME_WAIT && now >= c->tw_expiry) {
            free_conn(i);
            continue;
        }

        /* Retransmit unacknowledged data or SYN/FIN */
        if (c->state == TCP_SYN_SENT || c->state == TCP_SYN_RECEIVED) {
            if (now - c->retx_time >= c->rto) {
                if (c->retx_count >= TCP_MAX_RETRIES) {
                    free_conn(i);
                    continue;
                }
                c->retx_count++;
                c->rto = c->rto * 2;
                if (c->rto > TCP_RTO_MAX) c->rto = TCP_RTO_MAX;

                if (c->state == TCP_SYN_SENT)
                    tcp_send_segment(c, TCP_SYN, c->iss, NULL, 0);
                else
                    tcp_send_segment(c, TCP_SYN | TCP_ACK, c->iss, NULL, 0);
            }
        }

        /* Retransmit data in ESTABLISHED, FIN_WAIT_1 */
        if ((c->state == TCP_ESTABLISHED || c->state == TCP_FIN_WAIT_1) &&
            c->tx_len > 0 && c->snd_una < c->snd_nxt) {
            if (now - c->retx_time >= c->rto) {
                if (c->retx_count >= TCP_MAX_RETRIES) {
                    free_conn(i);
                    continue;
                }
                c->retx_count++;
                c->rto = c->rto * 2;
                if (c->rto > TCP_RTO_MAX) c->rto = TCP_RTO_MAX;

                /* Retransmit from snd_una */
                uint16_t resend = c->tx_len;
                if (resend > 1460) resend = 1460; /* MSS */
                tcp_send_segment(c, TCP_ACK | TCP_PSH, c->snd_una,
                                 c->txbuf, resend);
            }
        }
    }
}

/* ---- Public API ---- */

int tcp_connect(uint32_t dst_ip, uint16_t dst_port) {
    int idx = alloc_conn();
    if (idx < 0) return -1;

    struct tcp_conn *c = &conns[idx];
    c->local_ip    = net_cfg.ip;
    c->local_port  = alloc_ephemeral();
    c->remote_ip   = dst_ip;
    c->remote_port = dst_port;
    c->iss         = tcp_gen_iss();
    c->snd_una     = c->iss;
    c->snd_nxt     = c->iss + 1;
    c->tx_seq      = c->iss + 1;
    c->state       = TCP_SYN_SENT;

    /* Ensure ARP is resolved for destination (or gateway) before sending SYN */
    uint32_t next_hop = dst_ip;
    if ((dst_ip & net_cfg.netmask) != (net_cfg.ip & net_cfg.netmask))
        next_hop = net_cfg.gateway;
    uint8_t mac[6];
    if (arp_resolve(next_hop, mac) < 0) {
        arp_request(next_hop);
        uint32_t arp_start = pit_get_ticks();
        while (arp_resolve(next_hop, mac) < 0) {
            if (pit_get_ticks() - arp_start > 200) {
                free_conn(idx);
                return -1;
            }
            __asm__ volatile ("sti; hlt");
        }
    }

    /* Send SYN */
    tcp_send_segment(c, TCP_SYN, c->iss, NULL, 0);

    /* Block until established or timeout */
    uint32_t start = pit_get_ticks();
    while (!c->established && !c->closed) {
        if (pit_get_ticks() - start > 500) { /* 5 second timeout */
            free_conn(idx);
            return -1;
        }
        __asm__ volatile ("sti; hlt");
    }

    if (c->closed) return -1;
    return idx;
}

int tcp_listen(uint16_t port) {
    int idx = alloc_conn();
    if (idx < 0) return -1;

    struct tcp_conn *c = &conns[idx];
    c->local_ip   = net_cfg.ip;
    c->local_port = port;
    c->state      = TCP_LISTEN;
    return idx;
}

int tcp_accept(int listen_conn) {
    if (listen_conn < 0 || listen_conn >= TCP_MAX_CONNS) return -1;
    struct tcp_conn *lc = &conns[listen_conn];
    if (!lc->used || lc->state != TCP_LISTEN) return -1;

    /* Wait for incoming connection */
    while (!lc->data_ready) {
        __asm__ volatile ("sti; hlt");
    }
    lc->data_ready = false;

    /* Find the child connection for this port */
    int child = -1;
    for (int i = 0; i < TCP_MAX_CONNS; i++) {
        if (i == listen_conn) continue;
        if (!conns[i].used) continue;
        if (conns[i].local_port == lc->local_port &&
            (conns[i].state == TCP_SYN_RECEIVED ||
             conns[i].state == TCP_ESTABLISHED)) {
            child = i;
            break;
        }
    }
    if (child < 0) return -1;

    /* Wait for connection to become ESTABLISHED (with timeout) */
    uint32_t start = pit_get_ticks();
    while (conns[child].state == TCP_SYN_RECEIVED) {
        if (pit_get_ticks() - start > 500) { /* 5s timeout */
            free_conn(child);
            return -1;
        }
        __asm__ volatile ("sti; hlt");
    }

    if (conns[child].state != TCP_ESTABLISHED) {
        free_conn(child);
        return -1;
    }

    return child;
}

int tcp_send(int conn, const void *data, uint16_t len) {
    if (conn < 0 || conn >= TCP_MAX_CONNS) return -1;
    struct tcp_conn *c = &conns[conn];
    if (!c->used || (c->state != TCP_ESTABLISHED && c->state != TCP_CLOSE_WAIT))
        return -1;

    const uint8_t *p = (const uint8_t *)data;
    uint16_t sent = 0;

    while (sent < len) {
        /* Wait for transmit buffer space */
        while (c->tx_len >= TCP_TXBUF_SIZE) {
            if (c->closed) return -1;
            __asm__ volatile ("sti; hlt");
        }

        uint16_t space = TCP_TXBUF_SIZE - c->tx_len;
        uint16_t chunk = len - sent;
        if (chunk > space) chunk = space;
        if (chunk > 1460) chunk = 1460;  /* MSS */

        /* Copy to txbuf */
        memcpy(c->txbuf + c->tx_len, p + sent, chunk);
        c->tx_len += chunk;

        /* Send segment */
        tcp_send_segment(c, TCP_ACK | TCP_PSH, c->snd_nxt,
                         c->txbuf + c->tx_len - chunk, chunk);
        c->snd_nxt += chunk;
        sent += chunk;
    }

    return sent;
}

int tcp_recv(int conn, void *buf, uint16_t len) {
    if (conn < 0 || conn >= TCP_MAX_CONNS) return -1;
    struct tcp_conn *c = &conns[conn];
    if (!c->used) return -1;

    /* Wait for data or close */
    while (c->rx_count == 0 && !c->rx_fin && !c->closed) {
        c->data_ready = false;
        __asm__ volatile ("sti; hlt");
    }

    if (c->rx_count == 0) {
        /* EOF or closed */
        return 0;
    }

    uint16_t n = rxbuf_read(c, (uint8_t *)buf, len);

    /* Update window and send ACK if we freed significant space */
    if (n > 0) {
        tcp_send_segment(c, TCP_ACK, c->snd_nxt, NULL, 0);
    }

    return n;
}

void tcp_close(int conn) {
    if (conn < 0 || conn >= TCP_MAX_CONNS) return;
    struct tcp_conn *c = &conns[conn];
    if (!c->used) return;

    switch (c->state) {
    case TCP_ESTABLISHED:
        c->state = TCP_FIN_WAIT_1;
        tcp_send_segment(c, TCP_FIN | TCP_ACK, c->snd_nxt, NULL, 0);
        c->snd_nxt++;
        break;

    case TCP_CLOSE_WAIT:
        c->state = TCP_LAST_ACK;
        tcp_send_segment(c, TCP_FIN | TCP_ACK, c->snd_nxt, NULL, 0);
        c->snd_nxt++;
        break;

    case TCP_LISTEN:
    case TCP_SYN_SENT:
        free_conn(conn);
        break;

    default:
        break;
    }

    /* Wait for close to complete (with timeout) */
    uint32_t start = pit_get_ticks();
    while (c->used && c->state != TCP_CLOSED && c->state != TCP_TIME_WAIT) {
        if (pit_get_ticks() - start > 500) { /* 5s timeout */
            free_conn(conn);
            return;
        }
        __asm__ volatile ("sti; hlt");
    }
}

enum tcp_state tcp_get_state(int conn) {
    if (conn < 0 || conn >= TCP_MAX_CONNS) return TCP_CLOSED;
    if (!conns[conn].used) return TCP_CLOSED;
    return conns[conn].state;
}
