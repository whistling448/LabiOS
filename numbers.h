#ifndef LAB_NUMBERS_H
#define LAB_NUMBERS_H

#include <stddef.h>

int parse_number(const char *token, float *value);
size_t format_number(float value, char *output);

#endif
