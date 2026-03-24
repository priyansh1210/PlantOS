#include "user/libc/nn.h"
#include "user/libc/ulibc.h"
#include "user/libc/umath.h"

/* ---- Activation functions ---- */

static void act_forward(nn_activation_t act, const Mat *z, Mat *a) {
    int n = z->rows * z->cols;
    switch (act) {
    case ACT_SIGMOID:
        for (int i = 0; i < n; i++)
            a->data[i] = 1.0 / (1.0 + exp(-z->data[i]));
        break;
    case ACT_TANH:
        for (int i = 0; i < n; i++)
            a->data[i] = tanh(z->data[i]);
        break;
    case ACT_RELU:
        for (int i = 0; i < n; i++)
            a->data[i] = z->data[i] > 0.0 ? z->data[i] : 0.0;
        break;
    case ACT_SOFTMAX: {
        /* Per-column softmax with max subtraction for stability */
        int rows = z->rows;
        int cols = z->cols;
        for (int j = 0; j < cols; j++) {
            double mx = z->data[0 * cols + j];
            for (int i = 1; i < rows; i++) {
                double v = z->data[i * cols + j];
                if (v > mx) mx = v;
            }
            double sum = 0.0;
            for (int i = 0; i < rows; i++) {
                double e = exp(z->data[i * cols + j] - mx);
                a->data[i * cols + j] = e;
                sum += e;
            }
            for (int i = 0; i < rows; i++)
                a->data[i * cols + j] /= sum;
        }
        break;
    }
    case ACT_NONE:
    default:
        for (int i = 0; i < n; i++)
            a->data[i] = z->data[i];
        break;
    }
}

/* Multiply delta by activation derivative in-place: delta[i] *= f'(z[i]) or f'(a[i]) */
static void act_backward_inplace(nn_activation_t act, const Mat *a, const Mat *z, Mat *delta) {
    int n = a->rows * a->cols;
    switch (act) {
    case ACT_SIGMOID:
        for (int i = 0; i < n; i++)
            delta->data[i] *= a->data[i] * (1.0 - a->data[i]);
        break;
    case ACT_TANH:
        for (int i = 0; i < n; i++)
            delta->data[i] *= (1.0 - a->data[i] * a->data[i]);
        break;
    case ACT_RELU:
        for (int i = 0; i < n; i++)
            delta->data[i] *= (z->data[i] > 0.0) ? 1.0 : 0.0;
        break;
    case ACT_SOFTMAX:
        /* Handled specially in backward pass (combined with cross-entropy) */
        break;
    case ACT_NONE:
    default:
        break;
    }
}

/* ---- Network lifecycle ---- */

void nn_init(nn_net_t *net, nn_loss_t loss, double lr, double momentum, uint32_t seed) {
    memset(net, 0, sizeof(nn_net_t));
    net->loss_fn = loss;
    net->learning_rate = lr;
    net->momentum = momentum;
    net->rng_state = seed;
}

int nn_add_dense(nn_net_t *net, int in_size, int out_size, nn_activation_t act) {
    if (net->num_layers >= NN_MAX_LAYERS) return -1;

    /* Infer input size from previous layer */
    if (in_size <= 0 && net->num_layers > 0)
        in_size = net->layers[net->num_layers - 1].out_size;
    if (in_size <= 0) return -1;

    nn_layer_t *l = &net->layers[net->num_layers];
    memset(l, 0, sizeof(nn_layer_t));
    l->in_size = in_size;
    l->out_size = out_size;
    l->activation = act;
    net->num_layers++;
    return 0;
}

int nn_build(nn_net_t *net, int batch_size) {
    net->batch_size = batch_size;

    /* Find max layer size for scratch allocation */
    int max_size = 0;
    for (int i = 0; i < net->num_layers; i++) {
        if (net->layers[i].in_size > max_size)
            max_size = net->layers[i].in_size;
        if (net->layers[i].out_size > max_size)
            max_size = net->layers[i].out_size;
    }

    net->delta = mat_alloc(max_size, batch_size);
    if (!net->delta) goto fail;

    for (int i = 0; i < net->num_layers; i++) {
        nn_layer_t *l = &net->layers[i];

        l->W  = mat_alloc(l->out_size, l->in_size);
        l->b  = mat_alloc(l->out_size, 1);
        l->z  = mat_alloc(l->out_size, batch_size);
        l->a  = mat_alloc(l->out_size, batch_size);
        l->dW = mat_alloc(l->out_size, l->in_size);
        l->db = mat_alloc(l->out_size, 1);
        l->vW = mat_alloc(l->out_size, l->in_size);
        l->vb = mat_alloc(l->out_size, 1);

        if (!l->W || !l->b || !l->z || !l->a ||
            !l->dW || !l->db || !l->vW || !l->vb)
            goto fail;

        /* Xavier initialization: scale = 1/sqrt(fan_in) */
        double scale = 1.0 / sqrt((double)l->in_size);
        mat_random(l->W, scale, &net->rng_state);
    }

    return 0;

fail:
    nn_free(net);
    return -1;
}

void nn_free(nn_net_t *net) {
    for (int i = 0; i < net->num_layers; i++) {
        nn_layer_t *l = &net->layers[i];
        mat_free(l->W);  l->W = NULL;
        mat_free(l->b);  l->b = NULL;
        mat_free(l->z);  l->z = NULL;
        mat_free(l->a);  l->a = NULL;
        mat_free(l->dW); l->dW = NULL;
        mat_free(l->db); l->db = NULL;
        mat_free(l->vW); l->vW = NULL;
        mat_free(l->vb); l->vb = NULL;
    }
    mat_free(net->delta);
    net->delta = NULL;
}

/* ---- Forward pass ---- */

Mat *nn_forward(nn_net_t *net, const Mat *input, int batch_size) {
    const Mat *prev = input;

    for (int i = 0; i < net->num_layers; i++) {
        nn_layer_t *l = &net->layers[i];

        /* Adjust z/a cols for actual batch size */
        l->z->rows = l->out_size;
        l->z->cols = batch_size;
        l->a->rows = l->out_size;
        l->a->cols = batch_size;

        /* z = W * prev + b */
        mat_mul(l->W, prev, l->z);
        mat_add_bias(l->z, l->b);

        /* a = activation(z) */
        act_forward(l->activation, l->z, l->a);

        prev = l->a;
    }

    return net->layers[net->num_layers - 1].a;
}

/* ---- Loss ---- */

double nn_loss(nn_net_t *net, const Mat *output, const Mat *target, int batch_size) {
    int n = output->rows * batch_size;
    double loss = 0.0;

    switch (net->loss_fn) {
    case LOSS_MSE:
        for (int i = 0; i < n; i++) {
            double e = output->data[i] - target->data[i];
            loss += e * e;
        }
        loss /= (double)n;
        break;
    case LOSS_CROSS_ENTROPY: {
        double eps = 1e-7;
        for (int i = 0; i < n; i++) {
            double o = output->data[i];
            double t = target->data[i];
            /* Clamp to avoid log(0) */
            if (o < eps) o = eps;
            if (o > 1.0 - eps) o = 1.0 - eps;
            loss -= t * log(o) + (1.0 - t) * log(1.0 - o);
        }
        loss /= (double)batch_size;
        break;
    }
    }
    return loss;
}

/* ---- Backpropagation ---- */

void nn_backward(nn_net_t *net, const Mat *input, const Mat *target, int batch_size) {
    nn_layer_t *out_layer = &net->layers[net->num_layers - 1];
    Mat *delta = net->delta;
    delta->rows = out_layer->out_size;
    delta->cols = batch_size;

    /* Compute output delta based on loss + activation combination */
    int n = out_layer->out_size * batch_size;

    if (net->loss_fn == LOSS_CROSS_ENTROPY && out_layer->activation == ACT_SIGMOID) {
        /* Simplified gradient: delta = output - target */
        for (int i = 0; i < n; i++)
            delta->data[i] = out_layer->a->data[i] - target->data[i];
    } else if (net->loss_fn == LOSS_CROSS_ENTROPY && out_layer->activation == ACT_SOFTMAX) {
        /* Simplified gradient: delta = output - target */
        for (int i = 0; i < n; i++)
            delta->data[i] = out_layer->a->data[i] - target->data[i];
    } else {
        /* MSE gradient: delta = 2*(output - target) / n, then multiply by activation derivative */
        for (int i = 0; i < n; i++)
            delta->data[i] = 2.0 * (out_layer->a->data[i] - target->data[i]) / (double)n;
        act_backward_inplace(out_layer->activation, out_layer->a, out_layer->z, delta);
    }

    /* Backprop through layers */
    for (int i = net->num_layers - 1; i >= 0; i--) {
        nn_layer_t *l = &net->layers[i];
        const Mat *prev_a = (i > 0) ? net->layers[i - 1].a : input;

        /* dW = delta * prev_a^T */
        mat_mul_bt(delta, prev_a, l->dW);

        /* db = sum of delta across batch (columns) */
        mat_sum_cols(delta, l->db);

        /* Propagate delta to previous layer (if not first) */
        if (i > 0) {
            nn_layer_t *prev = &net->layers[i - 1];
            /* new_delta = W^T * delta */
            Mat *new_delta = mat_alloc(l->in_size, batch_size);
            if (new_delta) {
                mat_mul_at(l->W, delta, new_delta);
                /* Multiply by activation derivative of previous layer */
                act_backward_inplace(prev->activation, prev->a, prev->z, new_delta);
                /* Copy to delta for next iteration */
                delta->rows = new_delta->rows;
                delta->cols = new_delta->cols;
                int sz = new_delta->rows * new_delta->cols;
                for (int k = 0; k < sz; k++)
                    delta->data[k] = new_delta->data[k];
                mat_free(new_delta);
            }
        }
    }
}

/* ---- Weight update ---- */

void nn_update(nn_net_t *net, int batch_size) {
    double lr = net->learning_rate;
    double mom = net->momentum;
    double scale = 1.0 / (double)batch_size;

    for (int i = 0; i < net->num_layers; i++) {
        nn_layer_t *l = &net->layers[i];
        int wn = l->out_size * l->in_size;
        int bn = l->out_size;

        for (int k = 0; k < wn; k++) {
            l->vW->data[k] = mom * l->vW->data[k] + lr * scale * l->dW->data[k];
            l->W->data[k] -= l->vW->data[k];
        }
        for (int k = 0; k < bn; k++) {
            l->vb->data[k] = mom * l->vb->data[k] + lr * scale * l->db->data[k];
            l->b->data[k] -= l->vb->data[k];
        }
    }
}

/* ---- Convenience ---- */

double nn_train_step(nn_net_t *net, const Mat *input, const Mat *target, int batch_size) {
    Mat *output = nn_forward(net, input, batch_size);
    double loss = nn_loss(net, output, target, batch_size);
    nn_backward(net, input, target, batch_size);
    nn_update(net, batch_size);
    return loss;
}

void nn_predict(nn_net_t *net, const double *input_data, double *output_data,
                int in_dim, int out_dim) {
    /* Wrap raw arrays as 1-sample column matrices */
    Mat input;
    input.rows = in_dim;
    input.cols = 1;
    input.data = (double *)input_data;

    Mat *out = nn_forward(net, &input, 1);

    for (int i = 0; i < out_dim; i++)
        output_data[i] = out->data[i];
}

const char *nn_activation_name(nn_activation_t act) {
    switch (act) {
    case ACT_SIGMOID: return "sigmoid";
    case ACT_TANH:    return "tanh";
    case ACT_RELU:    return "relu";
    case ACT_SOFTMAX: return "softmax";
    case ACT_NONE:    return "linear";
    default:          return "unknown";
    }
}

void nn_summary(const nn_net_t *net) {
    printf("Neural Network: %d layers\n", net->num_layers);
    printf("Loss: %s, LR: ", net->loss_fn == LOSS_MSE ? "MSE" : "cross-entropy");
    /* Simple float print */
    int lr_w = (int)net->learning_rate;
    int lr_f = (int)((net->learning_rate - lr_w) * 100);
    printf("%d.%02d, Momentum: ", lr_w, lr_f);
    int m_w = (int)net->momentum;
    int m_f = (int)((net->momentum - m_w) * 100);
    printf("%d.%02d\n", m_w, m_f);
    int total_params = 0;
    for (int i = 0; i < net->num_layers; i++) {
        const nn_layer_t *l = &net->layers[i];
        int params = l->out_size * l->in_size + l->out_size;
        total_params += params;
        printf("  Layer %d: Dense %d -> %d (%s) [%d params]\n",
               i, l->in_size, l->out_size,
               nn_activation_name(l->activation), params);
    }
    printf("Total parameters: %d\n", total_params);
}

/* ---- Model serialization ---- */

/* File format:
 *   4 bytes: magic "PLNN"
 *   4 bytes: num_layers (uint32)
 *   4 bytes: loss_fn (uint32)
 *   8 bytes: learning_rate (double)
 *   8 bytes: momentum (double)
 *   Per layer:
 *     4 bytes: in_size (uint32)
 *     4 bytes: out_size (uint32)
 *     4 bytes: activation (uint32)
 *     out_size * in_size * 8 bytes: W data
 *     out_size * 8 bytes: b data
 */

static int write_u32(int fd, uint32_t v) {
    return write(fd, &v, 4) == 4 ? 0 : -1;
}

static int write_f64(int fd, double v) {
    return write(fd, &v, 8) == 8 ? 0 : -1;
}

static int read_u32(int fd, uint32_t *v) {
    return read(fd, v, 4) == 4 ? 0 : -1;
}

static int read_f64(int fd, double *v) {
    return read(fd, v, 8) == 8 ? 0 : -1;
}

int nn_save(const nn_net_t *net, const char *path) {
    int fd = open(path, O_CREATE | O_WRONLY);
    if (fd < 0) return -1;

    /* Header */
    if (write(fd, "PLNN", 4) != 4) goto fail;
    if (write_u32(fd, (uint32_t)net->num_layers) < 0) goto fail;
    if (write_u32(fd, (uint32_t)net->loss_fn) < 0) goto fail;
    if (write_f64(fd, net->learning_rate) < 0) goto fail;
    if (write_f64(fd, net->momentum) < 0) goto fail;

    /* Layers */
    for (int i = 0; i < net->num_layers; i++) {
        const nn_layer_t *l = &net->layers[i];
        if (write_u32(fd, (uint32_t)l->in_size) < 0) goto fail;
        if (write_u32(fd, (uint32_t)l->out_size) < 0) goto fail;
        if (write_u32(fd, (uint32_t)l->activation) < 0) goto fail;

        /* Write weights */
        int wn = l->out_size * l->in_size;
        for (int k = 0; k < wn; k++)
            if (write_f64(fd, l->W->data[k]) < 0) goto fail;

        /* Write biases */
        for (int k = 0; k < l->out_size; k++)
            if (write_f64(fd, l->b->data[k]) < 0) goto fail;
    }

    close(fd);
    return 0;

fail:
    close(fd);
    return -1;
}

int nn_load(nn_net_t *net, const char *path, int batch_size) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;

    /* Read and verify magic */
    char magic[4];
    if (read(fd, magic, 4) != 4) goto fail;
    if (magic[0] != 'P' || magic[1] != 'L' || magic[2] != 'N' || magic[3] != 'N')
        goto fail;

    uint32_t num_layers, loss;
    double lr, mom;
    if (read_u32(fd, &num_layers) < 0) goto fail;
    if (read_u32(fd, &loss) < 0) goto fail;
    if (read_f64(fd, &lr) < 0) goto fail;
    if (read_f64(fd, &mom) < 0) goto fail;

    if (num_layers > NN_MAX_LAYERS) goto fail;

    nn_init(net, (nn_loss_t)loss, lr, mom, 0);

    /* Read layer configs */
    for (uint32_t i = 0; i < num_layers; i++) {
        uint32_t in_sz, out_sz, act;
        if (read_u32(fd, &in_sz) < 0) goto fail;
        if (read_u32(fd, &out_sz) < 0) goto fail;
        if (read_u32(fd, &act) < 0) goto fail;
        nn_add_dense(net, (int)in_sz, (int)out_sz, (nn_activation_t)act);
    }

    /* Build (allocates all matrices with Xavier init, which we'll overwrite) */
    if (nn_build(net, batch_size) < 0) goto fail;

    /* Read weights and biases */
    for (int i = 0; i < net->num_layers; i++) {
        nn_layer_t *l = &net->layers[i];
        int wn = l->out_size * l->in_size;
        for (int k = 0; k < wn; k++)
            if (read_f64(fd, &l->W->data[k]) < 0) goto fail_free;
        for (int k = 0; k < l->out_size; k++)
            if (read_f64(fd, &l->b->data[k]) < 0) goto fail_free;
    }

    close(fd);
    return 0;

fail_free:
    nn_free(net);
fail:
    close(fd);
    return -1;
}
