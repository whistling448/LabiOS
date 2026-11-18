#include "io.h"

int read_block(HANDLE handle, void *buffer, DWORD size)
{
    DWORD count;
    if (!ReadFile(handle, buffer, size, &count, NULL)) {
        DWORD error = GetLastError();
        if (error == ERROR_BROKEN_PIPE || error == ERROR_HANDLE_EOF) {
            return 0;
        }
        return -1;
    }
    return (int)count;
}

static int read_console(Reader *reader)
{
    WCHAR input[1024];
    DWORD count;
    if (!ReadConsoleW(reader->handle, input, ARRAYSIZE(input), &count, NULL)) {
        return -1;
    }
    if (count == 0) {
        return 0;
    }
    if (count == ARRAYSIZE(input) && input[count - 1] >= 0xD800 && input[count - 1] <= 0xDBFF) {
        WCHAR tail[2] = {input[count - 1], 0};
        DWORD extra;
        if (!ReadConsoleW(reader->handle, tail + 1, 1, &extra, NULL)) {
            return -1;
        }
        int prefix = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, input, (int)count - 1,
                                        reader->buffer, sizeof reader->buffer, NULL, NULL);
        if (prefix == 0) {
            return -1;
        }
        int suffix = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, tail, 1 + (int)extra,
                                        reader->buffer + prefix, sizeof reader->buffer - prefix,
                                        NULL, NULL);
        return suffix != 0 ? prefix + suffix : -1;
    }
    int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, input, (int)count,
                                   reader->buffer, sizeof reader->buffer, NULL, NULL);
    return size != 0 ? size : -1;
}

int read_byte(Reader *reader, char *byte)
{
    if (reader->next == reader->end) {
        int count = reader->console ? read_console(reader) :
                    read_block(reader->handle, reader->buffer, sizeof reader->buffer);
        if (count <= 0) {
            return count;
        }
        reader->next = 0;
        reader->end = (DWORD)count;
    }
    *byte = reader->buffer[reader->next++];
    return 1;
}

int write_all(HANDLE handle, const void *buffer, DWORD size)
{
    const char *cursor = buffer;
    while (size != 0) {
        DWORD written;
        if (!WriteFile(handle, cursor, size, &written, NULL)) {
            return -1;
        }
        if (written == 0) {
            SetLastError(ERROR_WRITE_FAULT);
            return -1;
        }
        cursor += written;
        size -= written;
    }
    return 0;
}

void diagnostic(const WCHAR *message)
{
    HANDLE output = GetStdHandle(STD_ERROR_HANDLE);
    DWORD mode;
    if (GetConsoleMode(output, &mode)) {
        DWORD size = 0;
        DWORD written;
        while (message[size] != L'\0') {
            ++size;
        }
        WriteConsoleW(output, message, size, &written, NULL);
    } else {
        char utf8[512];
        int size = WideCharToMultiByte(CP_UTF8, 0, message, -1, utf8, sizeof utf8, NULL, NULL);
        if (size > 0) {
            write_all(output, utf8, (DWORD)(size - 1));
        }
    }
}

int system_error(const WCHAR *operation)
{
    DWORD error = GetLastError();
    WCHAR digits[16];
    size_t start = ARRAYSIZE(digits);
    digits[--start] = L'\0';
    digits[--start] = L'\n';
    digits[--start] = L'\r';
    do {
        digits[--start] = (WCHAR)(L'0' + error % 10);
        error /= 10;
    } while (error != 0);
    diagnostic(operation);
    diagnostic(L": код ошибки Windows — ");
    diagnostic(digits + start);
    return -1;
}

int close_checked(HANDLE handle)
{
    if (handle != NULL && !CloseHandle(handle)) {
        return system_error(L"Не удалось закрыть дескриптор (CloseHandle)");
    }
    return 0;
}
