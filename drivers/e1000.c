#include "drivers/e1000.h"
#include "drivers/pci.h"
#include "drivers/pic.h"
#include "cpu/ports.h"
#include "cpu/idt.h"
#include "mm/pmm.h"
#include "mm/heap.h"
#include "lib/string.h"
#include "lib/printf.h"
#include "net/netbuf.h"
#include "net/ethernet.h"

/* ---- E1000 Registers ---- */
#define E1000_CTRL      0x0000
#define E1000_STATUS    0x0008
#define E1000_ICR       0x00C0  /* Interrupt Cause Read */
#define E1000_IMS       0x00D0  /* Interrupt Mask Set */
#define E1000_IMC       0x00D8  /* Interrupt Mask Clear */
#define E1000_RCTL      0x0100  /* Receive Control */
#define E1000_RDBAL     0x2800  /* RX Descriptor Base Low */
#define E1000_RDBAH     0x2804  /* RX Descriptor Base High */
#define E1000_RDLEN     0x2808  /* RX Descriptor Length */
#define E1000_RDH       0x2810  /* RX Descriptor Head */
#define E1000_RDT       0x2818  /* RX Descriptor Tail */
#define E1000_TCTL      0x0400  /* Transmit Control */
#define E1000_TDBAL     0x3800  /* TX Descriptor Base Low */
#define E1000_TDBAH     0x3804  /* TX Descriptor Base High */
#define E1000_TDLEN     0x3808  /* TX Descriptor Length */
#define E1000_TDH       0x3810  /* TX Descriptor Head */
#define E1000_TDT       0x3818  /* TX Descriptor Tail */
#define E1000_RAL       0x5400  /* Receive Address Low */
#define E1000_RAH       0x5404  /* Receive Address High */
#define E1000_MTA       0x5200  /* Multicast Table Array (128 entries) */
#define E1000_TIPG      0x0410  /* TX Inter-Packet Gap */

/* CTRL bits */
#define CTRL_SLU        (1 << 6)   /* Set Link Up */
#define CTRL_RST        (1 << 26)  /* Device Reset */

/* RCTL bits */
#define RCTL_EN         (1 << 1)
#define RCTL_BAM        (1 << 15)  /* Broadcast Accept */
#define RCTL_BSIZE_2048 (0 << 16)
#define RCTL_SECRC      (1 << 26)  /* Strip Ethernet CRC */

/* TCTL bits */
#define TCTL_EN         (1 << 1)
#define TCTL_PSP        (1 << 3)   /* Pad Short Packets */
#define TCTL_CT_SHIFT   4
#define TCTL_COLD_SHIFT 12

/* TX descriptor command bits */
#define TXCMD_EOP       (1 << 0)   /* End of Packet */
#define TXCMD_IFCS      (1 << 1)   /* Insert FCS */
#define TXCMD_RS        (1 << 3)   /* Report Status */

/* Descriptor status bits */
#define DESC_DD         (1 << 0)   /* Descriptor Done */

/* ICR bits */
#define ICR_TXDW        (1 << 0)
#define ICR_RXDMT0      (1 << 4)
#define ICR_RXO         (1 << 6)
#define ICR_RXT0        (1 << 7)

/* ---- Descriptors ---- */
#define NUM_RX_DESC 32
#define NUM_TX_DESC 8

struct e1000_rx_desc {
    uint64_t addr;
    uint16_t length;
    uint16_t checksum;
    uint8_t  status;
    uint8_t  errors;
    uint16_t special;
} __attribute__((packed));

struct e1000_tx_desc {
    uint64_t addr;
    uint16_t length;
    uint8_t  cso;
    uint8_t  cmd;
    uint8_t  status;
    uint8_t  css;
    uint16_t special;
} __attribute__((packed));

/* ---- Driver state ---- */
static volatile uint8_t *mmio_base = NULL;
static bool e1000_present = false;
static uint8_t mac_addr[6];

static struct e1000_rx_desc *rx_descs;
static struct e1000_tx_desc *tx_descs;
static uint8_t *rx_bufs[NUM_RX_DESC];
static int rx_tail = 0;
static int tx_tail = 0;

/* ---- MMIO access ---- */

static inline void e1000_write(uint32_t reg, uint32_t val) {
    *(volatile uint32_t *)(mmio_base + reg) = val;
}

static inline uint32_t e1000_read(uint32_t reg) {
    return *(volatile uint32_t *)(mmio_base + reg);
}

/* ---- RX init ---- */

static void e1000_rx_init(void) {
    /* Allocate descriptor ring (use a page, 32 * 16 = 512 bytes) */
    rx_descs = (struct e1000_rx_desc *)pmm_alloc_page();
    memset(rx_descs, 0, 4096);

    /* Allocate RX buffers */
    for (int i = 0; i < NUM_RX_DESC; i++) {
        rx_bufs[i] = (uint8_t *)pmm_alloc_page();
        memset(rx_bufs[i], 0, 4096);
        rx_descs[i].addr = (uint64_t)rx_bufs[i];
        rx_descs[i].status = 0;
    }

    /* Program descriptor ring address */
    uint64_t rx_phys = (uint64_t)rx_descs;
    e1000_write(E1000_RDBAL, (uint32_t)(rx_phys & 0xFFFFFFFF));
    e1000_write(E1000_RDBAH, (uint32_t)(rx_phys >> 32));

    /* Ring length (must be 128-byte aligned) */
    e1000_write(E1000_RDLEN, NUM_RX_DESC * sizeof(struct e1000_rx_desc));

    /* Head and tail */
    e1000_write(E1000_RDH, 0);
    e1000_write(E1000_RDT, NUM_RX_DESC - 1);
    rx_tail = 0;

    /* Enable receiver */
    e1000_write(E1000_RCTL, RCTL_EN | RCTL_BAM | RCTL_BSIZE_2048 | RCTL_SECRC);
}

/* ---- TX init ---- */

static void e1000_tx_init(void) {
    tx_descs = (struct e1000_tx_desc *)pmm_alloc_page();
    memset(tx_descs, 0, 4096);

    uint64_t tx_phys = (uint64_t)tx_descs;
    e1000_write(E1000_TDBAL, (uint32_t)(tx_phys & 0xFFFFFFFF));
    e1000_write(E1000_TDBAH, (uint32_t)(tx_phys >> 32));
    e1000_write(E1000_TDLEN, NUM_TX_DESC * sizeof(struct e1000_tx_desc));

    e1000_write(E1000_TDH, 0);
    e1000_write(E1000_TDT, 0);
    tx_tail = 0;

    /* TX inter-packet gap (recommended values) */
    e1000_write(E1000_TIPG, 10 | (10 << 10) | (10 << 20));

    /* Enable transmitter */
    e1000_write(E1000_TCTL, TCTL_EN | TCTL_PSP |
                (15 << TCTL_CT_SHIFT) | (64 << TCTL_COLD_SHIFT));
}

/* ---- IRQ handler ---- */

static void e1000_handle_rx(void) {
    while (rx_descs[rx_tail].status & DESC_DD) {
        uint16_t len = rx_descs[rx_tail].length;

        if (len > 0 && len <= 2048) {
            struct netbuf *nb = netbuf_alloc();
            if (nb) {
                /* Copy received data into netbuf */
                memcpy(nb->data, rx_bufs[rx_tail], len);
                nb->len = len;
                eth_rx(nb);
            }
        }

        /* Reset descriptor for reuse */
        rx_descs[rx_tail].status = 0;
        uint32_t old_tail = rx_tail;
        rx_tail = (rx_tail + 1) % NUM_RX_DESC;
        e1000_write(E1000_RDT, old_tail);
    }
}

static void e1000_irq_handler(struct registers *regs) {
    (void)regs;
    uint32_t icr = e1000_read(E1000_ICR);

    if (icr & (ICR_RXT0 | ICR_RXDMT0 | ICR_RXO)) {
        e1000_handle_rx();
    }

    /* TX complete — nothing to do, we poll DD in send */
    (void)icr;
}

/* ---- Public API ---- */

void e1000_init(void) {
    struct pci_device *dev = pci_find_device(0x8086, 0x100E);
    if (!dev) {
        kprintf("[E1000] Not found\n");
        return;
    }

    kprintf("[E1000] Found at PCI %02x:%02x.%d  IRQ %d\n",
            dev->bus, dev->slot, dev->func, dev->irq_line);

    /* BAR0 is MMIO */
    uint64_t bar0 = dev->bar[0] & ~0xFULL;
    mmio_base = (volatile uint8_t *)bar0;

    /* Enable bus mastering + memory space */
    uint32_t cmd = pci_config_read(dev->bus, dev->slot, dev->func, 0x04);
    cmd |= (1 << 2) | (1 << 1); /* Bus Master + Memory Space */
    pci_config_write(dev->bus, dev->slot, dev->func, 0x04, cmd);

    /* Reset device */
    e1000_write(E1000_CTRL, e1000_read(E1000_CTRL) | CTRL_RST);
    /* Wait for reset to complete */
    for (volatile int i = 0; i < 100000; i++);
    /* Clear interrupt mask */
    e1000_write(E1000_IMC, 0xFFFFFFFF);

    /* Set link up */
    e1000_write(E1000_CTRL, e1000_read(E1000_CTRL) | CTRL_SLU);

    /* Read MAC address from RAL/RAH */
    uint32_t ral = e1000_read(E1000_RAL);
    uint32_t rah = e1000_read(E1000_RAH);
    mac_addr[0] = ral & 0xFF;
    mac_addr[1] = (ral >> 8) & 0xFF;
    mac_addr[2] = (ral >> 16) & 0xFF;
    mac_addr[3] = (ral >> 24) & 0xFF;
    mac_addr[4] = rah & 0xFF;
    mac_addr[5] = (rah >> 8) & 0xFF;

    kprintf("[E1000] MAC: %02x:%02x:%02x:%02x:%02x:%02x\n",
            mac_addr[0], mac_addr[1], mac_addr[2],
            mac_addr[3], mac_addr[4], mac_addr[5]);

    /* Clear multicast table */
    for (int i = 0; i < 128; i++)
        e1000_write(E1000_MTA + i * 4, 0);

    /* Initialize RX and TX */
    e1000_rx_init();
    e1000_tx_init();

    /* Register IRQ handler */
    register_interrupt_handler(32 + dev->irq_line, e1000_irq_handler);
    pic_clear_mask(dev->irq_line);

    /* Enable interrupts on NIC */
    e1000_write(E1000_IMS, ICR_TXDW | ICR_RXT0 | ICR_RXDMT0 | ICR_RXO);

    e1000_present = true;
    kprintf("[E1000] Driver initialized (link %s)\n",
            e1000_is_link_up() ? "up" : "down");
}

int e1000_send(const void *data, uint16_t len) {
    if (!e1000_present || len == 0 || len > 1518)
        return -1;

    struct e1000_tx_desc *desc = &tx_descs[tx_tail];

    /* Wait for previous transmit to complete */
    while (!(desc->status & DESC_DD) && desc->cmd != 0) {
        __asm__ volatile ("pause");
    }

    /* Copy data to a DMA-safe buffer */
    static uint8_t tx_buf_pool[NUM_TX_DESC][2048] __attribute__((aligned(16)));
    memcpy(tx_buf_pool[tx_tail], data, len);

    desc->addr   = (uint64_t)tx_buf_pool[tx_tail];
    desc->length = len;
    desc->cmd    = TXCMD_EOP | TXCMD_IFCS | TXCMD_RS;
    desc->status = 0;

    tx_tail = (tx_tail + 1) % NUM_TX_DESC;
    e1000_write(E1000_TDT, tx_tail);

    return 0;
}

void e1000_get_mac(uint8_t mac[6]) {
    memcpy(mac, mac_addr, 6);
}

bool e1000_is_link_up(void) {
    if (!mmio_base) return false;
    return (e1000_read(E1000_STATUS) & 0x02) != 0;
}
