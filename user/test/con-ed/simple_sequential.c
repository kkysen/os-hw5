/*
 * simple sequential test for kkv
 *
 * usage:
 *	./simple_sequential
 *
 * Simple sequential test case for fridge syscalls that inserts and retreives
 * various values with the same key.
 */

#include "con_ed.h"

void simple_sequential(void)
{
	int key = 0xbeef;
	char *orange = "orange";
	char *apple = "apple";
	char *banana = "banana";
	char res[MAX_VAL_SIZE];

	memset(res, 0xff, sizeof(res));

	assert(kkv_init(0) == 0);

	/* Sequential put/get with valid key */
	assert(kkv_put(key, orange, strlen(orange) + 1, 0) == 0);
	assert(kkv_get(key, res, MAX_VAL_SIZE, KKV_NONBLOCK) == 0);
	assert(strcmp(orange, res) == 0);

	/* Sequential put/put on same key, different values */
	assert(kkv_put(key, orange, strlen(orange) + 1, 0) == 0);
	assert(kkv_put(key, apple, strlen(apple) + 1, 0) == 0);
	assert(kkv_get(key, res, MAX_VAL_SIZE, KKV_NONBLOCK) == 0);
	assert(strcmp(res, apple) == 0);

	/* Sequential get on nonexistent value */
	assert(kkv_get(0, res, MAX_VAL_SIZE, KKV_NONBLOCK) != 0);
	assert(errno == ENOENT);
	errno = 0;

	/* Sequential put/get/get with same key */
	assert(kkv_put(key, orange, strlen(orange) + 1, 0) == 0);
	assert(kkv_get(key, res, MAX_VAL_SIZE, KKV_NONBLOCK) == 0);
	assert(strcmp(orange, res) == 0);
	assert(kkv_get(key, res, MAX_VAL_SIZE, KKV_NONBLOCK) != 0);
	assert(errno == ENOENT);
	errno = 0;

	/* Make sure collisions can coexist */
	assert(kkv_put(1, orange, strlen(orange) + 1, 0) == 0);
	assert(kkv_put(18, apple, strlen(apple) + 1, 0) == 0);
	assert(kkv_get(1, res, MAX_VAL_SIZE, KKV_NONBLOCK) == 0);
	assert(strcmp(res, orange) == 0);
	assert(kkv_get(18, res, MAX_VAL_SIZE, KKV_NONBLOCK) == 0);
	assert(strcmp(res, apple) == 0);

	/* Make sure you can't put/get/get with collisions */
	assert(kkv_put(1, orange, strlen(orange) + 1, 0) == 0);
	assert(kkv_put(18, apple, strlen(apple) + 1, 0) == 0);
	assert(kkv_get(1, res, MAX_VAL_SIZE, KKV_NONBLOCK) == 0);
	assert(strcmp(orange, res) == 0);
	assert(kkv_get(1, res, MAX_VAL_SIZE, KKV_NONBLOCK) != 0);
	assert(errno == ENOENT);
	errno = 0;
	assert(kkv_get(18, res, MAX_VAL_SIZE, KKV_NONBLOCK) == 0);
	assert(strcmp(apple, res) == 0);
	assert(kkv_get(18, res, MAX_VAL_SIZE, KKV_NONBLOCK) != 0);
	assert(errno == ENOENT);
	errno = 0;

	/* Make sure put/put will replace value with collisions */
	assert(kkv_put(1, orange, strlen(orange) + 1, 0) == 0);
	assert(kkv_put(18, apple, strlen(apple) + 1, 0) == 0);
	assert(kkv_put(1, banana, strlen(banana) + 1, 0) == 0);
	assert(kkv_get(1, res, MAX_VAL_SIZE, KKV_NONBLOCK) == 0);
	assert(strcmp(banana, res) == 0);
	assert(kkv_get(18, res, MAX_VAL_SIZE, KKV_NONBLOCK) == 0);
	assert(strcmp(apple, res) == 0);

	assert(kkv_destroy(0) == 0);
}

int main(void)
{
	RUN_TEST(simple_sequential);
	return 0;
}
