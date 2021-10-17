/*
 * fridge_test.h
 *
 * Test driver for fridge kkv.
 */
#ifndef CON_ED_H
#define CON_ED_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <fridge.h>

/*
 * for older version of libfridge that don't define these
 *
 * #ifndef KKV_NONBLOCK
 * #define KKV_NONBLOCK 0
 * #endif

 * #ifndef KKV_BLOCK
 * #define KKV_BLOCK 1
 * #endif
 */

#ifdef VERBOSE
#define DEBUG(...) fprintf(stderr, "\t" __VA_ARGS__)
#else
#define DEBUG(...)
#endif

#ifndef MAX_VAL_SIZE
#define MAX_VAL_SIZE 200
#endif

char *random_string(size_t max_len);
void free_string(char *rstring);
unsigned int *random_buf(size_t max_len);
void free_buf(unsigned int *rbuf);

void random_sleep(useconds_t max_time);

#define CON_ED_SIGNAL SIGUSR1

int install_signal_handler(void);
int raise_signal(pid_t pid);

#define die(msg) \
	do { \
		perror(msg); \
		exit(1); \
	} while (0)

#define RUN_TEST(test, ...) \
	do { \
		srandom(time(NULL)); \
		fprintf(stderr, "[ TEST ] " #test " ...\n"); \
		test(__VA_ARGS__); \
		fprintf(stderr, "... PASS\n"); \
	} while (0)

#endif /* CON_ED_H */
