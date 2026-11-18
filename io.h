#ifndef LAB_IO_H
#define LAB_IO_H

#include <unistd.h>

enum Reply {
    READY = 1,
    LINE_OK,
    ZERO_DIVISOR,
    INVALID_NUMBER,
    IO_ERROR
};

typedef struct {
    size_t next;
    size_t end;
    char buffer[4096];
} Reader;

ssize_t read_retry(int fd, void *buffer, size_t length);
int read_byte(Reader *reader, int fd, char *byte);
int write_all(int fd, const void *buffer, size_t length);
void diagnostic(const char *message);
int system_error(const char *operation);
int close_checked(int fd);

#endif
