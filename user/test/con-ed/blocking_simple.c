/*
 * simple blocking test for kkv
 *
 * usage:
 *	./blocking_simple
 *
 * Simple blocking test case for fridge syscalls that blocks on an absent entry.
 */

#include "con_ed.h"

void blocking_simple(void)
{
	pid_t pid;
	char res[MAX_VAL_SIZE];
	const int key = 0xdead;
	char *val = random_string(16);

	kkv_init(0);

	pid = fork();
	if (pid < 0)
		die("fork() failed\n");

	if (pid == 0) {
		assert(kkv_get(key, res, MAX_VAL_SIZE, KKV_BLOCK) == 0);
		assert(strcmp(val, res) == 0);
	} else {
		random_sleep(2);
		assert(kkv_put(key, val, strlen(val) + 1, 0) == 0);
		assert(waitpid(pid, NULL, 0) >= 0); /* reap child */
	}

	free_string(val);
	kkv_destroy(0);

	if (pid == 0)
		exit(0);
}

int main(void)
{
	RUN_TEST(blocking_simple);
	return 0;
}
