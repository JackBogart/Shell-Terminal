#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>

#define MYEXIT 60
#define WILD_CARD 69
#define BUF_SIZE 1024

typedef struct token
{
    char ch[BUF_SIZE];
} tok;

typedef struct command
{
    int infd;  // file descriptor of input stream
    int outfd; // file descriptor of output stream
    int tokens;
    int arrSize;
    tok *token;
} cmd;

int runCmd(cmd*);

void addToken(cmd *c, char *inpToken)
{
    if (c->tokens == c->arrSize)
    {
        tok *newArr = calloc(2 * c->arrSize, sizeof(tok));
        for (int iTok = 0; iTok < c->tokens; iTok++)
        {
            for (int iCha = 0; iCha < BUF_SIZE; iCha++)
            {
                newArr[iTok].ch[iCha] = c->token[iTok].ch[iCha];
            }
        }
        free(c->token);
        c->token = newArr;
    }
    strcpy(c->token[c->tokens++].ch, inpToken);
}

void prtCmd(cmd *);

cmd *newCmd()
{
    cmd *new = malloc(sizeof(cmd));
    new->token = calloc(10, sizeof(tok));
    new->tokens = 0;
    new->arrSize = 10;
    new->infd = STDIN_FILENO;
    new->outfd = STDOUT_FILENO;
    return new;
}
void freeCmd(cmd *freeCmd)
{
    free(freeCmd->token);
    free(freeCmd);
}
// returns 0 on success
int bareName(cmd *command) // char *token, tok *cmd
{
    char **args = malloc(8 * ((command->tokens) + 1));
    for (int i = 0; i < command->tokens; i++)
    {
        args[i] = (char *)&command->token[i];
    }
    args[command->tokens] = NULL;

    if (execvp(command->token->ch, args) == 0)
    {
        free(args);
        exit(EXIT_SUCCESS);
    }
    if (command->token[0].ch[0] == '~'){
        chdir(getenv("HOME"));
        if(command->token[0].ch[2] != 0)
            if (chdir (&(command->token[0].ch[2])) == -1){
                perror("File or directory not found\n");
                return (1);
            }
        if (execvp(&(command[0].token[0].ch[2]), args) == 0)
        {
            free(args);
            exit(EXIT_SUCCESS);
        }
    }

    struct stat buffer;
    char path[6][BUF_SIZE];
    strcpy(path[0], "/usr/local/sbin");
    strcpy(path[1], "/usr/local/bin");
    strcpy(path[2], "/usr/sbin");
    strcpy(path[3], "/usr/bin");
    strcpy(path[4], "/sbin");
    strcpy(path[5], "/bin");

    for (int i = 0; i < 6; i++)
    {
        strcat(path[i], "/");
        strcat(path[i], command->token->ch); // current path test + given bare name
        if (stat(path[i], &buffer) == 0)     // file found
        {
            if (buffer.st_mode & S_IXUSR || buffer.st_mode & S_IXGRP || buffer.st_mode & S_IXOTH) // checks executable permissions
            {
                if (execv(path[i], args) == -1)
                {
                    perror("Failed to execute command");
                    free(args);
                    exit(EXIT_FAILURE);
                }
                free(args);
                exit(EXIT_SUCCESS);
            }
            else // cant be executed
            {
                printf("Error: file cannot be executed\n");
                free(args);
                exit(EXIT_FAILURE);
            }
        }
    }

    perror("Failed to execute command");
    free(args);
    exit(EXIT_FAILURE);
}

int wild_equals(char* wild_string, char* fixed_string){
    int wild_string_length = strlen(wild_string);
    int fixed_string_length = strlen(fixed_string);
    for (int i = 0; wild_string[i] != '*'; i++)
        if (fixed_string[i] != wild_string[i] 
          || i == fixed_string_length)
            return 0;
    for (int i = 0; wild_string[wild_string_length - i] != '*'; i++){
        if (fixed_string[fixed_string_length - i] != wild_string[wild_string_length - i]
          || i < 0)
            return 0;
    }
    return 1;
}

cmd * dup_cmd(cmd* old)
{
    cmd* new = malloc(sizeof(cmd));
    new->token = malloc (old->tokens * sizeof(tok));
    memcpy(new->token, old->token, sizeof(tok) * old->tokens);
    new->tokens = old->tokens;
    new->infd = old->infd;
    old->outfd = old->outfd;
    return new;
}

void run_wild (cmd* wild_cmd, int wild_ind){

    char* wild_string = wild_cmd->token[wild_ind].ch;
    
    int wild_strlen = strlen (wild_string);
        int last_slash_ind = -1;
        for (int i = wild_strlen - 1; i>=0; i--){
            if (wild_string[i] == '/'){
                last_slash_ind = i;
                break;
            }
        }

        char wild_part [BUF_SIZE+1];
        char path [BUF_SIZE+1];
        for(int i = 0; i < BUF_SIZE+1; i++){
            wild_part[i] = 0;
            path[i] = 0;
        }

        
        for (int i = 0; i < last_slash_ind; i++)
            path[i] = wild_string[i];
        for (int i = last_slash_ind + 1; i < wild_strlen; i++){
            wild_part[i - last_slash_ind - 1] = wild_string[i]; 
        }

        
        DIR* search_directory;
        if(last_slash_ind == -1){
            search_directory = opendir(".");
        }else if (path[0] == 0){
            chdir("/");
            search_directory = opendir(".");
        }
        else if (path[0] == '~'){
            chdir(getenv("HOME"));
            if (path[2] == 0){
                search_directory = opendir(".");
            }else{
                search_directory = opendir(path+2);
            }
        }
        else search_directory = opendir(path);
        struct dirent* search_item;
        while((search_item = readdir(search_directory)) != NULL){
            char* real_name = search_item->d_name;
            //printf("Wild name: %s\n", wild_part);
            //printf("found file: %s\n\n ", real_name);
            if (wild_equals(wild_part, real_name)){
                
                cmd* new = dup_cmd(wild_cmd);
                memset(&new->token[wild_ind].ch, 0, BUF_SIZE);
                strcpy(new->token[wild_ind].ch, real_name);
                //printf("Command: %s\n", new->token->ch);
                runCmd(new);
                freeCmd(new);
            }
        }
        closedir(search_directory);
}

int execute(cmd *cmd, int in, int out)
{
    // command is pwd
    if (strcmp(cmd->token[0].ch, "pwd") == 0)
    {
        char buff[BUF_SIZE+1];
        getcwd(buff, BUF_SIZE); // verify pwd works
        printf("%s\n", buff);
        return EXIT_SUCCESS;
    }

    else if (strcmp(cmd->token[0].ch, "cd") == 0) // change directory
    {
        // cd NULL will take us to home directory
        if (cmd->tokens == 1)
        {
            if (chdir(getenv("HOME")) == -1)
            {
                printf("cd: No such file or directory\n");

                return EXIT_FAILURE;
            }
            return EXIT_SUCCESS;
        } 
        else if (cmd->token[1].ch[0] == '~'){
            if (chdir(getenv("HOME")) == -1)
            {
                printf("cd: No such file or directory\n");

                return EXIT_FAILURE;
            }
            if (cmd->token[1].ch[2] != 0)
            chdir(&cmd->token[1].ch[2]);
            return EXIT_SUCCESS;
        }

        else if (chdir(cmd->token[1].ch) == -1)
        {
            // May need to change based on interactions with batch mode
            printf("cd: No such file or directory\n");

            return EXIT_FAILURE;
        }
        return EXIT_SUCCESS;
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
        if (in != STDIN_FILENO) // handle input redirection
        {
            if (dup2(in, STDIN_FILENO) == -1)
            {
                perror("Failed to redirect input");
                exit(EXIT_FAILURE);
            }
            close(in);
        }

        if (out != STDOUT_FILENO) // handle output redirection
        {
            if (dup2(out, STDOUT_FILENO) == -1)
            {
                perror("Failed to redirect output");
                exit(EXIT_FAILURE);
            }
            close(out);
        }

        if (bareName(cmd) == -1)
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
        return WEXITSTATUS(status);
    }
    // Control should never reach here
    printf("check end of execute - TESTING\n");
    return -1;
}

int runCmd(cmd *run)
{
    //no input
    if(strcmp(run->token->ch, "") == 0)
        return EXIT_SUCCESS;
        
    int end_of_pipe = 1;
    // check for the exit command
    char exit_literal[5] = "exit";
    if (strcmp(exit_literal, run->token[0].ch) == 0)
    {
        return (MYEXIT);
    }

    // check for wild cards, pass it on if so
    for (int tokInd = 0; tokInd < run->tokens; tokInd++)
    {
        int i = 0;
        char ch;
        while ((ch = run->token[tokInd].ch[i++]) != 0)
        {
            if (ch == '*')
            {
                run_wild(run, tokInd);
                return WILD_CARD;
            }
        }
    }

    cmd *currCmd = newCmd();
    int currTokInd = 0;
    for (int tokInd = 0; tokInd < run->tokens; tokInd++)
    {
        char first_char = run->token[tokInd].ch[0];
        // printf("%c,%d\n", first_char, currTokInd);
        //  ensure that no consecutive tokens or last token are special characters
        if ((currTokInd == 0 || tokInd == run->tokens - 1) && (first_char == '<' || first_char == '>' || first_char == '|'))
        {
            continue;
            return EXIT_FAILURE;
        }

        // redirect the stdin for the < token
        if (first_char == '<')
        {
            // printf("line 239 redirect input \n");
            currCmd->infd = open(run->token[++tokInd].ch, O_RDONLY);
            if (currCmd->infd < 0)
            {
                printf("File not found\n");
                return EXIT_FAILURE;
            }
        } // redirect stdout for the > token
        else if (first_char == '>')
        {
            currCmd->outfd = open(run->token[++tokInd].ch, O_WRONLY | O_TRUNC | O_CREAT, 0640);
            if (currCmd->outfd < 0)
            {
                perror("File not found\n");
                return EXIT_FAILURE;
            }
        } // handle the pipe condition
        else if (first_char == '|')
        {
            int myPipe[2];
            if (pipe(myPipe) < 0)
            {
                perror("Pipe failed!\n");
                exit(1);
            }
            int pid = fork();
            if (pid < 0)
            {
                perror("Fork failed\n");
                exit(1);
            }
            else if (pid > 0)
            { // parent process, waiting for child
                close(myPipe[1]);
                freeCmd(currCmd);
                currCmd = newCmd(); // parent discards currCmd, child runs it
                currCmd->infd = myPipe[0];
                wait(NULL);
                currTokInd = -1; // parent starts parsing a new command
            }
            else
            { // child process
                end_of_pipe = 0;
                currCmd->outfd = myPipe[1];
                close(myPipe[0]);
                break; // child process leaves the parsing loop to exec
            }
        }
        else if (first_char == 0)
            continue;
        else
            addToken(currCmd, run->token[tokInd].ch);
        currTokInd++;
    }
    int e = execute(currCmd, currCmd->infd, currCmd->outfd);

    if(currCmd->outfd != STDOUT_FILENO)
        close(currCmd->outfd);
    if(currCmd->infd != STDIN_FILENO)
        close(currCmd->infd);

    freeCmd(currCmd);
    if (!end_of_pipe)
        exit(0);
    return e;
}

int main(int argc, char *argv[])
{
    int interactive = 1;
    // batch mode setup
    if (argc > 1)
    {
        int batchFD = open(argv[1], O_RDONLY);
        dup2(batchFD, STDIN_FILENO); // setting stdin
        close(batchFD);
        interactive = 0;
    }

    cmd *currCmd = newCmd();
    char currTok[BUF_SIZE]; // (TO_DO) make token size expandable
    memset(currTok, 0, BUF_SIZE);
    char ch;
    if (interactive)
    {
        fprintf(stderr, "Welcome to my shell!\nmysh> ");
    }
    for (int chInd = 0; read(0, &ch, 1) == 1; chInd++)
    {
        if (ch == '<' || ch == '>' || ch == '|' || ch == '\n' || ch == ' ')
        {
            // add previous token to command
            if (currTok[0] != 0)
            {
                addToken(currCmd, currTok);
                for (int i = 0; i < BUF_SIZE; i++)
                    currTok[i] = 0;
            }

            if (ch == '<' || ch == '>' || ch == '|')
            {
                currTok[0] = ch;
                addToken(currCmd, currTok);
                currTok[0] = 0;
            }

            else if (ch == '\n')
            {
                int status = runCmd(currCmd);
                if (status == MYEXIT)
                {
                    freeCmd(currCmd);
                    fprintf(stderr, "mysh: exiting\n");
                    exit(EXIT_SUCCESS);
                }
                else if (status == EXIT_FAILURE && interactive)
                {
                    fprintf(stderr, "!");
                }
                // free old command before generating new command
                freeCmd(currCmd);

                // allocate a new command struct
                currCmd = newCmd();
                if (interactive)
                    fprintf(stderr, "mysh> ");
            }
            chInd = -1;
        }
        else
        {
            currTok[chInd] = ch;
        }
    }
    addToken(currCmd, currTok);

    runCmd(currCmd);
    freeCmd(currCmd);
    return EXIT_SUCCESS;
}
