updated_shell.c
#include <stdio.h>      // printf, perror, fprintf, fgets
#include <stdlib.h>     // malloc, free, exit, setenv, unsetenv
#include <string.h>     // strcmp, strtok, strncpy, strlen
#include <unistd.h>     // fork, execvp, pipe, dup2, chdir, getcwd, setenv, unsetenv
#include <sys/wait.h>   // waitpid, WNOHANG
#include <sys/types.h>  // pid_t
#include <fcntl.h>      // open, O_RDONLY, O_WRONLY, O_CREAT, O_TRUNC, O_APPEND
#include <signal.h>     // signal, SIGINT
#include <errno.h>

#define MAX_CMD_LEN 1024
#define MAX_ARGS 64
#define MAX_HISTORY 100
#define MAX_JOBS 64
#define MAX_CMDLINE_COPY 512

// --- Function Declarations ---

// Built-in commands
int shell_cd(char **args);
int shell_exit(char **args);
int shell_help(char **args);
int shell_pwd(char **args);
int shell_echo(char **args);
int shell_history_cmd(char **args);
int shell_env(char **args);
int shell_set(char **args);
int shell_unset(char **args);
int shell_jobs(char **args);
int shell_kill(char **args);

// Core shell functions
void shell_loop(void);
int execute_command(char **args, const char *orig_cmdline);
void handle_signal(int signo);
void reap_children_nonblocking(void);

// Job & history data structures
typedef struct {
    pid_t pid;
    char cmdline[MAX_CMDLINE_COPY];
    int running; // 1 = running, 0 = finished
} job_t;

static job_t jobs[MAX_JOBS];
static int job_count = 0;

static char history[MAX_HISTORY][MAX_CMD_LEN];
static int history_count = 0;

// Built-ins arrays
const char *builtin_str[] = {
    "cd",
    "exit",
    "help",
    "pwd",
    "echo",
    "history",
    "env",
    "set",
    "unset",
    "jobs",
    "kill"
};

int (*builtin_func[]) (char **) = {
    &shell_cd,
    &shell_exit,
    &shell_help,
    &shell_pwd,
    &shell_echo,
    &shell_history_cmd,
    &shell_env,
    &shell_set,
    &shell_unset,
    &shell_jobs,
    &shell_kill
};

int num_builtins() {
    return sizeof(builtin_str) / sizeof(char *);
}

// --- Signal Handler ---

void handle_signal(int signo) {
    if (signo == SIGINT) {
        // Print newline and redisplay prompt on Ctrl+C
        printf("\n");
        // We will redisplay prompt in shell_loop.
    }
}

// --- Job helpers ---

void add_job(pid_t pid, const char *cmdline) {
    if (job_count >= MAX_JOBS) return;
    jobs[job_count].pid = pid;
    strncpy(jobs[job_count].cmdline, cmdline, MAX_CMDLINE_COPY - 1);
    jobs[job_count].cmdline[MAX_CMDLINE_COPY - 1] = '\0';
    jobs[job_count].running = 1;
    job_count++;
}

void mark_job_finished(pid_t pid) {
    for (int i = 0; i < job_count; ++i) {
        if (jobs[i].pid == pid) {
            jobs[i].running = 0;
            return;
        }
    }
}

void reap_children_nonblocking(void) {
    int status;
    pid_t wpid;
    // Reap any finished children without blocking
    while ((wpid = waitpid(-1, &status, WNOHANG)) > 0) {
        // Mark job finished if it was a tracked background job
        mark_job_finished(wpid);
    }
    // If wpid == -1 and errno == ECHILD, no child processes.
}

// --- Main ---
int main(int argc, char **argv) {
    // Set up signal handlers.
    signal(SIGINT, handle_signal);
    // Do NOT ignore SIGCHLD; we'll reap in the main loop. Set default behavior.
    signal(SIGCHLD, SIG_DFL);

    shell_loop();

    return EXIT_SUCCESS;
}


// --- Shell Loop ---
void shell_loop(void) {
    char cmd[MAX_CMD_LEN];
    char cmd_copy[MAX_CMDLINE_COPY];
    char *args[MAX_ARGS];
    int status = 1;

    do {
        // Reap finished background jobs before showing prompt
        reap_children_nonblocking();

        // Display prompt
        char cwd[1024];
        if (getcwd(cwd, sizeof(cwd)) != NULL) {
            printf("🚀 %s > ", cwd);
        } else {
            perror("getcwd() error");
            printf("🚀 > ");
        }
        fflush(stdout);

        // Read input
        if (fgets(cmd, MAX_CMD_LEN, stdin) == NULL) {
            // EOF (Ctrl+D)
            printf("exit\n");
            break;
        }
        // Remove trailing newline
        cmd[strcspn(cmd, "\n")] = 0;

        // If it's an empty line, continue
        if (strlen(cmd) == 0) {
            continue;
        }

        // Save a copy of the original command line for history/jobs
        strncpy(cmd_copy, cmd, MAX_CMDLINE_COPY - 1);
        cmd_copy[MAX_CMDLINE_COPY - 1] = '\0';

        // Save to history (circular)
        if (history_count < MAX_HISTORY) {
            strncpy(history[history_count++], cmd_copy, MAX_CMD_LEN - 1);
            history[history_count - 1][MAX_CMD_LEN - 1] = '\0';
        } else {
            // shift up to make room
            for (int i = 1; i < MAX_HISTORY; ++i) {
                strncpy(history[i - 1], history[i], MAX_CMD_LEN);
            }
            strncpy(history[MAX_HISTORY - 1], cmd_copy, MAX_CMD_LEN - 1);
            history[MAX_HISTORY - 1][MAX_CMD_LEN - 1] = '\0';
        }

        // Tokenize into args
        char *token;
        int i = 0;
        token = strtok(cmd, " \t\r\n\a");
        while (token != NULL && i < MAX_ARGS - 1) {
            args[i++] = token;
            token = strtok(NULL, " \t\r\n\a");
        }
        args[i] = NULL;

        // Execute command
        status = execute_command(args, cmd_copy);

    } while (status);
}


// --- Execution logic ---
int execute_command(char **args, const char *orig_cmdline) {
    if (args[0] == NULL) {
        return 1;
    }

    // Built-ins
    for (int i = 0; i < num_builtins(); i++) {
        if (strcmp(args[0], builtin_str[i]) == 0) {
            return (*builtin_func[i])(args);
        }
    }

    // External command
    pid_t pid = fork();
    int background = 0;
    int arg_count = 0;
    while (args[arg_count] != NULL) arg_count++;

    if (arg_count > 0 && strcmp(args[arg_count - 1], "&") == 0) {
        background = 1;
        args[arg_count - 1] = NULL;
    }

    if (pid < 0) {
        perror("fork failed");
        return 1;
    }

    if (pid == 0) {
        // CHILD: handle redirection and pipe
        char *inputFile = NULL;
        char *outputFile = NULL;
        int append = 0;
        int pipe_pos = -1;

        for (int i = 0; args[i] != NULL; i++) {
            if (strcmp(args[i], "<") == 0) {
                inputFile = args[i + 1];
                args[i] = NULL;
            } else if (strcmp(args[i], ">") == 0) {
                outputFile = args[i + 1];
                args[i] = NULL;
            } else if (strcmp(args[i], ">>") == 0) {
                outputFile = args[i + 1];
                append = 1;
                args[i] = NULL;
            } else if (strcmp(args[i], "|") == 0) {
                pipe_pos = i;
                args[i] = NULL;
                break;
            }
        }

        if (inputFile) {
            int fd_in = open(inputFile, O_RDONLY);
            if (fd_in < 0) {
                perror("open input file failed");
                exit(EXIT_FAILURE);
            }
            dup2(fd_in, STDIN_FILENO);
            close(fd_in);
        }
        if (outputFile) {
            int flags = O_WRONLY | O_CREAT;
            if (append) flags |= O_APPEND; else flags |= O_TRUNC;
            int fd_out = open(outputFile, flags, 0644);
            if (fd_out < 0) {
                perror("open output file failed");
                exit(EXIT_FAILURE);
            }
            dup2(fd_out, STDOUT_FILENO);
            close(fd_out);
        }

        if (pipe_pos != -1) {
            int pipefd[2];
            if (pipe(pipefd) < 0) {
                perror("pipe failed");
                exit(EXIT_FAILURE);
            }
            pid_t pipe_pid = fork();
            if (pipe_pid < 0) {
                perror("pipe fork failed");
                exit(EXIT_FAILURE);
            }
            if (pipe_pid == 0) {
                // right side
                close(pipefd[1]);
                dup2(pipefd[0], STDIN_FILENO);
                close(pipefd[0]);
                if (execvp(args[pipe_pos + 1], &args[pipe_pos + 1]) < 0) {
                    perror("execvp for piped command failed");
                    exit(EXIT_FAILURE);
                }
            } else {
                // left side
                close(pipefd[0]);
                dup2(pipefd[1], STDOUT_FILENO);
                close(pipefd[1]);
                // wait for right side to finish
                waitpid(pipe_pid, NULL, 0);
                if (execvp(args[0], args) < 0) {
                    perror("execvp for first command failed");
                    exit(EXIT_FAILURE);
                }
            }
        } else {
            if (execvp(args[0], args) < 0) {
                perror("execvp failed");
                exit(EXIT_FAILURE);
            }
        }
    } else {
        // PARENT
        if (!background) {
            int status;
            if (waitpid(pid, &status, 0) < 0) {
                perror("waitpid failed");
            }
        } else {
            // Add to job list and don't wait
            add_job(pid, orig_cmdline);
            printf("Started background job with PID: %d\n", pid);
        }
    }

    return 1;
}


// --- Built-in Implementations ---

int shell_cd(char **args) {
    if (args[1] == NULL) {
        fprintf(stderr, "shell: expected argument to \"cd\"\n");
    } else {
        if (chdir(args[1]) != 0) {
            perror("shell");
        }
    }
    return 1;
}

int shell_help(char **args) {
    printf("My Advanced C Shell\n");
    printf("Type program names and arguments, and hit enter.\n");
    printf("The following are built in:\n");
    for (int i = 0; i < num_builtins(); i++) {
        printf("  %s\n", builtin_str[i]);
    }
    printf("Use the man command for information on other programs.\n");
    printf("Supports piping ('|'), I/O redirection ('<', '>', '>>'), and background tasks ('&').\n");
    return 1;
}

int shell_exit(char **args) {
    return 0;
}

int shell_pwd(char **args) {
    char cwd[1024];
    if (getcwd(cwd, sizeof(cwd)) != NULL) {
        printf("%s\n", cwd);
    } else {
        perror("pwd");
    }
    return 1;
}

int shell_echo(char **args) {
    for (int i = 1; args[i] != NULL; ++i) {
        printf("%s", args[i]);
        if (args[i + 1] != NULL) printf(" ");
    }
    printf("\n");
    return 1;
}

int shell_history_cmd(char **args) {
    int start = (history_count > MAX_HISTORY) ? history_count - MAX_HISTORY : 0;
    int show = history_count;
    for (int i = 0; i < history_count; ++i) {
        printf("%4d  %s\n", i + 1, history[i]);
    }
    return 1;
}

extern char **environ;
int shell_env(char **args) {
    for (char **env = environ; *env != 0; env++) {
        printf("%s\n", *env);
    }
    return 1;
}

int shell_set(char **args) {
    // Usage: set VAR VALUE
    if (args[1] == NULL || args[2] == NULL) {
        fprintf(stderr, "Usage: set VAR VALUE\n");
    } else {
        if (setenv(args[1], args[2], 1) != 0) {
            perror("setenv");
        }
    }
    return 1;
}

int shell_unset(char **args) {
    // Usage: unset VAR
    if (args[1] == NULL) {
        fprintf(stderr, "Usage: unset VAR\n");
    } else {
        if (unsetenv(args[1]) != 0) {
            perror("unsetenv");
        }
    }
    return 1;
}

int shell_jobs(char **args) {
    // Before showing jobs, reap finished children
    reap_children_nonblocking();

    if (job_count == 0) {
        printf("No background jobs.\n");
        return 1;
    }
    for (int i = 0; i < job_count; ++i) {
        printf("[%d] PID: %d  %s  (%s)\n", i + 1, jobs[i].pid,
               jobs[i].cmdline,
               (jobs[i].running ? "running" : "finished"));
    }
    return 1;
}

int shell_kill(char **args) {
    // Usage: kill PID
    if (args[1] == NULL) {
        fprintf(stderr, "Usage: kill PID\n");
    } else {
        pid_t pid = (pid_t) atoi(args[1]);
        if (pid <= 0) {
            fprintf(stderr, "Invalid PID: %s\n", args[1]);
        } else {
            if (kill(pid, SIGTERM) != 0) {
                perror("kill");
            } else {
                // optionally mark job finished if tracked
                mark_job_finished(pid);
            }
        }
    }
    return 1;
}
