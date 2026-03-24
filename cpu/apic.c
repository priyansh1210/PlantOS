#include "cpu/apic.h"
#include "cpu/ports.h"
#include "cpu/gdt.h"
#include "mm/vmm.h"
#include "mm/heap.h"
#include "lib/string.h"
#include "lib/printf.h"

/* ---- ACPI structures ---- */

struct rsdp {
    char     signature[8]; /* "RSD PTR " */
    uint8_t  checksum;
    char     oemid[6];
    uint8_t  revision;
    uint32_t rsdt_addr;
} PACKED;

struct acpi_sdt_header {
    char     signature[4];
    uint32_t length;
    uint8_t  revision;
    uint8_t  checksum;
    char     oemid[6];
    char     oem_table_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
} PACKED;

/* MADT entry types */
#define MADT_LOCAL_APIC    0
#define MADT_IO_APIC       1
#define MADT_INT_OVERRIDE  2

struct madt_entry_header {
    uint8_t type;
    uint8_t length;
} PACKED;

struct madt_local_apic {
    struct madt_entry_header hdr;
    uint8_t  processor_id;
    uint8_t  apic_id;
    uint32_t flags;
} PACKED;

struct madt_io_apic {
    struct madt_entry_header hdr;
    uint8_t  io_apic_id;
    uint8_t  reserved;
    uint32_t io_apic_addr;
    uint32_t gsi_base;
} PACKED;

/* ---- Local APIC registers (memory-mapped) ---- */

#define LAPIC_ID       0x020
#define LAPIC_EOI      0x0B0
#define LAPIC_SVR      0x0F0
#define LAPIC_ICR_LO   0x300
#define LAPIC_ICR_HI   0x310
#define LAPIC_TIMER_LVT 0x320
#define LAPIC_TIMER_ICR 0x380
#define LAPIC_TIMER_CCR 0x390
#define LAPIC_TIMER_DCR 0x3E0

static volatile uint32_t *lapic_base = NULL;

static inline void lapic_write(uint32_t reg, uint32_t val) {
    lapic_base[reg / 4] = val;
}

static inline uint32_t lapic_read(uint32_t reg) {
    return lapic_base[reg / 4];
}

/* ---- IOAPIC registers ---- */

static volatile uint32_t *ioapic_base = NULL;

static inline void ioapic_write(uint32_t reg, uint32_t val) {
    ioapic_base[0] = reg;     /* IOREGSEL */
    ioapic_base[4] = val;     /* IOWIN */
}

static inline uint32_t ioapic_read(uint32_t reg) {
    ioapic_base[0] = reg;
    return ioapic_base[4];
}

/* ---- Global state ---- */

uint32_t cpu_count = 1;
uint32_t bsp_apic_id = 0;
struct cpu_info cpus[MAX_CPUS];
bool apic_active = false;

/* AP trampoline binary (embedded via objcopy) */
extern uint8_t _binary_build_cpu_ap_tramp_bin_start[];
extern uint8_t _binary_build_cpu_ap_tramp_bin_end[];

/* AP boot stack area — one 8KB stack per AP */
static uint8_t ap_stacks[MAX_CPUS][8192] __attribute__((aligned(16)));

/* Flag for AP to signal it's online */
static volatile uint32_t ap_online_flag = 0;

/* ---- ACPI RSDP search ---- */

static struct rsdp *find_rsdp(void) {
    /* Search EBDA (address 0x40E contains segment of EBDA) */
    uint16_t ebda_seg;
    __asm__ volatile ("movw 0x40E, %0" : "=r"(ebda_seg));
    uint64_t ebda_addr = (uint64_t)ebda_seg << 4;
    if (ebda_addr) {
        for (uint64_t addr = ebda_addr; addr < ebda_addr + 1024; addr += 16) {
            if (memcmp((void *)addr, "RSD PTR ", 8) == 0)
                return (struct rsdp *)addr;
        }
    }

    /* Search BIOS area */
    for (uint64_t addr = 0xE0000; addr < 0xFFFFF; addr += 16) {
        if (memcmp((void *)addr, "RSD PTR ", 8) == 0)
            return (struct rsdp *)addr;
    }

    return NULL;
}

/* ---- ACPI MADT parsing ---- */

static void parse_madt(struct acpi_sdt_header *madt) {
    uint32_t lapic_addr = *(uint32_t *)((uint8_t *)madt + 36);
    lapic_base = (volatile uint32_t *)(uint64_t)lapic_addr;
    kprintf("[APIC] Local APIC at 0x%x\n", lapic_addr);

    /* Walk MADT entries */
    uint8_t *ptr = (uint8_t *)madt + 44; /* skip header + LAPIC addr + flags */
    uint8_t *end = (uint8_t *)madt + madt->length;
    cpu_count = 0;

    while (ptr < end) {
        struct madt_entry_header *eh = (struct madt_entry_header *)ptr;
        if (eh->length == 0) break;

        switch (eh->type) {
        case MADT_LOCAL_APIC: {
            struct madt_local_apic *la = (struct madt_local_apic *)ptr;
            /* Bit 0 of flags: processor enabled. Bit 1: online capable */
            if ((la->flags & 0x03) && cpu_count < MAX_CPUS) {
                cpus[cpu_count].apic_id = la->apic_id;
                cpus[cpu_count].online = false;
                cpu_count++;
            }
            break;
        }
        case MADT_IO_APIC: {
            struct madt_io_apic *ia = (struct madt_io_apic *)ptr;
            ioapic_base = (volatile uint32_t *)(uint64_t)ia->io_apic_addr;
            kprintf("[APIC] I/O APIC at 0x%x\n", ia->io_apic_addr);
            break;
        }
        default:
            break;
        }

        ptr += eh->length;
    }
}

/* ---- Local APIC initialization ---- */

static void lapic_init(void) {
    if (!lapic_base) return;

    /* Enable LAPIC: set spurious vector to 0xFF, enable bit (bit 8) */
    lapic_write(LAPIC_SVR, 0x1FF);

    kprintf("[APIC] Local APIC enabled (ID=%d)\n", lapic_read(LAPIC_ID) >> 24);
}

void lapic_eoi(void) {
    if (lapic_base)
        lapic_write(LAPIC_EOI, 0);
}

uint32_t lapic_get_id(void) {
    if (lapic_base)
        return lapic_read(LAPIC_ID) >> 24;
    return 0;
}

/* ---- IOAPIC initialization ---- */

static void ioapic_route_irq(uint8_t irq, uint8_t vector, uint32_t dest_apic) {
    uint32_t reg_lo = 0x10 + irq * 2;
    uint32_t reg_hi = 0x10 + irq * 2 + 1;

    /* Write high dword first (destination) to avoid brief misroute */
    ioapic_write(reg_hi, dest_apic << 24);
    /* Low: vector, delivery mode fixed, active high, edge-triggered, unmasked */
    ioapic_write(reg_lo, (uint32_t)vector);
}

static void ioapic_init(void) {
    if (!ioapic_base) return;

    /* Disable 8259 PIC by masking all IRQs */
    outb(0x21, 0xFF);
    outb(0xA1, 0xFF);

    /* Route timer (IRQ 0) → vector 32 to BSP */
    ioapic_route_irq(0, 32, bsp_apic_id);
    /* Route keyboard (IRQ 1) → vector 33 to BSP */
    ioapic_route_irq(1, 33, bsp_apic_id);
    /* Route cascade (IRQ 2) — not needed but set up */
    ioapic_route_irq(2, 34, bsp_apic_id);
    /* Route COM1 (IRQ 4) → vector 36 */
    ioapic_route_irq(4, 36, bsp_apic_id);
    /* Route ATA (IRQ 14) → vector 46 */
    ioapic_route_irq(14, 46, bsp_apic_id);
    /* Route mouse (IRQ 12) → vector 44 */
    ioapic_route_irq(12, 44, bsp_apic_id);
    /* Route E1000 (IRQ 11) → vector 43 */
    ioapic_route_irq(11, 43, bsp_apic_id);

    apic_active = true;
    kprintf("[APIC] I/O APIC configured\n");
}

/* ---- AP entry point (called from trampoline) ---- */

void ap_entry(uint32_t apic_id) {
    /* Find our CPU index */
    int idx = -1;
    for (uint32_t i = 0; i < cpu_count; i++) {
        if (cpus[i].apic_id == apic_id) {
            idx = (int)i;
            break;
        }
    }

    if (idx >= 0) {
        cpus[idx].online = true;
    }

    /* Enable this CPU's Local APIC */
    lapic_write(LAPIC_SVR, 0x1FF);

    /* Signal BSP we're online */
    __asm__ volatile ("lock incl %0" : "+m"(ap_online_flag) :: "memory");

    /* AP idle loop — just HLT forever for now */
    __asm__ volatile ("sti");
    for (;;)
        __asm__ volatile ("hlt");
}

/* ---- SMP AP boot ---- */

/*
 * Trampoline data layout (offsets from end of code):
 * The trampoline .asm puts data at known offsets from 0x8000.
 * We patch the data area with the PML4 addr, stack pointer, and entry fn.
 *
 * The trampoline binary has these at fixed offsets (see ap_tramp.asm):
 *   tramp_size - 28: ap_pml4_addr (4 bytes, 32-bit)
 *   tramp_size - 24: ap_stack_ptr (8 bytes, 64-bit)
 *   tramp_size - 16: ap_entry_fn  (8 bytes, 64-bit)
 */

#define AP_TRAMP_ADDR 0x8000

void smp_boot_aps(void) {
    if (cpu_count <= 1) {
        kprintf("[SMP] Single CPU system\n");
        return;
    }

    uint32_t tramp_size = (uint32_t)(_binary_build_cpu_ap_tramp_bin_end -
                                      _binary_build_cpu_ap_tramp_bin_start);

    /* Copy trampoline to 0x8000 */
    memcpy((void *)AP_TRAMP_ADDR, _binary_build_cpu_ap_tramp_bin_start, tramp_size);

    /* Patch PML4 address (at end - 24) */
    uint64_t pml4 = vmm_get_boot_cr3();
    *(uint32_t *)((uint8_t *)AP_TRAMP_ADDR + tramp_size - 24) = (uint32_t)pml4;

    /* Patch entry function (at end - 8) */
    *(uint64_t *)((uint8_t *)AP_TRAMP_ADDR + tramp_size - 8) = (uint64_t)ap_entry;

    kprintf("[SMP] Booting %d application processors...\n", cpu_count - 1);

    for (uint32_t i = 0; i < cpu_count; i++) {
        if (cpus[i].apic_id == bsp_apic_id) continue; /* Skip BSP */

        /* Patch stack pointer for this AP (at end - 16) */
        uint64_t stack_top = (uint64_t)&ap_stacks[i][8192];
        *(uint64_t *)((uint8_t *)AP_TRAMP_ADDR + tramp_size - 16) = stack_top;

        ap_online_flag = 0;

        /* Send INIT IPI */
        lapic_write(LAPIC_ICR_HI, cpus[i].apic_id << 24);
        lapic_write(LAPIC_ICR_LO, 0x00004500); /* INIT, level, assert */

        /* Wait 10ms */
        for (volatile int d = 0; d < 1000000; d++);

        /* Send INIT deassert */
        lapic_write(LAPIC_ICR_HI, cpus[i].apic_id << 24);
        lapic_write(LAPIC_ICR_LO, 0x00008500); /* INIT, level, deassert */

        for (volatile int d = 0; d < 100000; d++);

        /* Send SIPI (startup vector = 0x8000 / 0x1000 = 0x08) */
        lapic_write(LAPIC_ICR_HI, cpus[i].apic_id << 24);
        lapic_write(LAPIC_ICR_LO, 0x00004608); /* SIPI, vector=0x08 */

        /* Wait up to 200ms for AP to come online */
        for (volatile int d = 0; d < 20000000; d++) {
            if (ap_online_flag) break;
        }

        if (!ap_online_flag) {
            /* Send second SIPI */
            lapic_write(LAPIC_ICR_HI, cpus[i].apic_id << 24);
            lapic_write(LAPIC_ICR_LO, 0x00004608);

            for (volatile int d = 0; d < 20000000; d++) {
                if (ap_online_flag) break;
            }
        }

        if (ap_online_flag) {
            kprintf("[SMP] CPU %d (APIC ID %d) online\n", i, cpus[i].apic_id);
        } else {
            kprintf("[SMP] CPU %d (APIC ID %d) failed to start\n", i, cpus[i].apic_id);
        }
    }
}

uint32_t smp_cpu_count(void) {
    uint32_t online = 0;
    for (uint32_t i = 0; i < cpu_count; i++)
        if (cpus[i].online) online++;
    return online;
}

/* ---- Main APIC init ---- */

void apic_init(void) {
    memset(cpus, 0, sizeof(cpus));
    kprintf("[APIC] Searching for ACPI RSDP...\n");

    /* Find RSDP */
    struct rsdp *rsdp = find_rsdp();
    if (!rsdp) {
        kprintf("[APIC] No ACPI RSDP found\n");
        cpus[0].apic_id = 0;
        cpus[0].online = true;
        cpu_count = 1;
        return;
    }
    kprintf("[APIC] RSDP found at 0x%llx\n", (uint64_t)rsdp);

    /* Parse RSDT */
    struct acpi_sdt_header *rsdt = (struct acpi_sdt_header *)(uint64_t)rsdp->rsdt_addr;
    uint32_t entries = (rsdt->length - sizeof(struct acpi_sdt_header)) / 4;
    uint32_t *ptrs = (uint32_t *)((uint8_t *)rsdt + sizeof(struct acpi_sdt_header));

    bool found_madt = false;
    for (uint32_t i = 0; i < entries; i++) {
        struct acpi_sdt_header *hdr = (struct acpi_sdt_header *)(uint64_t)ptrs[i];
        if (memcmp(hdr->signature, "APIC", 4) == 0) {
            parse_madt(hdr);
            found_madt = true;
            break;
        }
    }

    if (!found_madt) {
        kprintf("[APIC] No MADT found\n");
        cpus[0].apic_id = 0;
        cpus[0].online = true;
        cpu_count = 1;
        return;
    }

    kprintf("[APIC] Found %d CPU(s)\n", cpu_count);

    /* Get BSP APIC ID */
    bsp_apic_id = lapic_read(LAPIC_ID) >> 24;

    /* Mark BSP as online */
    for (uint32_t i = 0; i < cpu_count; i++) {
        if (cpus[i].apic_id == bsp_apic_id) {
            cpus[i].online = true;
            break;
        }
    }

    /* Initialize Local APIC on BSP */
    lapic_init();

    /* Initialize IOAPIC */
    ioapic_init();
}
