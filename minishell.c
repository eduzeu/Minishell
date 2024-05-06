/*******************************************************************************
 * Name        : minishell.c
 * Author      : Eduardo Hernandez
 * Pledge      : I pledge my honor that I have abided by the Stevens Honor System. 
 ******************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <string.h>
#include <dirent.h>
#include <sys/types.h>
#include <pwd.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#define BLUE "\x1b[34;1m"
#define DEFAULT "\x1b[0m"
#include <errno.h>
#include <fcntl.h>
#include <ctype.h>

#define MAX_COMMAND_LEN 512

volatile sig_atomic_t interrupted = 0;



void do_cd(char *directory) {
    if (strcmp(directory, "..") == 0) {
        if (chdir("..") != 0) {
            fprintf(stderr, "Error: Cannot change directory to %s. %s\n", directory, strerror(errno));
        }
        return;
    }

    char *token = strtok(directory, " \n");
    int count_args = 0;

    while (token != NULL) {
        count_args++;
        token = strtok(NULL, " \n");
    }

    if (count_args > 1) {
        fprintf(stderr, "Error: Too many arguments to cd.\n");
        return;
    }

    if (directory == NULL || count_args == 0 || strcmp(directory, "~") == 0) {
        struct passwd *pw = getpwuid(getuid());
        if (pw == NULL) {
            fprintf(stderr, "Error: Could not get home directory. %s\n", strerror(errno));
            exit(EXIT_FAILURE);
        }
        directory = pw->pw_dir;
    } else if (directory[0] == '~') {
        // Handle the case where the path starts with ~
        struct passwd *pw = getpwuid(getuid());
        if (pw == NULL) {
            fprintf(stderr, "Error: Could not get home directory. %s\n", strerror(errno));
            exit(EXIT_FAILURE);
        }
        char *home_dir = pw->pw_dir;
        char *new_directory = malloc(strlen(home_dir) + strlen(directory) + 1);
        if (new_directory == NULL) {
            fprintf(stderr, "Error: malloc() failed. %s\n", strerror(errno));
            exit(EXIT_FAILURE);
        }
        strcpy(new_directory, home_dir);
        strcat(new_directory, directory + 1); // Skip ~
        directory = new_directory;
    }

    if (chdir(directory) != 0) {
        fprintf(stderr, "Error: Cannot change directory to %s. %s\n", directory, strerror(errno));
    }
}



void sigint_handler(int signum) {
    interrupted = 1;
    //this was used for testing purposes 
   // printf("\nReceived SIGINT signal. Pressing Ctrl+C again will terminate.\n"); 


}

void do_exit() {
    exit(EXIT_SUCCESS);
}

/* void do_echo(char *message) {
    printf("%s\n", message);
} */

void do_pwd(){
    char directory[1024];
    if(getcwd(directory, sizeof(directory)) == NULL){
    fprintf(stderr, "Error: Cannot get current working directory. %s.\n", strerror(errno));
    }else{
        printf("%s\n", directory);
    }

}

void do_lf() {
    DIR *dir;
    struct dirent *entry;

    dir = opendir(".");
    if (dir == NULL) {
        perror("opendir() error");
        exit(EXIT_FAILURE);
    }

    while ((entry = readdir(dir)) != NULL) {
        if(strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;   
        }
        else{
        printf("%s\n", entry->d_name);

        }
    }

    closedir(dir);
}

typedef struct {
    int process_id;
    char user_name[32];
    char command[MAX_COMMAND_LEN];
} CustomProcessInfo;

//use this to compare 
int compare(const void *first, const void *sec) {
    const CustomProcessInfo *p1 = (const CustomProcessInfo *)first;
    const CustomProcessInfo *p2 = (const CustomProcessInfo *)sec;
    return p1->process_id - p2->process_id;
}
void do_lp() {
    // Open /proc directory
    DIR *proc_dir = opendir("/proc");
    if (proc_dir == NULL) {
        fprintf(stderr, "Error: Cannot open the directory. %s.\n",  strerror(errno));
        exit(EXIT_FAILURE);
    }

    // Process information array
    CustomProcessInfo process[1024];
    int num = 0;

    // Read /proc directory
    struct dirent *entry;
     while ((entry = readdir(proc_dir)) != NULL) {
        // Check if the entry is a directory and its name is a valid process ID
        if (entry->d_type == DT_DIR && atoi(entry->d_name) != 0) {
            char path[512]; 
            snprintf(path, sizeof(path), "/proc/%s/cmdline", entry->d_name);
            
            // Open the cmdline file
            int line = open(path, O_RDONLY);
            if (line == -1) {
                if (errno != ENOENT) {
                    fprintf(stderr, "Error: Cannot open %s: %s.\n", path, strerror(errno));
                    exit(EXIT_FAILURE);
                }
            } 
            else {
                // Read the command line from the cmdline file
                ssize_t read_lines = read(line, process[num].command, sizeof(process[num].command) - 1);
                if (read_lines == -1) {
                    fprintf(stderr, "Error: Failed to read from %s: %s.\n", path, strerror(errno));
                    exit(EXIT_FAILURE);
                }
                close(line);

                // Null-terminate the command string
                process[num].command[read_lines] = '\0';
                process[num].process_id = atoi(entry->d_name);

                // Get user name from stat information
                struct stat stat_buf;
                if (stat(path, &stat_buf) != -1) {
                    struct passwd *passwd_entry = getpwuid(stat_buf.st_uid);
                    if (passwd_entry != NULL) {
                        strncpy(process[num].user_name, passwd_entry->pw_name, sizeof(process[num].user_name));
                    } 
                    else {
                        fprintf(stderr, "Error: Cannot get passwd entry for %s: %s.\n", path, strerror(errno));
                    }
                } 
                else {
                    fprintf(stderr, "Error: Failed to stat %s: %s.\n", path, strerror(errno));
                    exit(EXIT_FAILURE);
                }

                num++;
            }
        }
    }

    // Sort processes by PID
    qsort(process, num, sizeof(CustomProcessInfo), compare);

    // Print process information
    int i = 0;
    while (i < num) {
        printf("%d %s %s\n", process[i].process_id, process[i].user_name, process[i].command);
        i++;
    }
    closedir(proc_dir);
}

int main() {
    // Set up signal handling for SIGINT
    struct sigaction do_handle;
    do_handle.sa_handler = sigint_handler;
    sigemptyset(&do_handle.sa_mask);
    do_handle.sa_flags = 0;
    if (sigaction(SIGINT, &do_handle, NULL) == -1) {
        fprintf(stderr, "Error: Cannot register signal handler. %s.\n",  strerror(errno));
        exit(EXIT_FAILURE);
    }

    char input[1024];


    while (1) {
        char cwd[1024];
        if (getcwd(cwd, sizeof(cwd)) != NULL) {
            printf(BLUE "[%s]" DEFAULT "> ", cwd);
        } else {
            fprintf(stderr, "Error: Cannot read. %s.\n",  strerror(errno));
            exit(EXIT_FAILURE);
        }
        int interrupt = 0;

        
        while (1) {
            if (fgets(input, sizeof(input), stdin) == NULL) {
                if (feof(stdin)) {
                    fprintf(stderr, "Error: Failed to read from stdin. %s\n", strerror(errno));
                    exit(EXIT_FAILURE);
                }
                if (interrupted) {
                    printf("\n");
                    interrupt = 1;
                    break;
                } else {
                    // Other error occurred
                    fprintf(stderr, "Error: Failed to read from stdin. %s.\n", strerror(errno));
                    exit(EXIT_FAILURE);
                }
            }
            break; // Read successful, break from loop
        }

        if(interrupt){
            continue;
        } 

    


        // Remove trailing newline
        input[strcspn(input, "\n")] = 0;

        if (strncmp(input, "exit",4) == 0) {
            do_exit();
        } else if (strncmp(input, "cd",2) == 0) {
            // ~
            //~
            char *dir = input + 3;
            do_cd(dir);
        } else if (strncmp(input, "lf",2) == 0) {
            do_lf();
        } else if (strncmp(input, "lp",2) == 0) {
            do_lp();
        } else if (strncmp(input, "pwd",3) == 0) {
            do_pwd();
        } else {
            // Fork a new process
            pid_t pid = fork();

            if (pid == -1) {
                // Fork failed
                fprintf(stderr, "Error: fork() failed. %s.\n", strerror(errno));
                exit(EXIT_FAILURE);
            } else if (pid == 0) {
                // Child process
                struct sigaction child_action;
                child_action.sa_handler = SIG_DFL;
                sigemptyset(&child_action.sa_mask);
                child_action.sa_flags = 0;
                if (sigaction(SIGINT, &child_action, NULL) == -1) {
                    fprintf(stderr, "Error: Cannot register signal handler. %s.\n",  strerror(errno));
                    exit(EXIT_FAILURE);
                }
                char *args[MAX_COMMAND_LEN];
                char *token = strtok(input, " "); // Tokenize input by space
                int i = 0;
                while (token != NULL) {
                    args[i++] = token;
                    token = strtok(NULL, " "); // Get next token
                }
                args[i] = NULL; // Null-terminate the args array
                execvp(args[0], args);
                fprintf(stderr, "Error: exec() failed. %s.\n", strerror(errno));
                exit(EXIT_FAILURE);
            }else {
                // Parent process
                int status;
                int result;
                int interrupted_by_sigint = 0; // Flag to track if interrupted by SIGINT

                // Wait for the child process to terminate
                do {
                    result = waitpid(pid, &status, 0);
                    if (result == -1 && errno != EINTR) {
                        fprintf(stderr, "Error: wait() failed. %s.\n", strerror(errno));
                        exit(EXIT_FAILURE);
                    }
                    if (result == -1 && errno == EINTR && errno != ECHILD) {
                        // Child process was interrupted by SIGINT
                        interrupted_by_sigint = 1;
                    }
                } while (result == -1);

                if (interrupted_by_sigint) {
                    printf("\n"); // Print a new line for interrupting sleep
                }
            }
        }
    }
    return 0;
}

