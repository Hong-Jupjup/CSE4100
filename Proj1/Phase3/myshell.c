/* $begin shellmain */
#include "myshell.h"
#include <errno.h>
#define MAXARGS   128

/* State values */
#define DEFAULT		0
#define RUNNING		1 /* Run command in bg */
#define DONE		2 /* The command done */
#define STOP		3 /* The command stopped */
#define KILL		4 /* The command killed */
#define FOREGROUND	5 /* The command runs in fg */

typedef struct {
	pid_t pid;
	int id;
	int state;
	char cmdline[MAXLINE];
} job;

job jobs[MAXARGS];
int job_num = 0;

/* Function prototypes */
void eval(char *cmdline);
void pipe_parse(char *cmdline);
void eval_pipe(char cmd[][MAXLINE], char *cmdline, int pipe_num, int index, int bg); /* Evaluate when the cmdline has a pipe */
void parseline(char *buf, char **argv);
int builtin_command(char **argv);
int is_background(char cmdline[]); /* Determine that the cmdline is bg or fg */

void sigchld_handler(int sig);
void sigtstp_handler(int sig);
void sigint_handler(int sig);

void job_init(job *jobs);
void job_reinit(job *job);

void print_done(job *jobs);

int main() 
{
	char cmdline[MAXLINE]; /* Command line */
	int original_stdin;

	job_init(jobs);

	while (1) {
		Signal(SIGCHLD, sigchld_handler);
		Signal(SIGTSTP, sigtstp_handler);
		Signal(SIGINT, SIG_IGN);

		original_stdin = dup(STDIN_FILENO);
		/* Read */
		printf("CSE4100-SP-P#1> ");

		fgets(cmdline, MAXLINE, stdin);

		if (feof(stdin)) {
			exit(0);
		}

		/* Evaluate */
		if(strstr(cmdline, "|")) {
			pipe_parse(cmdline);
		}
		else {
			eval(cmdline);
		}
		Dup2(original_stdin, STDIN_FILENO);
		Close(original_stdin);
		cmdline[0] = '\0';
		print_done(jobs);
	}
}
/* $end shellmain */

/* $begin eval */
/* eval - Evaluate a command line */
void eval(char *cmdline) 
{
	char *argv[MAXARGS]; /* Argument list execve() */
	char buf[MAXLINE];   /* Holds modified command line */
	int bg = 0;			 /* Should the job run in bg or fg? */
	pid_t pid;           /* Process id */
	char path1[MAXLINE];
	char path2[MAXLINE];

	strcpy(path1, "/bin/");
	strcpy(path2, "/usr/bin/");

	bg = is_background(cmdline);

	strcpy(buf, cmdline);
	parseline(buf, argv);

	if (argv[0] == NULL)  
		return;   /* Ignore empty lines */
	if (!builtin_command(argv)) { //quit -> exit(0), & -> ignore, other -> run
		if((pid = Fork()) == 0) {
			if(!bg) {
				Signal(SIGINT, sigint_handler);
				Signal(SIGTSTP, sigtstp_handler);
			}
			else {
				Signal(SIGINT, SIG_IGN);
				Signal(SIGTSTP, SIG_IGN);
			}

			if ((execve(strcat(path1, argv[0]), argv, environ) < 0) && (execve(strcat(path2, argv[0]), argv, environ) < 0)) {	//ex) /bin/ls ls -al &
				printf("%s: Command not found.\n", argv[0]);
				exit(0);
			}
		}

		/* Parent waits for foreground job to terminate */
		if (!bg){
			int status;
			jobs[job_num].id = job_num + 1;
			jobs[job_num].state = FOREGROUND;
			jobs[job_num].pid = pid;
			strcpy(jobs[job_num].cmdline, cmdline);

			job_num++;

			if(waitpid(pid, &status, WUNTRACED) < 0)
				unix_error("waitfg: watipid error");
			else {
				for(int i=0; i<MAXARGS; i++) {
					if(jobs[i].state == FOREGROUND) {
						job_reinit(&jobs[i]);
					}
				}
			}
		}
		else { //when there is background process!
			jobs[job_num].id = job_num + 1;
			jobs[job_num].state = RUNNING;
			jobs[job_num].pid = pid;
			strcpy(jobs[job_num].cmdline, cmdline);

			printf("[%d] %d\t\t%s", jobs[job_num].id, jobs[job_num].pid, jobs[job_num].cmdline);
			job_num++;
		}
	}
	return;
}
/* $end eval */

/* $begin pipe_parse */
/* pipe_parse - Parse the command line based on the pipes */
void pipe_parse(char *cmdline)
{
	char cmd_tmp[MAXLINE];
	char cmd[MAXARGS][MAXLINE] ={};
	int pipe_num = 0;
	int i;
	int bg = 0;

	bg = is_background(cmdline);

	/* Count the number of the pipes */
	for(i=0; i < strlen(cmdline); i++) {
		if(cmdline[i] == '|') {
			pipe_num++;
		}
	}
	strcpy(cmd_tmp, cmdline);

	for(int i=0; i < pipe_num; i++) {
		strncpy(cmd[i], cmd_tmp, strchr(cmd_tmp, '|') - cmd_tmp);
		strcpy(cmd_tmp, strchr(cmd_tmp, '|') + 1);
		strcat(cmd[i], " ");
	}
	strcpy(cmd[pipe_num], cmd_tmp);

	eval_pipe(cmd, cmdline, pipe_num, 0, bg);
}
/* $end pipe_parse */

/* $begin eval_pipe */
/* eval_pipe - Evaluate a command line when the command line has a pipe */
void eval_pipe(char cmd[][MAXLINE], char *cmdline, int pipe_num, int index, int bg)
{
	char *argv[MAXARGS]; /* Argument list execve() */
	char buf[MAXLINE];   /* Holds modified command line */
	pid_t pid;           /* Process id */
	char path1[MAXLINE];
	char path2[MAXLINE];
	int fd[MAXARGS][2];

	pipe(fd[index]);
	strcpy(path1, "/bin/");
	strcpy(path2, "/usr/bin/");

	strcpy(buf, cmd[index]);
	parseline(buf, argv);

	if (argv[0] == NULL)  
		return;   /* Ignore empty lines */

	if (!builtin_command(argv)) { //quit -> exit(0), & -> ignore, other -> run
		/* Child */
		if((pid = Fork()) == 0) {
			if(!bg) {
				Signal(SIGINT, sigint_handler);
				Signal(SIGTSTP, sigtstp_handler);
			}
			else {
				Signal(SIGINT, SIG_IGN);
				Signal(SIGTSTP, SIG_IGN);
			}

			if(index != pipe_num) {
				Close(fd[index][0]);
				Dup2(fd[index][1], STDOUT_FILENO);
				Close(fd[index][1]);
			}

			if((execve(strcat(path1, argv[0]), argv, environ) < 0) && (execve(strcat(path2, argv[0]), argv, environ) < 0)) {	//ex) /bin/ls ls -al &
				printf("%s: Command not found.\n", argv[0]);
				exit(0);
			}
		}

		/* Parent */
		else {
			/* Parent waits for foreground job to terminate */
			if(!bg) {
				int status;
			
				if(index != pipe_num) {
					Close(fd[index][1]);
					Dup2(fd[index][0], STDIN_FILENO);
					Close(fd[index][0]);

					index++;
					eval_pipe(cmd, cmdline, pipe_num, index, bg);
				}
				else {
					return;
				}

				jobs[job_num].id = job_num + 1;
				jobs[job_num].state = FOREGROUND;
				jobs[job_num].pid = pid;
				strcpy(jobs[job_num].cmdline, cmdline);
				job_num++;

				if(waitpid(pid, &status, WUNTRACED) < 0)
					unix_error("waitfg: waitpid error");

				else {
					for(int i=0; i<MAXARGS; i++) {
						if(jobs[i].state == FOREGROUND) {
							job_reinit(&jobs[i]);
						}
					}
				}
			}
			else { //when there is backgrount process!
				if(index != pipe_num) {
					Close(fd[index][1]);
					Dup2(fd[index][0], STDIN_FILENO);
					Close(fd[index][0]);

					index++;
					eval_pipe(cmd, cmdline, pipe_num, index, bg);
				}
				else {
					return;
				}

				if(jobs[job_num].state == DEFAULT) {
					jobs[job_num].id = job_num + 1;
					jobs[job_num].state = RUNNING;
					jobs[job_num].pid = pid;
					strcpy(jobs[job_num].cmdline, cmdline);

					printf("[%d] %d\t\t%s", jobs[job_num].id, jobs[job_num].pid, jobs[job_num].cmdline);
					job_num++;
				}
			} 
		} 
	}
	return;
}
/* $end eval_pipe */

/* If first arg is a builtin command, run it and return true */
int builtin_command(char **argv) 
{
	if (!strcmp(argv[0], "quit") || !strcmp(argv[0], "exit")) /* quit command */
		exit(0);  

	if (!strcmp(argv[0], "&"))    /* Ignore singleton & */
		return 1;

	if(!strcmp(argv[0], "cd")) { /* change directory */
		if(chdir(argv[1]) == -1) {
			printf("No such file or directory\n");
		}
		return 1;
	}

	if(!strcmp(argv[0], "jobs")) { /* List the running and stopped background jobs */
		for(int i=0; i < MAXARGS; i++) {
			if(jobs[i].state != DEFAULT) {
				switch(jobs[i].state) {
					case RUNNING:
						printf("[%d] Running\t\t%s", jobs[i].id, jobs[i].cmdline);
						break;
					case STOP:
						printf("[%d] Suspended\t\t%s", jobs[i].id, jobs[i].cmdline);
						break;
				}
			}
		}
		return 1;
	}

	if(!strcmp(argv[0], "kill")) {
		if(argv[1][0] != '%') {
			printf("Command Error! Input '%%' to use kill.\n");
			return 1;
		}

		int id;
		argv[1][0] = ' ';
		id = atoi(argv[1]);
			
		if((jobs[id-1].state == RUNNING) || (jobs[id-1].state == STOP)) {
			jobs[id-1].state = KILL;
			Kill(jobs[id-1].pid, SIGKILL);
		}
		else if(jobs[id-1].state == DEFAULT) {
			printf("No Such Job\n");
		}
		return 1;
	}

	if(!strcmp(argv[0], "bg")) {
		if(argv[1][0] != '%') {
			printf("Command Error! Input '%%' to use bg.\n");
			return 1;
		}

		int id;
		argv[1][0] = ' ';
		id = atoi(argv[1]);

		if(jobs[id-1].state == STOP) {
			jobs[id-1].state = RUNNING;
			Kill(jobs[id-1].pid, SIGCONT);
			printf("[%d] Running\t\t%s", jobs[id-1].id, jobs[id-1].cmdline);
		}
		else if(jobs[id-1].state == DEFAULT) {
			printf("No Such Job\n");
		}
		return 1;
	}

	if(!strcmp(argv[0], "fg")) {
		if(argv[1][0] != '%') {
			printf("Command Error! Input '%%' to use bg.\n");
			return 1;
		}

		int id;
		argv[1][0] = ' ';
		id = atoi(argv[1]);

		if((jobs[id-1].state == STOP) || (jobs[id-1].state == RUNNING)) {
			jobs[id-1].state = FOREGROUND;
			Kill(jobs[id-1].pid, SIGCONT);
			printf("[%d] Running\t\t%s", jobs[id-1].id, jobs[id-1].cmdline);
			while(1) {
				if(jobs[id-1].state != FOREGROUND)
					break;
			}
		}
		return 1;
	}

	return 0;                     /* Not a builtin command */
}

/* $begin parseline */
/* parseline - Parse the command line and build the argv array */
void parseline(char *buf, char **argv) 
{
	char *delim;         /* Points to first space delimiter */
	char *quote;		 /* Points to a single or double quote */
	int argc;            /* Number of args */
	int bg;              /* Background job? */

	buf[strlen(buf)-1] = ' ';  /* Replace trailing '\n' with space */
	while (*buf && (*buf == ' ')) /* Ignore leading spaces */
		buf++;

	/* Build the argv list */
	argc = 0;
	while ((delim = strchr(buf, ' '))) {
		if(*buf == '\"') {
			buf++;
			quote = strchr(buf, '\"');
			delim = quote;
		}

		else if(*buf == '\'') {
			buf++;
			quote = strchr(buf, '\'');
			delim = quote;
		}

		argv[argc++] = buf;
		*delim = '\0';
		buf = delim + 1;
		while (*buf && (*buf == ' ')) { /* Ignore spaces */
			buf++;
		}
	}
	argv[argc] = NULL;
}
/* $end parseline */

int is_background(char cmdline[])
{
	/* Should the job run in the background? */
	int i;	

	for(i=strlen(cmdline)-1; i >= 0; i--) {
		if(cmdline[i] == ' ' || cmdline[i] == '\n') {
			;
		}
		else if(cmdline[i] == '&') {
			cmdline[i] = ' ';
			return 1;
		}
		else {
			return 0;
		}
	}
}

void sigchld_handler(int sig)
{
	pid_t pid;
	int status;

	while((pid = waitpid(-1, &status, WNOHANG | WCONTINUED)) > 0)
		if(WIFEXITED(status) || WIFSIGNALED(status))
			if(pid >= 1)
				for(int i=0; i < MAXARGS; i++)
					if((jobs[i].pid == pid) && (jobs[i].state != DEFAULT)) {
						jobs[i].state = DONE;
						break;
					}
	return;
}

void sigtstp_handler(int sig)
{
	for(int i=0; i <= MAXARGS; i++) {
		if(jobs[i].state == FOREGROUND) {
			jobs[i].state = STOP;
			kill(-jobs[i].pid, SIGSTOP);
			printf("[%d] Suspended\t\t%s", jobs[i].id, jobs[i].cmdline);
			job_num++;
			return;
		}
	}
}

void sigint_handler(int sig)
{
	printf("\n");
}

void job_init(job *jobs)
{
	for(int i=0; i<MAXARGS; i++) {
		job_reinit(&jobs[i]);
	}
}

void job_reinit(job *job)
{
	job->pid = 0;
	job->id = 0;
	job->state = DEFAULT;
	job->cmdline[0] = '\0';
}

void print_done(job *jobs)
{
	int i, j;
	
	for(i=0; i < job_num; i++) {
		if(jobs[i].state == KILL) {
			jobs[i].state = DEFAULT;
		}
	}

	for(i=0; i< job_num; i++) {
		if(jobs[i].state == DONE) {
			jobs[i].state = DEFAULT;
			printf("[%d] Done\t\t%s", jobs[i].id, jobs[i].cmdline);
		}
	}
	


	for(i=job_num-1; i>=0; i--) {
		if((jobs[i].state == RUNNING) || (jobs[i].state == STOP)) {
			job_num = i + 1;
			break;
		}
	}

	for(j=0; j<MAXARGS; j++) {
		if((jobs[j].state == DEFAULT) || (jobs[j].state == KILL)) {
			;
		}
		else {
			break;
		}
	}

	if(j == MAXARGS) {
		job_num = 0;
	}
}
