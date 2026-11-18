#include "numbers.h"

#include <float.h>

static int is_digit(char byte)
{
    return byte >= '0' && byte <= '9';
}

int parse_number(const char *token, float *value)
{
    int negative = *token == '-';
    if (*token == '+' || *token == '-') {
        ++token;
    }

    long double magnitude = 0.0L;
    int base_exponent = 0;
    int significant = 0;
    int after_point = 0;
    int has_digits = 0;
    for (; is_digit(*token) || *token == '.'; ++token) {
        if (*token == '.') {
            if (after_point) {
                return -1;
            }
            after_point = 1;
        } else {
            has_digits = 1;
            if (significant < 19) {
                magnitude = magnitude * 10.0L + (*token - '0');
                significant += magnitude != 0.0L;
                base_exponent -= after_point;
            } else {
                base_exponent += !after_point;
            }
        }
    }
    if (!has_digits) {
        return -1;
    }

    if (*token == 'e' || *token == 'E') {
        ++token;
        int exponent_negative = *token == '-';
        if (*token == '+' || *token == '-') {
            ++token;
        }
        if (!is_digit(*token)) {
            return -1;
        }
        int exponent = 0;
        for (; is_digit(*token); ++token) {
            if (exponent < 1000) {
                exponent = exponent * 10 + (*token - '0');
            }
        }
        base_exponent += exponent_negative ? -exponent : exponent;
    }
    if (*token != '\0') {
        return -1;
    }

    if (magnitude != 0.0L) {
        if (base_exponent > 38) {
            return -1;
        }
        if (base_exponent < -64) {
            magnitude = 0.0L;
        } else {
            while (base_exponent > 0) {
                magnitude *= 10.0L;
                --base_exponent;
            }
            while (base_exponent < 0) {
                magnitude /= 10.0L;
                ++base_exponent;
            }
            if (magnitude >= (long double)FLT_MAX + 0x1p103L) {
                return -1;
            }
        }
    }
    *value = (float)(negative ? -magnitude : magnitude);
    return 0;
}

size_t format_number(float value, char *output)
{
    size_t length = 0;
    long double magnitude = value;
    if (magnitude < 0.0L) {
        output[length++] = '-';
        magnitude = -magnitude;
    }
    if (magnitude > FLT_MAX) {
        output[length++] = 'i';
        output[length++] = 'n';
        output[length++] = 'f';
    } else if (magnitude == 0.0L) {
        output[length++] = '0';
    } else {
        int exponent = 0;
        while (magnitude >= 10.0L) {
            magnitude /= 10.0L;
            ++exponent;
        }
        while (magnitude < 1.0L) {
            magnitude *= 10.0L;
            --exponent;
        }

        unsigned long digits = (unsigned long)(magnitude * 100000000.0L + 0.5L);
        if (digits == 1000000000UL) {
            digits /= 10;
            ++exponent;
        }
        char significant[9];
        for (size_t index = sizeof significant; index > 0; --index) {
            significant[index - 1] = (char)('0' + digits % 10);
            digits /= 10;
        }
        int used = (int)sizeof significant;
        while (used > 1 && significant[used - 1] == '0') {
            --used;
        }

        int scientific = exponent < -4 || exponent >= (int)sizeof significant;
        int point = scientific ? 1 : exponent + 1;
        if (point <= 0) {
            output[length++] = '0';
            output[length++] = '.';
        }
        for (int index = point < 0 ? point : 0; index < used || index < point; ++index) {
            if (index == point && point > 0) {
                output[length++] = '.';
            }
            output[length++] = index < 0 || index >= used ? '0' : significant[index];
        }
        if (scientific) {
            output[length++] = 'e';
            if (exponent < 0) {
                output[length++] = '-';
                exponent = -exponent;
            }
            if (exponent >= 10) {
                output[length++] = (char)('0' + exponent / 10);
            }
            output[length++] = (char)('0' + exponent % 10);
        }
    }
    output[length++] = '\n';
    return length;
}
