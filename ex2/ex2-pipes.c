#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <assert.h>
#include <string.h>
#include <sys/wait.h>
#include "proc-common.h"
#define SLEEP_PROC_SEC 10
#define SLEEP_TREE_SEC 3
#include "tree.h"

void child(int fd, struct tree_node *root) {
    // Set the process name to the given name
    change_pname(root->name);
    printf("%s: Created\n", root->name);

    // Check if the tree is correct
    if ((strcmp(root->name, "+") == 0) || (strcmp(root->name, "*") == 0)) { // Non-terminal node (operator)
        // Must have exactly 2 children
        if (root->nr_children != 2) {
            perror("child: invalid number of children in + or *");
            exit(1);
        }

        // NON-TERMINAL NODE!
        int i, j;
        // Create an array to store the 2 pids of the non-terminal node's children
        pid_t pid_child[2];
        int status_child;
        double result[2];
        int pfd[2];

        // Create the pipe
        printf("%s: Creating pipe\n", root->name);
        if (pipe(pfd) < 0) {
            perror("pipe");
            exit(1);
        }

        // printf("pfd[0]= %d , pfd[1]= %d\n",pfd[0], pfd[1]);

        // Create the 2 children of the parent process and store their pids in the array
        for (i = 0; i < 2; i++) {
            pid_child[i] = fork();
            printf("our pid= %d\n", pid_child[i]);
            if (pid_child[i] < 0) {
                perror("pid_child: fork");
                exit(1);
            }
            if (pid_child[i] == 0) { // RECURSION
                // We want the child to write its data, so we give it the write end of the pipe
                // created when "running" from the parent, to communicate with its children
                // printf("h anadromh kaleitai gia fd=pfd[1]= %d\n", pfd[1]);
                child(pfd[1], root->children + i); // Where it sees fd, it will use pfd[1], and where it sees root, it will use root->children + i
            }
        }

        // Tell the parent process to wait for its 2 children to raise SIGSTOP
        wait_for_ready_children(2); // Returns to fork and enters the if for recursion
        raise(SIGSTOP); // After stopping the children with SIGSTOP, do the same for the parent
        printf("PID = %ld, name = %s is awake\n", (long)getpid(), root->name);

        // We have printed the tree in main and we wake them all up one by one, starting from the children of root
        for (j = 0; j < root->nr_children; j++) {
            // printf("j = %d\n pid_child = %d\n", j, pid_child[j]);
            // Send SIGCONT to the children of the current process
            kill(pid_child[j], SIGCONT); // After this, the code continues where we did SIGSTOP for each node
            pid_child[j] = wait(&status_child);
            explain_wait_status(pid_child[j], status_child); // After the child dies, we perform the operation
            
            double res = 0;
            // Close the write end of the pipe with which the parent communicates with the child
            // so there is no "active" fd with which the parent can still write to this pipe, and the program won't terminate
            // Here, we ensure the read operation only reads the characters we want
            // Even without the close(), it might work, but if we asked it to read everything from the read end, it would block
            close(pfd[1]);

            // The parent reads from the read end of the pipe, through which it communicates with the child,
            // the value that the child passed to the write end before it died
            if (read(pfd[0], &res, sizeof(res)) != sizeof(res)) {
                perror("parent: read from pipe");
                exit(1);
            }
            printf("res=%f\n", res);
            result[j] = res; // printf("result[%d]=%f\n", j, res);
        }

        // After the for loop, we have stored the values of the two children (who died)
        // and now we are in the parent
        double final_res = 0;
        if (strcmp(root->name, "+") == 0) {
            final_res = result[0] + result[1];
        } else {
            final_res = result[0] * result[1];
        }

        // printf("writing the final result to fd=%d\n", fd);
        // printf("while pfd[1]=%d\n", pfd[1]);

        // Write the final result to the write end of the pipe connecting the process to its parent,
        // not the pipe connecting it to its children (in that case, we would write to pfd[1])
        // Here, we want to pass the value from the child to the same fd passed in the recursion
        if (write(fd, &final_res, sizeof(final_res)) != sizeof(final_res)) {
            perror("parent: write to pipe");
            exit(1);
        }
    }
    // TERMINAL NODE!
    else {
        if (root->nr_children != 0) {
            perror("child: invalid number of children in digit");
            exit(1);
        }

        // Leaves just need to raise SIGSTOP as soon as they are created, since they have no children
        raise(SIGSTOP);

        // We reach here once we send a SIGCONT signal to a process (without children)
        printf("PID = %ld, name = %s is awake\n", (long)getpid(), root->name);

        // Pass the value of the child to the pipe that it communicates with its parent
        double root_name = atof(root->name); // Convert the pointer to double for the write operation to work correctly
        // We want to write the value of the child to the pipe so that the child "sends" its value to the parent,
        // which can then read it from the read end of the same pipe
        if (write(fd, &root_name, sizeof(root_name)) != sizeof(root_name)) {
            perror("child: write to pipe");
            exit(1);
        }

        close(fd);
    }

    /*
    * Exit
    */
    printf(" %s: Exiting...\n", root->name);
    exit(16);
}

int main(int argc, char *argv[]) {
    // Print the tree we get as input
    struct tree_node *root;
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <input_tree_file>\n\n", argv[0]);
        exit(1);
    }

    // Get the root
    root = get_tree_from_file(argv[1]);
    print_tree(root);

    pid_t p;
    int status;
    int pfd[2];

    printf("Parent: Creating pipe...\n");
    if (pipe(pfd) < 0) {
        perror("pipe");
        exit(1);
    }

    printf("Parent: Creating child...\n");
    p = fork();
    if (p < 0) {
        /* fork failed */
        perror("fork");
        exit(1);
    }

    if (p == 0) {
        /* In child process */
        child(pfd[1], root);
        /*
        * Should never reach this point,
        * child() does not return
        */
        assert(0);
    }

    wait_for_ready_children(1);

    /* Print the process tree root at pid */
    // When the whole tree has raised SIGSTOP, we print it
    show_pstree(p);

    // Wake up the root process of all
    kill(p, SIGCONT);

    /* Wait for the child to terminate */
    printf("Parent: Created child with PID = %ld, waiting for it to terminate...\n", (long)p);
    p = wait(&status); // No difference even if we just use wait(&status); since the initial process only has 1 child
    explain_wait_status(p, status);

    // Once all children die, we get the final result from the pipe
    double final_val;
    if (read(pfd[0], &final_val, sizeof(final_val)) != sizeof(final_val)) {
        perror("initial parent: read from pipe");
        exit(1);
    }

    printf("FINAL RESULT: %f\n", final_val);
    printf("Parent: All done, exiting...\n");

    return 0;
}
