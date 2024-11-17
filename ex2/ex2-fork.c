#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/wait.h>
#include "proc-common.h"

#define SLEEP_PROC_SEC  10
#define SLEEP_TREE_SEC  3

/*
 * Create this process tree:
 * A-+-B---D
 *   `-C
 */
void fork_procs(void) {
    /*
     * Initial process is A.
     */
    // The first process/child created by fork is called A. 
    // For the rest of the processes, A will act as the parent.
    // The PID of A will be the PID returned by the first fork in the main process,
    // i.e., pid_A = our_pid before our_pid becomes 0.
    change_pname("A");

    /* Process_B */
    pid_t pidB;
    int statusB;
    pidB = fork(); 
    // When executed by the parent (A), pidB is equal to the PID of B.
    printf("pid of B = %d\n", pidB);

    if (pidB < 0) {
        perror("B: fork");
        exit(1);
    }

    if (pidB == 0) {
        change_pname("B");

        // Process_D
        // To define D as a child of B, B must act as the parent. 
        // Therefore, pidB = fork() must return 0, meaning this block is "called" by B.
        pid_t pidD;
        int statusD;
        pidD = fork();
        printf("pid of D = %d\n", pidD);

        if (pidD < 0) {
            perror("D: fork");
            exit(1);
        }

        if (pidD == 0) {
            change_pname("D");
            printf("D: Sleeping...\n");
            sleep(SLEEP_PROC_SEC);
            printf("D: Exiting...\n");
            exit(13);
        }

        // Similar to A, since B is the parent, we need to sleep it 
        // so that its child (D) can be created.
        printf("B: Sleeping...\n");
        sleep(SLEEP_PROC_SEC);

        pidD = wait(&statusD); 
        // Make the parent wait for all its children to terminate (here, B has one child: D).
        explain_wait_status(pidD, statusD);
        printf("B: Exiting...\n");
        exit(19);
    }

    /* Process_C */
    // In the first fork for B, we don’t enter if (pidB == 0), so we call fork again
    // to give A a second child, C.
    pid_t pidC;
    int statusC;
    pidC = fork();
    printf("pid of C = %d\n", pidC);

    if (pidC < 0) {
        perror("C: fork");
        exit(1);
    }

    if (pidC == 0) {
        change_pname("C");
        printf("C: Sleeping...\n");
        sleep(SLEEP_PROC_SEC);
        // Sleep C to allow the tree to be created and printed in time.
        printf("C: Exiting...\n");
        exit(17);
    }

    // Again, in the first fork (from A), we don’t enter if (pid == 0), 
    // so we sleep the parent to let one of the child processes call fork 
    // and enter its respective if (pid == 0) block.
    printf("A: Sleeping...\n");
    sleep(SLEEP_PROC_SEC);

    // The first process to wake up will be the first one we slept, which is A.
    // Make the parent wait for all its children to terminate.
    // When one process wakes up, it proceeds with the next statement after sleep,
    // which is the printf("Exiting") and returns a specific exit status.
    pidB = wait(&statusB);
    explain_wait_status(pidB, statusB);
    pidC = wait(&statusC);
    explain_wait_status(pidC, statusC);

    printf("A: Exiting...\n");
    exit(16);
}

/*
 * The initial process forks the root of the process tree,
 * waits for the process tree to be completely created,
 * then takes a photo of it using show_pstree().
 *
 * How to wait for the process tree to be ready?
 * In ask2-{fork, tree}:
 *      wait for a few seconds, hope for the best.
 * In ask2-signals:
 *      use wait_for_ready_children() to wait until
 *      the first process raises SIGSTOP.
 */
int main(void) {
    pid_t pid;
    int status;

    /* Fork root of process tree */
    pid = fork();
    printf("our pid = %d\n", pid);

    if (pid < 0) {
        perror("main: fork");
        exit(1);
    }

    if (pid == 0) {
        /* Child */
        fork_procs();
        exit(1);
    }

    /*
     * Father
     */
    /* For ask2-signals */
    /* wait_for_ready_children(1); */

    /* For ask2-{fork, tree} */
    // Sleep the parent to allow the tree to be created.
    // When it wakes up, the tree will be ready to print.
    sleep(SLEEP_TREE_SEC);

    /* Print the process tree rooted at pid */
    printf("father is awake!... \n");
    show_pstree(pid);

    /* For ask2-signals */
    /* kill(pid, SIGCONT); */

    /* Wait for the root of the process tree to terminate */
    printf("father is waiting \n");
    // Make the parent wait for all its children to terminate.
    // Here, the initial parent process only has A as its child,
    // so the PID returned by wait will be A's PID.
    pid = wait(&status);
    explain_wait_status(pid, status); // It will indicate that A has terminated.
    return 0;
}
