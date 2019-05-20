/* In this program I check 3 possible options for the input of process_arglist and take care of each one of them:
 * case 1: no "&" and no "|" - parent wait for his child
 * case 2: there is "&" in the end but no "|" - parent doesnt wait for child
 * case 3: no "&" but command containing "|" - two children connecting each other with a pipe */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <zconf.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <sys/wait.h>

#define READ 0 /* The index of the "read" end of the pipe */
#define WRITE 1 /* The index of the "write" end of the pipe */
static sigset_t signal_set;

/* prepare and finalize calls for initialization and destruction of anything required - I don't use it. */
int prepare(void) {
    int x;
    /* we will define ignore to sigchld signals in order to get rid of zombies as fast as possible */
    struct sigaction sigchld_action = {
            .sa_handler = SIG_DFL,
            .sa_flags = SA_NOCLDWAIT
    };
    sigaction(SIGCHLD, &sigchld_action, NULL);
    /* we will now block all SIGINT signals. later we will take care of them */
    sigemptyset (&signal_set);
    sigaddset (&signal_set, SIGINT);
    x = sigprocmask(SIG_BLOCK, &signal_set, NULL);
    if (x == -1) {
        printf("Error in sigprocmask: %s\n", strerror(errno));
        exit(1);
    }
    return 0;
}

int finalize(void) {
    return 0;
}

/* arglist - a list of char* arguments (words) provided by the user
it contains count+1 items, where the last item (arglist[count]) and *only* the last is NULL
RETURNS - 1 if should cotinue, 0 otherwise */
int process_arglist(int count, char ** arglist) {
    int containsPipeFlag = 0, pipeSymbolIndex = 0, pid, tpid, x;
    for (int i = 0; i < count; i++) {
        if (strcmp(arglist[i], "|") == 0) { /* checks if we have "|" in arglist */
            containsPipeFlag = 1;
        }
    }
    if (strcmp(arglist[count - 1], "&") != 0 && containsPipeFlag == 0) { /* case 1 */
        pid = fork();
        if (pid == -1) {
            printf("Error in fork: %s\n", strerror(errno));
            exit(0);
        }
        if (pid == 0) {
            /* This is done by the child process. */
            x = sigprocmask(SIG_UNBLOCK, &signal_set, NULL); /* we will now take care of SIGINT signal */
            if (x == -1) {
                printf("Error in sigprocmask: %s\n", strerror(errno));
                exit(1);
            }
            execvp(arglist[0], arglist);
            /* If execvp returns, it must have failed. */
            printf("Unknown command - child failed: %s\n", strerror(errno));
            exit(1);
        } else {
            /* This is done by the parent.  Wait for the child to terminate. */
            tpid = waitpid(pid, NULL, 0);
            if (tpid == -1) {
                if (errno == ECHILD) {
                    /* that is okay and normal. sig_ign to sigchld caused it,but the parent actually waits for child. */
                } else {
                    /* that is a real waitpid error*/
                    printf("Error in waitpid(): %s\n", strerror(errno));
                    exit(0);
                }
            }
            return 1;
        }
    }
    if (strcmp(arglist[count - 1], "&") == 0 && containsPipeFlag == 0) { /* case 2 */
        pid = fork();
        if (pid == -1) {
            printf("Error in fork: %s\n", strerror(errno));
            exit(0);
        }
        if (pid == 0) {
            /* This is done by the child process. */
            arglist[count - 1] = NULL; /* we cut off the & symbol */
            execvp(arglist[0], arglist);
            /* If execvp returns, it must have failed. */
            printf("Unknown command - child failed: %s\n", strerror(errno));
            exit(1);
        }
        /* this part is done by the parent */
        return 1;
    }
    if (strcmp(arglist[count - 1], "&") != 0 && containsPipeFlag == 1) { /* case 3 */
        for (int index = 0; index < count; index++) {
            if (strcmp(arglist[index], "|") == 0) {
                pipeSymbolIndex = index;
                arglist[index] = NULL; /* we replace the "|" with NULL so the string will be considered as splitted */
                break;
            }
        }
        int fd[2];
        if (pipe(fd) == -1) {
            printf("Error creating pipe: %s\n", strerror(errno));
            exit(1);
        }
        int child1 = fork();
        if (child1 == -1) {
            printf("Error in fork: %s\n", strerror(errno));
            exit(0);
        }
        if (child1 == 0) /* we are child 1 */
        {
            x = sigprocmask(SIG_UNBLOCK, &signal_set, NULL);
            if (x == -1) {
                printf("Error in sigprocmask: %s\n", strerror(errno));
                exit(1);
            }
            // we will be writing to the pipe, no need to read:
            close(fd[READ]);
            if (dup2(fd[WRITE], STDOUT_FILENO) == -1) {
                printf("Error in dup2(): %s\n", strerror(errno));
                exit(1);
            }
            execvp(arglist[0], arglist);
            // execvp() does not return (except when an error occurred).
            printf("execvp() failed: %s\n", strerror(errno));
            exit(1); // exit child process if exec fails.
        }
        int child2 = fork();
        if (child2 == -1) {
            printf("Error in fork: %s\n", strerror(errno));
            exit(0);
        }
        if (child2 == 0) { /* we are child 2 */
            x = sigprocmask(SIG_UNBLOCK, &signal_set, NULL);
            if (x == -1) {
                printf("Error in sigprocmask: %s\n", strerror(errno));
                exit(1);
            }
            // we will be reading from the pipe, no need to write:
            close(fd[WRITE]);
            // whenever we read from stdin, actually read from the pipe.
            dup2(fd[READ], STDIN_FILENO);
            /* we will give execvp the second part of command */
            execvp(arglist[pipeSymbolIndex + 1], arglist + pipeSymbolIndex + 1);
            // execvp() does not return (except when an error occurred).
            printf("execvp() failed: %s\n", strerror(errno));
            exit(1); // exit child process if exec fails.
        }
        /* Only parent gets here. If parent doesn't close the WRITE_END of
         * the pipe, then child2 might not exit because the parent could
         * still write data into the pipe that child2 is reading from. */
        tpid = waitpid(child1, NULL, 0);
        if (tpid == -1) {
            if (errno == ECHILD) {
                /*that is okay and normal. sig_ign to sigchld caused it, but the parent actually waits.*/
            } else {
                /* that is a real waitpid error*/
                printf("Error in waitpid(): %s\n", strerror(errno));
                exit(0);
            }
        }
        tpid = waitpid(child2, NULL, 0);
        if (tpid == -1) {
            if (errno == ECHILD) {
                /*that is okay and normal. sig_ign to sigchld caused it, but the parent actually waits.*/
            } else {
                /* that is a real waitpid error*/
                printf("Error in waitpid(): %s\n", strerror(errno));
                exit(0);
            }
        }
        close(fd[READ]);
        close(fd[WRITE]);
        return 1;
    }
    return 1;
}

