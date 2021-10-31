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

#define MUST_USE __attribute__((warn_unused_result))

extern long (*kkv_init_ptr)(int flags);
extern long (*kkv_destroy_ptr)(int flags);
extern long (*kkv_put_ptr)(u32 key, const void *val, size_t size, int flags);
extern long (*kkv_get_ptr)(u32 key, void *val, size_t size, int flags);

struct kkv_buckets {
	struct kkv_ht_bucket *ptr;
	size_t len;
	u8 len_bits;
};

struct kkv_inner {
	struct kkv_buckets buckets;
};

struct kkv {
	struct kkv_inner inner;
	bool initialized;
	rwlock_t lock; /* guards initialized */
};

static void kkv_pair_init(struct kkv_pair *this)
{
	this->key = (u32)-1;
	this->size = 0;
	this->val = NULL;
}

static void kkv_pair_free(struct kkv_pair *this)
{
	if (this->size != 0) {
		kfree(this->val);
		this->val = NULL;
		this->size = 0;
	}
	/* Not really necessary, but a bit safer and can be easier to debug. */
	this->key = (u32)-1;
}

static MUST_USE long kkv_pair_init_from_user(struct kkv_pair *this, u32 key,
					     const void *user_val, size_t size)
{
	long e;

	e = 0;

	if (size == 0) {
		this->val = NULL;
	} else {
		this->val = kmalloc(size, GFP_KERNEL);
		if (!this->val) {
			e = -ENOMEM;
			goto ret;
		}
		if (copy_from_user(this->val, user_val, size) != 0) {
			e = -EFAULT;
			goto free_val;
		}
	}

	this->key = key;
	this->size = size;
	goto ret;

free_val:
	kfree(this->val);
ret:
	return 0;
}

static MUST_USE long kkv_pair_copy_to_user(struct kkv_pair *this,
					   void *user_val, size_t user_size)
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

/* Return number of entries freed. */
static MUST_USE size_t kkv_ht_bucket_free(struct kkv_ht_bucket *this)
{
	struct kkv_ht_entry *entry;
	struct kkv_ht_entry *tmp;
	size_t n;

	n = 0;
	list_for_each_entry_safe(entry, tmp, &this->entries, entries) {
		kkv_ht_entry_free(entry);
		list_del(&entry->entries);
		kfree(entry);
		this->count--;
		n++;
	}

	/* spinlocks don't need to be freed */

	return n;
}

static MUST_USE struct kkv_buckets kkv_buckets_new(void)
{
	return (struct kkv_buckets){
		.ptr = NULL,
		.len = 0,
		.len_bits = 0,
	};
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

static MUST_USE long kkv_buckets_init(struct kkv_buckets *this, size_t len)
{
	size_t i;
	struct kkv_ht_bucket *bucket;

	this->ptr = kmalloc_array(len, sizeof(*this->ptr), GFP_KERNEL);
	if (!this->ptr) {
		this->len = 0;
		this->len_bits = 0;
		return -ENOMEM;
	}
	this->len = len;
	this->len_bits = num_bits_used(len);
	for (i = 0; i < this->len; i++) {
		bucket = &this->ptr[i];
		kkv_ht_bucket_init(bucket);
	}
	return 0;
}

/* Return number of entries freed. */
static MUST_USE size_t kkv_buckets_free(struct kkv_buckets *this)
{
	size_t i;
	struct kkv_ht_bucket *bucket;
	size_t n;

	n = 0;
	for (i = 0; i < this->len; i++) {
		bucket = &this->ptr[i];
		n += kkv_ht_bucket_free(bucket);
	}

	this->len_bits = 0;
	this->len = 0;
	kfree(this->ptr);
	this->ptr = NULL;

	return n;
}

static MUST_USE u32 kkv_buckets_index(const struct kkv_buckets *this, u32 key)
{
	return hash_32(key, this->len_bits) % this->len;
}

static MUST_USE struct kkv_ht_bucket *kkv_buckets_get(struct kkv_buckets *this,
						      u32 key)
{
	return &this->ptr[kkv_buckets_index(this, key)];
}

static MUST_USE struct kkv_ht_entry *
kkv_ht_bucket_find(const struct kkv_ht_bucket *this, u32 key)
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

static MUST_USE struct kkv_inner kkv_inner_new(void)
{
	return (struct kkv_inner){
		.buckets = kkv_buckets_new(),
	};
}

static MUST_USE long kkv_inner_init(struct kkv_inner *this, size_t len)
{
	return kkv_buckets_init(&this->buckets, len);
}

/* Return number of entries freed. */
static MUST_USE size_t kkv_inner_free(struct kkv_inner *this)
{
	return kkv_buckets_free(&this->buckets);
}

static MUST_USE struct kkv kkv_new(void)
{
	return (struct kkv){
		.inner = kkv_inner_new(),
		.initialized = false,
		.lock = __RW_LOCK_UNLOCKED(),
	};
}

/* Return if the lock was acquired. */
static MUST_USE bool kkv_lock(struct kkv *this, bool write,
			      bool expecting_initialized)
{
	bool fail_fast;
	bool locked;

	/**
	 * Fail fast on writes, b/c they can be starved,
	 * and it doesn't reduce get/put concurrency, which are reads.
	 */
	fail_fast = write;
	locked = false;

	/**
	 * If we want to fail fast and notify the user
	 * that they are using the kkv API wrong,
	 * i.e., interspersing kkv_get/kkv_put calls
	 * with kkv_init/kkv_destroy calls,
	 * we shouldn't only trylock here and return EPERM immediately.
	 *
	 * On the other hand, we can also lock
	 * and succeed if the initialized state is correct after,
	 * i.e. a kkv_put waiting for a kkv_init, for example.
	 */
	if (fail_fast) {
		if (this->initialized ^ expecting_initialized) {
			/**
			 * Already initialized.
			 * First check before lock so we avoid lock in most cases.
			 */
			goto ret;
		}
		if (!(write ? write_trylock(&this->lock) :
				    read_trylock(&this->lock))) {
			/**
			 * If we can't get the lock,
			 * then init or destroy are being called currently,
			 * which is not when you're supposed to call anything else.
			 */
			goto ret;
		}
	} else {
		write ? write_lock(&this->lock) : read_lock(&this->lock);
	}
	if (this->initialized ^ expecting_initialized) {
		/**
		 * Already initialized.
		 * Second real check while holding lock.
		 */
		goto unlock;
	}
	locked = true;
	goto ret;

unlock:
	write ? write_unlock(&this->lock) : read_unlock(&this->lock);
	locked = false;
ret:
	return locked;
}

static MUST_USE long kkv_init_(struct kkv *this, size_t len)
{
	long e;
	struct kkv_inner inner;
	size_t _num_freed;

	e = 0;

	if (this->initialized) {
		/**
		 * An extra check before allocating, since that's
		 * expensive and we'd like to avoid it if possible.
		 */
		e = -EPERM;
		goto ret;
	}
	e = kkv_inner_init(&inner, len);
	if (e < 0)
		goto ret;

	if (!kkv_lock(this, /* write */ true, /* expect init */ false)) {
		e = -EPERM;
		/**
		 * Unfortunately we already allocated the inner.
		 * But nothing we can do about it since
		 * we can't allocate in the critical section.
		 */
		goto inner_free;
	}
	{
		/**
		 * Critical section, which is why
		 * we have to do the allocation before.
		 * And only swap it in inside the critical section.
		 * And as soon as we unlock, other threads can
		 * start reading inner, so we can't allocate after either.
		 */
		this->initialized = true;
		this->inner = inner;
	}
	write_unlock(&this->lock);
	goto ret;

inner_free:
	_num_freed = kkv_inner_free(&inner);
	/* should be zero */
ret:
	return e;
}

/* Return number of entries freed. */
static MUST_USE long kkv_free(struct kkv *this)
{
	long e;
	struct kkv_inner inner;

	e = 0;

	if (!kkv_lock(this, /* write */ true, /* expect init */ true)) {
		e = -EPERM;
		goto ret;
	}
	{
		/**
		 * Critical section, so we can't do the deallocation here.
		 * So we set initialized to false so other threads
		 * don't try to access it, and we swap out inner
		 * so that we can free it before another thread tries
		 * to re-initialize it.
		 */
		this->initialized = false;
		inner = this->inner;
		this->inner = kkv_inner_new();
	}
	write_unlock(&this->lock);
	/**
	 * Now inner is detached any only accessible here,
	 * so we can free it without holding the lock.
	 */
	e = (long)kkv_inner_free(&inner);
	goto ret;

ret:
	return e;
}

static MUST_USE long kkv_put_(struct kkv *this, u32 key, const void *user_val,
			      size_t user_size, int flags)
{
	long e;
	struct kkv_ht_bucket *bucket;
	struct kkv_ht_entry *new_entry;
	struct kkv_pair pair;
	bool adding;

	e = 0;

	if (flags != 0) {
		e = -EINVAL;
		goto ret;
	}

	/* Allocates, so put it before critical section. */
	e = kkv_pair_init_from_user(&pair, key, user_val, user_size);
	if (e < 0)
		goto ret;

	/**
	 * Unfortunately, need to alloc always, b/c we can't alloc in the critical section,
	 * and we don't know if we need to alloc until we see if the entry is already there,
	 * but we need to lock the bucket to do that.
	 * At least this will be more efficient with a slab cache later.
	 */
	new_entry = kmalloc(sizeof(*new_entry), GFP_KERNEL);
	if (!new_entry) {
		e = -ENOMEM;
		goto pair_free;
	}
	adding = false;

	if (!kkv_lock(this, /* write */ false, /* expect init */ true)) {
		e = -EPERM;
		goto free_entry;
	}

	bucket = kkv_buckets_get(&this->inner.buckets, key);

	spin_lock(&bucket->lock);
	{
		/* Critical section: no allocs. */
		struct kkv_ht_entry *entry;
		struct kkv_pair tmp;

		entry = kkv_ht_bucket_find(bucket, key);
		if (!entry) {
			adding = true;
			entry = new_entry;
			kkv_ht_entry_init(entry);
			kkv_ht_bucket_add(bucket, entry);
		}
		tmp = entry->kv_pair;
		entry->kv_pair = pair;
		pair = tmp;
	}
	spin_unlock(&bucket->lock);
	read_unlock(&this->lock);
	goto free_entry;

free_entry:
	if (!adding)
		kfree(new_entry);
pair_free:
	kkv_pair_free(&pair);
ret:
	return e;
}

static MUST_USE long kkv_get_(struct kkv *this, u32 key, void *user_val,
			      size_t user_size, int flags)
{
	long e;
	struct kkv_ht_bucket *bucket;
	struct kkv_ht_entry *entry;

	e = 0;

	if (flags != KKV_NONBLOCK) {
		e = -EINVAL;
		goto ret;
	}

	if (!kkv_lock(this, /* write */ false, /* expect init */ true)) {
		e = -EPERM;
		goto ret;
	}

	bucket = kkv_buckets_get(&this->inner.buckets, key);

	spin_lock(&bucket->lock);
	{
		/* Critical section: no allocs. */
		entry = kkv_ht_bucket_find(bucket, key);
		if (entry) {
			/**
			 * `entry` removed but not freed; do that outside critical section.
			 * But since it's been removed, no one else has a reference to it anymore,
			 * so we can modify it more later.
			 */
			kkv_ht_bucket_remove(bucket, entry);
		}
	}
	spin_unlock(&bucket->lock);
	read_unlock(&this->lock);

	if (!entry) {
		e = -ENOENT;
		goto ret;
	}
	e = kkv_pair_copy_to_user(&entry->kv_pair, user_val, user_size);
	goto free_entry;

free_entry:
	kkv_ht_entry_free(entry);
	kfree(entry);
ret:
	return e;
}

static struct kkv kkv;

/**
 * Initialize the Kernel Key-Value store.
 *
 * Returns 0 on success.
 * Returns -1 on failure, with errno set accordingly.
 * The flags parameter is currently unused.
 *
 * The result of initializing twice (without an intervening
 * kkv_destroy() call) is undefined.
 */
static MUST_USE long kkv_init(int flags)
{
	long e;

	e = 0;

	if (flags != 0) {
		e = -EINVAL;
		goto ret;
	}

	e = kkv_init_(&kkv, HASH_TABLE_LENGTH);
	if (e < 0)
		goto ret;
	goto ret;

ret:
	return e;
}

/**
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
static MUST_USE long kkv_destroy(int flags)
{
	long e;

	e = 0;

	if (flags != 0) {
		e = -EINVAL;
		goto ret;
	}

	e = kkv_free(&kkv);
	if (e < 0)
		goto ret;
	goto ret;

ret:
	return e;
}

/**
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
static MUST_USE long kkv_put(u32 key, const void *val, size_t size, int flags)
{
	return kkv_put_(&kkv, key, val, size, flags);
}

/**
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
static MUST_USE long kkv_get(u32 key, void *val, size_t size, int flags)
{
	return kkv_get_(&kkv, key, val, size, flags);
}

int fridge_init(void)
{
	pr_info("Installing fridge\n");
	kkv = kkv_new();
	kkv_init_ptr = kkv_init;
	kkv_destroy_ptr = kkv_destroy;
	kkv_put_ptr = kkv_put;
	kkv_get_ptr = kkv_get;
	return 0;
}

void fridge_exit(void)
{
	long e_;

	pr_info("Removing fridge\n");
	kkv_get_ptr = NULL;
	kkv_put_ptr = NULL;
	kkv_destroy_ptr = NULL;
	kkv_init_ptr = NULL;
	/**
	 * Destroy in case the user forgot so we don't leak anything.
	 * It's also okay if it returns an error; we don't care.
	 */
	e_ = kkv_free(&kkv);
}

module_init(fridge_init);
module_exit(fridge_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION(MODULE_NAME);
MODULE_AUTHOR("FireFerrises");
