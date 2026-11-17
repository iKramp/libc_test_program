#include <stdio.h>
#include <syscalls/time.h>
#include <syscalls/proc.h>
#include "extra.h"

int main() {
    char buffer[10];

    int child_pid = fork();

    // FILE *tty_read = fopen("/tty", "r");
    // FILE *tty_write = fopen("/tty", "a");
    FILE *out = freopen("/tty", "a", stdout);

    if (child_pid < 0) {
        printf("Failed to create child process.\n");
        return 1;
    } else if (child_pid == 0) {
        // Child process
        printf("Child process is running.\n");
        // Perform child-specific tasks here
    } else {
        // Parent process
        printf("Parent process created a child process (PID: %d).\n", child_pid);
        // Perform parent-specific tasks here
    }

    
    return 0;
}

