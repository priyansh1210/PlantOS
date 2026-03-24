#include "user/libc/ulibc.h"

/*
 * fpudemo — Floating-point / SSE test program for PlantOS
 *
 * Tests basic floating-point arithmetic to verify FPU/SSE support
 * is properly enabled and context-switched.
 */

/* Simple integer-based float printing: prints "X.DDDD" */
static void print_float(double val) {
    if (val < 0.0) {
        printf("-");
        val = -val;
    }
    int integer_part = (int)val;
    int frac_part = (int)((val - (double)integer_part) * 10000.0);
    if (frac_part < 0) frac_part = -frac_part;
    printf("%d.%04d", integer_part, frac_part);
}

/* Simple absolute value */
static double fabs_simple(double x) {
    return x < 0.0 ? -x : x;
}

/* Approximate sqrt using Newton's method */
static double sqrt_approx(double x) {
    if (x <= 0.0) return 0.0;
    double guess = x / 2.0;
    for (int i = 0; i < 20; i++) {
        guess = (guess + x / guess) / 2.0;
    }
    return guess;
}

/* Approximate pi using Leibniz series */
static double compute_pi(int terms) {
    double pi = 0.0;
    double sign = 1.0;
    for (int i = 0; i < terms; i++) {
        pi += sign / (2.0 * (double)i + 1.0);
        sign = -sign;
    }
    return pi * 4.0;
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    printf("=== FPU/SSE Demo ===\n\n");

    /* Test 1: Basic arithmetic */
    printf("Test 1: Basic arithmetic\n");
    double a = 3.14;
    double b = 2.72;
    printf("  a = "); print_float(a); printf("\n");
    printf("  b = "); print_float(b); printf("\n");
    printf("  a + b = "); print_float(a + b); printf("\n");
    printf("  a * b = "); print_float(a * b); printf("\n");
    printf("  a / b = "); print_float(a / b); printf("\n");

    /* Test 2: Square root approximation */
    printf("\nTest 2: Square root (Newton's method)\n");
    double vals[] = {2.0, 9.0, 144.0, 1000.0};
    for (int i = 0; i < 4; i++) {
        double s = sqrt_approx(vals[i]);
        printf("  sqrt("); print_float(vals[i]);
        printf(") = "); print_float(s); printf("\n");
    }

    /* Test 3: Pi calculation */
    printf("\nTest 3: Pi approximation (Leibniz series)\n");
    int terms[] = {100, 1000, 10000, 100000};
    for (int i = 0; i < 4; i++) {
        double pi = compute_pi(terms[i]);
        printf("  %d terms: pi = ", terms[i]);
        print_float(pi); printf("\n");
    }

    /* Test 4: Verify precision */
    printf("\nTest 4: Precision check\n");
    double x = 1.0;
    for (int i = 0; i < 10; i++) {
        x = x / 3.0;
    }
    for (int i = 0; i < 10; i++) {
        x = x * 3.0;
    }
    double error = fabs_simple(x - 1.0);
    printf("  1.0 / 3^10 * 3^10 = "); print_float(x); printf("\n");
    printf("  error = "); print_float(error); printf("\n");
    if (error < 0.0001) {
        printf("  PASS: FPU precision is good!\n");
    } else {
        printf("  FAIL: FPU precision issue\n");
    }

    printf("\n=== FPU/SSE Demo Complete ===\n");
    return 0;
}
