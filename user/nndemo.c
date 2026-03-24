#include "user/usyscall.h"
#include "user/libc/ulibc.h"
#include "user/libc/umath.h"
#include "user/libc/nn.h"

/*
 * PlantOS Neural Network Demo
 * Uses the NN library to train configurable networks.
 * Default: learns XOR with 2 -> 8 (tanh) -> 1 (sigmoid)
 */

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

/* ---- XOR demo ---- */

static void demo_xor(void) {
    printf("--- Task: Learn XOR ---\n");
    printf("Architecture: 2 -> 8 (tanh) -> 1 (sigmoid)\n");
    printf("Optimizer: SGD + momentum (0.9)\n");
    printf("Loss: binary cross-entropy\n\n");

    nn_net_t net;
    nn_init(&net, LOSS_CROSS_ENTROPY, 2.0, 0.9, 98765);
    nn_add_dense(&net, 2, 8, ACT_TANH);
    nn_add_dense(&net, -1, 1, ACT_SIGMOID);
    if (nn_build(&net, 4) < 0) {
        printf("Failed to build network!\n");
        return;
    }

    /* Training data: 2 inputs x 4 samples (column-major batch) */
    Mat *X = mat_alloc(2, 4);
    Mat *Y = mat_alloc(1, 4);
    /* XOR truth table as columns: (0,0) (0,1) (1,0) (1,1) */
    double xdata[] = { 0, 0, 1, 1,   /* row 0: input 1 */
                       0, 1, 0, 1 };  /* row 1: input 2 */
    double ydata[] = { 0, 1, 1, 0 };
    memcpy(X->data, xdata, sizeof(xdata));
    memcpy(Y->data, ydata, sizeof(ydata));

    int epochs = 5000;
    printf("Training for %d epochs...\n\n", epochs);

    for (int e = 0; e < epochs; e++) {
        double loss = nn_train_step(&net, X, Y, 4);
        if (e % 1000 == 0 || e == epochs - 1) {
            printf("  Epoch %d  loss=", e);
            print_float(loss, 6);
            printf("\n");
        }
    }

    /* Predictions */
    printf("\n  Input     Target  Output\n");
    double in[2], out[1];
    double tests[][2] = {{0,0},{0,1},{1,0},{1,1}};
    double targets[] = {0, 1, 1, 0};
    int correct = 0;
    for (int i = 0; i < 4; i++) {
        in[0] = tests[i][0];
        in[1] = tests[i][1];
        nn_predict(&net, in, out, 2, 1);
        int ok = (out[0] > 0.5) == (targets[i] > 0.5);
        correct += ok;
        printf("  (%d, %d)     %d      ", (int)tests[i][0], (int)tests[i][1], (int)targets[i]);
        print_float(out[0], 4);
        printf("  %s\n", ok ? "OK" : "FAIL");
    }
    printf("  Accuracy: %d/4\n", correct);

    /* Save model to disk */
    printf("\n  Saving model to /disk/xor.nn ...");
    if (nn_save(&net, "/disk/xor.nn") == 0) {
        printf(" OK\n");

        /* Load it back and verify */
        nn_net_t loaded;
        printf("  Loading model back ...");
        if (nn_load(&loaded, "/disk/xor.nn", 4) == 0) {
            printf(" OK\n");
            nn_summary(&loaded);

            /* Verify predictions match */
            printf("  Verifying loaded model:\n");
            int match = 0;
            for (int i = 0; i < 4; i++) {
                in[0] = tests[i][0];
                in[1] = tests[i][1];
                nn_predict(&loaded, in, out, 2, 1);
                int ok = (out[0] > 0.5) == (targets[i] > 0.5);
                match += ok;
                printf("    (%d,%d) -> ", (int)tests[i][0], (int)tests[i][1]);
                print_float(out[0], 4);
                printf("  %s\n", ok ? "OK" : "FAIL");
            }
            printf("  Loaded model accuracy: %d/4\n", match);
            nn_free(&loaded);
        } else {
            printf(" FAILED\n");
        }
    } else {
        printf(" FAILED (disk not mounted?)\n");
    }

    nn_free(&net);
    mat_free(X);
    mat_free(Y);
}

/* ---- Circle classification demo ---- */

static uint32_t demo_rng = 42;

static double demo_rand(void) {
    demo_rng ^= demo_rng << 13;
    demo_rng ^= demo_rng >> 17;
    demo_rng ^= demo_rng << 5;
    return ((double)(demo_rng % 10000) / 5000.0) - 1.0;
}

static void demo_circle(void) {
    printf("--- Task: Circle Classification ---\n");
    printf("Architecture: 2 -> 16 (relu) -> 8 (relu) -> 1 (sigmoid)\n");
    printf("Classify points inside/outside unit circle\n\n");

    nn_net_t net;
    nn_init(&net, LOSS_CROSS_ENTROPY, 0.5, 0.9, 77777);
    nn_add_dense(&net, 2, 16, ACT_RELU);
    nn_add_dense(&net, -1, 8, ACT_RELU);
    nn_add_dense(&net, -1, 1, ACT_SIGMOID);

    #define NSAMPLES 32
    if (nn_build(&net, NSAMPLES) < 0) {
        printf("Failed to build network!\n");
        return;
    }

    /* Generate training data */
    Mat *X = mat_alloc(2, NSAMPLES);
    Mat *Y = mat_alloc(1, NSAMPLES);
    demo_rng = 42;
    for (int j = 0; j < NSAMPLES; j++) {
        double x = demo_rand();
        double y = demo_rand();
        X->data[0 * NSAMPLES + j] = x;  /* row 0 */
        X->data[1 * NSAMPLES + j] = y;  /* row 1 */
        Y->data[j] = (x * x + y * y < 0.64) ? 1.0 : 0.0;  /* inside circle r=0.8 */
    }

    int epochs = 3000;
    printf("Training on %d samples for %d epochs...\n\n", NSAMPLES, epochs);

    for (int e = 0; e < epochs; e++) {
        double loss = nn_train_step(&net, X, Y, NSAMPLES);
        if (e % 500 == 0 || e == epochs - 1) {
            printf("  Epoch %d  loss=", e);
            print_float(loss, 6);
            printf("\n");
        }
    }

    /* Test accuracy on training set */
    int correct = 0;
    double in[2], out[1];
    for (int j = 0; j < NSAMPLES; j++) {
        in[0] = X->data[0 * NSAMPLES + j];
        in[1] = X->data[1 * NSAMPLES + j];
        nn_predict(&net, in, out, 2, 1);
        int pred = out[0] > 0.5 ? 1 : 0;
        int actual = (int)Y->data[j];
        if (pred == actual) correct++;
    }
    printf("\n  Train accuracy: %d/%d\n", correct, NSAMPLES);

    /* Test on a few specific points */
    printf("\n  Sample predictions:\n");
    double test_pts[][2] = { {0.0, 0.0}, {0.5, 0.5}, {0.9, 0.0}, {0.3, 0.2}, {-0.7, -0.7} };
    for (int i = 0; i < 5; i++) {
        in[0] = test_pts[i][0];
        in[1] = test_pts[i][1];
        double r2 = in[0]*in[0] + in[1]*in[1];
        int expect = (r2 < 0.64) ? 1 : 0;
        nn_predict(&net, in, out, 2, 1);
        printf("  (");
        print_float(in[0], 1);
        printf(", ");
        print_float(in[1], 1);
        printf(")  r2=");
        print_float(r2, 2);
        printf("  pred=");
        print_float(out[0], 3);
        printf("  %s\n", ((out[0] > 0.5) == expect) ? "OK" : "FAIL");
    }

    nn_free(&net);
    mat_free(X);
    mat_free(Y);
}

int main(int argc, char **argv) {
    printf("=== PlantOS Neural Network Engine ===\n");
    printf("Features: dense layers, save/load, batch training\n");
    printf("Activations: sigmoid, tanh, relu, softmax\n");
    printf("Optimizers: SGD with momentum\n\n");

    demo_xor();
    printf("\n");
    demo_circle();

    printf("\nAll demos complete.\n");
    uexit(0);
    return 0;
}
