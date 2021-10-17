/*
 * Hot potato test for kkv
 *
 * usage:
 *	./hot_potato [<nthreads>]
 *
 * Tests that the same value can be passed along a number of threads spinning
 * on the same key.
 *
 * Multiple player threads (play_hotpotato_thread()) spin on kkv_get() waiting
 * for the hot potato. Once they receive the potato from the fridge, they
 * kkv_put() it back into the fridge for the next lucky thread.
 *
 * The process is started by the init_hotpotato_thread thread.
 */

#include "con_ed.h"

#define usage(arg0) fprintf(stderr, "usage: %s [<nthreads>]\n", arg0)

#define KEY 0xbae
#define POTATO "hot potato"

pid_t gettid(void)
{
	return syscall(__NR_gettid);
}

void *init_hotpotato_thread(void *ignore)
{
	DEBUG("Thread [%u] init_hotpotato initialized\n", gettid());

	/* let loose the hot potato! */
	assert(kkv_put(KEY, POTATO, strlen(POTATO) + 1, 0) == 0);

	pthread_exit(NULL);
}

void *play_hotpotato_thread(void *ignore)
{
	char res[MAX_VAL_SIZE];

	DEBUG("Thread [%u] play_hotpotato initialized\n", gettid());

	/* spin wait for the hot potato */
	while (kkv_get(KEY, res, MAX_VAL_SIZE, KKV_NONBLOCK) != 0)
		DEBUG("[%u] Cold potato!\n", gettid());

	DEBUG("[%u] Hot potato!\n", gettid());

	/* check that we still have the same potato */
	assert(strcmp(res, POTATO) == 0);

	/* pass on the potato */
	assert(kkv_put(KEY, res, strlen(res) + 1, 0) == 0);

	pthread_exit(NULL);
}

void hot_potato(int nthreads)
{
	pthread_t threads[nthreads];
	int i, ret;

	kkv_init(0);

	ret = pthread_create((pthread_t *) threads, NULL,
			init_hotpotato_thread, NULL);
	if (ret)
		die("pthread_create() failed");

	for (i = 1; i < nthreads; i++) {
		ret = pthread_create((threads + i), NULL,
				play_hotpotato_thread, NULL);
		if (ret)
			die("pthread_create() failed");
	}

	for (i = 0; i < nthreads; i++)
		pthread_join(threads[i], NULL);

	kkv_destroy(0);
}

int main(int argc, char **argv)
{
	int nthreads = argc > 1 ? atoi(argv[1]) : 64;

	if (!nthreads) {
		usage(argv[0]);
		return 2;
	}

	RUN_TEST(hot_potato, nthreads);
	return 0;
}
