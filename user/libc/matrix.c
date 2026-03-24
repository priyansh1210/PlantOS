#include "user/libc/matrix.h"
#include "user/libc/ulibc.h"

Mat *mat_alloc(int rows, int cols) {
    Mat *m = (Mat *)malloc(sizeof(Mat));
    if (!m) return NULL;
    m->rows = rows;
    m->cols = cols;
    m->data = (double *)malloc((size_t)(rows * cols) * sizeof(double));
    if (!m->data) { free(m); return NULL; }
    for (int i = 0; i < rows * cols; i++)
        m->data[i] = 0.0;
    return m;
}

void mat_free(Mat *m) {
    if (!m) return;
    if (m->data) free(m->data);
    free(m);
}

void mat_zero(Mat *m) {
    for (int i = 0; i < m->rows * m->cols; i++)
        m->data[i] = 0.0;
}

void mat_fill(Mat *m, double val) {
    for (int i = 0; i < m->rows * m->cols; i++)
        m->data[i] = val;
}

void mat_copy(Mat *dst, const Mat *src) {
    int n = src->rows * src->cols;
    dst->rows = src->rows;
    dst->cols = src->cols;
    for (int i = 0; i < n; i++)
        dst->data[i] = src->data[i];
}

void mat_mul(const Mat *A, const Mat *B, Mat *C) {
    for (int i = 0; i < A->rows; i++)
        for (int j = 0; j < B->cols; j++) {
            double sum = 0.0;
            for (int k = 0; k < A->cols; k++)
                sum += A->data[i * A->cols + k] * B->data[k * B->cols + j];
            C->data[i * C->cols + j] = sum;
        }
}

void mat_mul_at(const Mat *A, const Mat *B, Mat *C) {
    /* C = A^T * B: C is (A->cols x B->cols) */
    for (int i = 0; i < A->cols; i++)
        for (int j = 0; j < B->cols; j++) {
            double sum = 0.0;
            for (int k = 0; k < A->rows; k++)
                sum += A->data[k * A->cols + i] * B->data[k * B->cols + j];
            C->data[i * C->cols + j] = sum;
        }
}

void mat_mul_bt(const Mat *A, const Mat *B, Mat *C) {
    /* C = A * B^T: C is (A->rows x B->rows) */
    for (int i = 0; i < A->rows; i++)
        for (int j = 0; j < B->rows; j++) {
            double sum = 0.0;
            for (int k = 0; k < A->cols; k++)
                sum += A->data[i * A->cols + k] * B->data[j * B->cols + k];
            C->data[i * C->cols + j] = sum;
        }
}

void mat_hadamard(const Mat *A, const Mat *B, Mat *C) {
    int n = A->rows * A->cols;
    for (int i = 0; i < n; i++)
        C->data[i] = A->data[i] * B->data[i];
}

void mat_add(const Mat *A, const Mat *B, Mat *C) {
    int n = A->rows * A->cols;
    for (int i = 0; i < n; i++)
        C->data[i] = A->data[i] + B->data[i];
}

void mat_add_inplace(Mat *A, const Mat *B) {
    int n = A->rows * A->cols;
    for (int i = 0; i < n; i++)
        A->data[i] += B->data[i];
}

void mat_scale(Mat *m, double s) {
    int n = m->rows * m->cols;
    for (int i = 0; i < n; i++)
        m->data[i] *= s;
}

void mat_add_bias(Mat *A, const Mat *b) {
    /* A is (rows x cols), b is (rows x 1). Add b to each column. */
    for (int j = 0; j < A->cols; j++)
        for (int i = 0; i < A->rows; i++)
            A->data[i * A->cols + j] += b->data[i];
}

void mat_sum_cols(const Mat *A, Mat *out) {
    /* out[i] = sum_j A[i][j] */
    for (int i = 0; i < A->rows; i++) {
        double s = 0.0;
        for (int j = 0; j < A->cols; j++)
            s += A->data[i * A->cols + j];
        out->data[i] = s;
    }
}

static uint32_t xorshift32(uint32_t *state) {
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

void mat_random(Mat *m, double scale, uint32_t *rng) {
    int n = m->rows * m->cols;
    for (int i = 0; i < n; i++) {
        double r = ((double)(xorshift32(rng) % 20000) / 10000.0) - 1.0;
        m->data[i] = r * scale;
    }
}

static void print_float(double v, int decimals) {
    if (v < 0.0) { printf("-"); v = -v; }
    int whole = (int)v;
    double frac = v - (double)whole;
    printf("%d.", whole);
    for (int d = 0; d < decimals; d++) {
        frac *= 10.0;
        int digit = (int)frac;
        printf("%d", digit);
        frac -= (double)digit;
    }
}

void mat_print(const char *name, const Mat *m) {
    printf("%s [%dx%d]:\n", name, m->rows, m->cols);
    for (int i = 0; i < m->rows; i++) {
        printf("  ");
        for (int j = 0; j < m->cols; j++) {
            double v = m->data[i * m->cols + j];
            if (v >= 0.0) printf(" ");
            print_float(v, 4);
            printf(" ");
        }
        printf("\n");
    }
}
