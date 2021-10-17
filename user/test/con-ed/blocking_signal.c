/*
 * blocking signal test for kkv
 *
 * usage:
 *	./blocking_signal
 *
 * Simple blocking test which blocks on a kkv_get() call, only to be interrupted
 * by a signal.
 */

#include "con_ed.h"

void blocking_signal(void)
{
	pid_t pid;

	char res[MAX_VAL_SIZE];
	const int key = 0xbeef;
	char *val = random_string(16);

	kkv_init(0);

	pid = fork();
	if (pid < 0)
		die("fork() failed\n");

	if (pid == 0) {
		assert(kkv_get(key, res, MAX_VAL_SIZE, KKV_BLOCK) == -1 &&
				errno == EINTR);
	} else {
		random_sleep(2);
		/*
		 * if random_sleep() barely sleeps, the signal may get lost.
		 * so we snooze a little more to give the child a chance to
		 * block on kkv_get().
		 */
		usleep(100);
		assert(raise_signal(pid) == 0);
		assert(waitpid(pid, NULL, 0) >= 0); /* reap child */
	}

	free_string(val);
	kkv_destroy(0);

	if (pid == 0)
		exit(0);
}

int main(void)
{
	install_signal_handler();
	RUN_TEST(blocking_signal);
	return 0;
}
