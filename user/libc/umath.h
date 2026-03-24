#ifndef USER_LIBC_UMATH_H
#define USER_LIBC_UMATH_H

/* Mathematical constants */
#define M_PI        3.14159265358979323846
#define M_PI_2      1.57079632679489661923
#define M_PI_4      0.78539816339744830962
#define M_E         2.71828182845904523536
#define M_LN2       0.69314718055994530942
#define M_LN10      2.30258509299404568402
#define M_LOG2E     1.44269504088896340736
#define M_LOG10E    0.43429448190325182765
#define M_SQRT2     1.41421356237309504880
#define M_1_PI      0.31830988618379067154
#define M_2_PI      0.63661977236758134308

/* Special values */
#define INFINITY    (__builtin_inf())
#define NAN         (__builtin_nan(""))
#define HUGE_VAL    (__builtin_huge_val())

/* Classification */
int isnan(double x);
int isinf(double x);
int isfinite(double x);

/* Basic operations */
double fabs(double x);
double fmod(double x, double y);
double remainder(double x, double y);

/* Rounding */
double ceil(double x);
double floor(double x);
double round(double x);
double trunc(double x);

/* Power and root */
double sqrt(double x);
double cbrt(double x);
double pow(double base, double exponent);
double hypot(double x, double y);

/* Exponential and logarithmic */
double exp(double x);
double exp2(double x);
double log(double x);
double log2(double x);
double log10(double x);
double log1p(double x);
double expm1(double x);

/* Trigonometric */
double sin(double x);
double cos(double x);
double tan(double x);
double asin(double x);
double acos(double x);
double atan(double x);
double atan2(double y, double x);

/* Hyperbolic (important for neural networks) */
double sinh(double x);
double cosh(double x);
double tanh(double x);

/* Min/max */
double fmin(double x, double y);
double fmax(double x, double y);

/* Decomposition */
double frexp(double x, int *exp);
double ldexp(double x, int exp);
double modf(double x, double *iptr);

/* Conversion */
double atof(const char *s);

#endif
