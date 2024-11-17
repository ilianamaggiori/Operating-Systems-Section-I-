#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/wait.h>
#include "proc-common.h"
#include "tree.h"

#define SLEEP_PROC_SEC  10
#define SLEEP_TREE_SEC  3

void fork_procs(struct tree_node *root) {
    change_pname(root->name);
    int i, j;

    // Create an array to store the PIDs of the children for each process
    pid_t pid_child[root->nr_children];
    int status_child;

    // Create as many children as the current process has (defined in `root->nr_children`)
    for (i = 0; i < root->nr_children; i++) {
        // Fork the process to create a child and store its PID in the array
        pid_child[i] = fork();
        printf("pid_child = %d\n", pid_child[i]); // Print the PID of the created child

        if (pid_child[i] < 0) {
            perror("pid_child: fork");
            exit(1);
        }

        // After creating all children (when the `for` loop finishes),
        // the parent will sleep, allowing the child processes to begin.
        // Each child process will then enter this `if` block and proceed with recursion
        // to create the rest of the tree.
        if (pid_child[i] == 0) {
            fork_procs(root->children + i);
            printf("Recursion finished, returning\n");
        }
    }

    // Sleep the parent to allow the rest of the tree to be created
    printf("%s: Sleeping...\n", root->name);
    sleep(SLEEP_PROC_SEC);

    // The first process to wake up will be the root, as it was put to sleep first
    printf("%s is now awake...\n", root->name);

    // If the current process has children, wait for them to terminate
    if (root->nr_children != 0) {
        for (j = 0; j < root->nr_children; j++) {
            printf("j = %d\npid_child = %d\n", j, pid_child[j]);
            pid_child[j] = wait(&status_child);
            explain_wait_status(pid_child[j], status_child);
        }
    }

    // The current process exits after its children are done
    printf("%s: Exiting...\n", root->name);
    exit(16);
}

int main(int argc, char *argv[]) {
    // Print the tree structure given as input
    struct tree_node *root;
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <input_tree_file>\n\n", argv[0]);
        exit(1);
    }

    root = get_tree_from_file(argv[1]);
    print_tree(root);

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
        fork_procs(root);
        printf("fork_procs has finished running\n");
        exit(1);
    }

    /*
     * Father
     */
    /* for ask2-signals */
    /* wait_for_ready_children(1); */

    /* for ask2-{fork, tree} */
    // Sleep the parent to allow the tree to be created before it terminates
    sleep(SLEEP_TREE_SEC);

    /* Print the process tree rooted at `pid` */
    // The parent process wakes up, and the rest of the tree is still asleep
    // (so no one has terminated yet). Thus, the tree can be printed.
    printf("father is awake!...\n");
    show_pstree(pid);

    /* for ask2-signals */
    /* kill(pid, SIGCONT); */

    /* Wait for the root of the process tree to terminate */
    printf("father is waiting\n");
    pid = wait(&status);
    // Make the parent process wait for all its children to terminate.
    explain_wait_status(pid, status); // This will show that the root (A) has terminated

    return 0;
}
