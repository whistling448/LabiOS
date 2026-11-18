#ifndef LAB_WINDOWS_IO_H
#define LAB_WINDOWS_IO_H

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

enum Reply {
    READY = 1,
    LINE_OK,
    ZERO_DIVISOR,
    INVALID_NUMBER,
    IO_ERROR
};

typedef struct {
    HANDLE handle;
    BOOL console;
    DWORD next;
    DWORD end;
    char buffer[4096];
} Reader;

int read_block(HANDLE handle, void *buffer, DWORD size);
int read_byte(Reader *reader, char *byte);
int write_all(HANDLE handle, const void *buffer, DWORD size);
void diagnostic(const WCHAR *message);
int system_error(const WCHAR *operation);
int close_checked(HANDLE handle);

#endif
