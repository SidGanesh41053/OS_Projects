#ifndef WSH_H
#define WSH_H

#include <stdio.h>
#define MAXLINE 1024
#define MAXARGS 128
#define DEFAULTHISTORY 5

// shell variable relevant structs
typedef struct ShellVariable {
    char *name;
    char *value;
    struct ShellVariable *next;
} ShellVariable;

typedef struct {
    ShellVariable *head;
} ShellVariables;

// history command relevant struct
typedef struct {
    char **commands;
    int size;
    int capacity;
} History;

// function prototypes
// history functions
void init_history(History *history, int capacity);
void free_history(History *history);
void add_to_history(History *history, const char *command);
void print_history(const History *history);

// shell variable functions
void set_shell_variable(ShellVariables *sv, const char *name, const char *value);
char *get_shell_variable(ShellVariables *sv, const char *name);
void free_shell_variables(ShellVariables *sv);
void substitute_variables(char *command, ShellVariables *sv);

// built-in implementation functions
void builtin_ls();

#endif // WSH_H