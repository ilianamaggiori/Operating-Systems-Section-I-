/*
 * mandel.c
 * A program to draw the Mandelbrot Set on a 256-color xterm.
 */

#include <stdio.h>
#include <unistd.h>
#include <assert.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include <errno.h>
#include <signal.h>
#include <sys/wait.h>
#include <semaphore.h>
#include <sys/mman.h>
#include "mandel-lib.h"

#define MANDEL_MAX_ITERATION 100000

/***************************
 * Compile-time parameters *
 ***************************/

/*
 * Output at the terminal is x_chars wide by y_chars long
 */
int y_chars = 50;
int x_chars = 90;

/*
 * The part of the complex plane to be drawn:
 * upper left corner is (xmin, ymax), lower right corner is (xmax, ymin)
 */
double xmin = -1.8, xmax = 1.0;
double ymin = -1.0, ymax = 1.0;

/*
 * Every character in the final output is
 * xstep x ystep units wide on the complex plane.
 */
double xstep;
double ystep;

/*
 * A (distinct) instance of this structure
 * is passed to each process.
 */
struct process_info_struct {
    pid_t pid;
    // int* color_val; /* Pointer to array to manipulate */
    int mypid; /* Application-defined process id */
    int pcnt;
};

int safe_atoi(char *s, int *val) {
    long l;
    char *endp;
    l = strtol(s, &endp, 10);
    if (s != endp && *endp == '\0') {
        *val = l;
        return 0;
    } else {
        return -1;
    }
}

void usage(char *argv0) {
    fprintf(stderr,
        "Usage: %s thread_count array_size\n\n"
        "Exactly two arguments required:\n"
        "  thread_count: The number of threads to create.\n"
        "  array_size: The size of the array to run with.\n",
        argv0);
    exit(1);
}

/*
 * This function computes a line of output
 * as an array of x_char color values.
 */
void compute_mandel_line(int line, int color_val[]) {
    /*
     * x and y traverse the complex plane.
     */
    double x, y;
    int n, val;

    /* Find out the y value corresponding to this line */
    y = ymax - ystep * line;

    /* And iterate for all points on this line */
    for (x = xmin, n = 0; n < x_chars; x += xstep, n++) {
        /* Compute the point's color value */
        val = mandel_iterations_at_point(x, y, MANDEL_MAX_ITERATION);
        if (val > 255) val = 255;

        /* And store it in the color_val[] array */
        val = xterm_color(val);
        color_val[n] = val; // Array of x_chars cells for each line
    }
}

/*
 * This function outputs an array of x_char color values
 * to a 256-color xterm.
 */
void output_mandel_line(int fd, int *buffer) {
    int i, j;
    char point = '@';
    char newline = '\n';

    // In previous exercises, this function printed line-by-line.
    // Now all data to be saved is in the "2D" buffer, so we just print its contents
    // using a double for-loop.
    // There's no problem with print order (and thus no need for synchronization),
    // since this function is called by the parent process after all its children have terminated.
    for (i = 0; i < y_chars; i++) {
        for (j = 0; j < x_chars; j++) {
            /* Set the current color, then output the point */
            set_xterm_color(fd, buffer[(i * x_chars) + j]);
            if (write(fd, &point, 1) != 1) {
                perror("compute_and_output_mandel_line: write point");
                exit(1);
            }
        }
        // Add the newline
        printf("%c", newline);
    }

    /* Now that the line is done, output a newline character */
    if (write(fd, &newline, 1) != 1) {
        perror("compute_and_output_mandel_line: write newline");
        exit(1);
    }
}

void compute_and_store_mandel_line(void *arg, int *buffer) {
    /*
     * A temporary array, used to hold color values for the line being drawn.
     */
    int color_val[x_chars];
    struct process_info_struct *pr = arg;
    int i, j;

    // This function now computes each line and stores it in the correct position
    // in the shared buffer between processes.
    // Each process computes a different line and stores its result in a different
    // "part" of the buffer.
    // This way, there's no risk of a process overwriting another's calculations.
    for (i = pr->mypid; i < y_chars; i += pr->pcnt) {
        compute_mandel_line(i, color_val);
        for (j = 0; j < x_chars; j++) {
            buffer[(i * x_chars) + j] = color_val[j];
        }
    }
}

/*
 * Create a shared memory area, usable by all descendants of the calling process.
 */
void *create_shared_memory_area(unsigned int numbytes) {
    int pages;
    void *addr;

    if (numbytes == 0) {
        fprintf(stderr, "%s: internal error: called for numbytes == 0\n", __func__);
        exit(1);
    }

    /*
     * Determine the number of pages needed, round up the requested number of pages.
     */
    pages = (numbytes - 1) / sysconf(_SC_PAGE_SIZE) + 1;

    /* Create a shared, anonymous mapping for this number of pages */
    addr = mmap(NULL, pages * sysconf(_SC_PAGE_SIZE), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);

    if (addr == MAP_FAILED) {
        perror("mmap");
        exit(1);
    }

    return addr;
}

void destroy_shared_memory_area(void *addr, unsigned int numbytes) {
    int pages;

    if (numbytes == 0) {
        fprintf(stderr, "%s: internal error: called for numbytes == 0\n", __func__);
        exit(1);
    }

    /*
     * Determine the number of pages needed, round up the requested number of pages.
     */
    pages = (numbytes - 1) / sysconf(_SC_PAGE_SIZE) + 1;

    if (munmap(addr, pages * sysconf(_SC_PAGE_SIZE)) == -1) {
        perror("destroy_shared_memory_area: munmap failed");
        exit(1);
    }
}

int main(int argc, char *argv[]) {
    int line, nprocs, i, status;
    struct process_info_struct *pr;
    pid_t p;
    int *buf;

    // Check for incorrect number of arguments
    if (argc != 2) usage(argv[0]);

    // nprocs = the number of processes passed as input
    if (safe_atoi(argv[1], &nprocs) < 0 || nprocs <= 0) {
        fprintf(stderr, "`%s' is not valid for `nprocs'\n", argv[1]);
        exit(1);
    }

    xstep = (xmax - xmin) / x_chars;
    ystep = (ymax - ymin) / y_chars;

    // Array of structs, sizeof(*pr) calculates the size of one process_info_struct (sizeof(pr) gives the size of a pointer)
    pr = create_shared_memory_area(nprocs * sizeof(*pr));

    // Shared buffer between processes
    buf = create_shared_memory_area(y_chars * x_chars * sizeof(int));

    for (i = 0; i < nprocs; i++) {
        pr[i].mypid = i;
        pr[i].pcnt = nprocs;
    }

    for (i = 0; i < nprocs; i++) {
        p = fork();
        if (p < 0) {
            perror("fork");
            exit(1); // die("fork");
        }

        if (p == 0) {
            pr[i].pid = getpid();
            compute_and_store_mandel_line(&pr[i], buf);
            return 0;
        }
    }

    // Make the parent wait for all its children to terminate
    for (i = 0; i < nprocs; i++) {
        p = wait(&status);
    }

    // Print the buffer after it has been created
    output_mandel_line(1, buf);

    // Free the memory we created
    destroy_shared_memory_area(pr, nprocs * sizeof(*pr));
    destroy_shared_memory_area(buf, y_chars * x_chars * sizeof(int));

    reset_xterm_color(1);
    return 0;
}
