// This program computes and draws the Mandelbrot set using multiple threads. 
// The threads compute portions of the Mandelbrot set, each taking care of a series of lines. 
// Synchronization between threads is managed using semaphores to ensure that output is done sequentially. 
// The use of semaphores ensures that only one thread writes output at a time.

/*
* mandel.c
*
* A program to draw the Mandelbrot Set on a 256-color xterm. *
*/
#include <errno.h>
#include <unistd.h>
#include <pthread.h>
#include <stdio.h>
#include <unistd.h>
#include <assert.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include <semaphore.h>
#include "mandel-lib.h"
#define MANDEL_MAX_ITERATION 100000

/*************************** * Compile-time parameters * ***************************/
/*
* Output at the terminal is x_chars wide by y_chars long
*/
int y_chars = 50; // Number of lines
int x_chars = 90; // Number of columns

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

// We create the semaphore
sem_t *sem;

// FROM phthread-test.c
/*
* POSIX thread functions do not return error numbers in errno,
* but in the actual return value of the function call instead.
* This macro helps with error reporting in this case.
*/
#define perror_pthread(ret, msg) \
do { errno = ret; perror(msg); } while (0)

/*
* A (distinct) instance of this structure is passed to each thread
*/
struct thread_info_struct {
    pthread_t tid; /* POSIX thread id, as returned by the library */
    int thrid; /* Application-defined thread id */
    int thrcnt; 
};

int safe_atoi(char *s, int *val) {
    long l;
    char *endp;
    l = strtol(s, &endp, 10);
    if (s != endp && *endp == '\0') {
        *val = l;
        return 0;
    } else
        return -1;
}

void *safe_malloc(size_t size) {
    void *p;
    if ((p = malloc(size)) == NULL) {
        fprintf(stderr, "Out of memory, failed to allocate %zd bytes\n", size);
        exit(1);
    }
    return p;
}

void usage(char *argv0) {
    fprintf(stderr, "Usage: %s thread_count array_size\n\n" 
            "Exactly two arguments required:\n"
            " thread_count: The number of threads to create.\n"
            " array_size: The size of the array to run with.\n", argv0);
    exit(1);
}

/*
* This function computes a line of output as an array of x_char color values.
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

/*
* The function to compute and output a Mandelbrot line
*/
void* compute_and_output_mandel_line(void *arg) {
    /*
    * A temporary array, used to hold color values for the line being drawn
    */
    int color_val[x_chars];
    struct thread_info_struct *thr = arg;
    int i;

    // Each thread takes care of the lines i, i + n, i + 2×n, i + 3×n, ..., where n = thrcnt = the number of threads
    // All threads can perform the compute operation in parallel, but the output must be done sequentially,
    // so we identify the critical part of the code as output_mandel_line(1, color_val);
    compute_mandel_line(i, color_val);
    sem_wait(&sem[thr->thrid]);

    // I tell the next semaphore to increase its value by 1, thus allowing the wait_sem to pass
    // We use the mod operation, because when we reach the last thread (i = thrid), (thr->thrid) + 1 will not exist
    output_mandel_line(1, color_val);
    sem_post(&sem[((thr->thrid) + 1) % thr->thrcnt]);

    return NULL;
}

int main(int argc, char *argv[]) {
    int i, thrcnt, ret;
    struct thread_info_struct *thr;

    /*
    * Parse the command line
    */
    if (argc != 2) usage(argv[0]);

    // thrcnt = the number of threads passed as input
    if (safe_atoi(argv[1], &thrcnt) < 0 || thrcnt <= 0) {
        fprintf(stderr, "`%s' is not valid for `thread_count'\n", argv[1]);
        exit(1);
    }

    /*
    * Allocate and initialize big array of doubles
    */
    thr = safe_malloc(thrcnt * sizeof(*thr)); // Array of structs
    sem = safe_malloc(thrcnt * sizeof(sem_t)); // An array to contain as many semaphores as there are threads
    
    xstep = (xmax - xmin) / x_chars;
    ystep = (ymax - ymin) / y_chars;

    /*
    * Draw the Mandelbrot Set, one line at a time.
    * Output is sent to file descriptor '1', i.e., standard output.
    */
    for (i = 0; i < thrcnt; i++) {
        /* Initialize per-thread structure */
        thr[i].thrid = i;
        thr[i].thrcnt = thrcnt;
        
        // The first thread is the only one that will "enter" the sem_wait,
        // and that's why we gave it an initial value of 1 (so the wait operation will decrease it by 1)
        // While the others are initialized to be in a locked state
        if (i == 0) {
            sem_init(&sem[i], 0, 1); // Unlocked state
        } else {
            sem_init(&sem[i], 0, 0); // Locked state
        }

        /* Spawn new thread */
        ret = pthread_create(&thr[i].tid, NULL, compute_and_output_mandel_line, &thr[i]);
        if (ret) {
            perror_pthread(ret, "pthread_create");
            exit(1);
        }
    }

    // Wait for each created thread to finish
    for (i = 0; i < thrcnt; i++) {
        ret = pthread_join(thr[i].tid, NULL);
        if (ret) {
            perror_pthread(ret, "pthread_join");
            exit(1);
        }
    }

    // Destroy the semaphores
    sem_destroy(sem);
    reset_xterm_color(1);
    return 0;
}
