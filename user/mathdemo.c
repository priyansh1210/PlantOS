#include "user/libc/ulibc.h"

/* Print a double with 4 decimal places */
static void pf(double val) {
    if (isnan(val)) { printf("NaN"); return; }
    if (isinf(val)) { printf(val > 0 ? "inf" : "-inf"); return; }
    if (val < 0.0) { printf("-"); val = -val; }
    int integer = (int)val;
    int frac = (int)((val - (double)integer) * 10000.0 + 0.5);
    if (frac >= 10000) { integer++; frac -= 10000; }
    printf("%d.%04d", integer, frac);
}

static void test_basic(void) {
    printf("=== Test 1: Basic Math ===\n");
    printf("  fabs(-7.5) = "); pf(fabs(-7.5)); printf("\n");
    printf("  fmod(10.5, 3.0) = "); pf(fmod(10.5, 3.0)); printf("\n");
    printf("  floor(3.7) = "); pf(floor(3.7)); printf("\n");
    printf("  floor(-3.7) = "); pf(floor(-3.7)); printf("\n");
    printf("  ceil(3.2) = "); pf(ceil(3.2)); printf("\n");
    printf("  ceil(-3.2) = "); pf(ceil(-3.2)); printf("\n");
    printf("  round(3.5) = "); pf(round(3.5)); printf("\n");
    printf("  round(-3.5) = "); pf(round(-3.5)); printf("\n");
    printf("  trunc(3.9) = "); pf(trunc(3.9)); printf("\n");
    printf("  trunc(-3.9) = "); pf(trunc(-3.9)); printf("\n");
}

static void test_power(void) {
    printf("\n=== Test 2: Power & Root ===\n");
    printf("  sqrt(2) = "); pf(sqrt(2.0)); printf("\n");
    printf("  sqrt(144) = "); pf(sqrt(144.0)); printf("\n");
    printf("  cbrt(27) = "); pf(cbrt(27.0)); printf("\n");
    printf("  pow(2, 10) = "); pf(pow(2.0, 10.0)); printf("\n");
    printf("  pow(3, 3) = "); pf(pow(3.0, 3.0)); printf("\n");
    printf("  pow(2, 0.5) = "); pf(pow(2.0, 0.5)); printf("\n");
    printf("  hypot(3, 4) = "); pf(hypot(3.0, 4.0)); printf("\n");
}

static void test_exp_log(void) {
    printf("\n=== Test 3: Exp & Log ===\n");
    printf("  exp(0) = "); pf(exp(0.0)); printf("\n");
    printf("  exp(1) = "); pf(exp(1.0)); printf(" (e=2.7183)\n");
    printf("  exp(2) = "); pf(exp(2.0)); printf("\n");
    printf("  log(1) = "); pf(log(1.0)); printf("\n");
    printf("  log(e) = "); pf(log(M_E)); printf(" (expect 1)\n");
    printf("  log2(8) = "); pf(log2(8.0)); printf("\n");
    printf("  log2(1024) = "); pf(log2(1024.0)); printf("\n");
    printf("  log10(100) = "); pf(log10(100.0)); printf("\n");
    printf("  log10(1000) = "); pf(log10(1000.0)); printf("\n");
    printf("  exp(log(42)) = "); pf(exp(log(42.0))); printf(" (expect 42)\n");
}

static void test_trig(void) {
    printf("\n=== Test 4: Trigonometry ===\n");
    printf("  sin(0) = "); pf(sin(0.0)); printf("\n");
    printf("  sin(pi/6) = "); pf(sin(M_PI / 6.0)); printf(" (expect 0.5)\n");
    printf("  sin(pi/2) = "); pf(sin(M_PI_2)); printf(" (expect 1)\n");
    printf("  cos(0) = "); pf(cos(0.0)); printf(" (expect 1)\n");
    printf("  cos(pi/3) = "); pf(cos(M_PI / 3.0)); printf(" (expect 0.5)\n");
    printf("  cos(pi) = "); pf(cos(M_PI)); printf(" (expect -1)\n");
    printf("  tan(pi/4) = "); pf(tan(M_PI_4)); printf(" (expect 1)\n");
    printf("  atan(1) = "); pf(atan(1.0)); printf(" (expect pi/4=0.7854)\n");
    printf("  atan2(1,1) = "); pf(atan2(1.0, 1.0)); printf("\n");
    printf("  asin(0.5) = "); pf(asin(0.5)); printf(" (expect pi/6=0.5236)\n");
    printf("  acos(0.5) = "); pf(acos(0.5)); printf(" (expect pi/3=1.0472)\n");

    /* Verify identity: sin^2 + cos^2 = 1 */
    double x = 1.234;
    double s = sin(x);
    double c = cos(x);
    double identity = s * s + c * c;
    printf("  sin^2(1.234)+cos^2(1.234) = "); pf(identity); printf(" (expect 1)\n");
}

static void test_hyperbolic(void) {
    printf("\n=== Test 5: Hyperbolic (for Neural Nets) ===\n");
    printf("  sinh(0) = "); pf(sinh(0.0)); printf("\n");
    printf("  sinh(1) = "); pf(sinh(1.0)); printf(" (expect 1.1752)\n");
    printf("  cosh(0) = "); pf(cosh(0.0)); printf(" (expect 1)\n");
    printf("  cosh(1) = "); pf(cosh(1.0)); printf(" (expect 1.5431)\n");
    printf("  tanh(0) = "); pf(tanh(0.0)); printf(" (expect 0)\n");
    printf("  tanh(1) = "); pf(tanh(1.0)); printf(" (expect 0.7616)\n");
    printf("  tanh(-1) = "); pf(tanh(-1.0)); printf(" (expect -0.7616)\n");
    printf("  tanh(100) = "); pf(tanh(100.0)); printf(" (expect 1, saturated)\n");

    /* Neural network activation functions */
    printf("\n  -- Sigmoid: 1/(1+exp(-x)) --\n");
    double inputs[] = {-3.0, -1.0, 0.0, 1.0, 3.0};
    for (int i = 0; i < 5; i++) {
        double sigmoid = 1.0 / (1.0 + exp(-inputs[i]));
        printf("    sigmoid("); pf(inputs[i]); printf(") = "); pf(sigmoid); printf("\n");
    }

    printf("\n  -- ReLU: max(0, x) --\n");
    for (int i = 0; i < 5; i++) {
        double relu = fmax(0.0, inputs[i]);
        printf("    relu("); pf(inputs[i]); printf(") = "); pf(relu); printf("\n");
    }
}

static void test_misc(void) {
    printf("\n=== Test 6: Misc ===\n");
    printf("  fmin(3,5) = "); pf(fmin(3.0, 5.0)); printf("\n");
    printf("  fmax(3,5) = "); pf(fmax(3.0, 5.0)); printf("\n");
    printf("  atof(\"3.14\") = "); pf(atof("3.14")); printf("\n");
    printf("  atof(\"-2.5e2\") = "); pf(atof("-2.5e2")); printf(" (expect -250)\n");

    /* Constants */
    printf("  M_PI = "); pf(M_PI); printf("\n");
    printf("  M_E = "); pf(M_E); printf("\n");
    printf("  M_SQRT2 = "); pf(M_SQRT2); printf("\n");
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    printf("===== PlantOS Math Library Demo =====\n\n");
    test_basic();
    test_power();
    test_exp_log();
    test_trig();
    test_hyperbolic();
    test_misc();
    printf("\n===== Math Demo Complete =====\n");
    return 0;
}
