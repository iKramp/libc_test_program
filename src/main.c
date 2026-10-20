#include <stdio.h>

int main() {
    char buffer[100];

    FILE *tty_read = fopen("/tty", "r");
    FILE *tty_write = fopen("/tty", "+");

    if (tty_read == NULL || tty_write == NULL) {
        // Handle error
        return -1;
    }

    fprintf(tty_write, "Hello, tty! Testing my awesome prinft. Enter your name\n");
    int read_bytes = fread(buffer, 1, sizeof(buffer), tty_read);
    fprintf(tty_write, "You entered: %s\n", buffer);
    fprintf(tty_write, "Read %d bytes from tty\n", read_bytes);
    
    fclose(tty_read);
    fclose(tty_write);

    return 0;
}

