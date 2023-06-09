#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <glob.h>

#define true 1
#define false 0
#define MAX_COMMANDS 16

/*
TO-DO:
Combining with && and || - This is a pain in the ass to implement because of "||". Distinguishing OR versus piping input, combined with correct tokenization of spaces is a bitch. Gonna do this on a rainy day if I ever come back to this. Look into the parsing in main().
Look into pipes running in parallel. No big difference in functionality, but that is how they are supposed to work. I would need to fork for each pipe so a process can run while I am parsing the next command. Would need to look at main() and execute().
*/

typedef struct Command
{
    char *path;  // path to executable
    char **args; // list of arguments
    int arg_length;
    int fd_in;  // input file
    int fd_out; // output file
} Command;

volatile int active = 1;

void handle()
{
    active = 0;
}

/*
Creates handler for interrupt command to properly exit the shell.
*/
void install_handlers()
{
    struct sigaction act;
    act.sa_handler = handle;
    act.sa_flags = 0;
    sigemptyset(&act.sa_mask);
    sigaction(SIGINT, &act, NULL);
}

/*
Creates a new command with default input & output file descriptors.
*/
Command *newCommand()
{
    Command *new_command = malloc(sizeof(Command));
    new_command->path = calloc(1, BUFSIZ * sizeof(char));
    new_command->args = NULL;
    new_command->arg_length = 0;
    new_command->fd_in = STDIN_FILENO;
    new_command->fd_out = STDOUT_FILENO;
    return new_command;
}
/*
Frees a given command.
*/
void freeCommand(Command *cmd)
{
    if (cmd->args != NULL)
    {
        for (int i = 0; i < cmd->arg_length; i++)
        {
            free(cmd->args[i]); // free individual tokens
        }
        free(cmd->args);
    }
    free(cmd->path);
    free(cmd);
}

void freeCommandList(Command **cmdArray, int length)
{
    for (int i = 0; i < length; i++)
    {
        freeCommand(cmdArray[i]);
    }
}

/*
Dynamic arraylist of arguments for a command.
*/
char **add_to_args(Command *cmd, char **args, int *length, char *token)
{
    if (cmd->path[0] == '\0')
    { // executable is uninitialized, add it as the command path instead of an argument
        strcpy(cmd->path, token);
        free(token); // freeing previous argument dupllicated by strdup
        return args;
    }

    (*length)++;
    args = realloc(args, (*length) * sizeof(char *));

    if (args == NULL)
    {
        perror("Failed to allocate memory\n");
        exit(EXIT_FAILURE);
    }

    // Add the new element
    args[(*length) - 1] = token;

    return args;
}

int try_execute(char *path, Command *cmd)
{
    struct stat buffer;
    if (stat(path, &buffer) == 0) // file found
    {
        if (buffer.st_mode & S_IXUSR || buffer.st_mode & S_IXGRP || buffer.st_mode & S_IXOTH) // checks executable permissions
        {
            // Create a new array with the command name as the first argument
            char **exec_args = malloc((cmd->arg_length + 2) * sizeof(char *)); // +2 for the command name and NULL terminator
            exec_args[0] = cmd->path;
            for (int j = 0; j < cmd->arg_length; j++)
            {
                exec_args[j + 1] = cmd->args[j];
            }
            exec_args[cmd->arg_length + 1] = NULL; // NULL-terminate the array

            if (execv(path, exec_args) == -1)
            {
                perror("Failed to execute command");
                exit(EXIT_FAILURE);
            }
            free(exec_args); // Free the temporary array, no need to free the contents as they are pointing to existing strings
            exit(EXIT_SUCCESS);
        }
        else // cant be executed
        {
            perror("Error: file cannot be executed");
            exit(EXIT_FAILURE);
        }
    }
    return -1; // file not found
}

int non_built_in(Command *cmd)
{
    // Check if the command path contains '/'
    if (strchr(cmd->path, '/') != NULL)
    {
        if (try_execute(cmd->path, cmd) == EXIT_SUCCESS)
            return EXIT_SUCCESS;
        else
            return EXIT_FAILURE;
    }

    // Handle bare name
    char path[PATH_MAX];
    char *prefixes[] = {
        "/usr/local/sbin/",
        "/usr/local/bin/",
        "/usr/sbin/",
        "/usr/bin/",
        "/sbin/",
        "/bin/"};
    int num_prefixes = sizeof(prefixes) / sizeof(char *);

    for (int i = 0; i < num_prefixes; i++)
    {
        strcpy(path, prefixes[i]);
        strcat(path, cmd->path); // current path test + given bare name
        if (try_execute(path, cmd) == EXIT_SUCCESS)
            return EXIT_SUCCESS;
    }

    perror("Failed to execute command");
    exit(EXIT_FAILURE);
}

int execute_command(Command *cmd)
{
    if (cmd->path[0] == '\0') // Empty command
        return EXIT_SUCCESS;
    // Built-in pwd and cd checks before moving onto scanning files for executables
    if (strcmp(cmd->path, "pwd") == 0)
    {
        char cwd[PATH_MAX];

        if (getcwd(cwd, sizeof(cwd)) != NULL)
        {
            write(cmd->fd_out, cwd, strlen(cwd));
            write(cmd->fd_out, "\n", 1);
            if (cmd->fd_out != STDOUT_FILENO)
                close(cmd->fd_out);
            return EXIT_SUCCESS;
        }
        else
        {
            perror("getcwd() error");
            return EXIT_FAILURE;
        }
    }
    else if (strcmp(cmd->path, "cd") == 0)
    {
        if (cmd->arg_length > 1)
            return EXIT_FAILURE;
        else
        {
            if (cmd->arg_length == 0 && chdir(getenv("HOME")) == 0)
            {
                return EXIT_SUCCESS;
            }
            else if (chdir(cmd->args[0]) == 0)
            {
                return EXIT_SUCCESS;
            }
            else
            {
                perror("cd");
                return EXIT_FAILURE;
            }
        }
    }

    // Fork process
    pid_t pid = fork();
    if (pid == -1)
    {
        perror("Failed to fork process");
        return EXIT_FAILURE;
    }
    else if (pid == 0) // Child process
    {
        if (cmd->fd_in != STDIN_FILENO) // handle input redirection
        {
            if (dup2(cmd->fd_in, STDIN_FILENO) == -1)
            {
                perror("Failed to redirect input");
                exit(EXIT_FAILURE);
            }
            close(cmd->fd_in);
        }

        if (cmd->fd_out != STDOUT_FILENO) // handle output redirection
        {
            if (dup2(cmd->fd_out, STDOUT_FILENO) == -1)
            {
                perror("Failed to redirect output");
                exit(EXIT_FAILURE);
            }
            close(cmd->fd_out);
        }

        if (non_built_in(cmd) == -1)
        {
            perror("Failed to execute command");
            exit(EXIT_FAILURE);
        }
    }
    else
    {
        // Parent process, waits for child processs to complete
        int status;
        if (waitpid(pid, &status, 0) == -1)
        {
            perror("Failed to wait for child process");
            return EXIT_FAILURE;
        }

        return WEXITSTATUS(status);
    }
    return EXIT_FAILURE;
}

int main(int argc, char **argv)
{
    install_handlers();
    if (argc == 1)
        write(STDOUT_FILENO, "\e[1;92m""Welcome to my shell!\n", 28);

    // variables for buffer
    char c;
    char buf[BUFSIZ];
    Command *commands[MAX_COMMANDS];
    int bytes, pos, command_count = 0;
    int inputRedirect = false, outputRedirect = false, escapeChar = false, homeEnv = true;

    if (argc > 1)
    {
        int batchFD = open(argv[1], O_RDONLY);
        dup2(batchFD, STDIN_FILENO);
        close(batchFD);
    }

    while (active)
    {
        if (argc <= 1)
        {
            char cwd[1024];
            if (getcwd(cwd, sizeof(cwd)) != NULL)
            {
                char *home = getenv("HOME");
                char *relative_path = strstr(cwd, home);

                relative_path += strlen(home); // skip over the home part
                write(STDOUT_FILENO, "\e[1;95m""mysh:",12);
                write(STDOUT_FILENO, "\e[1;94m""~",8);
                write(STDOUT_FILENO, relative_path, strlen(relative_path));
                write(STDOUT_FILENO, "> ""\x1b[0m", 6);
            }
        }
        pos = 0;
        Command *curr_command = newCommand();
        commands[command_count++] = curr_command;
        while ((bytes = read(STDIN_FILENO, &c, 1)) >= 0)
        {
            if (bytes < 0)
            {
                perror("read error");
                break;
            }
            else if ((c == ' ' || bytes == 0 || c == '\n' || c == '<' || c == '>' || c == '|') && escapeChar == false) // end of command (newline or EOF) or a delimiter
            {
                if (pos == 0 && c != '\n')
                { // Skips/overwrites blank tokens
                    if (c == '<')
                    {
                        inputRedirect = true;
                    }
                    else if (c == '>')
                    {
                        outputRedirect = true;
                    }
                    else if (c == '|')
                    {
                        int fd[2];
                        pipe(fd);
                        commands[command_count - 1]->fd_out = fd[1];
                        curr_command = newCommand();
                        curr_command->fd_in = fd[0];
                        commands[command_count++] = curr_command;
                    }
                    else if (bytes == 0)
                    { // blank newline at end of a bash script
                        freeCommandList(commands, command_count);
                        goto shell_exit;
                    }
                    continue;
                }
                buf[pos] = '\0';
                // handling redirections
                if (inputRedirect)
                {
                    if ((curr_command->fd_in = open(buf, O_RDONLY)) == -1)
                        perror("open error");
                    inputRedirect = false;
                }
                else if (outputRedirect)
                {
                    if ((curr_command->fd_out = open(buf, O_WRONLY | O_CREAT | O_TRUNC, 0640)) == -1)
                        perror("open error");
                    outputRedirect = false;
                }
                else
                {
                    char *arg = strdup(buf); // making copy of argument

                    // if argument contains wildcard character
                    if (strchr(arg, '*') != NULL)
                    {
                        glob_t glob_result;
                        glob(arg, 0, NULL, &glob_result);
                        if (glob_result.gl_pathc == 0)
                        { // No matching wildcard expansion
                            if (strstr(arg, "\\*") != NULL)
                            { // If the argument contained an escaped wildcard, remove the backslash
                                char *temp = malloc(strlen(arg));
                                int j = 0;
                                for (int i = 0; arg[i]; ++i)
                                {
                                    if (arg[i] == '\\' && arg[i + 1] == '*')
                                    {
                                        temp[j++] = '*';
                                        i++; // skip the next character
                                    }
                                    else
                                        temp[j++] = arg[i];
                                }
                                temp[j] = '\0';
                                free(arg);
                                arg = temp;
                            }
                            curr_command->args = add_to_args(curr_command, curr_command->args, &curr_command->arg_length, arg);
                        }
                        else
                        { // Matching wildcard expansion, free original arg containing wildcard character
                            free(arg);
                            for (unsigned int i = 0; i < glob_result.gl_pathc; ++i)
                            {
                                char *arg_copy = strdup(glob_result.gl_pathv[i]);
                                curr_command->args = add_to_args(curr_command, curr_command->args, &curr_command->arg_length, arg_copy);
                            }
                        }
                        globfree(&glob_result);
                    }
                    else // no wildcard character
                    {
                        curr_command->args = add_to_args(curr_command, curr_command->args, &curr_command->arg_length, arg);
                    }
                }

                // Checks for redirect delimiters
                if (c == '<')
                {
                    inputRedirect = true;
                }
                else if (c == '>')
                {
                    outputRedirect = true;
                }
                else if (c == '|')
                {
                    int fd[2];
                    pipe(fd);
                    commands[command_count - 1]->fd_out = fd[1];
                    curr_command = newCommand();
                    curr_command->fd_in = fd[0];
                    commands[command_count++] = curr_command;
                }
                if (bytes == 0 || c == '\n') // end of command (newline or EOF)
                {
                    if (strcmp(curr_command->path, "exit") == 0) // Check for user exiting shell
                    {
                        freeCommandList(commands, command_count);
                        goto shell_exit;
                    }
                    else // executing command
                    {
                        for (int i = 0; i < command_count; ++i)
                        {
                            if (execute_command(commands[i]) == EXIT_FAILURE)
                            {
                                if (argc <= 1)
                                    write(STDOUT_FILENO, "\e[1;91m" "!", 8);
                                break;
                            }
                            if (commands[i]->fd_in != STDIN_FILENO) // closing input redirection
                                close(commands[i]->fd_in);
                            if (commands[i]->fd_out != STDOUT_FILENO) // closing output redirection
                                close(commands[i]->fd_out);
                        }
                    }

                    if (bytes == 0)
                    {
                        freeCommandList(commands, command_count);
                        goto shell_exit;
                    }

                    break;
                }

                memset(buf, 0, BUFSIZ);
                pos = 0;
                homeEnv = true;
            }
            else if (c == '\\' && escapeChar == false)
            {
                escapeChar = true;
                homeEnv = false;
            }
            else if (c != '\n')
            {
                if (escapeChar == true && c == '*')
                    buf[pos++] = '\\';
                buf[pos++] = c;
                if (pos == 1 && buf[0] == '~' && homeEnv == true)
                { // Token beginning with ~ and no escape sequence was used, set to home environment
                    char *homePath = getenv("HOME");
                    strcpy(buf, homePath);
                    pos = strlen(homePath);
                }
                escapeChar = false; // If escape sequence was used
            }
        }

        freeCommandList(commands, command_count);
        command_count = 0;

        if (!active)
        { // Checking for CTRL+C signal
            write(STDOUT_FILENO, "\n", 1);
            goto shell_exit;
        }
    }
    
shell_exit:
    if (argc <= 1)
        write(STDOUT_FILENO, "\e[1;91m" "mysh: exiting\n", 21);

    return EXIT_SUCCESS;
}