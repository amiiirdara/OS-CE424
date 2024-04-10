#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <unistd.h>
#include <termios.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <limits.h>
#include <pwd.h>
#include <fcntl.h>
#include <dirent.h>


#define FALSE 0
#define TRUE 1
#define INPUT_STRING_SIZE 80
#define MAX_VARS 10 // Maximum number of variables


#include "io.h"
#include "parse.h"
#include "process.h"
#include "shell.h"

int shell_terminal;
int shell_is_interactive;
struct termios shell_tmodes;
id_t shell_pgid;
process* first_process = NULL;

int cmd_quit(tok_t arg[])
{
    printf("_________\nGOODBYE!\n");
    printf("_________\n!خدانگهدار\n");
    kill_all_processes();
    exit(1);
    return 1;
}


int cmd_help(tok_t arg[]);
int cmd_pwd(tok_t arg[]);
int cmd_ls(tok_t arg[]);
int cmd_private_ls(tok_t arg[]);
int cmd_cd(tok_t arg[]);
int cmd_cat(tok_t arg[]);
int cmd_set_var(tok_t arg[]);
int cmd_get_var(tok_t arg[]);

int can_execute(const char* path);
int execute_command(char** args);
int lookup(char cmd[]);


/* Command Lookup table */
typedef int cmd_fun_t(tok_t args[]); /* cmd functions take token array and return int */
typedef struct fun_desc
{
    cmd_fun_t* fun;
    char* cmd;
    char* doc;
} fun_desc_t;

fun_desc_t cmd_table[] = {
    // Persian version
    {cmd_help, "?", "منوی راهنما"},
    {cmd_quit, "خروج", "از پوسته فرمان خارج شوید"},
    {cmd_pwd, "آدرس_فعلی", "چاپ مسیر پوشه فعلی"},
    {cmd_ls, "فهرست", "فهرست محتویات یک پوشه"},
    {cmd_private_ls, "فهرست_مخفی", "فهرست محتویات پوشه با حریم خصوصی"},
    {cmd_cd, "برو", "برو به پوشه‌ی هدف"},
    {cmd_cat, "محتوا", "نمایش محتوای یک فایل"},
    {cmd_set_var, "تنظیم", "تنظیم یک متغیر محلی"},
    {cmd_get_var, "تنظیمات", "گرفتن مقدار یک متغیر محلی"},
    {execute_command, "اجرا", "دستوراتی نظیر > $ و & را اجرا کنید"},

    // English version
    {cmd_help, "?", "show this help menu"},
    {cmd_quit, "quit", "quit the command shell"},
    {cmd_pwd, "pwd", "show the current directory path"},
    {cmd_ls, "ls", "list the contents of a directory"},
    {cmd_private_ls, "ls_p", "list the contents of a directory with privacy"},
    {cmd_cd, "cd", "changes the current working dirctory of shell"},
    {cmd_cat, "cat", "display the content of a file"},
    {cmd_set_var, "set", "set a local variable"},
    {cmd_get_var, "get", "get the value of a local variable"},
    {execute_command, "exec", "execute commands like >, $ and &"},
};

typedef struct
{
    char name[INPUT_STRING_SIZE];
    char value[INPUT_STRING_SIZE];
} Variable;

Variable variables[MAX_VARS];
int variable_count = 0;

int cmd_set_var(tok_t arg[])
{
    if (arg[0] && arg[1])
    {
        int varFound = 0;
        for (int i = 0; i < variable_count; i++)
        {
            if (strcmp(variables[i].name, arg[0]) == 0)
            {
                strcpy(variables[i].value, arg[1]);
                printf("Variable updated: %s = %s\n", arg[0], arg[1]);
                printf("بروزرسانی در تنظیم: %s = %s\n", arg[0], arg[1]);
                varFound = 1;
                break;
            }
        }
        if (!varFound)
        {
            if (variable_count < MAX_VARS)
            {
                strcpy(variables[variable_count].name, arg[0]);
                strcpy(variables[variable_count].value, arg[1]);
                variable_count++;
                printf("Variable set: %s = %s\n", arg[0], arg[1]);
                printf("تنظیم: %s = %s\n", arg[0], arg[1]);
            }
            else
            {
                printf("Error: Variable limit reached.\n");
                printf("خطا: به حداکثر تعداد متغیرها رسیده‌اید.\n");
            }
        }
    }
    else
    {
        printf("Error: Invalid arguments.\n");
        printf("خطا: آرگومان‌های نامعتبر.\n");
    }
    return 1;
}


int cmd_get_var(tok_t arg[])
{
    if (arg[0])
    {
        for (int i = 0; i < variable_count; i++)
        {
            if (strcmp(variables[i].name, arg[0]) == 0)
            {
                printf("%s\n", variables[i].value);
                return 1;
            }
        }
        printf("Variable not found: %s\n", arg[0]);
        printf("متغیر یافت نشد: %s\n", arg[0]);
    }
    else
    {
        printf("Error: No variable name provided.\n");
        printf("خطا: نام متغیری ارائه نشده است\n");
    }
    return 1;
}

const char* get_variable_value(const char* name)
{
    for (int i = 0; i < variable_count; i++)
    {
        if (strcmp(variables[i].name, name) == 0)
        {
            return variables[i].value;
        }
    }
    return NULL; // Variable not found
}


// This function checks if a command can be executed
int can_execute(const char* path)
{
    struct stat st;
    return stat(path, &st) == 0 && st.st_mode & S_IXUSR;
}

// This function is a utility for joining strings together by seperator
char* join_strings(char** array, char seperator)
{
    int count = sizeof(array) / sizeof(array[0]);
    // Step 1: Calculate total length
    int totalLength = 0;
    for (int i = 0; i < count; i++)
    {
        totalLength += strlen(array[i]) + 1; // +1 for the space or null terminator
    }

    // Allocate memory for the new string (+1 for the final null terminator)
    char* result = malloc(totalLength * sizeof(char));
    if (result == NULL)
    {
        fprintf(stderr, "Memory allocation failed\n");
        exit(EXIT_FAILURE);
    }

    // Step 2 & 3: Copy strings and add spaces
    char* currentPosition = result;
    for (int i = 0; i < count; i++)
    {
        strcpy(currentPosition, array[i]);
        currentPosition += strlen(array[i]); // Move pointer to the end of the current string
        if (i < count - 1)
        {
            // Add a space if it's not the last element
            *currentPosition = seperator;
            currentPosition++;
        }
    }

    // Step 4: Null-terminate the new string
    *currentPosition = '\0';

    return result;
}


int execute_command(char** arg)
{
    int background = 0;
    int redirectIndex = -1;
    char* filename = NULL;

    // Check for background execution symbol '&' at the beginning
    if (arg[0] != NULL && strcmp(arg[0], "&") == 0)
    {
        background = 1;
        // Shift all arguments to the left to remove '&'
        int i;
        for (i = 0; arg[i + 1] != NULL; i++)
        {
            arg[i] = arg[i + 1];
        }
        arg[i] = NULL; // Ensure the last argument is NULL after shifting
    }

    for (int i = 0; arg[i] != NULL; i++)
    {
        if (arg[i][0] == '$')
        {
            const char* varValue = get_variable_value(arg[i + 1]);
            if (varValue)
            {
                arg[i] = strdup(varValue); // Replace the variable with its value
                int j = 1;
                while (arg[j] != NULL)
                {
                    arg[j] = NULL;
                    j++;
                }
            }
            else
            {
                fprintf(stderr, "Variable %s not found\n", arg[i + 1]);
                fprintf(stderr, "متغیر %s یافت نشد\n", arg[i + 1]);
                return 1;
            }
        }
    }

    // Check for output redirection '>'
    for (int i = 0; arg[i] != NULL; i++)
    {
        if (strcmp(arg[i], ">") == 0)
        {
            if (arg[i + 1] != NULL)
            {
                redirectIndex = i;
                filename = arg[i + 1];
                break;
            }
            else
            {
                fprintf(stderr, "no target file specified!\n");
                fprintf(stderr, "!هیچ فایل هدفی مشخص نشده است\n");
                return 1;
            }
        }
    }

    // Remove redirection symbol and filename from args if found
    if (redirectIndex != -1)
    {
        arg[redirectIndex] = NULL; // Truncate the arguments list before '>'
    }

    pid_t pid = fork();
    if (pid == 0)
    {
        // Child process

        // Handle output redirection if '>' was detected
        if (redirectIndex != -1)
        {
            int fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
            if (fd < 0)
            {
                perror("open");
                exit(EXIT_FAILURE);
            }
            if (dup2(fd, STDOUT_FILENO) < 0)
            {
                perror("dup2");
                exit(EXIT_FAILURE);
            }
            close(fd);
        }

        if (redirectIndex != -1)
        {
            if (execvp(arg[0], arg) == -1)
            {
                perror("execvp");
                exit(EXIT_FAILURE);
            }
        }
        else
        {
            char* s = join_strings(arg, ' ');
            tok_t* t = getToks(s);
            int fundex = lookup(t[0]);
            if (fundex >= 0) cmd_table[fundex].fun(&t[1]);
            else if (t[0] != NULL)
            {
                run(t);
            }
        }
    }
    else if (pid < 0)
    {
        // Error forking
        perror("fork");
    }
    else
    {
        // Parent process
        if (!background)
        {
            int status;
            waitpid(pid, &status, 0); // Wait for the command to complete if not in background
        }
        else
        {
            printf("Process %d running in background!\n", pid);
            printf("!پروسه‌ی %d در حال اجرا در پیش‌زمینه است\n", pid);
        }
    }
    return 1;
}


int cmd_pwd(tok_t arg[])
{
    char cwd[1000];
    getcwd(cwd, sizeof(cwd));
    printf("Current working directory is: %s\n", cwd);
    printf("مسیر پوشه فعلی: %s\n", cwd);
    return 1;
}

int cmd_ls(tok_t arg[])
{
    DIR* d;
    struct dirent* dir;
    char* dirName = arg[0] ? arg[0] : "."; // Use the current directory if no argument is provided
    d = opendir(dirName);
    if (d)
    {
        while ((dir = readdir(d)) != NULL)
        {
            // You might want to filter out "." and ".." entries
            if (strcmp(dir->d_name, ".") != 0 && strcmp(dir->d_name, "..") != 0)
            {
                printf("%s\n", dir->d_name);
            }
        }
        closedir(d);
    }
    else
    {
        perror("ls");
    }
    return 1;
}

int cmd_private_ls(tok_t arg[])
{
    DIR* d;
    struct dirent* dir;
    char* dirName = arg[0] ? arg[0] : ".";
    char modifiedName[256]; // Assuming file names won't exceed this length
    d = opendir(dirName);
    if (d)
    {
        while ((dir = readdir(d)) != NULL)
        {
            if (strcmp(dir->d_name, ".") != 0 && strcmp(dir->d_name, "..") != 0)
            {
                strncpy(modifiedName, dir->d_name, sizeof(modifiedName));
                int nameLen = strlen(modifiedName);
                // Replace last 6 characters with '_' if the name is long enough
                if (nameLen > 6)
                {
                    for (int i = nameLen - 6; i < nameLen; i++)
                    {
                        modifiedName[i] = '_';
                    }
                }
                else
                {
                    // If the name is shorter than 6 characters, replace the entire name
                    for (int i = 0; i < nameLen; i++)
                    {
                        modifiedName[i] = '_';
                    }
                }
                modifiedName[nameLen] = '\0'; // Null-terminate the modified name
                printf("%s\n", modifiedName);
            }
        }
        closedir(d);
    }
    else
    {
        perror("private_ls");
    }
    return 1;
}


int cmd_cd(tok_t arg[])
{
    if (chdir(arg[0]) != 0)
    {
        printf("an error occurred!\n");
        printf("!خطایی رخ داد\n");
    }
    return 1;
}

int cmd_cat(tok_t arg[])
{
    if (arg[0] == NULL)
    {
        printf("please specify a file!\n");
        printf("!یک فایل هدف مشخص کنید\n");
        return 1;
    }
    FILE* file = fopen(arg[0], "r");
    if (file == NULL)
    {
        perror("cat");
        return 1;
    }
    char line[1024]; // Adjust size as needed
    while (fgets(line, sizeof(line), file) != NULL)
    {
        printf("%s", line);
    }
    fclose(file);
    return 1;
}


int cmd_help(tok_t arg[])
{
    int i;
    for (i = 0; i < (sizeof(cmd_table) / sizeof(fun_desc_t)); i++)
    {
        if (i == 0)
        {
            printf("_______\n");
            printf("نسخه فارسی\n");
            printf("_______\n");
        }
        if (i == 10)
        {
            printf("_______\n");
            printf("English Version\n");
            printf("_______\n");
        }
        printf("%s - %s\n", cmd_table[i].cmd, cmd_table[i].doc);
    }
    return 1;
}

int lookup(char cmd[])
{
    int i;
    for (i = 0; i < (sizeof(cmd_table) / sizeof(fun_desc_t)); i++)
    {
        if (cmd && (strcmp(cmd_table[i].cmd, cmd) == 0)) return i;
    }
    return -1;
}


void init_shell()
{
    /* Check if we are running interactively */
    shell_terminal = STDIN_FILENO;

    /** Note that we cannot take control of the terminal if the shell
        is not interactive */
    shell_is_interactive = isatty(shell_terminal);

    if (shell_is_interactive)
    {
        /* force into foreground */
        while (tcgetpgrp(shell_terminal) != (shell_pgid = getpgrp()))
            kill(-shell_pgid, SIGTTIN);

        shell_pgid = getpid();
        /* Put shell in its own process group */
        if (setpgid(shell_pgid, shell_pgid) < 0)
        {
            perror("Couldn't put the shell in its own process group");
            exit(1);
        }

        /* Take control of the terminal */
        tcsetpgrp(shell_terminal, shell_pgid);
        tcgetattr(shell_terminal, &shell_tmodes);

        signal(SIGINT, SIG_IGN);
        signal(SIGQUIT, SIG_IGN);
        signal(SIGTSTP, SIG_IGN);
        signal(SIGTTIN, SIG_IGN);
        signal(SIGTTOU, SIG_IGN);
    }

    first_process = create_process(NULL);
    first_process->pid = getpid();
}


int shell(int argc, char* argv[])
{
    char* s; /* user input string */
    tok_t* t; /* tokens parsed from input */
    int fundex = -1;

    init_shell();

    while ((s = freadln(stdin)))
    {
        t = getToks(s); /* break the line into tokens */
        fundex = lookup(t[0]); /* Is first token a shell literal */
        if (fundex >= 0) cmd_table[fundex].fun(&t[1]);
        else if (t[0] != NULL)
        {
            run(t);
        }
    }
    return 0;
}
