#ifndef NET_NET_H
#define NET_NET_H

#include <plantos/types.h>

/* Byte-order helpers */
static inline uint16_t htons(uint16_t x) { return (x >> 8) | (x << 8); }
static inline uint16_t ntohs(uint16_t x) { return htons(x); }
static inline uint32_t htonl(uint32_t x) { return __builtin_bswap32(x); }
static inline uint32_t ntohl(uint32_t x) { return htonl(x); }

/* Make an IPv4 address from dotted quad */
#define IP4(a,b,c,d) (((uint32_t)(a)<<24)|((uint32_t)(b)<<16)|((uint32_t)(c)<<8)|(d))

/* Global network configuration */
struct net_config {
    uint32_t ip;
    uint32_t gateway;
    uint32_t netmask;
    uint8_t  mac[6];
    bool     up;
};

extern struct net_config net_cfg;

void net_init(void);

/* IP checksum (one's complement) */
uint16_t net_checksum(const void *data, int len);

#endif
