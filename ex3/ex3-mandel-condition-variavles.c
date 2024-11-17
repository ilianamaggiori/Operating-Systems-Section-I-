// This program calculates and displays the Mandelbrot set in parallel using multiple threads.
// Each thread handles a section of the computation, with the output being serialized
// using a mutex and condition variable to ensure that the lines are printed in the correct order.

/*
* mandel.c
*
* A program to draw the Mandelbrot Set on a 256-color xterm. *
*/
// CONDITION VARIABLES
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

// Create the mutex
pthread_mutex_t mutex; 
pthread_cond_t condition; 
int count = 0;

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

    // Each thread takes care of the lines i, i + n, i + 2×n, i + 3×n,... where n=thrcnt=the number of threads
    for (i = thr->thrid; i < y_chars; i += thr->thrcnt) {
        // All threads can do the compute operation in parallel, but the output must be done sequentially,
        // so we identify the critical part of the code as output_mandel_line(1, color_val);
        compute_mandel_line(i, color_val);

        // All threads arrive here, and only one will enter
        pthread_mutex_lock(&mutex);

        // We use the count variable so that the threads enter the output_mandel_line in order and correctly,
        // ensuring that the lines are printed in the correct sequence
        // The thread whose thrid is equal to count is the one that proceeds to print the line
        // to avoid printing a line before another one.
        while (count != thr->thrid) {
            // Threads that are not their turn to print will wait (giving the lock key to the next thread)
            // until the condition is met, practically meaning until the thread that is to print wakes them up.
            pthread_cond_wait(&condition, &mutex);
        }
        output_mandel_line(1, color_val);

        // Increment the count so we know which thread will print next, and thus, which lines will be printed.
        // We need the mod operation because the count should go from 0 to thrid of the last thread,
        // and after the last thread prints its line, we go back to the first thread and repeat the process.
        count = (count + 1) % thr->thrcnt;

        // Since we have cond_broadcast, this wakes up all threads waiting in the while loop. Then, the count has
        // changed, so all threads check if the while condition is met. For those that still meet the condition,
        // they go back to sleep, while the one that wakes up doesn't sleep again and proceeds to print.
        pthread_cond_broadcast(&condition);

        // Unlock because we have completed the critical section of code
        pthread_mutex_unlock(&mutex);
    }

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

    // Create an array of objects (structs) where each entry stores the information for the corresponding thread
    thr = safe_malloc(thrcnt * sizeof(*thr));
    xstep = (xmax - xmin) / x_chars;
    ystep = (ymax - ymin) / y_chars;

    // Initialize the array
    for (i = 0; i < thrcnt; i++) {
        /* Initialize per-thread structure */
        thr[i].thrid = i;
        thr[i].thrcnt = thrcnt;

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

    // Destroy the mutex and condition since we are done and no longer need them
    pthread_mutex_destroy(&mutex); 
    pthread_cond_destroy(&condition);

    reset_xterm_color(1);
    return 0;
}
