#ifndef USER_LIBC_MATRIX_H
#define USER_LIBC_MATRIX_H

#include <plantos/types.h>

/* Heap-allocated matrix (row-major) */
typedef struct {
    int     rows;
    int     cols;
    double *data;
} Mat;

/* Allocate/free */
Mat   *mat_alloc(int rows, int cols);
void   mat_free(Mat *m);

/* Initialize */
void   mat_zero(Mat *m);
void   mat_fill(Mat *m, double val);
void   mat_copy(Mat *dst, const Mat *src);

/* Element access */
static inline double mat_get(const Mat *m, int r, int c) {
    return m->data[r * m->cols + c];
}

static inline void mat_set(Mat *m, int r, int c, double v) {
    m->data[r * m->cols + c] = v;
}

/* C = A * B */
void mat_mul(const Mat *A, const Mat *B, Mat *C);

/* C = A^T * B */
void mat_mul_at(const Mat *A, const Mat *B, Mat *C);

/* C = A * B^T */
void mat_mul_bt(const Mat *A, const Mat *B, Mat *C);

/* Element-wise: C = A .* B */
void mat_hadamard(const Mat *A, const Mat *B, Mat *C);

/* C = A + B (element-wise) */
void mat_add(const Mat *A, const Mat *B, Mat *C);

/* A += B (in-place) */
void mat_add_inplace(Mat *A, const Mat *B);

/* Scale: A *= scalar (in-place) */
void mat_scale(Mat *m, double s);

/* Add column vector b to every column of A: A[i][j] += b[i] */
void mat_add_bias(Mat *A, const Mat *b);

/* Sum all columns: out[i] = sum_j(A[i][j]), out is (rows x 1) */
void mat_sum_cols(const Mat *A, Mat *out);

/* Random fill in [-scale, +scale] using xorshift */
void mat_random(Mat *m, double scale, uint32_t *rng);

/* Print matrix (for debugging) */
void mat_print(const char *name, const Mat *m);

#endif
