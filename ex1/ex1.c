#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>

// Writes up to len bytes from the buffer starting at buff 
// to the file referred to by the file descriptor fd.
void doWrite(int fd, const char *buff, int len) {
    size_t idx = 0;  // Buffer index
    ssize_t wcnt;    // Number of bytes written

    do {
        wcnt = write(fd, buff + idx, len - idx);
        if (wcnt == -1) {
            perror("write");
            close(fd);
            exit(1);
        }
        idx += wcnt;
    } while (idx < len);
}

// Writes the infile’s content into a buffer
// and then it calls the doWrite function in order to transfer the buffer’s content 
// into the file referred to by the file descriptor fd.
void write_file(int fd, const char *infile) {
    int fd_infile = open(infile, O_RDONLY);  // File descriptor of the invoked file
    char buff[1024];                         // Initialization of the buffer at 1kB
    ssize_t rcnt;                            // Number of bytes read

    if (fd_infile == -1) {
        perror(infile);
        exit(1);
    }

    for (;;) {
        rcnt = read(fd_infile, buff, sizeof(buff) - 1);
        if (rcnt == 0) {
            break;  // rcnt = 0 means end-of-file
        }
        if (rcnt == -1) {
            perror("read");
            exit(1);
        }
        buff[rcnt] = '\0';
        doWrite(fd, buff, rcnt);  // Writes from the buffer to the file referred by fd
    }

    close(fd_infile);
}

int main(int argc, char **argv) {
    if (argc != 3 && argc != 4) {  // Checks for the right amount of arguments
        printf("Usage: ./fconc infile1 infile2 [outfile (default:fconc.out)]\n");
        exit(1);
    }

    int fd_A = open(argv[1], O_RDONLY);  // File descriptor of the first argument
    if (fd_A == -1) {                    // Checks if the first argument exists
        perror(argv[1]);
        exit(1);
    }

    int fd_B = open(argv[2], O_RDONLY);  // File descriptor of the second argument
    if (fd_B == -1) {                    // Checks if the second argument exists
        perror(argv[2]);
        close(fd_A);
        exit(1);
    }

    // Now that we have checked that the 2 files exist, 
    // we write them together to a third file
    int oflags = O_CREAT | O_WRONLY | O_TRUNC;
    int mode = S_IRUSR | S_IWUSR;
    int fd_C;  // File descriptor of the final file

    if (argc == 3) {
        fd_C = open("fconc.out", oflags, mode);  // If the final file doesn't exist
    } else {
        fd_C = open(argv[3], oflags, mode);     // File descriptor of the given final file
    }

    if (fd_C == -1) {
        perror("open");
        close(fd_A);
        close(fd_B);
        exit(1);
    }

    // Write the two given files to the final file
    write_file(fd_C, argv[1]);
    write_file(fd_C, argv[2]);

    close(fd_A);
    close(fd_B);
    close(fd_C);  // Close all file descriptors

    return 0;
}
