#ifndef LIB_UTIL_H
#define LIB_UTIL_H

#include <plantos/types.h>

char *itoa(int64_t value, char *buf, int base);
char *utoa(uint64_t value, char *buf, int base);
int   tokenize(char *str, char **tokens, int max_tokens);

#endif
