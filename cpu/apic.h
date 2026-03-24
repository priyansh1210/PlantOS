#ifndef CPU_APIC_H
#define CPU_APIC_H

#include <plantos/types.h>

#define MAX_CPUS 8

/* Per-CPU info */
struct cpu_info {
    uint32_t apic_id;
    bool     online;
};

extern uint32_t cpu_count;
extern uint32_t bsp_apic_id;
extern struct cpu_info cpus[MAX_CPUS];
extern bool apic_active;  /* true if LAPIC/IOAPIC is in use */

/* Parse ACPI MADT, initialize Local APIC and IOAPIC */
void apic_init(void);

/* Boot application processors */
void smp_boot_aps(void);

/* Get current CPU's Local APIC ID */
uint32_t lapic_get_id(void);

/* Send End-Of-Interrupt to Local APIC */
void lapic_eoi(void);

/* Get number of online CPUs */
uint32_t smp_cpu_count(void);

#endif
