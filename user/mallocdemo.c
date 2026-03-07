#include "user/libc/ulibc.h"

int main(void) {
    printf("[MALLOCDEMO] PID=%lu\n", ugetpid());

    /* Test basic allocation */
    char *buf = (char *)malloc(128);
    if (!buf) {
        printf("[MALLOCDEMO] malloc(128) failed!\n");
        return 1;
    }
    strcpy(buf, "Hello from malloc'd memory!");
    printf("[MALLOCDEMO] buf = \"%s\"\n", buf);

    /* Test array allocation */
    int *nums = (int *)malloc(10 * sizeof(int));
    if (!nums) {
        printf("[MALLOCDEMO] malloc(40) failed!\n");
        return 1;
    }
    for (int i = 0; i < 10; i++)
        nums[i] = i * i;

    printf("[MALLOCDEMO] Squares:");
    for (int i = 0; i < 10; i++)
        printf(" %d", nums[i]);
    printf("\n");

    /* Test free and realloc */
    free(buf);
    printf("[MALLOCDEMO] Freed first buffer\n");

    char *buf2 = (char *)malloc(64);
    strcpy(buf2, "Reused memory!");
    printf("[MALLOCDEMO] buf2 = \"%s\"\n", buf2);

    buf2 = (char *)realloc(buf2, 256);
    strcat(buf2, " (after realloc)");
    printf("[MALLOCDEMO] buf2 = \"%s\"\n", buf2);

    free(buf2);
    free(nums);

    printf("[MALLOCDEMO] All tests passed!\n");
    return 0;
}
