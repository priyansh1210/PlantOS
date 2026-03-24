#include "kernel/env.h"
#include "lib/string.h"
#include "lib/printf.h"
#include "fs/vfs.h"

/* Environment variable storage */
struct env_entry {
    char key[ENV_KEY_MAX];
    char value[ENV_VAL_MAX];
    bool used;
};

static struct env_entry env_table[ENV_MAX_VARS];

/* Working directory */
static char current_cwd[VFS_PATH_MAX] = "/";

void env_init(void) {
    memset(env_table, 0, sizeof(env_table));
    env_set("PATH", "/bin");
    env_set("HOME", "/");
    env_set("SHELL", "plantos");
    strcpy(current_cwd, "/");
}

const char *env_get(const char *key) {
    for (int i = 0; i < ENV_MAX_VARS; i++) {
        if (env_table[i].used && strcmp(env_table[i].key, key) == 0)
            return env_table[i].value;
    }
    return NULL;
}

int env_set(const char *key, const char *value) {
    if (!key || !value) return -1;
    if (strlen(key) >= ENV_KEY_MAX || strlen(value) >= ENV_VAL_MAX) return -1;

    /* Update existing */
    for (int i = 0; i < ENV_MAX_VARS; i++) {
        if (env_table[i].used && strcmp(env_table[i].key, key) == 0) {
            strcpy(env_table[i].value, value);
            return 0;
        }
    }
    /* Allocate new slot */
    for (int i = 0; i < ENV_MAX_VARS; i++) {
        if (!env_table[i].used) {
            env_table[i].used = true;
            strcpy(env_table[i].key, key);
            strcpy(env_table[i].value, value);
            return 0;
        }
    }
    return -1; /* table full */
}

int env_unset(const char *key) {
    for (int i = 0; i < ENV_MAX_VARS; i++) {
        if (env_table[i].used && strcmp(env_table[i].key, key) == 0) {
            env_table[i].used = false;
            return 0;
        }
    }
    return -1;
}

int env_count(void) {
    int n = 0;
    for (int i = 0; i < ENV_MAX_VARS; i++)
        if (env_table[i].used) n++;
    return n;
}

const char *env_key_at(int index) {
    int n = 0;
    for (int i = 0; i < ENV_MAX_VARS; i++) {
        if (env_table[i].used) {
            if (n == index) return env_table[i].key;
            n++;
        }
    }
    return NULL;
}

const char *env_val_at(int index) {
    int n = 0;
    for (int i = 0; i < ENV_MAX_VARS; i++) {
        if (env_table[i].used) {
            if (n == index) return env_table[i].value;
            n++;
        }
    }
    return NULL;
}

/* ---- Working directory ---- */

const char *cwd_get(void) {
    return current_cwd;
}

int cwd_set(const char *path) {
    char resolved[VFS_PATH_MAX];
    path_resolve(path, resolved, sizeof(resolved));

    /* Verify it exists and is a directory */
    if (strcmp(resolved, "/") != 0) {
        struct vfs_stat st;
        if (vfs_stat(resolved, &st) < 0) return -1;
        if (st.type != VFS_DIR) return -1;
    }

    strcpy(current_cwd, resolved);
    return 0;
}

/* ---- Path resolution ---- */

void path_resolve(const char *input, char *output, size_t outsize) {
    char temp[VFS_PATH_MAX];

    /* Build absolute path */
    if (input[0] == '/') {
        /* Already absolute */
        strncpy(temp, input, VFS_PATH_MAX - 1);
        temp[VFS_PATH_MAX - 1] = '\0';
    } else {
        /* Relative: prepend cwd */
        int cwdlen = strlen(current_cwd);
        memcpy(temp, current_cwd, cwdlen);
        if (cwdlen > 1)
            temp[cwdlen++] = '/';
        strncpy(temp + cwdlen, input, VFS_PATH_MAX - 1 - cwdlen);
        temp[VFS_PATH_MAX - 1] = '\0';
    }

    /* Normalize: split into components, resolve . and .. */
    char *components[64];
    int depth = 0;

    char *p = temp;
    while (*p) {
        while (*p == '/') p++;
        if (!*p) break;
        char *start = p;
        while (*p && *p != '/') p++;
        int len = (int)(p - start);

        if (len == 1 && start[0] == '.') {
            /* Skip "." */
            continue;
        } else if (len == 2 && start[0] == '.' && start[1] == '.') {
            /* Go up */
            if (depth > 0) depth--;
        } else {
            /* Null-terminate this component in-place */
            if (*p) *p++ = '\0';
            else *p = '\0'; /* already at end */
            components[depth++] = start;
            if (depth >= 64) break;
            continue;
        }
        if (*p) p++;
    }

    /* Reconstruct path */
    if (depth == 0) {
        output[0] = '/';
        output[1] = '\0';
        return;
    }

    int pos = 0;
    for (int i = 0; i < depth && pos < (int)outsize - 2; i++) {
        output[pos++] = '/';
        int clen = strlen(components[i]);
        if (pos + clen >= (int)outsize - 1) break;
        memcpy(output + pos, components[i], clen);
        pos += clen;
    }
    output[pos] = '\0';
}

/* ---- PATH lookup ---- */

int path_lookup(const char *name, char *resolved, size_t size) {
    /* If name contains '/', resolve it directly */
    if (name[0] == '/' || name[0] == '.') {
        path_resolve(name, resolved, size);
        struct vfs_stat st;
        if (vfs_stat(resolved, &st) == 0 && st.type == VFS_FILE)
            return 0;
        return -1;
    }

    /* Search PATH directories */
    const char *path_val = env_get("PATH");
    if (!path_val) path_val = "/bin";

    char pathcopy[ENV_VAL_MAX];
    strncpy(pathcopy, path_val, ENV_VAL_MAX - 1);
    pathcopy[ENV_VAL_MAX - 1] = '\0';

    char *p = pathcopy;
    while (*p) {
        char *dir_start = p;
        while (*p && *p != ':') p++;
        if (*p == ':') *p++ = '\0';

        if (strlen(dir_start) == 0) continue;

        /* Build candidate: dir/name */
        char candidate[VFS_PATH_MAX];
        int dlen = strlen(dir_start);
        memcpy(candidate, dir_start, dlen);
        if (dir_start[dlen - 1] != '/')
            candidate[dlen++] = '/';
        strcpy(candidate + dlen, name);

        struct vfs_stat st;
        if (vfs_stat(candidate, &st) == 0 && st.type == VFS_FILE) {
            strncpy(resolved, candidate, size - 1);
            resolved[size - 1] = '\0';
            return 0;
        }
    }

    return -1;
}
