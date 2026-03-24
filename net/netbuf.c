#include "net/netbuf.h"
#include "mm/heap.h"
#include "lib/string.h"

struct netbuf *netbuf_alloc(void) {
    struct netbuf *nb = (struct netbuf *)kmalloc(sizeof(struct netbuf));
    if (!nb) return NULL;
    memset(nb, 0, sizeof(*nb));
    nb->data = nb->raw + NETBUF_HEADROOM;
    nb->len = 0;
    nb->next = NULL;
    return nb;
}

void netbuf_free(struct netbuf *nb) {
    if (nb) kfree(nb);
}

void *netbuf_push(struct netbuf *nb, uint16_t len) {
    if (nb->data - nb->raw < len) return NULL; /* No headroom */
    nb->data -= len;
    nb->len += len;
    return nb->data;
}

void *netbuf_pull(struct netbuf *nb, uint16_t len) {
    if (nb->len < len) return NULL;
    void *ptr = nb->data;
    nb->data += len;
    nb->len -= len;
    return ptr;
}

void *netbuf_put(struct netbuf *nb, uint16_t len) {
    uint8_t *tail = nb->data + nb->len;
    if (tail + len > nb->raw + NETBUF_SIZE) return NULL;
    nb->len += len;
    return tail;
}
