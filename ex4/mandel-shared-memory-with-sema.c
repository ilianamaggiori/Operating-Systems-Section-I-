/*
* mandel.c
*
* A program to draw the Mandelbrot Set on a 256-color xterm. *
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

/* TODO header file for m(un)map */
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

// Create the semaphore
sem_t *sem;

/*
* A (distinct) instance of this structure is passed to each process
*/
struct process_info_struct {
    pid_t pid;
    // int* color_val; /* Pointer to array to manipulate */
    int mypid; /* Application-defined process id */
    int pcnt;   /* Number of processes */
};

int safe_atoi(char *s, int *val) {
    long l;
    char *endp;
    l = strtol(s, &endp, 10);
    if (*endp == '\0') {
        *val = l;
        return 0;
    } else {
        return -1;
    }
}

void usage(char *argv0)
{
    fprintf(stderr, "Usage: %s thread_count array_size\n\n", argv0);
    exit(1);
}

/*
* Exactly two arguments required:
* thread_count: The number of threads to create.
* array_size: The size of the array to run with.
*/
 
/*
* This function computes a line of output
* as an array of x_char color values.
*/
void compute_mandel_line(int line, int color_val[]) {
    /*
    * x and y traverse the complex plane.
    */
    double x, y;
    int n;
    int val;

    /* Find out the y value corresponding to this line */
    y = ymax - ystep * line;

    /* and iterate for all points on this line */
    for (x = xmin, n = 0; n < x_chars; x += xstep, n++) {
        /* Compute the point's color value */
        val = mandel_iterations_at_point(x, y, MANDEL_MAX_ITERATION);
        if (val > 255)
            val = 255;
        /* And store it in the color_val[] array */
        val = xterm_color(val);
        color_val[n] = val;
    }
}

/*
* This function outputs an array of x_char color values to a 256-color xterm.
*/
void output_mandel_line(int fd, int color_val[]) {
    int i;
    char point = '@';
    char newline = '\n';

    for (i = 0; i < x_chars; i++) {
        /* Set the current color, then output the point */
        set_xterm_color(fd, color_val[i]);
        if (write(fd, &point, 1) != 1) {
            perror("compute_and_output_mandel_line: write point");
            exit(1);
        }
    }

    /* Now that the line is done, output a newline character */
    if (write(fd, &newline, 1) != 1) {
        perror("compute_and_output_mandel_line: write newline");
        exit(1);
    }
}

void compute_and_output_mandel_line(void *arg)
{
    int n;
    struct process_info_struct *pr = arg;

    // The number of processes
    n = pr->pcnt;
    for (int i = pr->mypid; i < y_chars; i += pr->pcnt) {
        /*
        * A temporary array, used to hold color values for the line being drawn
        */
        int color_val[x_chars];

        // Each process takes care of lines i, i + n, i + 2×n, i + 3×n, ..., where
        // they can all compute in parallel, but the output must be done sequentially.
        // So we identify the critical section of the code as output_mandel_line(1, color_val);
        compute_mandel_line(i, color_val);

        sem_wait(&sem[pr->mypid]);
        // I tell the next semaphore to increase its value by 1, and thus allow the wait_sem to pass.

        // We use the mod because when we reach the last process (when i = mypid of the last process),
        // this (pr->mypid)+1) doesn't exist
        output_mandel_line(1, color_val);
        sem_post(&sem[((pr->mypid) + 1) % (pr->pcnt)]); // signal(SIGINT, signal_handler);
    }
}

/*
* Create a shared memory area, usable by all descendants of the calling process.
*/
void *create_shared_memory_area(unsigned int numbytes)
{
    int pages;
    void *addr;

    if (numbytes == 0) {
        fprintf(stderr, "%s: internal error: called for numbytes == 0\n", __func__);
        exit(1);
    }

    /*
    * Determine the number of pages needed, round up the requested number of pages
    */
    pages = (numbytes - 1) / sysconf(_SC_PAGE_SIZE) + 1;

    /* Create a shared, anonymous mapping for this number of pages */
    /* TODO: addr = mmap(...) */
    // PROT_READ | PROT_WRITE: The memory protection flags. The access rights for this memory area here are read and write.
    // MAP_SHARED | MAP_ANONYMOUS: The mapping type flags.
    // MAP_SHARED = the mapping is shared between processes, allowing multiple processes to access and modify the memory.
    // MAP_ANONYMOUS = indicates that the mapping is not associated with any file.
    // -1: The file descriptor. This argument is ignored when MAP_ANONYMOUS is specified, so a value of -1 is passed.
    // 0: The offset that indicates from which point the mapping starts. Here, it doesn't matter.

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
    * Determine the number of pages needed, round up the requested number of pages
    */
    pages = (numbytes - 1) / sysconf(_SC_PAGE_SIZE) + 1;

    if (munmap(addr, pages * sysconf(_SC_PAGE_SIZE)) == -1) {
        perror("destroy_shared_memory_area: munmap failed");
        exit(1);
    }
}

int main(int argc, char *argv[])
{
    int nprocs, i, status;
    struct process_info_struct *pr;
    pid_t p;

    /*
    * Parse the command line
    */
    if (argc != 2) usage(argv[0]);

    // nprocs = the number of processes passed as input
    if (safe_atoi(argv[1], &nprocs) < 0 || nprocs <= 0) {
        fprintf(stderr, "`%s' is not valid for `nprocs'\n", argv[1]);
        exit(1);
    }

    xstep = (xmax - xmin) / x_chars;
    ystep = (ymax - ymin) / y_chars;

    /*
    * Draw the Mandelbrot Set, one line at a time.
    * Output is sent to file descriptor '1', i.e., standard output.
    */
    // Array of structs, sizeof(*pr) calculates the size of a process_info_struct (sizeof(pr) finds the size of a pointer)
    pr = create_shared_memory_area(nprocs * sizeof(*pr));

    // Array of semaphores, which will be as many as the processes
    sem = create_shared_memory_area(nprocs * sizeof(sem_t));

    for (i = 0; i < nprocs; i++) {
        pr[i].mypid = i;
        pr[i].pcnt = nprocs;

        // The first process is the only one that will enter the sem_wait, and
        // that's why we initialized it with an initial value of 1 (so that wait decrements it by 1)

        // While the rest are initialized in a locked state
        if (i == 0) {
            sem_init(&sem[i], 1, 1); // unlocked-state
        } else {
            sem_init(&sem[i], 1, 0); // locked-state
        }
    }

    for (i = 0; i < nprocs; i++) {
        p = fork();

        if (p < 0) {
            perror("fork");
            exit(1); // die("fork");
        }

        if (p == 0) {
            pr[i].pid = getpid();
            compute_and_output_mandel_line(&pr[i]);
            return 0;
        }
    }

    // We make the parent wait for all of its children to finish
    for (i = 0; i < nprocs; i++) {
        p = wait(&status);
    }

    for (i = 0; i < nprocs; i++) {
        sem_destroy(&sem[i]);
    }

    // Free the space we created
    destroy_shared_memory_area(sem, nprocs * sizeof(sem_t));
    destroy_shared_memory_area(pr, nprocs * sizeof(*pr));
    reset_xterm_color(1);

    return 0;
}
