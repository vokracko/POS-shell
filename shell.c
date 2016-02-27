#define _POSIX_SOURCE
#define _GNU_SOURCE
#include <stdlib.h>
#include <sys/wait.h>
#include <ctype.h>
#include <unistd.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <signal.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pthread.h>

typedef struct {
	char * input_file;
	char * output_file;
	bool background;
	char ** argv;
	int argc;
} program_t;

typedef struct {
	bool run;
	pthread_mutex_t mutex;
	pthread_mutex_t process_lock;
	pid_t pid;
	pthread_cond_t buff_cond;
	char buff[513];
} thread_data_t;

thread_data_t thread_data;

void sighandler_int(int signum)
{
	pthread_mutex_lock(&(thread_data.process_lock));
	if(thread_data.pid > 0)
		kill(thread_data.pid, SIGINT);
	pthread_mutex_unlock(&(thread_data.process_lock));
}

void sighandler_chld(int signum)
{
	int return_code = 0;
	pid_t pid = wait(&return_code);

	if(pid < 0) //alearedy handled (ie not bg procees, wait was direct)
		return;

	if(WIFEXITED(return_code))
		fprintf(stderr, "[%d] finished with return code: %d\n", pid, WEXITSTATUS(return_code));
	else if(WIFSIGNALED(return_code))
		fprintf(stderr, "[%d] terminated with signal: %d\n", pid, WTERMSIG(return_code));
	else if(WIFSTOPPED(return_code))
		fprintf(stderr, "[%d] stopped with signal: %d\n", pid, WSTOPSIG(return_code));
	else
		fprintf(stderr, "[%d] is no longer running\n", pid);

}

/**
 * \brief Set \0 in place of first [[space]] character
 * \return Position where \0 was set
*/
char * set_null(char * str)
{
	while(*str != '\0'&& !isspace(*str))
		str++;

	*str = '\0';
	return str;
}

/**
 * \brief Find redirect symbol and return file name 
 * \param str
 * \param c symbol
 * \param[out] start - first symbol
 * \return Start position of filename
*/
char * parse_redirect(char * str, char c, char ** start)
{
	char * symbol = *start = strchrnul(str, c);

	if(*symbol == '\0')
		 return symbol;

	symbol++;

	while(*symbol != '\0' && isspace(*symbol))
		symbol++;
	
	return symbol;
}

void * thread_read(void * thread_data)
{
	printf("$");
	int res;
	thread_data_t * data = (thread_data_t *) thread_data;
	bool wait_cond = true;

	while(data->run)
	{
		if(wait_cond)
		{
			pthread_mutex_lock(&(data->mutex));
			pthread_cond_wait(&(data->buff_cond), &(data->mutex));
			pthread_mutex_unlock(&(data->mutex));
		}

		wait_cond = true;

		printf("$");
		fflush(stdout);
		memset(data->buff, 0, 513);

		res = read(0, data->buff, 513);
		data->buff[512] = '\0';
		
		if(res == 513)
		{
			fprintf(stderr, "Input too long\n");
			while(getchar() != '\n');
			continue;
		}

		if(strcmp(data->buff, "exit\n") == 0)
			data->run = false;
		else if(data->buff[0] == '\n')
		{
			wait_cond = false;
			continue;
		}


		pthread_mutex_lock(&(data->mutex));
		pthread_cond_signal(&(data->buff_cond));
		pthread_mutex_unlock(&(data->mutex));
	}

	return NULL;
}

void * thread_run(void * thread_data)
{
	program_t p;
	thread_data_t * data = (thread_data_t *) thread_data;
	int argv_size = 10;
	int i_file, o_file;
	char * input_start, * output_start;

	p.argv = malloc(sizeof(char*) * argv_size); // +1 NULL delimiter

	while(data->run)
	{
		p.argc = 0;
		p.input_file = NULL;
		p.output_file = NULL;
		p.argv[0] = NULL;
		p.background = 0;

		// signal that buff is empty
		pthread_mutex_lock(&(data->mutex));
		pthread_cond_signal(&(data->buff_cond));
		pthread_mutex_unlock(&(data->mutex));

		// wait till buff is full
		pthread_mutex_lock(&(data->mutex));
		pthread_cond_wait(&(data->buff_cond), &(data->mutex));
		pthread_mutex_unlock(&(data->mutex));

		if(!data->run)
			break;

		// handle special chars
		char * background = strchrnul(data->buff, '&');
		p.background = *background != '\0';
		p.input_file = parse_redirect(data->buff, '<', &input_start);
		p.output_file = parse_redirect(data->buff, '>', &output_start);
		
		set_null(p.input_file);
		set_null(p.output_file);

		// find lowest position of first non argument
		char * argumentsEnd = background < input_start
								? (background < output_start ? background : output_start)
								: (input_start < output_start ? input_start : output_start) - 1; // newline

		set_null(background);
		
		// parse args
		char * null_pos = data->buff;
		p.argv[0] = data->buff;
		// set \0 after every argument
		while(null_pos < argumentsEnd)
		{
			if(argv_size <= p.argc + 1)
			{
				void * tmp = realloc(p.argv, argv_size * 2);

				if(tmp == NULL)
				{
					perror("realloc");
					free(p.argv);
					data->run = false;
					return NULL;
				}

				p.argv = tmp;
				argv_size *= 2;
			}
			// next argument stars one char after last \0
			p.argv[++p.argc] = null_pos = set_null(null_pos) + 1;
			// skip all whitespaces between arguments
			while(null_pos < argumentsEnd && isspace(*null_pos))
				null_pos++;

		}

		p.argv[p.argc] = NULL;

		if(p.argc < 1)
		{
			fprintf(stderr, "Invalid input\n");
			continue;
		}
		
		// fork part
		int fork_res = fork();

		if(fork_res > 0) // parent
		{
			if(!p.background)
			{
				pthread_mutex_lock(&(data->mutex));
				data->pid = fork_res; // save current pid, CTRL+c kill
				pthread_mutex_unlock(&(data->mutex));
				waitpid(fork_res, NULL, 0);
			}
		}
		else if(fork_res == 0) // child
		{
			/* for(int i = 0; i <= p.argc; ++i) */
			/* 	printf("argv[%d] = %s\n", i, p.argv[i]); */

			if(*p.input_file != '\0')
			{
				i_file = open(p.input_file, O_RDONLY);

				if(i_file < 0)
				{
					perror("open output");
					exit(1);
				}

				if(dup2(i_file, 0) < 0)
				{
					perror("dup input");
					exit(1);
				}
			}

			if(*p.output_file != '\0')
			{
				o_file = open(p.output_file, O_WRONLY | O_CREAT | O_TRUNC, 0666);

				if(o_file < 0)
				{
					perror("open input");
					exit(1);

				if(dup2(o_file, 1) < 0)
				{
					perror("dup output");
					exit(1);
				}
			}

			execvp(p.argv[0], p.argv); // returns only on fail
			perror("execvp");
			
			if(*p.input_file != '\0')
				close(i_file);

			if(*p.output_file != '\0')
				close(o_file);

			exit(1);
		}
		else 
		{
			perror("fork");
			data->run = false;
			break;
		}
	}

	free(p.argv);

	return NULL;
}

int main(void)
{
	pthread_t threads[2];
	struct sigaction sa_int, sa_chld;
	thread_data.pid = 0;
	thread_data.run = true;

	sa_int.sa_handler = sighandler_int;
	sa_int.sa_flags = 0;
	sa_chld.sa_handler = sighandler_chld;
	sa_chld.sa_flags = 0;
	sigemptyset(&sa_int.sa_mask);
	sigemptyset(&sa_chld.sa_mask);
	/* sigaction(SIGINT, &sa_int, NULL); */ //TODO odkomentovat
	sigaction(SIGCHLD, &sa_chld, NULL);

	pthread_mutex_init(&(thread_data.mutex), NULL);
	pthread_cond_init(&(thread_data.buff_cond), NULL);

	if(pthread_create(&(threads[0]), NULL, thread_read, (void *) &thread_data))
		perror("pthread_create");
	pthread_create(&(threads[1]), NULL, thread_run, (void *) &thread_data);

	pthread_join(threads[0], NULL);
	pthread_join(threads[1], NULL);

	pthread_mutex_destroy(&(thread_data.mutex));
	pthread_cond_destroy(&(thread_data.buff_cond));

	return 0;
}
