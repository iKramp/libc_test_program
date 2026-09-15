#include "syscalls/filesystem.h"
#include "memory.h"
#include <sys/abi/filesystem.h>
#include "syscalls/syscall_generic.h"

int strlen(const char *str) {
    int len = 0;
    while (str[len] != '\0') {
        len++;
    }
    return len;
}

int main() {

    char *test_line = "Hello, World!\n";

    char buffer[1024];
    
    char *tty_path = "/tty";
    uint64_t tty_path_len = strlen(tty_path);
    int tty_fd = _fopen(tty_path_len, tty_path, 0, make_open_flags(1, 1, 0, 0));
    if (tty_fd < 0) {
        // Handle error
        return -1;
    }

    while (1) {
        uint64_t bytes_written = _fwrite(tty_fd, strlen(test_line), test_line);

        if (bytes_written >= ((uint64_t)1 << 63)) {
            // Handle error
            break;
        }

        syscall_2ret ret = _fread(tty_fd, 1024, buffer, make_read_flags(0));
        uint64_t bytes_read = ret.ret0;
        uint64_t ret_result = ret.ret1;
        if (bytes_read >= ((uint64_t)1 << 63)) {
            // Handle error
            break;
        }

        if (bytes_read > 0) {
            _fwrite(tty_fd, bytes_read, buffer);
        }

        if (ret_result == PERMANENT_EOF) {
            break;
        }
    }

    _fclose(tty_fd);

    return 0;
}
