#ifndef USER_LIBC_NN_H
#define USER_LIBC_NN_H

#include "user/libc/matrix.h"

#define NN_MAX_LAYERS 16

/* Activation functions */
typedef enum {
    ACT_NONE,
    ACT_SIGMOID,
    ACT_TANH,
    ACT_RELU,
    ACT_SOFTMAX
} nn_activation_t;

/* Loss functions */
typedef enum {
    LOSS_MSE,
    LOSS_CROSS_ENTROPY
} nn_loss_t;

/* Dense (fully-connected) layer */
typedef struct {
    int             in_size;
    int             out_size;
    nn_activation_t activation;

    Mat *W;         /* out_size x in_size  */
    Mat *b;         /* out_size x 1        */

    /* Forward cache (sized for batch) */
    Mat *z;         /* pre-activation:  out_size x batch */
    Mat *a;         /* post-activation: out_size x batch */

    /* Gradients */
    Mat *dW;        /* out_size x in_size  */
    Mat *db;        /* out_size x 1        */

    /* Momentum */
    Mat *vW;
    Mat *vb;
} nn_layer_t;

/* Neural network */
typedef struct {
    int          num_layers;
    nn_layer_t   layers[NN_MAX_LAYERS];
    nn_loss_t    loss_fn;
    double       learning_rate;
    double       momentum;
    uint32_t     rng_state;
    int          batch_size;  /* max batch size (set at build) */
    Mat         *delta;       /* scratch for backprop */
} nn_net_t;

/* Initialize empty network */
void nn_init(nn_net_t *net, nn_loss_t loss, double lr, double momentum, uint32_t seed);

/* Add a dense layer. in_size=-1 infers from previous layer. Returns 0 or -1. */
int  nn_add_dense(nn_net_t *net, int in_size, int out_size, nn_activation_t act);

/* Allocate all matrices. Call after adding all layers. Returns 0 or -1. */
int  nn_build(nn_net_t *net, int batch_size);

/* Free all memory */
void nn_free(nn_net_t *net);

/* Forward pass. input is (input_dim x batch_size). Returns output activation. */
Mat *nn_forward(nn_net_t *net, const Mat *input, int batch_size);

/* Compute loss value. */
double nn_loss(nn_net_t *net, const Mat *output, const Mat *target, int batch_size);

/* Backprop: compute gradients. Call after nn_forward. */
void nn_backward(nn_net_t *net, const Mat *input, const Mat *target, int batch_size);

/* Update weights with SGD+momentum. */
void nn_update(nn_net_t *net, int batch_size);

/* Convenience: forward + loss + backward + update. Returns loss. */
double nn_train_step(nn_net_t *net, const Mat *input, const Mat *target, int batch_size);

/* Predict single sample. input_data[in_dim] -> output_data[out_dim]. */
void nn_predict(nn_net_t *net, const double *input_data, double *output_data,
                int in_dim, int out_dim);

/* Get string name for activation */
const char *nn_activation_name(nn_activation_t act);

/* Print network architecture summary */
void nn_summary(const nn_net_t *net);

/* Save trained model to file. Returns 0 on success, -1 on failure. */
int nn_save(const nn_net_t *net, const char *path);

/* Load model from file. Allocates all matrices. Returns 0 on success, -1 on failure.
 * After loading, the network is ready for inference. Call nn_build() first if you need
 * to continue training (to allocate gradient/cache matrices). */
int nn_load(nn_net_t *net, const char *path, int batch_size);

#endif
