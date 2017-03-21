#define _GNU_SOURCE
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <string.h>
#include <signal.h>
#include <termios.h>
#include <fcntl.h>
#include <limits.h>

#define MAX_PROMPT_LENGTH 500
#define MAX_LEN 500
#define SET 1
#define RESET 0

/*** STRUCTURE TYPEDEF ***/
typedef enum status
{
    BG,
    FG
} group_status_t;


typedef struct process
{
    pid_t pid;
    pid_t pgid;
    char *status;
    char *signal;
    char *argv[5];
    struct process *proc_link;
} process_t;

typedef struct group
{
    pid_t pgid;
    char status;
    int nprocess;
    struct process *proc_link;
    struct group *group_link;
} group_t;

typedef enum state
{
    EXITED = 0,
    STOPPED,
    RUNNING,
    KILLED,
    SIGNALLED
} stat_t;

/**** FUNCTION PROTOTYPES ***/
void initialize_msh(void);
void display_prompt(int prompt_pwd);
void change_prompt(char *new_prompt);
void command_parser(char *cmd, group_t **session_leader);
int change_dir(char *cmd);
int is_exit(char *cmd, group_t *session_leader);
int is_ps1(char * cmd, int *prompt_pwd);
int is_jobs(char *cmd, group_t **session_leader);
int is_fg(char *cmd, int terminal, group_t **session_leader, pid_t shell_pid, int *exit_status);
int is_null_input(char *cmd);
int is_echo(char *cmd, int exit_status);
process_t *insert_process(process_t **process_leader);
group_t *insert_group(group_t **session_leader);
void ignore_foreground_signals(int signum);
process_t *release_process_resource(group_t *group, process_t *process);
process_t *release_group_resource(group_t **session_leader);
void print_resource_for_my_shell(group_t *session);
void fg(int terminal, group_t **session_leader, pid_t shell_pid, int *exit_status);
void jobs(group_t **session_leader);
void wait_for_fg(group_t **session_leader, int *exit_status);
void update_status_of_bg(group_t **session_leader);

/**** GLOBAL VARIABLES ****/
pid_t shell_pid;
static char prompt[MAX_PROMPT_LENGTH] = "\033[32;1mShankar:\033[0m";
static char path[200];
static char *stat[] = {"exited", "stopped", "running", "killed", "signalled"};
static char *sig[] = {"", "SIGSTOP", "SIGTSTP", "SIGTTIN", "SIGTTOU"};
extern char **environ;

int main(int argc, char *argv[], char *envp[])
{
    int idx, jdx;
    int prompt_pwd = 1;
    int status, wait_status, exit_status = 0;
    int (*fd)[2];
    char cmd[MAX_LEN];
    pid_t shell_pid = getpid();
    pid_t cpid;
    pid_t backdground_leader_pid;
    group_t *session_leader = NULL;
    group_t *new_group = NULL;
    group_t *grp_ptr;
    process_t *new_process = NULL;
    process_t *proc_ptr;
    int terminal = open(ctermid(NULL), O_RDWR);

    /* Ignore foreground signals */
    signal(SIGINT, ignore_foreground_signals);
    signal(SIGTSTP, ignore_foreground_signals);
    signal(SIGQUIT, ignore_foreground_signals);
    signal(SIGTTOU, SIG_IGN);

    /* Intialize prompt for shell */
    initialize_msh();

    while (1)
    {
        display_prompt(prompt_pwd);

        /* Get command from user */
        fgets(cmd, MAX_LEN, stdin);
        cmd[strlen(cmd) - 1] = '\0';

        /* Check for built in commands */
        if (is_null_input(cmd))
            continue;
        else if (is_exit(cmd, session_leader))
            exit(0);
        else if (is_jobs(cmd, &session_leader))
            continue;
        else if (is_fg(cmd, terminal, &session_leader, shell_pid, &exit_status))
            continue;
        else if(change_dir(cmd))
            continue;
        else if (is_echo(cmd, exit_status))
            continue;
        else if (is_ps1(cmd, &prompt_pwd))
            continue;

        /* Parse external commands */
        command_parser(cmd, &session_leader);

        /* Create child process to execute commands */
        grp_ptr = session_leader;
        while (grp_ptr != NULL)
        {
            if (grp_ptr->pgid == 0)
            {
                idx = 0;
                /* Allocate memory for pipe fds */
                fd = (int (*)[2])calloc(grp_ptr->nprocess * 2, sizeof(int));

                proc_ptr = grp_ptr->proc_link;
                while (proc_ptr != NULL)
                {
                    idx++;

                    /* Create child processes to execute commands of pipeline */

                    /* Last process in the pipeline will not create a pipe */
                    if (proc_ptr->proc_link != NULL)
                        pipe(fd[idx]);

                    /* Fork nprocess times */
                    cpid = fork();

                    switch (cpid)
                    {
                        case -1:
                            /* Error handling for forking */
                            printf("Error forking\n");
                            exit(1);
                        case 0:
                            /* Code for Child to execute pipeline */
                            /* Code for 1st child */
                            if (grp_ptr->nprocess > 1)
                                if (idx == 1)
                                {
                                    if (setpgid(proc_ptr->pid, 0) == -1)
                                    {
                                        perror("setpgid");
                                        exit(1);
                                    }
                                    close(fd[idx][0]);
                                    if (dup2(fd[idx][1], 1) == -1)
                                    {
                                        printf("LINE NO : %d : ERROR : dup2\n", __LINE__);
                                        perror("dup2");
                                        exit(1);
                                    }
                                    close(fd[idx][1]);
                                }
                            /* Code for last child */
                                else if (idx == grp_ptr->nprocess)
                                {
                                    if (setpgid(proc_ptr->pid, backdground_leader_pid) == -1)
                                    {
                                        perror("setpgid");
                                        exit(1);
                                    }
                                    if (dup2(fd[idx - 1][0], 0) == -1)
                                    {
                                        printf("LINE NO : %d : ERROR : dup2\n", __LINE__);
                                        perror("dup2");
                                        exit(1);
                                    }
                                    close(fd[idx - 1][0]);
                                    close(fd[idx - 1][1]);
                                }
                            /* Code for other children */
                                else
                                {
                                    if (setpgid(proc_ptr->pid, backdground_leader_pid) == -1)
                                    {
                                        perror("setpgid");
                                        exit(1);
                                    }
                                    if (dup2(fd[idx - 1][0], 0) == -1)
                                    {
                                        printf("LINE NO : %d : ERROR : dup2\n", __LINE__);
                                        perror("dup2");
                                        exit(1);
                                    }
                                    close(fd[idx - 1][0]);
                                    close(fd[idx - 1][1]);

                                    if (dup2(fd[idx][1], 1) == -1)
                                    {
                                        printf("LINE NO : %d : ERROR : dup2\n", __LINE__);
                                        perror("dup2");
                                        exit(1);
                                    }
                                    close(fd[idx][1]);
                                    close(fd[idx][0]);
                                }

                            /* Do exec with a process in the pipeline for each child */
                            if (execvpe(proc_ptr->argv[0], proc_ptr->argv, envp) == -1)
                            {
                                fprintf(stderr, "%s : command not found\n", proc_ptr->argv[0]);
                                return 0;
                            }
                        default :
                            /* Store pid in process structure */
                            proc_ptr->pid = cpid;

                            /* Change group id */
                            /* Store group pid */
                            if (idx == 1)
                            {
                                /* Make process a group leader */
                                setpgid(proc_ptr->pid, 0);
                                kill(proc_ptr->pid, SIGCONT);
                                backdground_leader_pid = proc_ptr->pid;
                                proc_ptr->pgid = backdground_leader_pid;

                                /* Update group id to group structure */
                                grp_ptr->pgid = backdground_leader_pid;
                            }
                            else
                            {
                                /* Set group id of process as that of group leader */
                                setpgid(proc_ptr->pid, backdground_leader_pid);
                                proc_ptr->pgid = backdground_leader_pid;
                                kill(proc_ptr->pid, SIGCONT);
                            }

                            /* Close extra pipes */
                            if (idx > 1)
                            {
                                close(fd[idx - 1][0]);
                                close(fd[idx - 1][1]);
                            }
                    }
                    /* Move to next process */
                    proc_ptr = proc_ptr->proc_link;
                }
                /* Free memory allocated for pipes */
                free(fd);
            }

            /* Move to next group */
            grp_ptr = grp_ptr->group_link;
        }

        /* Give the control to foreground process */
        if (session_leader->status == FG)
        {
            /* Assign control terminal to forground process */
            if (tcsetpgrp(terminal, session_leader->pgid) == -1)
            {
                perror("tcsetpgrp1");
                exit(0);
            }

            /* Wait for foreground process to change state */
            wait_for_fg(&session_leader, &exit_status);

            /* Get control terminal back */
            if (tcsetpgrp(terminal, shell_pid) == -1)
            {
                perror("tcsetpgrp2");
                exit(0);
            }
        }

    } /* bracket for while(1) */
    return 0;
}

void command_parser(char *cmd, group_t **session_leader)
{
    int idx;
    char *str;
    char *saveptr1, *saveptr2, *saveptr3;
    char *group, *process, *cmd_args;
    group_t *grp_ptr;
    group_t *new_group;
    process_t *proc_ptr;
    process_t *new_process;
    int ampersand_count = 0;
    int no_of_group = 0;

    /* Get '&' count in command */
    for (idx = 0; cmd[idx] != '\0'; idx++)
        if (cmd[idx] == '&')
            ++ampersand_count;

    str = cmd;
    /* First level parsing for process groups */
    for (; ;str = NULL)
    {
        group = strtok_r(str, "&", &saveptr1);
        if (group == NULL)
            break;
        else
        {
            new_group = insert_group(session_leader);
            no_of_group++;
            new_group->nprocess = 0;
            /* Second level parsing for process groups */
            for (; ;group = NULL)
            {   
                process = strtok_r(group, "|", &saveptr2);
                if (process == NULL)
                    break;
                else
                {
                    new_process = insert_process(&new_group->proc_link);
                    new_group->nprocess++;
                    /* Third level parsing for arguments of process groups */
                    for (idx = 0; ; idx++, process = NULL)
                    {
                        cmd_args = strtok_r(process, " ", &saveptr3);
                        if (cmd_args == NULL)
                            break;
                        else
                        {
                            /* Allocate memory to store each argument of new process */
                            new_process->argv[idx] = (char *)calloc(1, sizeof(cmd_args));
                            strcpy(new_process->argv[idx], cmd_args);
                        }
                    }
                    /* Store NULL as end argument */
                    new_process->argv[idx] = NULL;
                }
            }
        }
    }

    /* Make a process forground */
    if (ampersand_count < no_of_group)
        new_group->status = FG;

#ifdef DEBUG
    /* Only for DEBUG : To print the arguments */
    grp_ptr = *session_leader;
    while (grp_ptr != NULL)
    {
        if (grp_ptr->pgid == 0)
        {
            printf("New group\n");
            printf("New group status = %d\n", grp_ptr->status);
            proc_ptr = grp_ptr->proc_link;
            while (proc_ptr != NULL)
            {
                printf("New process\n");
                for (idx = 0; proc_ptr->argv[idx] != NULL; idx++)
                {
                    printf("argv[%d] = %s\n", idx, proc_ptr->argv[idx]);
                }
                proc_ptr = proc_ptr->proc_link;
            }
        }
        grp_ptr = grp_ptr->group_link;
    }
#endif
}

group_t *insert_group(group_t **session_leader)
{
    /* Insert as first element */
    group_t *new_group;

    if (*session_leader == NULL)
    {
        (*session_leader) = (group_t *)calloc(1, sizeof(group_t));
        (*session_leader)->status = BG;
        (*session_leader)->proc_link = NULL;
        (*session_leader)->group_link = NULL;
    }
    else
    {
        new_group = (group_t *)calloc(1, sizeof(group_t));
        new_group->proc_link = NULL;
        new_group->status = BG;
        new_group->group_link = *session_leader;
        (*session_leader) = new_group;
    }
    return *session_leader;
}

process_t *insert_process(process_t **process_leader)
{
    /* Insert as last element */
    process_t *ptr = *process_leader;

    if (ptr == NULL)
    {
        (*process_leader) = (process_t *)calloc(1, sizeof(process_t));
        (*process_leader)->proc_link = NULL;
        (*process_leader)->status = stat[RUNNING];
        (*process_leader)->signal = sig[0];
        return (*process_leader);
    }
    else
    {
        while (ptr->proc_link != NULL)
        {
            ptr = ptr->proc_link;
        }
        ptr->proc_link = (process_t *)calloc(1, sizeof(process_t));
        ptr->proc_link->proc_link = NULL;
        ptr->proc_link->status = stat[RUNNING];
        ptr->proc_link->signal = sig[0];
        return ptr->proc_link;
    }
}

void initialize_msh(void)
{
    system("clear");
    printf("\033[32;1m");
    printf("-----------------------------------------------------------------------------------\n");
    printf("|                        WELCOME TO MINI SHELL                                    |\n");
    printf("|                            AUTHOR : ABHI                                        |\n");
    printf("-----------------------------------------------------------------------------------\n");
    printf("\033[0m");
}

void display_prompt(int prompt_pwd)
{
    if (prompt_pwd)
    printf("%s\033[34;1m%s:\033[0m ", prompt, getcwd(path, 200));
    else
    printf("%s", prompt);
    fflush(stdout);
}

void change_prompt(char *new_prompt)
{
    strcpy(prompt, "\033[32;1m");
    strcat(prompt, new_prompt + 4);
    strcat(prompt, "\033[0m");
    return;
}

int change_dir(char *cmd)
{
    if (strcmp(cmd, "cd") == 0)
    {
        if (chdir(getenv("HOME")) != 0)
            perror("cd");
        return 1;
    }
    else if (strncmp(cmd, "cd ", 3) == 0)
    {
        if ((chdir(cmd + 3)) != 0)
            perror("cd");
        return 1;
    }
    else
    {
        return 0;
    }
}

int is_exit(char *cmd, group_t *session_leader)
{
    group_t *grp_ptr;

    if (strcasecmp(cmd, "exit") == 0)
    {
        for (grp_ptr = session_leader; grp_ptr; grp_ptr = grp_ptr->group_link)
            killpg(grp_ptr->pgid, SIGHUP);
        return 1;
    }
    else
    {
        return 0;
    }
}

int is_null_input(char *cmd)
{
    if (strcmp(cmd, "") == 0)
        return 1;
    else
        return 0;
}

int is_ps(char *cmd)
{
    if (strcmp(cmd, "jobs -l") == 0)
        return 1;
    else
        return 0;
}

int is_jobs(char *cmd, group_t **session_leader)
{
    if (strcmp(cmd, "jobs") == 0)
    {
        update_status_of_bg(session_leader);
        jobs(session_leader);
        return 1;
    }
    else if (strcmp(cmd, "jobs -l") == 0)
    {
        update_status_of_bg(session_leader);
        print_resource_for_my_shell(*session_leader);
        return 1;
    }
    else
    {
        return 0;
    }
}

int is_fg(char *cmd, int terminal, group_t **session_leader, pid_t shell_pid, int *exit_status)
{
    if (strcmp(cmd, "fg") == 0)
    {
        fg(terminal, session_leader, shell_pid, exit_status);
        return 1;
    }
    else
    {
        return 0;
    }
}

int is_ps1(char * cmd, int *prompt_pwd)
{
        /* Check if command is exit */
        if (strncmp(cmd, "PS1=", 4) == 0)
        {
            change_prompt(cmd);
            *prompt_pwd = 0;
            return 1;
        }
        if (strncmp(cmd, "PS2=", 4) == 0)
        {
            change_prompt(cmd);
            *prompt_pwd = 1;
            return 1;
        }
        return 0;
}

int is_echo(char *cmd, int exit_status)
{
    char *env; 
    if (strncmp(cmd, "echo ", 5) != 0)
    {
        return 0;
    }
    else 
    {
        if ((env = strchr(cmd, '$')) == NULL)
        {
            return 0;
        }
        else
        {
            if (strncmp(env, "$PWD", 5) == 0)
            {
                printf("%s\n", path);
                return 1;
            }
            else if (strcmp(cmd, "echo $?") == 0)
            {
                printf("%d\n", exit_status);
            }
            else if (strcmp(cmd, "echo $$") == 0)
            {
                printf("%d\n", getpid());
            }
            else
            {
                env = getenv(++env);
                if (env != NULL)
                printf("%s\n", env);
                return 1;
            }
        }
    }
}

void ignore_foreground_signals(int signum)
{
    /* This is just to ignore the foreground signals by shell */
}

process_t *release_process_resource(group_t *group, process_t *process)
{
    int idx;
    group_t *grp_ptr = group;
    process_t *proc_ptr = grp_ptr->proc_link;
    process_t *prev_proc;

    if (grp_ptr->proc_link == process)
    {
        /* Free memory allocated for command line arguments */
        for (idx = 0; process->argv[idx] != NULL; idx++)
            free(process->argv[idx]);

        grp_ptr->proc_link = grp_ptr->proc_link->proc_link; 
        /* Release process resource */
        free(process);
        /* Decrement no of process in group */
        grp_ptr->nprocess--;

        /* Return next process in the group */
        return grp_ptr->proc_link;
    }
    else
    {
        while (proc_ptr != NULL)
        {
            if (proc_ptr == process)
                break;
            prev_proc = proc_ptr;
            proc_ptr = proc_ptr->proc_link;
        }

        if (proc_ptr == NULL)
        {
            printf("ERROR : Unable to find process in the group\n");
            exit(0);
        }
        else
        {
            /* Free memory allocated for command line arguments */
            for (idx = 0; process->argv[idx] != NULL; idx++)
                free(process->argv[idx]);

            prev_proc->proc_link = process->proc_link;
            /* Release process resource */
            free(process);
            /* Decrement no of process in group */
            grp_ptr->nprocess--;
            /* return next process in the group */
            return prev_proc->proc_link;
        }
    }
}

process_t *release_group_resource(group_t **session_leader)
{
    int idx;
    group_t *grp_ptr = *session_leader;
    group_t *prev_grp;

    while (grp_ptr != NULL)
    {
        if (grp_ptr->nprocess == 0)
        {
            if (grp_ptr == *session_leader)
            {
                *session_leader = grp_ptr->group_link;
                /* Release group resource */
                free(grp_ptr);
                grp_ptr = *session_leader;
                prev_grp = grp_ptr;
            }
            else
            {
                prev_grp->group_link = grp_ptr->group_link;
                /* Release group resource */
                free(grp_ptr);
                grp_ptr = prev_grp->group_link;
            }
        }
        else
        {
            /* Store previous group */
            prev_grp = grp_ptr;

            /* Move to next group */
            grp_ptr = grp_ptr->group_link;
        }
    }
}

/* Print info for each process with pid */
void print_resource_for_my_shell(group_t *session_leader)
{
    int idx, proc_count = 0;
    group_t *grp_ptr = session_leader;
    process_t * proc_ptr;

    if (session_leader != NULL)
    {
        printf("%-6s%-9s%-11s%-11s%-11s%-11s\n","PID", "PGID", "STATUS", "STAT", "SIGNAL", "COMMAND");
        printf("------------------------------------------------------------\n");
    }
    while (grp_ptr != NULL)
    {
        proc_count = 0;
        proc_ptr = grp_ptr->proc_link;
        while (proc_ptr != NULL)
        {
            proc_count++;

            printf("%-6d", proc_ptr->pid);
            printf("%-9d", proc_ptr->pgid);

            if(grp_ptr->status == FG)
            printf("%-11s", "Foreground");
            else
            printf("%-11s", "Background");

            printf("%-11s", proc_ptr->status);

            if (strcmp(proc_ptr->status, "stopped") == 0)
            printf("%-11s", proc_ptr->signal);
            else
            printf("%-11s", " ");

            if (proc_count == 1)
                printf("%-11s ", proc_ptr->argv[0]);
            else
                printf("| %-10s", proc_ptr->argv[0]);

            puts("");

            proc_ptr = proc_ptr->proc_link;
        }
        grp_ptr = grp_ptr->group_link;
    }
    return;
}

/* Print info for each each group with pgid */
void jobs(group_t **session_leader)
{
    int idx = 0;
    group_t *grp_ptr = *session_leader;
    process_t * proc_ptr;

    if (*session_leader != NULL)
    {
        printf("%-6s%-9s%-11s%-11s%-11s%-11s\n"," ", "PGID", "STATUS", "STAT", "SIGNAL", "COMMAND");
        printf("------------------------------------------------------------\n");
    }
    else
    {
        return;
    }
    while (grp_ptr != NULL)
    {
            idx++;
            if (idx == 1)
                printf("[%2d]+ ", idx);
            else if (idx == 2)
                printf("[%2d]- ", idx);
            else
                printf("[%2d]  ", idx);

            printf("%-9d", grp_ptr->pgid);

            if(grp_ptr->status == FG)
            printf("%-11s", "Foreground");
            else if (grp_ptr->status == BG)
            printf("%-11s", "Background");

            if (grp_ptr->proc_link != NULL)
            printf("%-11s",grp_ptr->proc_link->status);

            printf("%-11s", grp_ptr->proc_link->signal);

            proc_ptr = grp_ptr->proc_link;
            while (proc_ptr != NULL)
            {
                if(proc_ptr->proc_link != NULL)
                printf("%s | ",proc_ptr->argv[0]);
                else
                printf("%s",proc_ptr->argv[0]);
                proc_ptr = proc_ptr->proc_link;
            }
            puts("");
            grp_ptr = grp_ptr->group_link;
    }
    return;
}

/* Wait for foreground process group */
void wait_for_fg(group_t **session_leader, int *exit_status)
{
    int idx, status, wait_status;
    group_t *grp_ptr;
    process_t *proc_ptr;

        /* Code to wait for children to complete execution*/
        grp_ptr = *session_leader;
        while (grp_ptr != NULL)
        {
            /* Update status of Foreground/Background process */
            if (grp_ptr->status == FG)
            {
                idx = 0;
                proc_ptr = grp_ptr->proc_link;
                while (proc_ptr != NULL)
                {
                    idx++;

#ifdef DEBUG
                    printf("Waiting for %d %s to terminate\n", proc_ptr->pid, proc_ptr->argv[0]);
#endif
                    wait_status = waitpid(proc_ptr->pid, &status, WUNTRACED);
                    if (wait_status == -1) 
                    {
                        perror("waitpid on foreground process");
                        exit(EXIT_FAILURE);
                    }

#ifdef DEBUG
                    printf("Foreground process %d %s has changed state\n", proc_ptr->pid, proc_ptr->argv[0]);
#endif
                    if (WIFEXITED(status))
                    {
                        *exit_status = WEXITSTATUS(status);
#ifdef DEBUG
                        printf("exited, status=%d\n", WEXITSTATUS(status));
#endif
                        proc_ptr->status = stat[EXITED];

                        proc_ptr = release_process_resource(grp_ptr, proc_ptr);
                        continue;
                    }
                    else if (WIFSIGNALED(status))
                    {
                        *exit_status = WTERMSIG(status) + 128;
#ifdef DEBUG
                        printf("killed by signal %d\n", WTERMSIG(status));
#endif
                        proc_ptr->status = stat[SIGNALLED];
                        proc_ptr->status = stat[KILLED];

#ifdef DEBUG
                        if (WTERMSIG(status) == 11)
                            printf("Segmentation fault(core dumped)\n");
#endif

                        proc_ptr = release_process_resource(grp_ptr, proc_ptr);
                        continue;
                    }
                    else if (WIFSTOPPED(status))
                    {
                        *exit_status = WSTOPSIG(status) + 128;
#ifdef DEBUG
                        printf("stopped by signal %d\n", WSTOPSIG(status));
#endif
                        proc_ptr->status = stat[STOPPED];

                        switch (WSTOPSIG(status))
                        {
                            case 19:
                                proc_ptr->signal = sig[1];
                                break;
                            case 20:
                                proc_ptr->signal = sig[2];
                                break;
                            case 21:
                                proc_ptr->signal = sig[3];
                                break;
                            case 22:
                                break;
                                proc_ptr->signal = sig[4];
                        }

                    }
                    else if(WIFCONTINUED(status))
                    {
#ifdef DEBUG
                        printf("continued\n");
#endif
                        proc_ptr->status = stat[RUNNING];
                    }
                    /* Move to next process */
                    proc_ptr = proc_ptr->proc_link;
                } /* Bracket for foreground process */
                grp_ptr->status = BG;

            } /* bracket for process looping */

            /* Move to next group */
            grp_ptr = grp_ptr->group_link;

        } /* bracket for group looping */
        release_group_resource(session_leader);
        return;
}

/* Wait for background process group */
void update_status_of_bg(group_t **session_leader)
{
    int idx, status, wait_status;
    group_t *grp_ptr;
    process_t *proc_ptr;

    /* Code to wait for children to complete execution*/
    grp_ptr = *session_leader;
    while (grp_ptr != NULL)
    {
        /* Update status of Foreground/Background process */
        if (grp_ptr->status == BG)
        {
            idx = 0;
            proc_ptr = grp_ptr->proc_link;
            while (proc_ptr != NULL)
            {
                idx++;

#ifdef DEBUG
                printf("Waiting for %d %s to terminate\n", proc_ptr->pid, proc_ptr->argv[0]);
#endif
                if ((wait_status = waitpid(proc_ptr->pid, &status, WNOHANG|WUNTRACED|WCONTINUED)) == 0)
                {
                    proc_ptr = proc_ptr->proc_link;
                    continue;
                }

                if (wait_status == -1) 
                {
                    perror("waitpid on background process");
                    exit(EXIT_FAILURE);
                }
#ifdef DEBUG
                printf("Background process %d %s has changed state\n", proc_ptr->pid, proc_ptr->argv[0]);
#endif
                if (WIFEXITED(status))
                {
#ifdef DEBUG
                    printf("exited, status=%d\n", WEXITSTATUS(status));
#endif
                    proc_ptr->status = stat[EXITED];

                    proc_ptr = release_process_resource(grp_ptr, proc_ptr);
                    continue;
                }
                else if (WIFSIGNALED(status))
                {
                    proc_ptr->status = stat[SIGNALLED];
#ifdef DEBUG
                    printf("killed by signal %d\n", WTERMSIG(status));
#endif
                    proc_ptr->status = stat[KILLED];

                    if (WTERMSIG(status) == 11)
#ifdef DEBUG
                        printf("Segmentation fault(core dumped)\n");
#endif

                    proc_ptr = release_process_resource(grp_ptr, proc_ptr);
                    continue;
                }
                else if (WIFSTOPPED(status))
                {
#ifdef DEBUG
                    printf("stopped by signal %d\n", WSTOPSIG(status));
#endif
                    proc_ptr->status = stat[STOPPED];

                    switch (WSTOPSIG(status))
                    {
                        case 19:
                            proc_ptr->signal = sig[1];
                            break;
                        case 20:
                            proc_ptr->signal = sig[2];
                            break;
                        case 21:
                            proc_ptr->signal = sig[3];
                            break;
                        case 22:
                            break;
                            proc_ptr->signal = sig[4];
                    }
                } 
                else if(WIFCONTINUED(status))
                {
#ifdef DEBUG
                    printf("continued\n");
#endif
                    proc_ptr->status = stat[RUNNING];
                }
                /* Move to next process */
                proc_ptr = proc_ptr->proc_link;

            } /* bracket for process looping */
        }
        /* Move to next group */
        grp_ptr = grp_ptr->group_link;

    } /* bracket for group looping */
    release_group_resource(session_leader);
    return;
}

/* Put a background process to foreground */
void fg(int terminal, group_t **session_leader, pid_t shell_pid, int *exit_status)
{
    int status;
    process_t *proc_ptr;

    if (*session_leader != NULL)
    if ((*session_leader)->proc_link != NULL)
    if ((*session_leader)->status == BG)
    {
        printf("%s\n",(*session_leader)->proc_link->argv[0]);

        /* Give the control terminal to following group */
        if (tcsetpgrp(terminal, (*session_leader)->pgid) == -1)
        {
            perror("tcsetpgrp1");
            exit(0);
        }

        proc_ptr = (*session_leader)->proc_link;
        while (proc_ptr != NULL)
        {
            /* Send SIGCONT to each process to resume execution */
            kill(proc_ptr->pid, SIGCONT);
            proc_ptr = proc_ptr->proc_link;
        }
        (*session_leader)->status = FG;

        /* Wait for process group to change state */
        wait_for_fg(session_leader, exit_status);

        /* Retrive the controlling terminal back */
        if (tcsetpgrp(terminal, shell_pid) == -1)
        {
            perror("tcsetpgrp2");
            exit(0);
        }
    }
    return;
}
