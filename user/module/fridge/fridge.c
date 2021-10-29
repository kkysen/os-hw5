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
				  size_t user_size)
{
	/* The user tried to copy more bytes from kernel, just truncate it.
	 * If the user copies fewer bytes, return what they asked for,
	 * even though there's more data.
	 */
	if (copy_to_user(user_val, this->val, min(this->size, user_size)) !=
	    0) {
		return -EFAULT;
	}
	return 0;
}

static void kkv_ht_entry_init(struct kkv_ht_entry *this)
{
	INIT_LIST_HEAD(&this->entries);
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
		this->count--;
	}

	/* spinlocks don't need to be freed */
}

struct kkv_buckets {
	struct kkv_ht_bucket *ptr;
	size_t len;
	u8 len_bits;
};

static void kkv_buckets_for__each(struct kkv_buckets *this,
				  void (*f)(struct kkv_ht_bucket *bucket))
{
	size_t i;

	for (i = 0; i < this->len; i++)
		f(&this->ptr[i]);
}

static u8 num_bits_used(size_t n)
{
	const u8 max_bits = sizeof(n) * __CHAR_BIT__;
	u8 num_bits;

	for (num_bits = 0; num_bits < max_bits; num_bits++) {
		if (((1 << num_bits) - 1) >= n)
			return num_bits;
	}
	return max_bits;
}

static long kkv_buckets_init(struct kkv_buckets *this, size_t len)
{
	this->ptr = kmalloc_array(len, sizeof(*this->ptr), GFP_KERNEL);
	if (!this->ptr) {
		this->len = 0;
		this->len_bits = 0;
		return -ENOMEM;
	}
	this->len = len;
	this->len_bits = num_bits_used(len);
	kkv_buckets_for__each(this, kkv_ht_bucket_init);
	return 0;
}

static void kkv_buckets_free(struct kkv_buckets *this)
{
	kkv_buckets_for__each(this, kkv_ht_bucket_free);
	this->len_bits = 0;
	this->len = 0;
	kfree(this->ptr);
	this->ptr = NULL;
}

static u32 kkv_buckets_index(const struct kkv_buckets *this, u32 key)
{
	return hash_32(key, this->len_bits) % this->len;
}

static struct kkv_ht_bucket *kkv_buckets_get(struct kkv_buckets *this, u32 key)
{
	return &this->ptr[kkv_buckets_index(this, key)];
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
	list_add(&entry->entries, &this->entries);
	this->count++;
}

static void kkv_ht_bucket_remove(struct kkv_ht_bucket *this,
				 struct kkv_ht_entry *entry)
{
	this->count--;
	list_del(&entry->entries);
}

struct kkv {
	struct kkv_buckets buckets;
};

static long kkv_init_(struct kkv *this, size_t len)
{
	long e;

	e = kkv_buckets_init(&this->buckets, len);
	if (e < 0)
		return e;

	return 0;
}

static void kkv_free(struct kkv *this)
{
	kkv_buckets_free(&this->buckets);
}

static long kkv_put_(struct kkv *this, u32 key, const void *user_val,
		     size_t user_size, int flags)
{
	long e;
	struct kkv_ht_bucket *bucket;
	struct kkv_ht_entry *new_entry;
	struct kkv_pair pair;
	bool adding;

	if (flags != 0)
		return -EINVAL;

	/* Allocates, so put it before critical section. */
	e = kkv_pair_init_from_user(&pair, key, user_val, user_size);
	if (e < 0)
		return e;

	/* Unfortunately, need to alloc always, b/c we can't alloc in the critical section,
	 * and we don't know if we need to alloc until we see if the entry is already there,
	 * but we need to lock the bucket to do that.
	 * At least this will be more efficient with a slab cache later.
	 */
	new_entry = kmalloc(sizeof(*new_entry), GFP_KERNEL);
	if (!new_entry) {
		kkv_pair_free(&pair);
		return -ENOMEM;
	}

	bucket = kkv_buckets_get(&this->buckets, key);

	spin_lock(&bucket->lock);
	{
		/* Critical section: no allocs. */
		struct kkv_ht_entry *entry;
		struct kkv_pair tmp;

		entry = kkv_ht_bucket_find(bucket, key);
		adding = !entry;
		if (adding) {
			entry = new_entry;
			kkv_ht_entry_init(entry);
			kkv_ht_bucket_add(bucket, entry);
		}
		tmp = entry->kv_pair;
		entry->kv_pair = pair;
		pair = tmp;
	}
	spin_unlock(&bucket->lock);

	if (!adding)
		kfree(new_entry);
	kkv_pair_free(&pair);

	return 0;
}

static long kkv_get_(struct kkv *this, u32 key, void *user_val,
		     size_t user_size, int flags)
{
	long e;
	struct kkv_ht_bucket *bucket;
	struct kkv_ht_entry *entry;

	if (flags != KKV_NONBLOCK)
		return -EINVAL;

	bucket = kkv_buckets_get(&this->buckets, key);

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

	e = kkv_pair_copy_to_user(&entry->kv_pair, user_val, user_size);
	/* Free before returning the error. */
	kkv_ht_entry_free(entry);
	kfree(entry);
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
	long e;

	if (flags != 0)
		return -EINVAL;

	e = kkv_init_(&kkv, HASH_TABLE_LENGTH);
	if (e < 0)
		return e;

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
		return -EINVAL;

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
static long kkv_put(u32 key, const void *val, size_t size, int flags)
{
	return kkv_put_(&kkv, key, val, size, flags);
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
static long kkv_get(u32 key, void *val, size_t size, int flags)
{
	return kkv_get_(&kkv, key, val, size, flags);
}
