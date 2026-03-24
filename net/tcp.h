#ifndef NET_TCP_H
#define NET_TCP_H

#include <plantos/types.h>
#include "net/netbuf.h"

/* TCP header */
struct tcp_header {
    uint16_t src_port;
    uint16_t dst_port;
    uint32_t seq_num;
    uint32_t ack_num;
    uint8_t  data_off;    /* upper 4 bits = header length in 32-bit words */
    uint8_t  flags;
    uint16_t window;
    uint16_t checksum;
    uint16_t urgent_ptr;
} __attribute__((packed));

/* TCP flags */
#define TCP_FIN  0x01
#define TCP_SYN  0x02
#define TCP_RST  0x04
#define TCP_PSH  0x08
#define TCP_ACK  0x10
#define TCP_URG  0x20

/* TCP connection states */
enum tcp_state {
    TCP_CLOSED,
    TCP_LISTEN,
    TCP_SYN_SENT,
    TCP_SYN_RECEIVED,
    TCP_ESTABLISHED,
    TCP_FIN_WAIT_1,
    TCP_FIN_WAIT_2,
    TCP_CLOSE_WAIT,
    TCP_CLOSING,
    TCP_LAST_ACK,
    TCP_TIME_WAIT
};

/* Receive ring buffer */
#define TCP_RXBUF_SIZE  8192

/* Transmit buffer */
#define TCP_TXBUF_SIZE  8192

/* Max connections */
#define TCP_MAX_CONNS   16

/* Retransmission */
#define TCP_RTO_INIT    200   /* Initial retransmit timeout (ticks, ~2s at 100Hz) */
#define TCP_RTO_MAX     600   /* Max retransmit timeout (~6s) */
#define TCP_MAX_RETRIES 5

/* Transmission Control Block */
struct tcp_conn {
    bool        used;
    enum tcp_state state;

    /* Connection identity (local/remote) */
    uint32_t    local_ip;
    uint16_t    local_port;
    uint32_t    remote_ip;
    uint16_t    remote_port;

    /* Send sequence variables */
    uint32_t    snd_una;    /* Oldest unacknowledged seq */
    uint32_t    snd_nxt;    /* Next seq to send */
    uint32_t    snd_wnd;    /* Send window (from receiver) */
    uint32_t    iss;        /* Initial send sequence number */

    /* Receive sequence variables */
    uint32_t    rcv_nxt;    /* Next expected receive seq */
    uint32_t    rcv_wnd;    /* Receive window we advertise */
    uint32_t    irs;        /* Initial receive sequence number */

    /* Receive buffer (ring) */
    uint8_t     rxbuf[TCP_RXBUF_SIZE];
    uint16_t    rx_head;    /* Write pointer */
    uint16_t    rx_tail;    /* Read pointer */
    uint16_t    rx_count;   /* Bytes available */

    /* Transmit buffer */
    uint8_t     txbuf[TCP_TXBUF_SIZE];
    uint16_t    tx_len;     /* Unacknowledged bytes in txbuf */
    uint32_t    tx_seq;     /* Sequence number of txbuf[0] */

    /* Retransmission */
    uint32_t    rto;        /* Current retransmit timeout (ticks) */
    uint32_t    retx_time;  /* Tick when last segment was sent */
    uint8_t     retx_count; /* Number of retransmissions so far */

    /* Flags for blocking callers */
    volatile bool established;  /* Connection established */
    volatile bool closed;       /* Connection fully closed */
    volatile bool data_ready;   /* New data in rxbuf */
    volatile bool rx_fin;       /* Remote sent FIN (EOF) */

    /* TIME_WAIT expiry */
    uint32_t    tw_expiry;
};

/* Initialize TCP subsystem */
void tcp_init(void);

/* Process incoming TCP segment (called from ipv4_rx) */
void tcp_rx(struct netbuf *nb, uint32_t src_ip, uint32_t dst_ip);

/* Active open: connect to remote host:port. Returns conn index or -1. */
int tcp_connect(uint32_t dst_ip, uint16_t dst_port);

/* Passive open: listen on a local port. Returns conn index or -1. */
int tcp_listen(uint16_t port);

/* Accept a connection on a listening socket. Blocks. Returns conn index or -1. */
int tcp_accept(int listen_conn);

/* Send data on a connection. Returns bytes sent or -1. */
int tcp_send(int conn, const void *data, uint16_t len);

/* Receive data from a connection. Blocks until data available or closed.
   Returns bytes read, 0 on EOF/close, -1 on error. */
int tcp_recv(int conn, void *buf, uint16_t len);

/* Close a connection (initiate FIN). */
void tcp_close(int conn);

/* Get connection state */
enum tcp_state tcp_get_state(int conn);

/* Periodic timer: call from PIT to handle retransmissions and TIME_WAIT */
void tcp_timer(void);

#endif
