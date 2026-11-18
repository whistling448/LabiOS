#include "io.h"
#include "numbers.h"

#include <fcntl.h>

static int send_reply(unsigned char reply)
{
    if (write_all(STDOUT_FILENO, &reply, sizeof reply) < 0) {
        return system_error("Не удалось отправить ответ родительскому процессу");
    }
    return 0;
}

static int evaluate(int output_fd)
{
    Reader input;
    input.next = input.end = 0;
    char token[256];
    size_t length = 0;
    float result = 0.0f;
    int has_value = 0;
    enum Reply reply = LINE_OK;
    char byte;
    int count;
    while ((count = read_byte(&input, STDIN_FILENO, &byte)) > 0) {
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
                if (write_all(output_fd, formatted, size) < 0) {
                    system_error("Не удалось записать результат в выходной файл");
                    reply = IO_ERROR;
                }
            }
            if (send_reply((unsigned char)reply) < 0) {
                return 1;
            }
            if (reply != LINE_OK) {
                return reply == ZERO_DIVISOR ? 2 : 1;
            }
            has_value = 0;
        }
    }
    if (count < 0) {
        system_error("Не удалось прочитать команду из канала");
        send_reply(IO_ERROR);
        return 1;
    }
    if (has_value || length != 0 || reply != LINE_OK) {
        diagnostic("Канал команд закрылся посреди строки.\n");
        return 1;
    }
    return 0;
}

int main(int argc, char *argv[])
{
    if (argc != 2) {
        diagnostic("Запускайте ./parent, а не ./child напрямую.\n");
        return 1;
    }
    int output_fd = open(argv[1], O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (output_fd < 0) {
        system_error("Не удалось открыть выходной файл");
        send_reply(IO_ERROR);
        return 1;
    }
    int result = send_reply(READY) == 0 ? evaluate(output_fd) : 1;
    if (close_checked(output_fd) < 0 && result == 0) {
        return 1;
    }
    return result;
}
