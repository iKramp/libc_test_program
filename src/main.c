#include "syscalls/filesystem.h"

int strlen(const char *str) {
    int len = 0;
    while (str[len] != '\0') {
        len++;
    }
    return len;
}

int main() {
    
    char *path = "/test.txt";
    uint64_t path_len = strlen(path);

    int res = _fopen(path_len, path, 0, make_open_flags(1, 0, 0, 0));
    if (res < 0) {
        // Handle error
        return -1;
    }

    int fd = res;

    char buffer[100];
    int bytes_read = _fread(fd, 100, buffer);
    if (bytes_read < 0) {
        // Handle error
        return -1;
    }

    _fclose(fd);

    char *tty_path = "/tty";
    uint64_t tty_path_len = strlen(tty_path);
    int tty_fd = _fopen(tty_path_len, tty_path, 0, make_open_flags(0, 1, 0, 0));
    if (tty_fd < 0) {
        // Handle error
        return -1;
    }

    int bytes_written = _fwrite(tty_fd, bytes_read, buffer);
    if (bytes_written < 0) {
        // Handle error
        return -1;
    }

    _fclose(tty_fd);

    return 0;
}
