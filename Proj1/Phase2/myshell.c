/* $begin shellmain */
#include "myshell.h"
#include <errno.h>
#define MAXARGS   128

/* Function prototypes */
void eval(char *cmdline);
void pipe_parse(char *cmdline);
void eval_pipe(char cmd[][MAXLINE], char *cmdline, int pipe_num, int index); /* Evaluate when the cmdline has a pipe */
int parseline(char *buf, char **argv);
int builtin_command(char **argv);

void sigint_handler(int sig);

int main() 
{
	char cmdline[MAXLINE]; /* Command line */
	int original_stdin;

	while (1) {
		Signal(SIGTSTP, SIG_IGN);
		Signal(SIGINT, sigint_handler);

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
	}
}
/* $end shellmain */

/* $begin eval */
/* eval - Evaluate a command line */
void eval(char *cmdline) 
{
	char *argv[MAXARGS]; /* Argument list execve() */
	char buf[MAXLINE];   /* Holds modified command line */
	int bg;              /* Should the job run in bg or fg? */
	pid_t pid;           /* Process id */
	char path1[MAXLINE];
	char path2[MAXLINE];

	strcpy(path1, "/bin/");
	strcpy(path2, "/usr/bin/");

	strcpy(buf, cmdline);
	bg = parseline(buf, argv); 

	if (argv[0] == NULL)  
		return;   /* Ignore empty lines */
	if (!builtin_command(argv)) { //quit -> exit(0), & -> ignore, other -> run
		if((pid = Fork()) == 0) {
			if ((execve(strcat(path1, argv[0]), argv, environ) < 0) && (execve(strcat(path2, argv[0]), argv, environ) < 0)) {	//ex) /bin/ls ls -al &
				printf("%s: Command not found.\n", argv[0]);
				exit(0);
			}
		}

		/* Parent waits for foreground job to terminate */
		if (!bg){ 
			int status;
			if(waitpid(pid, &status, 0) < 0)
				unix_error("waitfg: watipid error");
		}
		else //when there is backgrount process!
			printf("%d %s", pid, cmdline);
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

	eval_pipe(cmd, cmdline, pipe_num, 0);
}
/* $end pipe_parse */

/* $begin eval_pipe */
/* eval_pipe - Evaluate a command line when the command line has a pipe */
void eval_pipe(char cmd[][MAXLINE], char *cmdline, int pipe_num, int index)
{
	char *argv[MAXARGS]; /* Argument list execve() */
	char buf[MAXLINE];   /* Holds modified command line */
	int bg;              /* Should the job run in bg or fg? */
	pid_t pid;           /* Process id */
	char path1[MAXLINE];
	char path2[MAXLINE];
	int fd[MAXARGS][2];

	pipe(fd[index]);
	strcpy(path1, "/bin/");
	strcpy(path2, "/usr/bin/");

	strcpy(buf, cmd[index]);
	bg = parseline(buf, argv); 

	if (argv[0] == NULL)  
		return;   /* Ignore empty lines */

	if (!builtin_command(argv)) { //quit -> exit(0), & -> ignore, other -> run
		/* Child */
		if((pid = Fork()) == 0) {
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
				if(waitpid(pid, &status, 0) < 0)
					unix_error("waitfg: waitpid error");

				if(index != pipe_num) {
					Close(fd[index][1]);
					Dup2(fd[index][0], STDIN_FILENO);
					Close(fd[index][0]);

					index++;
					eval_pipe(cmd, cmdline, pipe_num, index);
				}
				else {
					return;
				}
			}
			else //when there is backgrount process!
				printf("%d %s", pid, cmdline);
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
	return 0;                     /* Not a builtin command */
}

/* $begin parseline */
/* parseline - Parse the command line and build the argv array */
int parseline(char *buf, char **argv) 
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

	if (argc == 0)  /* Ignore blank line */
		return 1;

	/* Should the job run in the background? */
	if ((bg = (*argv[argc-1] == '&')) != 0)
		argv[--argc] = NULL;

	return bg;
}
/* $end parseline */

void sigint_handler(int sig)
{
	printf("\n");
}
