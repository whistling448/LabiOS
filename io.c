#include "io.h"

#include <errno.h>

ssize_t read_retry(int fd, void *buffer, size_t length)
{
    ssize_t count;
    do {
        count = read(fd, buffer, length);
    } while (count < 0 && errno == EINTR);
    return count;
}

int read_byte(Reader *reader, int fd, char *byte)
{
    if (reader->next == reader->end) {
        ssize_t count = read_retry(fd, reader->buffer, sizeof reader->buffer);
        if (count <= 0) {
            return (int)count;
        }
        reader->next = 0;
        reader->end = (size_t)count;
    }
    *byte = reader->buffer[reader->next++];
    return 1;
}

int write_all(int fd, const void *buffer, size_t length)
{
    const char *cursor = buffer;
    while (length > 0) {
        ssize_t count = write(fd, cursor, length);
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count <= 0) {
            if (count == 0) {
                errno = EIO;
            }
            return -1;
        }
        cursor += count;
        length -= (size_t)count;
    }
    return 0;
}

void diagnostic(const char *message)
{
    size_t length = 0;
    while (message[length] != '\0') {
        ++length;
    }
    write_all(STDERR_FILENO, message, length);
}

int system_error(const char *operation)
{
    unsigned int code = (unsigned int)errno;
    char buffer[16];
    size_t start = sizeof buffer;
    buffer[--start] = '\n';
    do {
        buffer[--start] = (char)('0' + code % 10);
        code /= 10;
    } while (code != 0);
    diagnostic(operation);
    diagnostic(": код системной ошибки (errno) — ");
    write_all(STDERR_FILENO, buffer + start, sizeof buffer - start);
    return -1;
}

int close_checked(int fd)
{
    return close(fd) == -1 ? system_error("Не удалось закрыть файловый дескриптор (close)") : 0;
}
