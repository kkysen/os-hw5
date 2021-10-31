#ifndef _FRIDGE_DATA_STRUCTURES_H_
#define _FRIDGE_DATA_STRUCTURES_H_

#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/wait.h>

#define HASH_TABLE_LENGTH 17
#define KKV_NONBLOCK 0
#define KKV_BLOCK 1

/*
 * A key-value pair.
 */
struct kkv_pair {
	void *val;
	uint32_t key;
	size_t size;
};

/*
 * A node in a linked list. Contains a key-value pair.
 */
struct kkv_ht_entry {
	struct list_head entries;
	struct kkv_pair kv_pair;
	wait_queue_head_t q;		/* only for part 4 (empty fridge) */
	uint32_t q_count;		/* only for part 4 (empty fridge) */
};

/*
 * A bucket in the hash table.
 * The hash table is an array of HASH_TABLE_LENGTH buckets.
 */
struct kkv_ht_bucket {
	spinlock_t lock;
	struct list_head entries;
	uint32_t count;
};

#endif
