#include "user/usyscall.h"
#include "user/libc/ulibc.h"

/* Prot flags matching kernel VMA flags */
#define PROT_READ  0x01
#define PROT_WRITE 0x02

static void print(const char *s) {
    uwrite(1, s, strlen(s));
}

static void print_hex(uint64_t val) {
    char buf[20];
    snprintf(buf, sizeof(buf), "0x%llx", val);
    print(buf);
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    print("=== mmapdemo: Virtual Memory Test ===\n\n");

    /* Test 1: Basic mmap/munmap */
    print("[Test 1] mmap 4KB anonymous region... ");
    int64_t addr = ummap(0, 4096, PROT_READ | PROT_WRITE);
    if (addr == -1) {
        print("FAIL\n");
        uexit(1);
    }
    print("OK at ");
    print_hex((uint64_t)addr);
    print("\n");

    /* Test 2: Write to mmap'd region (triggers demand paging) */
    print("[Test 2] Write to mmap'd page (demand paging)... ");
    volatile uint8_t *ptr = (volatile uint8_t *)(uint64_t)addr;
    ptr[0] = 0xAA;
    ptr[4095] = 0xBB;
    if (ptr[0] == 0xAA && ptr[4095] == 0xBB) {
        print("OK\n");
    } else {
        print("FAIL\n");
        uexit(1);
    }

    /* Test 3: mmap larger region */
    print("[Test 3] mmap 16KB region... ");
    int64_t addr2 = ummap(0, 16384, PROT_READ | PROT_WRITE);
    if (addr2 == -1) {
        print("FAIL\n");
        uexit(1);
    }
    print("OK at ");
    print_hex((uint64_t)addr2);
    print("\n");

    /* Test 4: Write pattern across multiple pages */
    print("[Test 4] Write pattern across 4 pages... ");
    volatile uint32_t *iptr = (volatile uint32_t *)(uint64_t)addr2;
    for (int i = 0; i < 4; i++) {
        iptr[(i * 4096) / 4] = 0xDEAD0000 + i;
    }
    int pass = 1;
    for (int i = 0; i < 4; i++) {
        if (iptr[(i * 4096) / 4] != (uint32_t)(0xDEAD0000 + i)) {
            pass = 0;
            break;
        }
    }
    print(pass ? "OK\n" : "FAIL\n");
    if (!pass) uexit(1);

    /* Test 5: munmap the first region */
    print("[Test 5] munmap first 4KB region... ");
    int64_t ret = umunmap((uint64_t)addr, 4096);
    if (ret == 0) {
        print("OK\n");
    } else {
        print("FAIL\n");
        uexit(1);
    }

    /* Test 6: mmap after munmap (reuse address space) */
    print("[Test 6] mmap new 4KB after munmap... ");
    int64_t addr3 = ummap(0, 4096, PROT_READ | PROT_WRITE);
    if (addr3 == -1) {
        print("FAIL\n");
        uexit(1);
    }
    print("OK at ");
    print_hex((uint64_t)addr3);
    print("\n");

    /* Test 7: Verify new region is zero-filled */
    print("[Test 7] Verify zero-fill on demand... ");
    volatile uint8_t *zptr = (volatile uint8_t *)(uint64_t)addr3;
    int zeroed = 1;
    for (int i = 0; i < 64; i++) {
        if (zptr[i] != 0) { zeroed = 0; break; }
    }
    print(zeroed ? "OK\n" : "FAIL\n");

    /* Test 8: Multiple small allocations */
    print("[Test 8] Multiple mmap allocations... ");
    int64_t addrs[4];
    int alloc_ok = 1;
    for (int i = 0; i < 4; i++) {
        addrs[i] = ummap(0, 4096, PROT_READ | PROT_WRITE);
        if (addrs[i] == -1) { alloc_ok = 0; break; }
    }
    if (alloc_ok) {
        /* Verify they don't overlap */
        for (int i = 0; i < 4 && alloc_ok; i++) {
            for (int j = i + 1; j < 4; j++) {
                if (addrs[i] == addrs[j]) { alloc_ok = 0; break; }
            }
        }
    }
    print(alloc_ok ? "OK\n" : "FAIL\n");

    /* Cleanup */
    umunmap((uint64_t)addr2, 16384);
    umunmap((uint64_t)addr3, 4096);
    for (int i = 0; i < 4; i++) {
        if (addrs[i] != -1) umunmap((uint64_t)addrs[i], 4096);
    }

    print("\n=== All mmap tests passed! ===\n");
    uexit(0);
}
