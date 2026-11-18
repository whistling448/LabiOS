#include "io.h"
#include "numbers.h"

#include <shellapi.h>

static int send_reply(HANDLE replies, unsigned char reply)
{
    if (write_all(replies, &reply, sizeof reply) < 0) {
        return system_error(L"Не удалось отправить ответ родительскому процессу");
    }
    return 0;
}

static DWORD evaluate(HANDLE output, HANDLE replies)
{
    static Reader input;
    input.handle = GetStdHandle(STD_INPUT_HANDLE);
    char token[256];
    size_t length = 0;
    float result = 0.0f;
    int has_value = 0;
    enum Reply reply = LINE_OK;
    char byte;
    int count;
    while ((count = read_byte(&input, &byte)) > 0) {
        int separator = byte == ' ' || byte == '\t' || byte == '\r' || byte == '\n';
        if (reply == LINE_OK && !separator) {
            if (length == sizeof token - 1 || byte == '\0') {
                reply = INVALID_NUMBER;
            } else {
                token[length++] = byte;
            }
        } else if (reply == LINE_OK && length != 0) {
            float number;
            token[length] = '\0';
            length = 0;
            if (parse_number(token, &number) < 0) {
                reply = INVALID_NUMBER;
            } else if (!has_value) {
                result = number;
                has_value = 1;
            } else if (number == 0.0f) {
                reply = ZERO_DIVISOR;
            } else {
                result /= number;
            }
        }

        if (byte == '\n') {
            if (reply == LINE_OK && has_value) {
                char formatted[32];
                size_t size = format_number(result, formatted);
                formatted[size - 1] = '\r';
                formatted[size++] = '\n';
                if (write_all(output, formatted, (DWORD)size) < 0) {
                    system_error(L"Не удалось записать результат в выходной файл");
                    reply = IO_ERROR;
                }
            }
            if (send_reply(replies, (unsigned char)reply) < 0) {
                return 1;
            }
            if (reply != LINE_OK) {
                return reply == ZERO_DIVISOR ? 2 : 1;
            }
            has_value = 0;
        }
    }
    if (count < 0) {
        system_error(L"Не удалось прочитать команду из канала");
        send_reply(replies, IO_ERROR);
        return 1;
    }
    if (has_value || length != 0 || reply != LINE_OK) {
        diagnostic(L"Канал команд закрылся посреди строки.\r\n");
        return 1;
    }
    return 0;
}

static DWORD run_child(void)
{
    int argc;
    WCHAR **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv == NULL) {
        system_error(L"Не удалось получить аргументы дочерней программы (CommandLineToArgvW)");
        return 1;
    }
    if (argc != 2) {
        LocalFree(argv);
        diagnostic(L"Запускайте parent.exe, а не child.exe напрямую.\r\n");
        return 1;
    }

    HANDLE replies = GetStdHandle(STD_OUTPUT_HANDLE);
    HANDLE output = CreateFileW(argv[1], GENERIC_WRITE, FILE_SHARE_READ, NULL,
                                CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (output == INVALID_HANDLE_VALUE) {
        system_error(L"Не удалось открыть выходной файл (CreateFileW)");
    }
    LocalFree(argv);
    if (output == INVALID_HANDLE_VALUE) {
        send_reply(replies, IO_ERROR);
        return 1;
    }
    DWORD result = send_reply(replies, READY) == 0 ? evaluate(output, replies) : 1;
    if (close_checked(output) < 0 && result == 0) {
        return 1;
    }
    return result;
}

void mainCRTStartup(void)
{
    ExitProcess(run_child());
}
