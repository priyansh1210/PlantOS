#include "net/net.h"
#include "drivers/e1000.h"
#include "lib/printf.h"

struct net_config net_cfg;

void net_init(void) {
    /* Initialize E1000 NIC */
    e1000_init();

    if (!e1000_is_link_up()) {
        kprintf("[NET] No network link\n");
        net_cfg.up = false;
        return;
    }

    /* Get MAC from NIC */
    e1000_get_mac(net_cfg.mac);

    /* Static IP configuration (QEMU user-mode networking defaults) */
    net_cfg.ip      = IP4(10, 0, 2, 15);
    net_cfg.gateway  = IP4(10, 0, 2, 2);
    net_cfg.netmask  = IP4(255, 255, 255, 0);
    net_cfg.up       = true;

    kprintf("[NET] IP: 10.0.2.15  GW: 10.0.2.2\n");
    kprintf("[NET] MAC: %02x:%02x:%02x:%02x:%02x:%02x\n",
            net_cfg.mac[0], net_cfg.mac[1], net_cfg.mac[2],
            net_cfg.mac[3], net_cfg.mac[4], net_cfg.mac[5]);
}

uint16_t net_checksum(const void *data, int len) {
    const uint8_t *p = (const uint8_t *)data;
    uint32_t sum = 0;

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
