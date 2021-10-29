#include "con_ed.h"

char *random_string(size_t max_len)
{
	int i;
	size_t len = random() % max_len;

	char *rstring = (char *) malloc((len + 1) * sizeof(char));

	/*
	 * Generate a random character from the range of
	 * visible ASCII characters which goes from '!' to '~'
	 */
	for (i = 0; i < len; i++)
		rstring[i] = '!' + (random() % ('~' - '!'));

	rstring[i] = '\0';
	return rstring;
}

void free_string(char *rstring)
{
	free(rstring);
}

unsigned int *random_buf(size_t max_len)
{
	int i;
	unsigned int r;
	size_t len = (random() % max_len) + 1;
	size_t size = len * sizeof(unsigned int);

	unsigned int *rbuf = (unsigned int *) malloc(size);

	rbuf[0] = size;

	for (i = 1; i < len; i++) {
		r = random();
		rbuf[i] = r ? r : 1;
	}

	return rbuf;
}

void free_buf(unsigned int *rbuf)
{
	free(rbuf);
}

void random_sleep(useconds_t max_time)
{
	usleep(random() % max_time);
}

static void dud_signal_handler(int signum)
{
}

int install_signal_handler(void)
{
	struct sigaction sa;

	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;
	sa.sa_handler = dud_signal_handler;
	return sigaction(CON_ED_SIGNAL, &sa, NULL);
}

int raise_signal(pid_t pid)
{
	return kill(pid, CON_ED_SIGNAL);
}
