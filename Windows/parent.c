#include "io.h"

enum { PATH_CAPACITY = 4096 };

static int read_filename(Reader *input, WCHAR filename[PATH_CAPACITY])
{
    static char bytes[PATH_CAPACITY * 4];
    size_t length = 0;
    char byte;
    int count;
    while ((count = read_byte(input, &byte)) > 0 && byte != '\n') {
        if (length == 0 && input->console && byte == '\x1a') {
            break;
        }
        if (length == sizeof bytes - 1 || byte == '\0' || byte == '"') {
            diagnostic(L"Недопустимое имя выходного файла.\r\n");
            return -1;
        }
        bytes[length++] = byte;
    }
    if (count < 0) {
        return system_error(L"Не удалось прочитать имя выходного файла");
    }
    if (length != 0 && bytes[length - 1] == '\r') {
        --length;
    }
    if (length == 0) {
        diagnostic(L"В первой строке нужно указать имя выходного файла.\r\n");
        return -1;
    }
    bytes[length] = '\0';
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes, -1, filename, PATH_CAPACITY)) {
        return system_error(L"Не удалось прочитать путь к выходному файлу в UTF-8");
    }
    return 0;
}

static int start_child(const WCHAR *filename, HANDLE commands, HANDLE replies,
                       PROCESS_INFORMATION *process)
{
    static WCHAR executable[PATH_CAPACITY];
    DWORD length = GetModuleFileNameW(NULL, executable, ARRAYSIZE(executable));
    if (length == 0 || length == ARRAYSIZE(executable)) {
        return system_error(L"Не удалось определить путь к parent.exe");
    }
    while (length > 0 && executable[length - 1] != L'\\') {
        --length;
    }
    const WCHAR name[] = L"child.exe";
    if (length + ARRAYSIZE(name) > ARRAYSIZE(executable)) {
        diagnostic(L"Путь к child.exe слишком длинный.\r\n");
        return -1;
    }
    for (size_t index = 0; index < ARRAYSIZE(name); ++index) {
        executable[length + index] = name[index];
    }

    static WCHAR command_line[2 * PATH_CAPACITY + 16];
    const WCHAR prefix[] = L"child.exe \"";
    size_t position = 0;
    while (prefix[position] != L'\0') {
        command_line[position] = prefix[position];
        ++position;
    }
    size_t trailing_backslashes = 0;
    while (*filename != L'\0') {
        trailing_backslashes = *filename == L'\\' ? trailing_backslashes + 1 : 0;
        command_line[position++] = *filename++;
    }
    while (trailing_backslashes > 0) {
        command_line[position++] = L'\\';
        --trailing_backslashes;
    }
    command_line[position++] = L'"';
    command_line[position] = L'\0';

    STARTUPINFOW startup = {
        .cb = sizeof startup,
        .dwFlags = STARTF_USESTDHANDLES,
        .hStdInput = commands,
        .hStdOutput = replies,
        .hStdError = GetStdHandle(STD_ERROR_HANDLE)
    };
    if (!CreateProcessW(executable, command_line, NULL, NULL, TRUE, 0,
                        NULL, NULL, &startup, process)) {
        return system_error(L"Не удалось запустить дочернюю программу child.exe (CreateProcessW)");
    }
    return 0;
}

static int check_reply(HANDLE handle, unsigned char expected)
{
    unsigned char reply;
    int count = read_block(handle, &reply, sizeof reply);
    if (count < 0) {
        system_error(L"Не удалось прочитать ответ дочернего процесса");
    } else if (count == 0) {
        diagnostic(L"Дочерний процесс закрыл канал, не отправив ответ.\r\n");
    } else if (reply == expected) {
        return 0;
    } else if (reply == ZERO_DIVISOR) {
        diagnostic(L"Деление на ноль. Оба процесса завершаются.\r\n");
        return 2;
    } else if (reply == INVALID_NUMBER) {
        diagnostic(L"Некорректное число или число вне диапазона типа float.\r\n");
    } else if (reply == IO_ERROR) {
        diagnostic(L"Дочерний процесс сообщил об ошибке ввода-вывода.\r\n");
    } else {
        diagnostic(L"Неизвестный код ответа дочернего процесса.\r\n");
    }
    return 1;
}

static int forward_commands(Reader *input, HANDLE commands, HANDLE replies)
{
    int result = check_reply(replies, READY);
    if (result != 0) {
        return result;
    }

    static char chunk[4096];
    DWORD length = 0;
    int pending = 0;
    char byte;
    for (;;) {
        int count = read_byte(input, &byte);
        if (count < 0) {
            system_error(L"Не удалось прочитать команду из стандартного ввода");
            return 1;
        }
        if (count == 0 || (input->console && !pending && byte == '\x1a')) {
            if (!pending) {
                return 0;
            }
            byte = '\n';
        }

        chunk[length++] = byte;
        pending = byte != '\n';
        if (length == sizeof chunk || !pending) {
            if (write_all(commands, chunk, length) < 0) {
                system_error(L"Не удалось отправить команду дочернему процессу");
                return 1;
            }
            length = 0;
        }
        if (!pending) {
            result = check_reply(replies, LINE_OK);
            if (result != 0 || count == 0) {
                return result;
            }
        }
    }
}

static DWORD run_parent(void)
{
    static Reader input;
    DWORD mode;
    input.handle = GetStdHandle(STD_INPUT_HANDLE);
    input.console = GetConsoleMode(input.handle, &mode);
    static WCHAR filename[PATH_CAPACITY];
    if (read_filename(&input, filename) < 0) {
        return 1;
    }

    SECURITY_ATTRIBUTES attributes = {
        .nLength = sizeof attributes,
        .bInheritHandle = TRUE
    };
    HANDLE command_read = NULL;
    HANDLE command_write = NULL;
    HANDLE reply_read = NULL;
    HANDLE reply_write = NULL;
    PROCESS_INFORMATION process = {0};
    DWORD result = 1;

    if (!CreatePipe(&command_read, &command_write, &attributes, 0)) {
        system_error(L"Не удалось создать канал для команд (CreatePipe)");
        return 1;
    }
    if (!CreatePipe(&reply_read, &reply_write, &attributes, 0)) {
        system_error(L"Не удалось создать канал для ответов (CreatePipe)");
    } else if (!SetHandleInformation(command_write, HANDLE_FLAG_INHERIT, 0) ||
               !SetHandleInformation(reply_read, HANDLE_FLAG_INHERIT, 0)) {
        system_error(L"Не удалось настроить наследование каналов (SetHandleInformation)");
    } else if (start_child(filename, command_read, reply_write, &process) == 0) {
        int close_command = close_checked(command_read);
        int close_reply = close_checked(reply_write);
        int close_thread = close_checked(process.hThread);
        command_read = NULL;
        reply_write = NULL;
        if (close_command == 0 && close_reply == 0 && close_thread == 0) {
            result = (DWORD)forward_commands(&input, command_write, reply_read);
        }
    }

    close_checked(command_read);
    close_checked(reply_write);
    if (close_checked(command_write) < 0 && result == 0) {
        result = 1;
    }
    if (close_checked(reply_read) < 0 && result == 0) {
        result = 1;
    }
    if (process.hProcess != NULL) {
        DWORD child_status;
        if (WaitForSingleObject(process.hProcess, INFINITE) != WAIT_OBJECT_0) {
            system_error(L"Не удалось дождаться завершения дочернего процесса (WaitForSingleObject)");
            result = 1;
        } else if (!GetExitCodeProcess(process.hProcess, &child_status)) {
            system_error(L"Не удалось получить код завершения дочернего процесса (GetExitCodeProcess)");
            result = 1;
        } else if (result == 0 && child_status != 0) {
            diagnostic(L"Дочерний процесс завершился с ошибкой.\r\n");
            result = 1;
        }
        if (close_checked(process.hProcess) < 0 && result == 0) {
            result = 1;
        }
    }
    return result;
}

void mainCRTStartup(void)
{
    ExitProcess(run_parent());
}
