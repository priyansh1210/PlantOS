#ifndef NET_UDP_H
#define NET_UDP_H

#include <plantos/types.h>
#include "net/netbuf.h"

struct udp_header {
    uint16_t src_port;
    uint16_t dst_port;
    uint16_t length;
    uint16_t checksum;
} __attribute__((packed));

/* UDP socket infrastructure */
#define UDP_MAX_SOCKETS  8
#define UDP_RXBUF_SIZE   4096

struct udp_socket {
    bool     used;
    uint16_t local_port;
    /* Receive ring buffer */
    uint8_t  rxbuf[UDP_RXBUF_SIZE];
    uint16_t rx_head;       /* Write pointer */
    uint16_t rx_tail;       /* Read pointer */
    uint16_t rx_count;      /* Bytes available */
    /* Metadata for last received packet */
    uint32_t rx_src_ip;     /* Source IP of last packet */
    uint16_t rx_src_port;   /* Source port of last packet */
    volatile bool data_ready;
};

void udp_rx(struct netbuf *nb, uint32_t src_ip);
int  udp_send(uint32_t dst_ip, uint16_t dst_port, uint16_t src_port,
              const void *data, uint16_t len);

/* Socket API */
int  udp_bind(uint16_t port);                /* Returns socket index or -1 */
int  udp_sendto(int sock, uint32_t dst_ip, uint16_t dst_port,
                const void *data, uint16_t len);
int  udp_recvfrom(int sock, void *buf, uint16_t len,
                  uint32_t *src_ip, uint16_t *src_port);
void udp_sock_close(int sock);

#endif
