#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <dirent.h>

#define MAX_VAR 100

typedef struct
{
    char name[50];
    char value[100];
} Variable;

Variable variables[MAX_VAR];
int varCount = 0;

void setVariable(char *name, char *value)
{
    for (int i = 0; i < varCount; i++)
    {
        if (strcmp(variables[i].name, name) == 0)
        {
            strcpy(variables[i].value, value);
            return;
        }
    }
    strcpy(variables[varCount].name, name);
    strcpy(variables[varCount].value, value);
    varCount++;
}

char *getVariable(char *name)
{
    for (int i = 0; i < varCount; i++)
    {
        if (strcmp(variables[i].name, name) == 0)
        {
            return variables[i].value;
        }
    }
    return NULL;
}

int newProcess(char **args, int background, char *output)
{
    pid_t pid;
    int status;

    pid = fork();
    if (pid == 0)
    {
        if (output)
        {
            int fd1 = creat(output, 0644);
            dup2(fd1, STDOUT_FILENO);
            close(fd1);
        }
        /* child process */
        if (execvp(args[0], args) == -1)
        {
            perror("error in newProcess: child process");
        }
        exit(EXIT_FAILURE);
    }
    else if (pid < 0)
    {
        /* error forking */
        perror("error in newProcess: forking");
    }
    else
    {
        if (!background)
        {
            /* parent process waits for child to complete */
            do
            {
                waitpid(pid, &status, WUNTRACED);
            } while (!WIFEXITED(status) && !WIFSIGNALED(status));
        }
        else
        {
            /* parent process does not wait for child to complete */
            printf("Process running in the background with PID %d\n", pid);
        }
    }
    return (-1);
}

int ls(char **args, int background, char *outputfile)
{
    args[0] = "ls";
    return newProcess(args, background, outputfile);
}

int hls(char **args, int background, char *outputfile)
{
    struct dirent *de;
    DIR *dr = opendir(args[1]);
    if (dr == NULL) // opendir returns NULL if couldn't open directory
    {
        printf("Could not open current directory");
        return 0;
    }
    int stdout_save = dup(STDOUT_FILENO);
    if (outputfile)
    {
        int fd1 = creat(outputfile, 0644);
        dup2(fd1, STDOUT_FILENO);
        close(fd1);
    }
    while ((de = readdir(dr)) != NULL)
    {
        char *entry = de->d_name;
        char *ent = entry;
        int is_hidden = 0;
        while (!is_hidden)
        {
            if (*entry == '\0')
            {
                int j = 0;
                entry--;
                while (j < 6 && entry >= ent)
                {
                    *entry = '_';
                    j++;
                    entry--;
                }
                is_hidden = 1;
            }
            else
            {
                entry++;
            }
        }
        printf("%s\n", ent);
    }
    if (outputfile)
    {
        dup2(stdout_save, STDOUT_FILENO);
        close(stdout_save);
    }
    closedir(dr);
    return (-1);
}

int cd(char **args, int background, char *outputfile)
{
    if (args[1] == NULL)
    {
        fprintf(stderr, "expected argument to \"cd\"\n");
    }
    else
    {
        if (chdir(args[1]) != 0)
        {
            perror("error in cd.c: changing dir\n");
        }
    }
    return (-1);
}

int set(char **args, int background, char *outputfile)
{
    if (args[1] == NULL || args[2] == NULL || args[3] == NULL)
    {
        fprintf(stderr, "Usage: set varname = value\n");
        return -1;
    }
    char *name = args[1];
    char *value = args[3]; // assuming the format is set varname = value
    setVariable(name, value);
    return 0;
}

int get(char **args, int background, char *outputfile)
{
    if (args[1] == NULL)
    {
        fprintf(stderr, "Usage: get varname\n");
        return -1;
    }
    char *value = getVariable(args[1]);
    if (value)
    {
        printf("%s\n", value);
    }
    else
    {
        printf("Variable not found\n");
    }
    return 0;
}

int cat(char **args, int background, char *outputfile)
{
    args[0] = "cat";
    return newProcess(args, background, outputfile);
}

int explain(char **args, int background, char *outputfile)
{
    printf("Available commands:\n");
    printf("_______________________\n");
    printf("set {var} = {value} : Set a variable with the specified name and value.\n");
    printf("get {var} : Get the value of the specified variable.\n");
    printf("ls : List directory contents.\n");
    printf("hls : List directory contents in a hidden way.\n");
    printf("cd {directory_path} : Change the current directory to the specified directory.\n");
    printf("cat {file_path} : Display the contents of the specified file.\n");
    printf("${var} : Execute the command stored in the specified variable.\n");
    printf("{command} & : Run the command in the background.\n");
    printf("? : Display this help message.\n");
    printf("exit : Exit the shell.\n");
    return 0;
}

char *readLine(void)
{
    char *line = NULL;
    size_t bufsize = 0;

    if (getline(&line, &bufsize, stdin) == -1) /* if getline fails */
    {
        if (feof(stdin)) /* test for the eof */
        {
            free(line);         /* avoid memory leaks when ctrl + d */
            exit(EXIT_SUCCESS); /* we received an eof */
        }
        else
        {
            free(line); /* avoid memory leaks when getline fails */
            perror("error while reading the line from stdin");
            exit(EXIT_FAILURE);
        }
    }
    return (line);
}

char **splitLine(char *line)
{
    int bufsize = 64;
    int i = 0;
    char **tokens = malloc(bufsize * sizeof(char *));
    char *token;
    const char TOK_DELIM[2] = " \t";
    if (!tokens)
    {
        fprintf(stderr, "allocation error in splitLine: tokens\n");
        exit(EXIT_FAILURE);
    }
    token = strtok(line, TOK_DELIM);
    while (token != NULL)
    {
        /* handle comments */
        if (token[0] == '#')
        {
            break;
        }
        tokens[i] = token;
        i++;
        if (i >= bufsize)
        {
            bufsize += bufsize;
            tokens = realloc(tokens, bufsize * sizeof(char *));
            if (!tokens)
            {
                fprintf(stderr, "reallocation error in splitLine: tokens");
                exit(EXIT_FAILURE);
            }
        }
        token = strtok(NULL, TOK_DELIM);
    }
    tokens[i] = NULL;
    return (tokens);
}

int execute(char **args, int background, char *output)
{
    char *builtin_func_list[] = {
        "set",
        "get",
        "ls",
        "hls",
        "cd",
        "cat",
        "?",
        "exit"};
    int (*builtin_func[])(char **, int, char *) = {
        &set,
        &get,
        &ls,
        &hls,
        &cd,
        &cat,
        &explain,
        NULL};
    int i = 0;

    if (args[0] == NULL)
    {
        /* empty command was entered */
        printf("\n");
        return (-1);
    }

    /* Check if the command is a variable */
    if (args[0][0] == '$')
    {
        char *var_name = args[0] + 1;
        char *command = getVariable(var_name);
        if (command)
        {
            char **new_args = splitLine(command);
            int status = execute(new_args, background, output);
            free(new_args);
            return status;
        }
        else
        {
            printf("Variable not found\n");
            return -1;
        }
    }

    /* find if the command is a builtin */
    for (; i < sizeof(builtin_func_list) / sizeof(char *); i++)
    {
        /* if there is a match execute the builtin command */
        if (strcmp(args[0], builtin_func_list[i]) == 0)
        {
            if (builtin_func[i] == NULL)
            {
                exit(0); // Exit command
            }
            return ((*builtin_func[i])(args, background, output));
        }
    }
    /* create a new process */
    return newProcess(args, background, output);
}

void shell(void)
{
    char *line;
    char **args;
    int status = -1;

    char cwd[1024];
    char hostname[1024];
    gethostname(hostname, sizeof(hostname));
    char *username = getlogin();

    do
    {
        getcwd(cwd, sizeof(cwd));
        printf("%s@%s-%s$ ", username, hostname, cwd); /* print prompt symbol */
        line = readLine();                             /* read line from stdin */
        char *temp = line;
        while (*temp)
        {
            if (*temp == '\n')
            {
                *temp = '\0';
            }
            else
            {
                temp++;
            }
        }
        if (strncmp(line, "set", 3) == 0)
        {
            args = splitLine(line);
            if (args[1] != NULL && args[2] != NULL && args[3] != NULL && strcmp(args[2], "=") == 0)
            {
                setVariable(args[1], args[3]);
            }
            else
            {
                fprintf(stderr, "Usage: set varname = value\n");
            }
            free(line);
            free(args);
            continue;
        }
        if (strncmp(line, "get", 3) == 0)
        {
            args = splitLine(line);
            if (args[1] != NULL)
            {
                char *value = getVariable(args[1]);
                if (value)
                {
                    printf("%s\n", value);
                }
                else
                {
                    printf("Variable not found\n");
                }
            }
            else
            {
                fprintf(stderr, "Usage: get varname\n");
            }
            free(line);
            free(args);
            continue;
        }
        if (line[0] == '$')
        {
            char *var;
            var = line + 1;

            char *command = getVariable(var);
            if (command == NULL)
                printf("Variable not found\n");
            else
            {
                char **new_args = splitLine(command);
                status = execute(new_args, 0, NULL);
                free(new_args);
            }
            free(line);
            continue;
        }
        int is_background = 0;
        if (line[strlen(line) - 1] == '&')
        {
            is_background = 1;
            line[strlen(line) - 1] = '\0';
        }
        args = splitLine(line); /* tokenize line */
        int i = 0;
        char *outputfile = NULL;
        while (args[i])
        {
            if (*(args[i]) == '>')
            {
                outputfile = args[i + 1];
                args[i] = NULL;
                i++;
                if (args[i] && strcmp(args[i], "&") == 0)
                {
                    is_background = 1;
                    args[i] = NULL;
                }
            }
            i++;
        }
        status = execute(args, is_background, outputfile);
        /* avoid memory leaks */
        free(line);
        free(args);
        /* reset status for non-exit commands */
        status = -1;
    } while (status == -1);
}

int main()
{
    shell();
    return 0;
}
