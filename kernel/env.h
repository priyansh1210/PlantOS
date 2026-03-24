#ifndef KERNEL_ENV_H
#define KERNEL_ENV_H

#include <plantos/types.h>

#define ENV_MAX_VARS   64
#define ENV_KEY_MAX    32
#define ENV_VAL_MAX   128

void        env_init(void);
const char *env_get(const char *key);
int         env_set(const char *key, const char *value);
int         env_unset(const char *key);

/* Iteration */
int         env_count(void);
const char *env_key_at(int index);
const char *env_val_at(int index);

/* Working directory */
const char *cwd_get(void);
int         cwd_set(const char *path);

/* Path resolution: resolve relative path using cwd, normalize . and .. */
void        path_resolve(const char *input, char *output, size_t outsize);

/* PATH lookup: search PATH dirs for an executable name, returns 0 on success */
int         path_lookup(const char *name, char *resolved, size_t size);

#endif
