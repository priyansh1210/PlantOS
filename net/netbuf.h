#ifndef NET_NETBUF_H
#define NET_NETBUF_H

#include <plantos/types.h>

#define NETBUF_SIZE     2048
#define NETBUF_HEADROOM 64

struct netbuf {
    uint8_t  raw[NETBUF_SIZE];
    uint8_t *data;       /* Current header start */
    uint16_t len;        /* Payload length from data */
    struct netbuf *next; /* For queuing */
};

struct netbuf *netbuf_alloc(void);
void           netbuf_free(struct netbuf *nb);
void          *netbuf_push(struct netbuf *nb, uint16_t len);  /* Prepend header */
void          *netbuf_pull(struct netbuf *nb, uint16_t len);  /* Strip header */
void          *netbuf_put(struct netbuf *nb, uint16_t len);   /* Append data */

#endif
