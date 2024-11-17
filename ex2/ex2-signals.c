#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include "tree.h"
#include "proc-common.h"

void fork_procs(struct tree_node *root) {
    /*
     * Start
     */
    printf("PID = %ld, name %s, starting...\n", (long)getpid(), root->name);
    change_pname(root->name);

    // Similar to the previous exercise
    // Create an array for each process to store the PIDs of its children
    int i, j;
    pid_t pid_child[root->nr_children];
    int status_child;

    // Create as many children as needed based on the process tree structure
    for (i = 0; i < root->nr_children; i++) {
        // Call fork to create each child and store its PID in the corresponding array position
        pid_child[i] = fork();
        printf("pid of child = %d\n", pid_child[i]); // Print the PID of the created child

        if (pid_child[i] < 0) {
            perror("pid_child: fork");
            exit(1);
        }

        // Once all children are created (when this loop finishes)
        // The parent will wait, and the fork will start being called recursively
        // From each child, creating the rest of the tree structure
        if (pid_child[i] == 0) {
            fork_procs(root->children + i);
        }
    }

    /*
     * Suspend Self
     */
    // After all children are created (when the loop finishes)
    // The parent process will stop itself and wait for all its children to stop
    if (root->nr_children != 0) { // Parent process
        // After wait_for_ready_children, we return to the fork call,
        // which will now be called by the children, starting the recursion
        wait_for_ready_children(root->nr_children); // Waits for all children to raise SIGSTOP
        raise(SIGSTOP); // Once all children have stopped, the parent also stops itself

        // We reach this printf the first time in main, after we tell the root of the tree to continue (send it SIGCONT)
        // In subsequent cases, we reach here when a process (with children) receives a SIGCONT signal
        printf("PID = %ld, name = %s is awake\n", (long)getpid(), root->name);

        // After printing the tree in main, we wake up all processes one by one, starting from the root's children
        for (j = 0; j < root->nr_children; j++) {
            printf("j = %d\npid_child = %d\n", j, pid_child[j]);
            // Send SIGCONT to the children of the current process
            kill(pid_child[j], SIGCONT); // After this, the code continues from where SIGSTOP was raised in the respective node
            pid_child[j] = wait(&status_child); // The parent waits for its child to terminate
            explain_wait_status(pid_child[j], status_child);
        }
    } else { // Leaf process
        // Leaves should immediately raise SIGSTOP when created
        // Since they have no children to wait for
        raise(SIGSTOP);

        // We reach here after sending a SIGCONT signal to a process (without children)
        printf("PID = %ld, name = %s is awake\n", (long)getpid(), root->name);
    }

    /*
     * Exit
     */
    printf("%s: Exiting...\n", root->name);
    exit(16); // All processes return the same exit status upon termination
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
int main(int argc, char *argv[]) {
    pid_t pid; // The PID of A will be this
    int status;
    struct tree_node *root;

    // Print the tree structure provided as input
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <tree_file>\n", argv[0]);
        exit(1);
    }

    /* Read tree into memory */
    // Get the root of the tree
    root = get_tree_from_file(argv[1]);

    /* Fork root of process tree */
    pid = fork();
    printf("pid from fork in main is: %d\n", pid);

    if (pid < 0) {
        perror("main: fork");
        exit(1);
    }

    if (pid == 0) {
        /* Child */
        fork_procs(root);
        exit(1);
    }

    /*
     * Father
     */
    /* for ask2-signals */
    // The parent process waits for "all" its children to raise SIGSTOP (i.e., to stop)
    // (Here, "all" is 1 because the initial process has only 1 child (the root),
    // which is created with the fork and does not directly "see" the entire tree)
    wait_for_ready_children(1);

    // We reach here once the parent raises SIGSTOP in fork_procs, i.e., at the root of the tree
    // At this point, all nodes-processes in the tree have raised SIGSTOP,
    // And we can print the tree safely without the risk of any process terminating unexpectedly
    /* Print the process tree rooted at pid */
    show_pstree(pid);

    // After printing the tree, wake up (resume) the child of the parent process
    // Which will be the root of the tree. The code then proceeds from the line
    // Right after the raise(SIGSTOP) command in the if (root->nr_children != 0)
    // Block concerning the process with children
    /* for ask2-signals */
    kill(pid, SIGCONT); // Tell the root of the tree to resume since it was previously stopped

    /* Wait for the root of the process tree to terminate */
    // The parent process then waits for all its children to terminate
    // (Here, only the root, but effectively we wait for all nodes-processes in the tree to terminate)
    wait(&status);
    explain_wait_status(pid, status);

    return 0;
}
