/*
 * umath.c — PlantOS math library
 *
 * Uses x87 FPU hardware instructions where available for accuracy,
 * with software fallbacks for derived functions.
 *
 * x87 instructions used:
 *   FSQRT, FABS, FSIN, FCOS, FPTAN, FPATAN, F2XM1, FYL2X,
 *   FRNDINT, FSCALE, FLDPI, FLDL2E, FLDLN2, FLDLG2
 */

#include <plantos/types.h>
#include "user/libc/umath.h"

/* ---- Classification ---- */

int isnan(double x) {
    return __builtin_isnan(x);
}

int isinf(double x) {
    return __builtin_isinf(x);
}

int isfinite(double x) {
    return __builtin_isfinite(x);
}

/* ---- Basic operations (x87) ---- */

double fabs(double x) {
    double result;
    __asm__ ("fldl %1; fabs; fstpl %0" : "=m"(result) : "m"(x));
    return result;
}

double fmod(double x, double y) {
    double result;
    __asm__ (
        "fldl %2;"    /* st0 = y */
        "fldl %1;"    /* st0 = x, st1 = y */
        "1: fprem;"
        "fstsw %%ax;"
        "test $0x400, %%ax;"
        "jnz 1b;"
        "fstpl %0;"   /* result = st0 */
        "fstp %%st(0);" /* pop y */
        : "=m"(result)
        : "m"(x), "m"(y)
        : "ax"
    );
    return result;
}

double remainder(double x, double y) {
    double result;
    __asm__ (
        "fldl %2;"
        "fldl %1;"
        "1: fprem1;"
        "fstsw %%ax;"
        "test $0x400, %%ax;"
        "jnz 1b;"
        "fstpl %0;"
        "fstp %%st(0);"
        : "=m"(result)
        : "m"(x), "m"(y)
        : "ax"
    );
    return result;
}

/* ---- Rounding (x87) ---- */

double floor(double x) {
    double result;
    short cw_old, cw_new;
    __asm__ (
        "fnstcw %1;"                /* Save control word */
        "movw %1, %%ax;"
        "andw $0xF3FF, %%ax;"       /* Clear rounding bits */
        "orw  $0x0400, %%ax;"       /* Set round-down mode */
        "movw %%ax, %2;"
        "fldcw %2;"
        "fldl %3;"
        "frndint;"
        "fstpl %0;"
        "fldcw %1;"                 /* Restore control word */
        : "=m"(result), "=m"(cw_old), "=m"(cw_new)
        : "m"(x)
        : "ax"
    );
    return result;
}

double ceil(double x) {
    double result;
    short cw_old, cw_new;
    __asm__ (
        "fnstcw %1;"
        "movw %1, %%ax;"
        "andw $0xF3FF, %%ax;"
        "orw  $0x0800, %%ax;"       /* Set round-up mode */
        "movw %%ax, %2;"
        "fldcw %2;"
        "fldl %3;"
        "frndint;"
        "fstpl %0;"
        "fldcw %1;"
        : "=m"(result), "=m"(cw_old), "=m"(cw_new)
        : "m"(x)
        : "ax"
    );
    return result;
}

double trunc(double x) {
    double result;
    short cw_old, cw_new;
    __asm__ (
        "fnstcw %1;"
        "movw %1, %%ax;"
        "andw $0xF3FF, %%ax;"
        "orw  $0x0C00, %%ax;"       /* Set round-toward-zero mode */
        "movw %%ax, %2;"
        "fldcw %2;"
        "fldl %3;"
        "frndint;"
        "fstpl %0;"
        "fldcw %1;"
        : "=m"(result), "=m"(cw_old), "=m"(cw_new)
        : "m"(x)
        : "ax"
    );
    return result;
}

double round(double x) {
    if (x >= 0.0)
        return floor(x + 0.5);
    else
        return ceil(x - 0.5);
}

/* ---- Power and root (x87) ---- */

double sqrt(double x) {
    if (x < 0.0) return NAN;
    double result;
    __asm__ ("fldl %1; fsqrt; fstpl %0" : "=m"(result) : "m"(x));
    return result;
}

double cbrt(double x) {
    if (x == 0.0) return 0.0;
    int neg = (x < 0.0);
    if (neg) x = -x;
    double result = pow(x, 1.0 / 3.0);
    return neg ? -result : result;
}

double hypot(double x, double y) {
    return sqrt(x * x + y * y);
}

/* ---- Exponential and logarithmic (x87) ---- */

/*
 * exp(x) implementation using x87:
 *   exp(x) = 2^(x * log2(e))
 *   Split into integer and fractional parts for 2^n * 2^f
 *   where f is in [-1,1] and we use f2xm1 for 2^f - 1
 */
double exp(double x) {
    if (x > 709.0) return INFINITY;
    if (x < -709.0) return 0.0;

    double result;
    __asm__ (
        "fldl  %1;"          /* st0 = x */
        "fldl2e;"            /* st0 = log2(e), st1 = x */
        "fmulp;"             /* st0 = x * log2(e) */
        "fld   %%st(0);"     /* duplicate */
        "frndint;"           /* st0 = int(x*log2e), st1 = x*log2e */
        "fsub  %%st(0), %%st(1);" /* st1 = frac part */
        "fxch;"              /* st0 = frac, st1 = int */
        "f2xm1;"             /* st0 = 2^frac - 1 */
        "fld1;"              /* st0 = 1, st1 = 2^frac-1, st2 = int */
        "faddp;"             /* st0 = 2^frac, st1 = int */
        "fscale;"            /* st0 = 2^frac * 2^int = 2^(x*log2e) = exp(x) */
        "fstp  %%st(1);"     /* clean up */
        "fstpl %0;"
        : "=m"(result)
        : "m"(x)
    );
    return result;
}

double exp2(double x) {
    if (x > 1023.0) return INFINITY;
    if (x < -1074.0) return 0.0;

    double result;
    __asm__ (
        "fldl  %1;"
        "fld   %%st(0);"
        "frndint;"
        "fsub  %%st(0), %%st(1);"
        "fxch;"
        "f2xm1;"
        "fld1;"
        "faddp;"
        "fscale;"
        "fstp  %%st(1);"
        "fstpl %0;"
        : "=m"(result)
        : "m"(x)
    );
    return result;
}

double log(double x) {
    if (x <= 0.0) return (x == 0.0) ? -INFINITY : NAN;

    double result;
    __asm__ (
        "fldln2;"            /* st0 = ln(2) */
        "fldl  %1;"          /* st0 = x, st1 = ln(2) */
        "fyl2x;"             /* st0 = ln(2) * log2(x) = ln(x) */
        "fstpl %0;"
        : "=m"(result)
        : "m"(x)
    );
    return result;
}

double log2(double x) {
    if (x <= 0.0) return (x == 0.0) ? -INFINITY : NAN;

    double result;
    __asm__ (
        "fld1;"              /* st0 = 1.0 */
        "fldl  %1;"          /* st0 = x, st1 = 1.0 */
        "fyl2x;"             /* st0 = 1.0 * log2(x) = log2(x) */
        "fstpl %0;"
        : "=m"(result)
        : "m"(x)
    );
    return result;
}

double log10(double x) {
    if (x <= 0.0) return (x == 0.0) ? -INFINITY : NAN;

    double result;
    __asm__ (
        "fldlg2;"            /* st0 = log10(2) */
        "fldl  %1;"          /* st0 = x, st1 = log10(2) */
        "fyl2x;"             /* st0 = log10(2) * log2(x) = log10(x) */
        "fstpl %0;"
        : "=m"(result)
        : "m"(x)
    );
    return result;
}

double log1p(double x) {
    /* For small x, use fyl2xp1 for better precision */
    if (x > -0.5 && x < 0.5) {
        double result;
        __asm__ (
            "fldln2;"
            "fldl  %1;"
            "fyl2xp1;"       /* st0 = ln(2) * log2(1+x) = ln(1+x) */
            "fstpl %0;"
            : "=m"(result)
            : "m"(x)
        );
        return result;
    }
    return log(1.0 + x);
}

double expm1(double x) {
    /* For small x, exp(x)-1 has better precision via series */
    if (x > -0.5 && x < 0.5) {
        /* Taylor series: x + x^2/2 + x^3/6 + x^4/24 + ... */
        double term = x;
        double sum = x;
        for (int i = 2; i <= 12; i++) {
            term *= x / (double)i;
            sum += term;
        }
        return sum;
    }
    return exp(x) - 1.0;
}

/*
 * pow(base, exponent) = exp(exponent * ln(base))
 * Special cases handled separately.
 */
double pow(double base, double exponent) {
    if (exponent == 0.0) return 1.0;
    if (base == 0.0) return 0.0;
    if (base == 1.0) return 1.0;
    if (exponent == 1.0) return base;
    if (exponent == 2.0) return base * base;

    /* Integer exponent — use repeated squaring */
    if (exponent == (double)(int)exponent && exponent > 0 && exponent < 64) {
        int n = (int)exponent;
        double result = 1.0;
        double b = base;
        while (n > 0) {
            if (n & 1) result *= b;
            b *= b;
            n >>= 1;
        }
        return result;
    }

    /* Negative base with non-integer exponent is NaN */
    if (base < 0.0) {
        if (exponent != (double)(int)exponent) return NAN;
        double result = exp(exponent * log(-base));
        return ((int)exponent & 1) ? -result : result;
    }

    return exp(exponent * log(base));
}

/* ---- Trigonometric (x87) ---- */

/*
 * x87 FSIN/FCOS require |x| < 2^63.
 * We reduce to [-pi, pi] range for large values.
 */
static double reduce_angle(double x) {
    if (x >= -M_PI && x <= M_PI) return x;
    /* Use fmod to reduce */
    double r = fmod(x, 2.0 * M_PI);
    if (r > M_PI) r -= 2.0 * M_PI;
    if (r < -M_PI) r += 2.0 * M_PI;
    return r;
}

double sin(double x) {
    x = reduce_angle(x);
    double result;
    __asm__ ("fldl %1; fsin; fstpl %0" : "=m"(result) : "m"(x));
    return result;
}

double cos(double x) {
    x = reduce_angle(x);
    double result;
    __asm__ ("fldl %1; fcos; fstpl %0" : "=m"(result) : "m"(x));
    return result;
}

double tan(double x) {
    x = reduce_angle(x);
    double result;
    __asm__ (
        "fldl  %1;"
        "fptan;"             /* st0 = 1.0, st1 = tan(x) */
        "fstp  %%st(0);"     /* pop the 1.0 */
        "fstpl %0;"
        : "=m"(result)
        : "m"(x)
    );
    return result;
}

double atan(double x) {
    double result;
    __asm__ (
        "fldl  %1;"          /* st0 = x */
        "fld1;"              /* st0 = 1.0, st1 = x */
        "fpatan;"            /* st0 = atan(st1/st0) = atan(x) */
        "fstpl %0;"
        : "=m"(result)
        : "m"(x)
    );
    return result;
}

double atan2(double y, double x) {
    double result;
    __asm__ (
        "fldl  %1;"          /* st0 = y */
        "fldl  %2;"          /* st0 = x, st1 = y */
        "fxch;"              /* st0 = y, st1 = x */
        "fpatan;"            /* st0 = atan2(y, x) */
        "fstpl %0;"
        : "=m"(result)
        : "m"(y), "m"(x)
    );
    return result;
}

double asin(double x) {
    if (x < -1.0 || x > 1.0) return NAN;
    if (x == 1.0) return M_PI_2;
    if (x == -1.0) return -M_PI_2;
    /* asin(x) = atan(x / sqrt(1 - x*x)) */
    return atan(x / sqrt(1.0 - x * x));
}

double acos(double x) {
    if (x < -1.0 || x > 1.0) return NAN;
    /* acos(x) = pi/2 - asin(x) */
    return M_PI_2 - asin(x);
}

/* ---- Hyperbolic functions ---- */

double sinh(double x) {
    if (fabs(x) < 1e-10) return x;  /* Small x optimization */
    double ex = exp(x);
    return (ex - 1.0 / ex) / 2.0;
}

double cosh(double x) {
    double ex = exp(x);
    return (ex + 1.0 / ex) / 2.0;
}

double tanh(double x) {
    if (x > 20.0) return 1.0;       /* Saturates to 1 */
    if (x < -20.0) return -1.0;     /* Saturates to -1 */
    double e2x = exp(2.0 * x);
    return (e2x - 1.0) / (e2x + 1.0);
}

/* ---- Min/max ---- */

double fmin(double x, double y) {
    if (isnan(x)) return y;
    if (isnan(y)) return x;
    return (x < y) ? x : y;
}

double fmax(double x, double y) {
    if (isnan(x)) return y;
    if (isnan(y)) return x;
    return (x > y) ? x : y;
}

/* ---- Decomposition ---- */

double modf(double x, double *iptr) {
    double i = trunc(x);
    if (iptr) *iptr = i;
    return x - i;
}

double frexp(double x, int *exp) {
    if (x == 0.0) { *exp = 0; return 0.0; }

    /* Extract exponent from IEEE 754 representation */
    union { double d; uint64_t u; } u;
    u.d = x;
    int e = (int)((u.u >> 52) & 0x7FF) - 1022;
    *exp = e;
    /* Set exponent to -1 (biased: 1022) to get mantissa in [0.5, 1) */
    u.u = (u.u & 0x800FFFFFFFFFFFFFULL) | 0x3FE0000000000000ULL;
    return u.d;
}

double ldexp(double x, int exp) {
    double scale;
    union { double d; uint64_t u; } u;
    u.u = (uint64_t)(exp + 1023) << 52;
    scale = u.d;
    return x * scale;
}

/* ---- String to double ---- */

double atof(const char *s) {
    double result = 0.0;
    double sign = 1.0;
    int i = 0;

    /* Skip whitespace */
    while (s[i] == ' ' || s[i] == '\t') i++;

    /* Sign */
    if (s[i] == '-') { sign = -1.0; i++; }
    else if (s[i] == '+') { i++; }

    /* Integer part */
    while (s[i] >= '0' && s[i] <= '9') {
        result = result * 10.0 + (s[i] - '0');
        i++;
    }

    /* Fractional part */
    if (s[i] == '.') {
        i++;
        double frac = 0.1;
        while (s[i] >= '0' && s[i] <= '9') {
            result += (s[i] - '0') * frac;
            frac *= 0.1;
            i++;
        }
    }

    /* Exponent part (e.g., 1.5e10) */
    if (s[i] == 'e' || s[i] == 'E') {
        i++;
        double esign = 1.0;
        if (s[i] == '-') { esign = -1.0; i++; }
        else if (s[i] == '+') { i++; }
        int exp = 0;
        while (s[i] >= '0' && s[i] <= '9') {
            exp = exp * 10 + (s[i] - '0');
            i++;
        }
        result *= pow(10.0, esign * exp);
    }

    return sign * result;
}
