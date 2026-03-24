#include "net/dns.h"
#include "net/net.h"
#include "net/udp.h"
#include "net/arp.h"
#include "net/netbuf.h"
#include "net/ipv4.h"
#include "lib/string.h"
#include "lib/printf.h"
#include "drivers/pit.h"

/* DNS server (QEMU user-mode networking built-in DNS) */
static uint32_t dns_server = 0;

/* ---- DNS cache ---- */
#define DNS_CACHE_SIZE 8

struct dns_cache_entry {
    char     hostname[64];
    uint32_t ip;
    uint32_t timestamp;   /* tick when cached */
    bool     valid;
};

static struct dns_cache_entry dns_cache[DNS_CACHE_SIZE];

static int dns_cache_lookup(const char *hostname, uint32_t *ip_out) {
    for (int i = 0; i < DNS_CACHE_SIZE; i++) {
        if (!dns_cache[i].valid) continue;
        /* Compare hostnames */
        const char *a = hostname;
        const char *b = dns_cache[i].hostname;
        bool match = true;
        while (*a && *b) {
            if (*a++ != *b++) { match = false; break; }
        }
        if (match && *a == '\0' && *b == '\0') {
            *ip_out = dns_cache[i].ip;
            return 0;
        }
    }
    return -1;
}

static void dns_cache_insert(const char *hostname, uint32_t ip) {
    /* Find free slot or oldest entry */
    int best = 0;
    uint32_t oldest = 0xFFFFFFFF;
    for (int i = 0; i < DNS_CACHE_SIZE; i++) {
        if (!dns_cache[i].valid) { best = i; break; }
        if (dns_cache[i].timestamp < oldest) {
            oldest = dns_cache[i].timestamp;
            best = i;
        }
    }
    struct dns_cache_entry *e = &dns_cache[best];
    e->valid = true;
    e->ip = ip;
    e->timestamp = pit_get_ticks();
    /* Copy hostname */
    int j = 0;
    while (hostname[j] && j < 63) {
        e->hostname[j] = hostname[j];
        j++;
    }
    e->hostname[j] = '\0';
}

/* DNS header */
struct dns_header {
    uint16_t id;
    uint16_t flags;
    uint16_t qdcount;
    uint16_t ancount;
    uint16_t nscount;
    uint16_t arcount;
} __attribute__((packed));

/* DNS flags */
#define DNS_FLAG_QR     0x8000  /* Response */
#define DNS_FLAG_RD     0x0100  /* Recursion Desired */
#define DNS_FLAG_RA     0x0080  /* Recursion Available */
#define DNS_RCODE_MASK  0x000F

/* DNS record types */
#define DNS_TYPE_A      1
#define DNS_CLASS_IN    1

/* Pending DNS reply state */
static volatile bool     dns_reply_received = false;
static volatile uint16_t dns_reply_id = 0;
static volatile uint32_t dns_resolved_ip = 0;
static volatile int      dns_reply_rcode = -1;

void dns_init(void) {
    /* QEMU user-mode networking DNS server */
    dns_server = IP4(10, 0, 2, 3);
}

/* Encode a hostname into DNS wire format (labels).
 * "www.example.com" → \3www\7example\3com\0
 * Returns number of bytes written. */
static int dns_encode_name(const char *name, uint8_t *buf, int bufsize) {
    int pos = 0;
    const char *p = name;

    while (*p) {
        /* Find next dot or end */
        const char *dot = p;
        while (*dot && *dot != '.') dot++;

        int label_len = (int)(dot - p);
        if (label_len > 63 || pos + label_len + 1 >= bufsize)
            return -1;

        buf[pos++] = (uint8_t)label_len;
        for (int i = 0; i < label_len; i++)
            buf[pos++] = (uint8_t)p[i];

        p = dot;
        if (*p == '.') p++;
    }

    if (pos >= bufsize) return -1;
    buf[pos++] = 0;  /* Root label */
    return pos;
}

/* Skip a DNS name (handles compression pointers) */
static int dns_skip_name(const uint8_t *data, int offset, int len) {
    int pos = offset;
    while (pos < len) {
        uint8_t label = data[pos];
        if (label == 0) {
            return pos + 1;
        } else if ((label & 0xC0) == 0xC0) {
            /* Compression pointer — 2 bytes */
            return pos + 2;
        } else {
            pos += 1 + label;
        }
    }
    return -1;
}

/* Process a DNS response from UDP */
void dns_process_reply(const uint8_t *data, uint16_t len) {
    if (len < sizeof(struct dns_header))
        return;

    struct dns_header *hdr = (struct dns_header *)data;
    uint16_t id = ntohs(hdr->id);
    uint16_t flags = ntohs(hdr->flags);
    uint16_t qdcount = ntohs(hdr->qdcount);
    uint16_t ancount = ntohs(hdr->ancount);

    /* Must be a response */
    if (!(flags & DNS_FLAG_QR))
        return;

    int rcode = flags & DNS_RCODE_MASK;

    /* Skip question section */
    int pos = sizeof(struct dns_header);
    for (uint16_t i = 0; i < qdcount; i++) {
        pos = dns_skip_name(data, pos, len);
        if (pos < 0) return;
        pos += 4;  /* QTYPE + QCLASS */
        if (pos > len) return;
    }

    /* Parse answer section — look for A record */
    uint32_t resolved = 0;
    for (uint16_t i = 0; i < ancount; i++) {
        pos = dns_skip_name(data, pos, len);
        if (pos < 0 || pos + 10 > len) return;

        uint16_t rtype = ((uint16_t)data[pos] << 8) | data[pos + 1];
        /* uint16_t rclass = ((uint16_t)data[pos + 2] << 8) | data[pos + 3]; */
        /* uint32_t ttl = ...; */
        uint16_t rdlength = ((uint16_t)data[pos + 8] << 8) | data[pos + 9];
        pos += 10;

        if (pos + rdlength > len) return;

        if (rtype == DNS_TYPE_A && rdlength == 4) {
            resolved = ((uint32_t)data[pos] << 24) |
                       ((uint32_t)data[pos + 1] << 16) |
                       ((uint32_t)data[pos + 2] << 8) |
                       (uint32_t)data[pos + 3];
            break;
        }

        pos += rdlength;
    }

    dns_reply_id = id;
    dns_resolved_ip = resolved;
    dns_reply_rcode = rcode;
    dns_reply_received = true;
}

int dns_resolve(const char *hostname, uint32_t *ip_out) {
    if (!dns_server) return -1;

    /* Check cache first */
    if (dns_cache_lookup(hostname, ip_out) == 0)
        return 0;

    /* Check if it's already an IP address */
    bool is_ip = true;
    for (int i = 0; hostname[i]; i++) {
        if (hostname[i] != '.' && (hostname[i] < '0' || hostname[i] > '9')) {
            is_ip = false;
            break;
        }
    }
    if (is_ip) {
        /* Parse directly */
        uint32_t parts[4] = {0};
        int part = 0;
        for (int i = 0; hostname[i] && part < 4; i++) {
            if (hostname[i] == '.')
                part++;
            else
                parts[part] = parts[part] * 10 + (hostname[i] - '0');
        }
        *ip_out = (parts[0] << 24) | (parts[1] << 16) | (parts[2] << 8) | parts[3];
        return 0;
    }

    /* Build DNS query */
    uint8_t qbuf[256];
    int qpos = 0;

    /* Header */
    uint16_t txid = (uint16_t)(pit_get_ticks() & 0xFFFF);
    struct dns_header *hdr = (struct dns_header *)qbuf;
    hdr->id = htons(txid);
    hdr->flags = htons(DNS_FLAG_RD);
    hdr->qdcount = htons(1);
    hdr->ancount = 0;
    hdr->nscount = 0;
    hdr->arcount = 0;
    qpos = sizeof(struct dns_header);

    /* Question: encoded name + type A + class IN */
    int name_len = dns_encode_name(hostname, qbuf + qpos, (int)(sizeof(qbuf) - qpos - 4));
    if (name_len < 0) return -1;
    qpos += name_len;

    /* QTYPE = A (1) */
    qbuf[qpos++] = 0;
    qbuf[qpos++] = DNS_TYPE_A;
    /* QCLASS = IN (1) */
    qbuf[qpos++] = 0;
    qbuf[qpos++] = DNS_CLASS_IN;

    /* Ensure ARP is resolved for DNS server */
    uint32_t next_hop = dns_server;
    if ((dns_server & net_cfg.netmask) != (net_cfg.ip & net_cfg.netmask))
        next_hop = net_cfg.gateway;
    uint8_t mac[6];
    if (arp_resolve(next_hop, mac) < 0) {
        arp_request(next_hop);
        uint32_t arp_start = pit_get_ticks();
        while (arp_resolve(next_hop, mac) < 0) {
            if (pit_get_ticks() - arp_start > 200)
                return -1;
            __asm__ volatile ("sti; hlt");
        }
    }

    /* Send via UDP to DNS server port 53 */
    dns_reply_received = false;
    dns_reply_rcode = -1;

    if (udp_send(dns_server, 53, 1053, qbuf, (uint16_t)qpos) < 0)
        return -1;

    /* Wait for reply with timeout (3 seconds) */
    uint32_t start = pit_get_ticks();
    while (!dns_reply_received) {
        if (pit_get_ticks() - start > 300) /* 3s at 100Hz */
            return -1;
        __asm__ volatile ("sti; hlt");
    }

    if (dns_reply_rcode != 0 || dns_resolved_ip == 0)
        return -1;

    *ip_out = dns_resolved_ip;

    /* Cache the result */
    dns_cache_insert(hostname, dns_resolved_ip);

    return 0;
}
