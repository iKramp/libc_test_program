#include <stdio.h>

int main() {
    char buffer[10];

    FILE *tty_read = fopen("/tty", "r");
    FILE *tty_write = fopen("/tty", "a");

    if (tty_read == NULL || tty_write == NULL) {
        // Handle error
        return -1;
    }

    while (1) {
        size_t bytes_read = fread(buffer, 1, sizeof(buffer), tty_read);
        if (bytes_read > 0) {
            fwrite(buffer, 1, bytes_read, tty_write);
        }

        if (feof(tty_read)) {
            break;
        }
    }
    
    fclose(tty_read);
    fclose(tty_write);

    return 0;
}

