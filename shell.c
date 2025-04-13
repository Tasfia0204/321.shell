#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <ctype.h> 

#define MAX_CMD_LEN 1024
#define MAX_ARGS 100
#define HISTORY_FILE "history.txt"

volatile sig_atomic_t interrupted = 0;
pid_t current_child = -1;

void handle_sigint(int sig) {
    if (current_child > 0) {
        kill(current_child, SIGINT);
    } else {
        write(STDOUT_FILENO, "\n", 1);
        exit(0);
    }
}

void save_history(const char *cmd) {
    FILE *file = fopen(HISTORY_FILE, "a");
    if (file) {
        fprintf(file, "%s\n", cmd);
        fclose(file);
    }
}

void trim_newline(char *str) {
    str[strcspn(str, "\n")] = 0;
}

void split_args(char *cmd, char *argv[]) {
    int i = 0;
    while (*cmd) {
        while (*cmd == ' ' || *cmd == '\t') cmd++;
        if (*cmd == '\0') break;
        argv[i++] = cmd;
        while (*cmd && *cmd != ' ' && *cmd != '\t') cmd++;
        if (*cmd) *cmd++ = '\0';
    }
    argv[i] = NULL;
}

void execute_piped(char *cmds[], int n) {
    int in_fd = 0, pipefd[2];

    for (int i = 0; i < n; i++) {
        pipe(pipefd);
        pid_t pid = fork();
        if (pid == 0) {
            dup2(in_fd, STDIN_FILENO);
            if (i < n - 1) {
                dup2(pipefd[1], STDOUT_FILENO);
            }
            close(pipefd[0]);
            close(pipefd[1]);

            char *argv[MAX_ARGS];
            split_args(cmds[i], argv);
            execvp(argv[0], argv);
            perror("execvp");
            exit(1);
        } else {
            current_child = pid;
            waitpid(pid, NULL, 0);
            current_child = -1;
            close(pipefd[1]);
            in_fd = pipefd[0];
        }
    }
}

void handle_redirection(char *cmd) {
    int in = -1, out = -1, append = 0;
    char *input = strchr(cmd, '<');
    char *output = strstr(cmd, ">>");

    if (output == NULL) {
        output = strchr(cmd, '>');
    }

    if (input) {
        *input = 0; 
        input++;    
        while (*input == ' ' || *input == '\t') input++;
        char *end = input;
        while (*end && *end != ' ' && *end != '\t') end++;
        *end = '\0'; 

        in = open(input, O_RDONLY);
        if (in < 0) {
            perror("Input file error");
            return;
        }
    }
    if (output) {
        if (strstr(cmd, ">>") != NULL) {
            append = 1;
            *output = 0;  
            output += 2;  
        } else {
            append = 0;
            *output = 0;  
            output += 1;  
        }
    
        while (*output == ' ' || *output == '\t') output++;

        char *end = output;
        while (*end && *end != ' ' && *end != '\t') end++;
        *end = '\0'; 

        int flags = O_WRONLY | O_CREAT;
        if (append) {
            flags |= O_APPEND; 
        } else {
            flags |= O_TRUNC;  
        }

        out = open(output, flags, 0644);
        if (out < 0) {
            perror("Output file error");
            return;
        }
    }
    pid_t pid = fork();
    if (pid == 0) {
 
        if (in != -1) {
            if (dup2(in, STDIN_FILENO) < 0) {  
                perror("dup2 for input failed");
                exit(1);
            }
        }
        if (out != -1) {
            if (dup2(out, STDOUT_FILENO) < 0) {  
                perror("dup2 for output failed");
                exit(1);
            }
        }
        char *argv[MAX_ARGS];
        char *token = strtok(cmd, " ");
        int i = 0;
        while (token) {
            if (strcmp(token, ">") != 0 && strcmp(token, "<") != 0 && strcmp(token, ">>") != 0) {
                argv[i++] = token; 
            }
            token = strtok(NULL, " ");
        }
        argv[i] = NULL; t

        // Execute the command
        execvp(argv[0], argv);
        perror("execvp");
        exit(1);
    } else {
        // Parent process waits for child to finish
        current_child = pid;
        waitpid(pid, NULL, 0);
        current_child = -1;
    }

    // Close the files
    if (in != -1) close(in);
    if (out != -1) close(out);
}


void exec_cmd(char *cmd) {
    if (strchr(cmd, '|')) {
        char *cmds[10];
        int n = 0;
        char *p = cmd;
        while (*p) {
            while (*p == '|' && *(p+1) == ' ') p++;
            cmds[n++] = p;
            while (*p && *p != '|') p++;
            if (*p) *p++ = '\0';
        }
        execute_piped(cmds, n);
    } else if (strchr(cmd, '<') || strchr(cmd, '>')) {
        handle_redirection(cmd);
    } else {
        pid_t pid = fork();
        if (pid == 0) {
            char *argv[MAX_ARGS];
            split_args(cmd, argv);
            execvp(argv[0], argv);
            perror("execvp");
            exit(1);
        } else {
            current_child = pid;
            waitpid(pid, NULL, 0);
            current_child = -1;
        }
    }
}

void parse_and_execute(char *line) {
    char *commands[100];
    int n = 0;
    char *p = line;
    while (*p) {
        while (*p == ';' && *(p+1) == ' ') p++;
        commands[n++] = p;
        while (*p && *p != ';') p++;
        if (*p) *p++ = '\0';
    }

    for (int i = 0; i < n; i++) {
        char *seq_cmds[100];
        int s = 0;
        char *q = commands[i];
        while (*q) {
            while (*q == '&' && *(q+1) == '&' && *(q+2) == ' ') q++;
            seq_cmds[s++] = q;
            while (*q && !(q[0] == '&' && q[1] == '&')) q++;
            if (*q) *q++ = '\0', *q++ = '\0';
        }

        int success = 1;
        for (int j = 0; j < s && success; j++) {
            trim_newline(seq_cmds[j]);
            if (strlen(seq_cmds[j]) == 0) continue;
            save_history(seq_cmds[j]);

            pid_t pid = fork();
            if (pid == 0) {
                exec_cmd(seq_cmds[j]);
                exit(0);
            } else {
                current_child = pid;
                int status;
                waitpid(pid, &status, 0);
                current_child = -1;
                if (WIFEXITED(status) && WEXITSTATUS(status) != 0)
                    success = 0;
            }
        }
    }
}

int main() {
    struct sigaction sa;
    sa.sa_handler = handle_sigint;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGINT, &sa, NULL);

    char line[MAX_CMD_LEN];

    while (1) {
        interrupted = 0;
    
        char cwd[1024];
        char hostname[1024];
        char *username = getenv("USER");
        if (username == NULL) {
            username = getenv("LOGNAME");
        }
        if (username == NULL) {
            username = "user";
        }
    
        if (gethostname(hostname, sizeof(hostname)) != 0) {
            strcpy(hostname, "unknown");
        }
    
        if (getcwd(cwd, sizeof(cwd)) != NULL) {
            printf("\033[1;32m%s@%s\033[0m:\033[1;34m%s\033[0m sh> ", username, hostname, cwd);
        } else {
            perror("getcwd");
            printf("\033[1;32m%s@%s\033[0m sh> ", username, hostname);
        }
    

        fflush(stdout);

        if (!fgets(line, sizeof(line), stdin)) {
            if (feof(stdin)) break;
            continue;
        }

        if (interrupted) continue;
        trim_newline(line);
        if (strcmp(line, "") == 0) continue;
        if (strcmp(line, "exit") == 0) break;

        parse_and_execute(line);
    }

    return 0;
}
