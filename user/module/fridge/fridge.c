/*
 * fridge.c
 *
 * A kernel-level key-value store. Accessed via user-defined
 * system calls. This is the module implementation.
 */

/* I turned on `-Wextra` but the linux headers don't use that,
 * so I need to disable it for the includes.
 */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"

#include <linux/module.h>
#include <linux/printk.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/hash.h>

#pragma GCC diagnostic pop

#include "fridge_data_structures.h"

#define MODULE_NAME "Fridge"

static long kkv_init(int flags);
static long kkv_destroy(int flags);
static long kkv_put(u32 key, const void *val, size_t size, int flags);
static long kkv_get(u32 key, void *val, size_t size, int flags);

extern long (*kkv_init_ptr)(int flags);
extern long (*kkv_destroy_ptr)(int flags);
extern long (*kkv_put_ptr)(u32 key, const void *val, size_t size, int flags);
extern long (*kkv_get_ptr)(u32 key, void *val, size_t size, int flags);

int fridge_init(void)
{
	pr_info("Installing fridge\n");
	kkv_init_ptr = kkv_init;
	kkv_destroy_ptr = kkv_destroy;
	kkv_put_ptr = kkv_put;
	kkv_get_ptr = kkv_get;
	return 0;
}

void fridge_exit(void)
{
	pr_info("Removing fridge\n");
	kkv_get_ptr = NULL;
	kkv_put_ptr = NULL;
	kkv_destroy_ptr = NULL;
	kkv_init_ptr = NULL;
}

module_init(fridge_init);
module_exit(fridge_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION(MODULE_NAME);
MODULE_AUTHOR("FireFerrises");

static void kkv_pair_init(struct kkv_pair *this)
{
	this->key = (u32)-1;
	this->size = 0;
	this->val = NULL;
}

static void kkv_pair_free(struct kkv_pair *this)
{
	kfree(this->val);
	/* Not really necessary, but a bit safer and can be easier to debug. */
	this->val = NULL;
	this->size = 0;
	this->key = (u32)-1;
}

static long kkv_pair_init_from_user(struct kkv_pair *this, u32 key,
				    const void *user_val, size_t size)
{
	this->val = kmalloc(size, GFP_KERNEL);
	if (!this->val)
		return -ENOMEM;
	if (copy_from_user(this->val, user_val, size) != 0) {
		kfree(this->val);
		return -EFAULT;
	}
	this->key = key;
	this->size = size;
	return 0;
}

static long kkv_pair_copy_to_user(struct kkv_pair *this, void *user_val,
				  size_t size)
{
	/* The user tried to copy extra bytes from kernel. */
	if (size > this->size)
		return -EFAULT;
	/* If the user copies fewer bytes, that's okay, though they may run into problems. */
	if (copy_to_user(user_val, this->val, this->size) != 0)
		return -EFAULT;
	return 0;
}

static void kkv_ht_entry_init(struct kkv_ht_entry *this)
{
	/* `this->entries` freed by container. */
	kkv_pair_init(&this->kv_pair);
	/* `this->q` unused until part 4. */
	/* `this->q_count` unused until part 4. */
}

static void kkv_ht_entry_free(struct kkv_ht_entry *this)
{
	/* `this->q_count` unused until part 4. */
	/* `this->q` unused until part 4. */
	kkv_pair_free(&this->kv_pair);
	/* `this->entries` freed by container. */
}

static void kkv_ht_bucket_init(struct kkv_ht_bucket *this)
{
	*this = (struct kkv_ht_bucket){
		.lock = __SPIN_LOCK_UNLOCKED(),
		.entries = LIST_HEAD_INIT(this->entries),
		.count = 0,
	};
}

static void kkv_ht_bucket_free(struct kkv_ht_bucket *this)
{
	struct kkv_ht_entry *entry;
	struct kkv_ht_entry *tmp;

	list_for_each_entry_safe(entry, tmp, &this->entries, entries) {
		kkv_ht_entry_free(entry);
		list_del(&entry->entries);
		kfree(entry);
	}

	this->count = 0;

	/* spinlocks don't need to be freed */
}

#define HASH_TABLE_LENGTH_BITS 5

struct kkv {
	struct kkv_ht_bucket buckets[HASH_TABLE_LENGTH];
};

static void kkv_for__each_bucket(struct kkv *this,
				 void (*f)(struct kkv_ht_bucket *bucket))
{
	size_t i;

	for (i = 0; i < ARRAY_SIZE(this->buckets); i++)
		f(&this->buckets[i]);
}

static void kkv_init_(struct kkv *this)
{
	kkv_for__each_bucket(this, kkv_ht_bucket_init);
}

static void kkv_free(struct kkv *this)
{
	kkv_for__each_bucket(this, kkv_ht_bucket_free);
}

static u32 kkv_bucket_index(const struct kkv *this, u32 key)
{
	BUILD_BUG_ON(ARRAY_SIZE(this->buckets) > (1 << HASH_TABLE_LENGTH_BITS));
	return hash_32(key, HASH_TABLE_LENGTH_BITS) % ARRAY_SIZE(this->buckets);
}

static struct kkv_ht_bucket *kkv_get_bucket(struct kkv *this, u32 key)
{
	return &this->buckets[kkv_bucket_index(this, key)];
}

static struct kkv_ht_entry *kkv_ht_bucket_find(const struct kkv_ht_bucket *this,
					       u32 key)
{
	struct kkv_ht_entry *entry;

	list_for_each_entry(entry, &this->entries, entries) {
		if (entry->kv_pair.key == key)
			return entry;
	}
	return NULL;
}

static void kkv_ht_bucket_add(struct kkv_ht_bucket *this,
			      struct kkv_ht_entry *entry)
{
	list_add(&this->entries, &entry->entries);
	this->count++;
}

static void kkv_ht_bucket_remove(struct kkv_ht_bucket *this,
				 struct kkv_ht_entry *entry)
{
	this->count--;
	list_del(&entry->entries);
}

static long kkv_put_(struct kkv *this, u32 key, const void *user_val,
		     size_t size, int flags)
{
	long e;
	struct kkv_ht_bucket *bucket;
	struct kkv_ht_entry *new_entry;
	struct kkv_pair pair;
	bool adding;

	if (flags != 0)
		return -ENOSYS;

	/* Allocates, so put it before critical section. */
	e = kkv_pair_init_from_user(&pair, key, user_val, size);
	if (e < 0)
		return e;

	/* Unfortunately, need to alloc always, b/c we can't alloc in the critical section,
	 * and we don't know if we need to alloc until we see if the entry is already there,
	 * but we need to lock the bucket to do that.
	 * At least this will be more efficient with a slab cache later.
	 */
	new_entry = kmalloc(sizeof(*new_entry), GFP_KERNEL);
	if (!new_entry)
		return -ENOMEM;

	bucket = kkv_get_bucket(this, key);

	spin_lock(&bucket->lock);
	{
		/* Critical section: no allocs. */
		struct kkv_ht_entry *entry;

		entry = kkv_ht_bucket_find(bucket, key);
		adding = !entry;
		if (adding) {
			entry = new_entry;
			kkv_ht_entry_init(entry);
			kkv_ht_bucket_add(bucket, entry);
		}
		entry->kv_pair = pair;
	}
	spin_unlock(&bucket->lock);

	if (!adding)
		kfree(new_entry);

	return 0;
}

static long kkv_get_(struct kkv *this, u32 key, void *user_val, size_t size,
		     int flags)
{
	long e;
	struct kkv_ht_bucket *bucket;
	struct kkv_ht_entry *entry;

	if (flags != KKV_NONBLOCK)
		return -ENOSYS;

	bucket = kkv_get_bucket(this, key);

	spin_lock(&bucket->lock);
	{
		/* Critical section: no allocs. */
		entry = kkv_ht_bucket_find(bucket, key);
		if (entry) {
			/* `entry` removed but not freed; do that outside critical section.
			 * But since it's been removed, no one else has a reference to it anymore,
			 * so we can modify it more later.
			 */
			kkv_ht_bucket_remove(bucket, entry);
		}
	}
	spin_unlock(&bucket->lock);

	if (!entry)
		return -ENOENT;

	e = kkv_pair_copy_to_user(&entry->kv_pair, user_val, size);
	/* Free before returning the error. */
	kkv_ht_entry_free(entry);
	if (e < 0)
		return e;

	return 0;
}

static struct kkv kkv;

/*
 * Initialize the Kernel Key-Value store.
 *
 * Returns 0 on success.
 * Returns -1 on failure, with errno set accordingly.
 * The flags parameter is currently unused.
 *
 * The result of initializing twice (without an intervening
 * kkv_destroy() call) is undefined.
 */
static long kkv_init(int flags)
{
	if (flags != 0)
		return -ENOSYS;

	kkv_init_(&kkv);

	return 0;
}

/*
 * Destroy the Kernel Key-Value store, removing all entries
 * and deallocating all memory.
 *
 * Returns the number of entries removed on success.
 * Returns -1 on failure, with errno set accordingly.
 * The flags parameter is currently unused.
 *
 * After calling kkv_destroy(), the Kernel Key-Value store can be
 * re-initialized by calling kkv_init() again.
 *
 * The result of destroying before initializing is undefined.
 */
static long kkv_destroy(int flags)
{
	if (flags != 0)
		return -ENOSYS;

	kkv_free(&kkv);

	return 0;
}

/*
 * Insert a new key-value pair. The previous value for the key, if any, is
 * replaced by the new value.
 *
 * The "size" bytes from the buffer pointed to by the "val" parameter
 * will be copied into the Kernel Key-Value store.
 *
 * If "val" is a string, users of this syscall should make sure that
 * size == strlen(val) + 1, so that the null character is stored as part
 * of the value.
 *
 * Returns 0 on success.
 * Returns -1 on failure, with errno set accordingly.
 * The flags parameter is currently unused.
 *
 * The result of calling kkv_put() before initializing the Kernel
 * Key-Value store is undefined.
 */
static long kkv_put(u32 key, const void *user_val, size_t size, int flags)
{
	return kkv_put_(&kkv, key, user_val, size, flags);
}

/*
 * If a key-value pair is found for the given key, the pair is
 * REMOVED from the Kernel Key-Value store.
 *
 * The value is copied into the buffer pointed to by the "val" parameter,
 * up to "size" bytes. Note that if the value was a string of
 * length >= "size", the string will be truncated and it will not be
 * null-terminated.
 *
 * Returns 0 if a key-value pair was found, removed and returned.
 * Returns -1 with errno set to ENOENT if the key was not found.
 * Returns -1 on other failures, with errno set accordingly.
 * The flags parameter is currently unused; later used for specifying
 * blocking behavior.
 *
 * The result of calling kkv_get() before initializing the Kernel Key-Value
 * store is undefined.
 */
static long kkv_get(u32 key, void *user_val, size_t size, int flags)
{
	return kkv_get_(&kkv, key, user_val, size, flags);
}
