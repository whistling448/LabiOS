#include "io.h"

#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <sys/wait.h>

extern char **environ;

static int read_filename(Reader *input, char *filename, size_t capacity)
{
    size_t length = 0;
    char byte;
    int count;
    while ((count = read_byte(input, STDIN_FILENO, &byte)) > 0 && byte != '\n') {
        if (length == capacity - 1 || byte == '\0') {
            diagnostic("Родительский процесс: недопустимое имя выходного файла.\n");
            return -1;
        }
        filename[length++] = byte;
    }
    if (count < 0) {
        return system_error("Не удалось прочитать имя выходного файла");
    }
    if (length > 0 && filename[length - 1] == '\r') {
        --length;
    }
    if (length == 0) {
        diagnostic("В первой строке нужно указать имя выходного файла.\n");
        return -1;
    }
    filename[length] = '\0';
    return 0;
}

static int redirect(int fd, int target)
{
    if (fd == target) {
        return 0;
    }
    if (dup2(fd, target) < 0) {
        return system_error("Не удалось перенаправить дескриптор дочернего процесса (dup2)");
    }
    return close_checked(fd);
}

static void start_child(const int commands[2], const int replies[2], char *filename)
{
    if (close_checked(commands[1]) == 0 && close_checked(replies[0]) == 0 &&
        redirect(commands[0], STDIN_FILENO) == 0 && redirect(replies[1], STDOUT_FILENO) == 0) {
        char *const args[] = {"./child", filename, NULL};
        execve(args[0], args, environ);
        system_error("Не удалось запустить дочернюю программу ./child (execve)");
    }
    _exit(1);
}

static int check_reply(int fd, unsigned char expected)
{
    unsigned char reply;
    ssize_t count = read_retry(fd, &reply, sizeof reply);
    if (count < 0) {
        system_error("Не удалось прочитать ответ дочернего процесса");
    } else if (count == 0) {
        diagnostic("Дочерний процесс закрыл канал, не отправив ответ.\n");
    } else if (reply == expected) {
        return 0;
    } else if (reply == ZERO_DIVISOR) {
        diagnostic("Деление на ноль. Оба процесса завершаются.\n");
        return 2;
    } else if (reply == INVALID_NUMBER) {
        diagnostic("Некорректное число или число вне диапазона типа float.\n");
    } else if (reply == IO_ERROR) {
        diagnostic("Дочерний процесс сообщил об ошибке ввода-вывода.\n");
    } else {
        diagnostic("Неизвестный код ответа дочернего процесса.\n");
    }
    return 1;
}

static int forward_commands(Reader *input, int command_fd, int reply_fd)
{
    int result = check_reply(reply_fd, READY);
    if (result != 0) {
        return result;
    }

    char chunk[4096];
    size_t length = 0;
    int pending = 0;
    char byte;
    for (;;) {
        int count = read_byte(input, STDIN_FILENO, &byte);
        if (count < 0) {
            system_error("Не удалось прочитать команду из стандартного ввода");
            return 1;
        }
        if (count == 0) {
            if (!pending) {
                return 0;
            }
            byte = '\n';
        }

        chunk[length++] = byte;
        pending = byte != '\n';
        if (length == sizeof chunk || !pending) {
            if (write_all(command_fd, chunk, length) < 0) {
                system_error("Не удалось отправить команду дочернему процессу");
                return 1;
            }
            length = 0;
        }
        if (!pending) {
            result = check_reply(reply_fd, LINE_OK);
            if (result != 0 || count == 0) {
                return result;
            }
        }
    }
}

int main(void)
{
    Reader input;
    input.next = input.end = 0;
    char filename[PATH_MAX];
    if (read_filename(&input, filename, sizeof filename) < 0) {
        return 1;
    }

    struct sigaction ignore_pipe = {.sa_handler = SIG_IGN};
    if (sigaction(SIGPIPE, &ignore_pipe, NULL) < 0) {
        system_error("Не удалось настроить обработку сигнала SIGPIPE (sigaction)");
        return 1;
    }

    int commands[2];
    int replies[2];
    if (pipe(commands) < 0) {
        system_error("Не удалось создать канал для команд (pipe1)");
        return 1;
    }
    if (pipe(replies) < 0) {
        system_error("Не удалось создать канал для ответов (pipe2)");
        close_checked(commands[0]);
        close_checked(commands[1]);
        return 1;
    }

    pid_t child = fork();
    if (child == 0) {
        start_child(commands, replies, filename);
    }
    if (child < 0) {
        system_error("Не удалось создать дочерний процесс (fork)");
    }
    int close_commands = close_checked(commands[0]);
    int close_replies = close_checked(replies[1]);
    int result = 1;
    if (child > 0 && close_commands == 0 && close_replies == 0) {
        result = forward_commands(&input, commands[1], replies[0]);
    }
    if (close_checked(commands[1]) < 0 && result == 0) {
        result = 1;
    }
    if (close_checked(replies[0]) < 0 && result == 0) {
        result = 1;
    }
    if (child < 0) {
        return 1;
    }

    int status;
    pid_t waited;
    do {
        waited = waitpid(child, &status, 0);
    } while (waited < 0 && errno == EINTR);
    if (waited < 0) {
        system_error("Не удалось дождаться завершения дочернего процесса (waitpid)");
        return 1;
    }
    if (!WIFEXITED(status)) {
        diagnostic("Дочерний процесс завершился аварийно.\n");
        return 1;
    }
    return result != 0 ? result : WEXITSTATUS(status);
}
