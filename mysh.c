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

/*
TO-DO:
Refactor to get rid of dirty gotos
Test path names (broken I believe, doesn't allow me to run ./mysh)
Finish wildcards (expansion and directory works, but need to implement for commands as well such as ec*o)
Implement pipes
Implement all extensions - wildcard directories done
Verify batch mode still works(should be the same but use fd instead of STDIN)
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

/*
Dynamic arraylist of arguments for a command.
*/
char **add_to_args(char **args, int *length, char *token)
{
    // Increase the length of the array
    (*length)++;

    // Reallocate the memory for the array
    args = realloc(args, (*length) * sizeof(char *));

    // Check if the reallocation was successful
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

    // Check if the command path starts with '/'
    if (cmd->path[0] == '/' && try_execute(cmd->path, cmd) == EXIT_SUCCESS)
    {
        return EXIT_SUCCESS;
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
    // Built-in pwd and cd checks before moving onto scanning files for executables
    if (strcmp(cmd->path, "pwd") == 0)
    {
        char cwd[PATH_MAX];

        if (getcwd(cwd, sizeof(cwd)) != NULL)
        {
            write(cmd->fd_out, cwd, strlen(cwd));
            write(cmd->fd_out, "\n", 1);
            if(cmd->fd_out != STDOUT_FILENO)
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
        if (cmd->arg_length != 1)
            return EXIT_FAILURE;
        else
        {
            if (chdir(cmd->args[0]) == 0)
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
        // Parent process
        // Wait for child process to complete
        int status;
        if (waitpid(pid, &status, 0) == -1)
        {
            perror("Failed to wait for child process");
            return EXIT_FAILURE;
        }

        if (cmd->fd_in != STDIN_FILENO) // closing input redirection
            close(cmd->fd_in);
        if (cmd->fd_out != STDOUT_FILENO) // closing output redirection
            close(cmd->fd_out);

        return WEXITSTATUS(status);
    }
    return EXIT_FAILURE;
}

// Deprecated?
char **tokenizer(char *buf)
{

    return NULL;
}

int main(int argc, char **argv)
{

    install_handlers();

    if(argc == 1)
        write(STDOUT_FILENO, "Welcome to my shell!\n", 21);

    // variables for buffer
    char c;
    char buf[BUFSIZ];
    int bytes, pos;
    int inputRedirect = false, outputRedirect = false;

    if (argc > 1)
    {
        int batchFD = open(argv[1], O_RDONLY);
        dup2(batchFD, STDIN_FILENO);
        close(batchFD);
    }

    while (active)
    {
        if (argc <= 1)
            write(STDOUT_FILENO, "mysh> ", 6);

        pos = 0;
        Command *curr_command = newCommand();
        while ((bytes = read(STDIN_FILENO, &c, 1)) >= 0)
        {
            if (bytes < 0)
            {
                perror("read error");
                break;
            }
            else if (c == ' ' || bytes == 0 || c == '\n' || c == '<' || c == '>') // end of command (newline or EOF) or a delimiter
            {
                if(pos == 0){ //Skips/overwrites blank tokens
                    if (c == '<')
                    {
                        inputRedirect = true;
                    }
                    else if (c == '>')
                    {
                        outputRedirect = true;
                    }
                    else if(bytes == 0){ //blank newline at end of a bash script
                        freeCommand(curr_command);
                        goto batch_exit;
                    }
                    continue;
                }
                buf[pos] = '\0';
                if (curr_command->path[0] == '\0')
                { // executable is uninitialized
                    strcpy(curr_command->path, buf);
                }
                // handling redirections
                else if (inputRedirect)
                {
                    if((curr_command->fd_in = open(buf, O_RDONLY)) == -1)
                        perror("open error");
                    inputRedirect = false;
                }
                else if (outputRedirect)
                {
                    if((curr_command->fd_out = open(buf, O_WRONLY | O_CREAT | O_TRUNC, 0640)) == -1)
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
                            curr_command->args = add_to_args(curr_command->args, &curr_command->arg_length, arg);
                        }
                        else
                        { // Matching wildcard expansion, free original arg containing wildcard character
                            free(arg);
                            for (unsigned int i = 0; i < glob_result.gl_pathc; ++i)
                            {
                                char *arg_copy = strdup(glob_result.gl_pathv[i]);
                                curr_command->args = add_to_args(curr_command->args, &curr_command->arg_length, arg_copy);
                            }
                        }
                        globfree(&glob_result);
                    }
                    else // no wildcard character
                    {
                        curr_command->args = add_to_args(curr_command->args, &curr_command->arg_length, arg);
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

                if (bytes == 0 || c == '\n') // end of command (newline or EOF)
                {
                    // tokenizer(buf);
                    if (strcmp(curr_command->path, "exit") == 0) // Check for user exiting shell
                    {
                        freeCommand(curr_command);
                        goto shell_exit;
                    }
                    else
                    {
                        if (execute_command(curr_command) == EXIT_FAILURE)
                        {
                            write(STDOUT_FILENO, "!", 1);
                        }
                    }
                    /* TESTING FOR PARSING
                    printf("Testing executable parsing: %s\n", curr_command->path);
                    for (int i = 0; i < curr_command->arg_length; i++)
                    {
                        printf("Testing argument parsing %d: %s\n", i, curr_command->args[i]);
                    }
                    */

                    if (bytes == 0)
                    {
                        freeCommand(curr_command);
                        goto batch_exit;
                    }

                    break;
                }

                memset(buf, 0, BUFSIZ);
                pos = 0;
            }
            else
            {
                buf[pos++] = c;
            }
        }

        freeCommand(curr_command);

        if (!active)
        { // Checking for CTRL+C signal
            write(STDOUT_FILENO, "\n", 1);
            goto shell_exit;
        }
    }

    if (0)
    { // This is accessed only by the user typing "exit" or sending SIGINT
    shell_exit:
        write(STDOUT_FILENO, "mysh: exiting\n", 15);
    }
    batch_exit:
    // Free remaining resources here (if any)

    return EXIT_SUCCESS;
}