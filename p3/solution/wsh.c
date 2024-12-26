#include "wsh.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/wait.h>

// the following history functions allow modification of the history of commands we track
void init_history(History *history, int capacity) {
    history->commands = malloc(capacity * sizeof(char *));
    history->size = 0;
    history->capacity = capacity;
}

void free_history(History *history) {
    for (int i = 0; i < history->size; i++) {
        free(history->commands[i]);
    }
    free(history->commands);
}

void add_to_history(History *history, const char *command) {
    if (history->size > 0 && strcmp(history->commands[history->size - 1], command) == 0) {
        return;
    }

    if (history->size == history->capacity) {
        free(history->commands[0]);
        for (int i = 1; i < history->size; i++) {
            history->commands[i - 1] = history->commands[i];
        }
        history->size--;
    }

    history->commands[history->size] = strdup(command);
    history->size++;
}

void print_history(const History *history) {
    for (int i = history->size - 1; i >= 0; i--) {
        printf("%d) %s\n", history->size - i, history->commands[i]);
    }
}

// the following functions allow modification and management of shell variables
void set_shell_variable(ShellVariables *sv, const char *name, const char *value) {
    ShellVariable *current = sv->head;

    while (current) { // while the var exists
        if (strcmp(current->name, name) == 0) {
            free(current->value);
            current->value = strdup(value);
            return;
        }
        current = current->next;
    }

    ShellVariable *new_var = malloc(sizeof(ShellVariable));
    new_var->name = strdup(name);
    new_var->value = strdup(value);
    new_var->next = NULL;
    if (sv->head == NULL) {
        sv->head = new_var;
    } else {
        current = sv->head;
        while (current->next) {
            current = current->next;
        }
        current->next = new_var;
    }
}

char *get_shell_variable(ShellVariables *sv, const char *name) {
    ShellVariable *current = sv->head;
    while (current) {
        if (strcmp(current->name, name) == 0) {
            return current->value;
        }
        current = current->next;
    }
    return NULL;
}

void free_shell_variables(ShellVariables *sv) {
    ShellVariable *current = sv->head;
    while (current) {
        ShellVariable *to_free = current;
        current = current->next;
        free(to_free->name);
        free(to_free->value);
        free(to_free);
    }
}

void substitute_variables(char *command, ShellVariables *sv) {
    char temp[MAXLINE] = "";
    char *pos = command;
    while (*pos) {
        if (*pos == '$') {
            pos++;
            char var_name[MAXLINE] = "";
            int i = 0;
            while (*pos && *pos != ' ' && *pos != '$') {
                var_name[i++] = *pos++;
            }
            var_name[i] = '\0';

            char *value = getenv(var_name);
            if (!value) {
                value = get_shell_variable(sv, var_name);
            }
            if (value) {
                strcat(temp, value);
            }
        } else {
            strncat(temp, pos, 1);
            pos++;
        }
    }
    strcpy(command, temp);
}

// built in version of ls, making sure it matches ls -1 like in bash
void builtin_ls() {
    DIR *dir;
    struct dirent *entry;
    char *entries[MAXARGS];
    int count = 0;

    dir = opendir("."); // getting current dir open
    if (dir == NULL) {
        perror("opendir");
        return;
    }

    while ((entry = readdir(dir)) != NULL) { // storing all current contents in our list, skipping hidden files
        if (entry->d_name[0] != '.') {
            entries[count] = strdup(entry->d_name);
            if (entries[count] == NULL) {
                perror("strdup");
                closedir(dir);
                return;
            }
            count++;
        }
    }
    closedir(dir);

    for (int i = 0; i < count - 1; i++) { // sorting alphabetically to match .out file on test
        for (int j = i + 1; j < count; j++) {
            if (strcmp(entries[i], entries[j]) > 0) {
                char *temp = entries[i];
                entries[i] = entries[j];
                entries[j] = temp;
            }
        }
    }
    for (int i = 0; i < count; i++) {
        printf("%s\n", entries[i]);
        free(entries[i]);
    }
}

int main(int argc, char *argv[]) {
    FILE *input = stdin;
    char command[MAXLINE];
    char *args[MAXARGS];
    int had_error = 0;  // tracking if an error has occurred

    ShellVariables shell_vars = {.head = NULL};
    History history;
    init_history(&history, DEFAULTHISTORY);

    setenv("PATH", "/bin", 1);

    if (argc > 2) {
        fprintf(stderr, "Usage: %s [batch file]\n", argv[0]);
        exit(-1);
    } else if (argc == 2) {
        input = fopen(argv[1], "r");
        if (input == NULL) {
            perror("Error opening batch file");
            exit(-1);
        }
    }

    while (1) {
        if (input == stdin) {
            printf("wsh> ");
            fflush(stdout);
        }

        if (fgets(command, MAXLINE, input) == NULL) {
            break;
        }

        command[strcspn(command, "\n")] = '\0';

        if (command[0] == '\0') {
            continue;
        }

        char *trimmed = command;
        while (*trimmed == ' ') {
            trimmed++;
        }
        if (trimmed[0] == '#') {
            continue;
        }

        substitute_variables(command, &shell_vars);

        // checking builtin commands, excluding them from being included in history
        if (strncmp(trimmed, "history", 7) != 0 && strcmp(trimmed, "exit") != 0 &&
            strncmp(trimmed, "cd", 2) != 0 && strncmp(trimmed, "local", 5) != 0 &&
            strncmp(trimmed, "export", 6) != 0 && strncmp(trimmed, "vars", 4) != 0 &&
            strcmp(trimmed, "ls") != 0) {
            add_to_history(&history, trimmed);
        }

        int arg_count = 0;
        char *redirection_file = NULL;
        int redirect_type = 0;

        char *token = strtok(command, " ");
        while (token != NULL) {
            if (strncmp(token, "2>", 2) == 0) {
                redirect_type = 6; // stderr overwrite redirection
                redirection_file = token + 2;
                if (*redirection_file == '\0') { // if no file name is directly attached, get the next token
                    token = strtok(NULL, " ");
                    if (token) {
                        redirection_file = token;
                    }
                }
            } else if (strstr(token, ">>") != NULL) {
                if (strncmp(token, "&>>", 3) == 0) {
                    redirect_type = 5;
                    redirection_file = token + 3;
                } else {
                    redirect_type = 3;
                    redirection_file = token + 2;
                }
            } else if (strstr(token, "&>") != NULL) {
                redirect_type = 4;
                redirection_file = token + 2;
            } else if (strchr(token, '>') != NULL) {
                redirect_type = 2;
                redirection_file = token + 1;
            } else if (strchr(token, '<') != NULL) {
                redirect_type = 1;
                redirection_file = token + 1;
            } else {
                args[arg_count] = token;
                arg_count++;
            }
            token = strtok(NULL, " ");
        }
        args[arg_count] = NULL;

        if (strcmp(args[0], "exit") == 0) {
            if (arg_count > 1) {
                fprintf(stderr, "exit: too many arguments\n");
                continue;
            }
            break;
        } else if (strcmp(args[0], "cd") == 0) {
            if (arg_count != 2) {
                fprintf(stderr, "cd: expected one argument\n");
                had_error = 1;
            } else {
                if (chdir(args[1]) != 0) {
                    perror("cd");
                    had_error = 1;
                } else {
                    had_error = 0;
                }
            }
            continue;
        } else if (strcmp(args[0], "local") == 0) {
            if (arg_count == 2) {
                char *eq_pos = strchr(args[1], '=');
                if (eq_pos) {
                    *eq_pos = '\0';
                    set_shell_variable(&shell_vars, args[1], eq_pos + 1);
                    had_error = 0;
                } else {
                    fprintf(stderr, "local: invalid format, expected VAR=value\n");
                    had_error = 1;
                }
            } else {
                fprintf(stderr, "local: expected one argument in VAR=value format\n");
                had_error = 1;
            }
            continue;
        } else if (strcmp(args[0], "export") == 0) {
            if (arg_count == 2) {
                char *eq_pos = strchr(args[1], '=');
                if (eq_pos) {
                    *eq_pos = '\0';
                    if (setenv(args[1], eq_pos + 1, 1) != 0) {
                        perror("export");
                        had_error = 1;
                    } else {
                        had_error = 0;
                    }
                } else {
                    // handle "export X=" to clear the variable
                    if (setenv(args[1], "", 1) != 0) {
                        perror("export");
                        had_error = 1;
                    } else {
                        had_error = 0;
                    }
                }
            } else {
                fprintf(stderr, "export: expected one argument in VAR=value format\n");
                had_error = 1;
            }
            continue;
        } else if (strcmp(args[0], "vars") == 0) {
            if (arg_count > 1) {
                fprintf(stderr, "vars: too many arguments\n");
                had_error = 1;
            } else {
                ShellVariable *current = shell_vars.head;
                while (current) {
                    printf("%s=%s\n", current->name, current->value);
                    current = current->next;
                }
                had_error = 0;
            }
            continue;
        } else if (strcmp(args[0], "history") == 0) {
            if (arg_count == 1) {
                print_history(&history);
                had_error = 0;
            } else if (arg_count == 3 && strcmp(args[1], "set") == 0) { // accounting for being able to run commands from history
                int new_capacity = atoi(args[2]);
                if (new_capacity <= 0) {
                    fprintf(stderr, "Invalid history size\n");
                    had_error = 1;
                } else {
                    char **new_commands = malloc(new_capacity * sizeof(char *)); // new memory for new history size
                    if (new_commands == NULL) {
                        perror("malloc");
                        had_error = 1;
                        continue;
                    }
                    // copy existing commands so as to not lose them, and free old structure
                    int copy_count = history.size < new_capacity ? history.size : new_capacity;
                    for (int i = 0; i < copy_count; i++) {
                        new_commands[i] = history.commands[i];
                    }
                    if (history.size > new_capacity) {
                        for (int i = new_capacity; i < history.size; i++) {
                            free(history.commands[i]);
                        }
                    }

                    free(history.commands);
                    history.commands = new_commands;
                    history.size = copy_count;
                    history.capacity = new_capacity;
                    had_error = 0;
                }
            } else if (arg_count == 2) {
                int n = atoi(args[1]);
                if (n > 0 && n <= history.size) {
                    int index = history.size - n; // correct index in array of command

                    if (index >= 0 && index < history.size) {
                        strcpy(command, history.commands[index]); // storing command to run
                        trimmed = command;
                        while (*trimmed == ' ') { // no repetitive add of command onto history list
                            trimmed++;
                        }
                        // executing command
                        pid_t pid = fork();
                        if (pid < 0) {
                            perror("fork");
                            had_error = 1;
                        } else if (pid == 0) {
                            // within child process
                            char *token = strtok(command, " ");
                            arg_count = 0;
                            while (token != NULL) {
                                args[arg_count] = token;
                                arg_count++;
                                token = strtok(NULL, " ");
                            }
                            args[arg_count] = NULL;

                            if (strchr(args[0], '/') != NULL) {
                                execv(args[0], args);
                                perror("execv");
                                exit(-1);
                            }

                            char *path_env = getenv("PATH");
                            if (path_env == NULL) {
                                perror("getenv");
                                exit(-1);
                            }

                            char *directory = strtok(path_env, ":");
                            char cmd_path[MAXLINE];

                            while (directory != NULL) {
                                snprintf(cmd_path, sizeof(cmd_path), "%s/%s", directory, args[0]);
                                if (access(cmd_path, X_OK) == 0) {
                                    execv(cmd_path, args);
                                    perror("execv");
                                    exit(-1);
                                }
                                directory = strtok(NULL, ":");
                            }
                            perror("execv");
                            exit(255);
                        } else {
                            // within parent process, nonzero return code (PID of parent I believe)
                            int status;
                            wait(&status);
                            if (WIFEXITED(status)) {
                                if (WEXITSTATUS(status) == 255) {
                                    had_error = 1;
                                } else {
                                    had_error = 0;
                                }
                            }
                        }
                    } else {
                        fprintf(stderr, "Invalid history index\n");
                        had_error = 1;
                    }
                    continue;
                } else {
                    fprintf(stderr, "Invalid history index\n");
                    had_error = 1;
                }
            }
            continue;
        } else if (strcmp(args[0], "ls") == 0) {
            if (arg_count > 1) {
                fprintf(stderr, "ls: too many arguments\n");
                had_error = 1;
            } else {
                builtin_ls();
                had_error = 0;
            }
            continue;
        }

        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            had_error = 1;
        } else if (pid == 0) {
            int fd = -1;
            if (redirection_file) {
                if (redirect_type == 1) {
                    fd = open(redirection_file, O_RDONLY);
                    if (fd == -1) {
                        perror("open");
                        exit(-1);
                    }
                    dup2(fd, STDIN_FILENO);
                } else if (redirect_type == 2) {
                    fd = open(redirection_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                    if (fd == -1) {
                        perror("open");
                        exit(-1);
                    }
                    dup2(fd, STDOUT_FILENO);
                } else if (redirect_type == 3) {
                    fd = open(redirection_file, O_WRONLY | O_CREAT | O_APPEND, 0644);
                    if (fd == -1) {
                        perror("open");
                        exit(-1);
                    }
                    dup2(fd, STDOUT_FILENO);
                } else if (redirect_type == 4) {
                    fd = open(redirection_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                    if (fd == -1) {
                        perror("open");
                        exit(-1);
                    }
                    dup2(fd, STDOUT_FILENO);
                    dup2(fd, STDERR_FILENO);
                } else if (redirect_type == 5) {
                    fd = open(redirection_file, O_WRONLY | O_CREAT | O_APPEND, 0644);
                    if (fd == -1) {
                        perror("open");
                        exit(-1);
                    }
                    dup2(fd, STDOUT_FILENO);
                    dup2(fd, STDERR_FILENO);
                } else if (redirect_type == 6) {
                    fd = open(redirection_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                    if (fd == -1) {
                        perror("open");
                        exit(-1);
                    }
                    dup2(fd, STDERR_FILENO);
                }
                close(fd);
            }

            if (strchr(args[0], '/') != NULL) {
                execv(args[0], args);
                perror("execv");
                exit(-1);
            }

            char *path_env = getenv("PATH");
            if (path_env == NULL) {
                perror("getenv");
                exit(-1);
            }

            char *directory = strtok(path_env, ":");
            char cmd_path[MAXLINE];

            while (directory != NULL) {
                snprintf(cmd_path, sizeof(cmd_path), "%s/%s", directory, args[0]);
                if (access(cmd_path, X_OK) == 0) {
                    execv(cmd_path, args);
                    perror("execv");
                    exit(-1);
                }
                directory = strtok(NULL, ":");
            }

            exit(255);
        } else {
            int status;
            wait(&status);
            if (WIFEXITED(status)) {
                if (WEXITSTATUS(status) == 255) {
                    had_error = 1;
                } else {
                    had_error = 0;
                }
            }
        }
    }

    free_shell_variables(&shell_vars);
    free_history(&history);
    if (input != stdin) {
        fclose(input);
    }

    if (had_error) {
        return -1;
    }
    return 0;
}