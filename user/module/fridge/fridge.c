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
#include <linux/wait.h>
#include <linux/sched/signal.h>

#pragma GCC diagnostic pop

#include "fridge_data_structures.h"

#define MODULE_NAME "Fridge"

#define MUST_USE __attribute__((warn_unused_result))

#define trace()                                                                \
	pr_info("[%d] %s:%u:%s", current->pid, __FILE__, __LINE__, __func__)

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
	struct kmem_cache *cache;
};

static MUST_USE struct kkv_pair kkv_pair_empty_with_key(u32 key)
{
	return (struct kkv_pair){
		.key = key,
		.size = 0,
		.val = NULL,
	};
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

static void kkv_pair_swap(struct kkv_pair *a, struct kkv_pair *b)
{
	struct kkv_pair tmp;

	tmp = *a;
	*a = *b;
	*b = tmp;
}

static char ZERO_LENGTH_ARRAY[] = {};

static MUST_USE long kkv_pair_init_from_user(struct kkv_pair *this, u32 key,
					     const void *user_val, size_t size)
{
	long e;

	e = 0;

	if (size == 0) {
		/**
		 * Don't use `NULL` here, because that indicates there is no value at all.
		 * This is just a zero-length value that can be shared
		 * b/c it is zero-length and thus effectively immutable.
		 */
		this->val = ZERO_LENGTH_ARRAY;
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
	long e;
	size_t size;

	e = 0;
	size = min(this->size, user_size);
	if (size == 0)
		goto ret;

	/* The user tried to copy more bytes from kernel, just truncate it.
	 * If the user copies fewer bytes, return what they asked for,
	 * even though there's more data.
	 */
	if (copy_to_user(user_val, this->val, size) != 0) {
		e = -EFAULT;
		goto ret;
	}
	goto ret;

ret:
	return e;
}

static void kkv_ht_entry_init(struct kkv_ht_entry *this, u32 key)
{
	INIT_LIST_HEAD(&this->entries);
	this->kv_pair = kkv_pair_empty_with_key(key);
	init_waitqueue_head(&this->q);
	this->q_count = 0;
}

static void kkv_ht_entry_free(struct kkv_ht_entry *this)
{
	this->q_count = 0;
	/* `this->q` has no destructor */
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

static void free_kkv_ht_entry(struct kkv_ht_entry *this,
			      struct kmem_cache *cache)
{
	/**
	 * We only free entries that have no `q_count`, however,
	 * since we have to no way of calling `finish_wait` on them,
	 * and if we free it here, then the blocking get
	 * can't access `q` to call `finish_wait`.
	 */
	list_del(&this->entries);
	pr_info("entry = %p, key = %u, q_count = %u\n", this, this->kv_pair.key,
		this->q_count);
	if (this->q_count == 0) {
		trace();
	} else {
		/* Threads woken up will know this entry is detached if it's an empty list. */
		INIT_LIST_HEAD(&this->entries);
		trace();
		wake_up(&this->q);
	}
	kkv_ht_entry_free(this);
	kmem_cache_free(cache, this);
}

/* Return number of entries freed. */
static MUST_USE size_t kkv_ht_bucket_free(struct kkv_ht_bucket *this,
					  struct kmem_cache *cache)
{
	struct kkv_ht_entry *entry;
	struct kkv_ht_entry *tmp;
	size_t n;

	n = 0;
	list_for_each_entry_safe(entry, tmp, &this->entries, entries) {
		/**
		 * Note that we count value-less `kkv_get(KKV_BLOCK)` entries here,
		 * which Hans said to do.
		 */
		free_kkv_ht_entry(entry, cache);
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
static MUST_USE size_t kkv_buckets_free(struct kkv_buckets *this,
					struct kmem_cache *cache)
{
	size_t i;
	struct kkv_ht_bucket *bucket;
	size_t n;

	n = 0;
	for (i = 0; i < this->len; i++) {
		bucket = &this->ptr[i];
		n += kkv_ht_bucket_free(bucket, cache);
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
	long e;

	e = 0;

	e = kkv_buckets_init(&this->buckets, len);
	if (e < 0)
		goto ret;
	goto ret;

ret:
	return e;
}

/* Return number of entries freed. */
static MUST_USE size_t kkv_inner_free(struct kkv_inner *this,
				      struct kmem_cache *cache)
{
	return kkv_buckets_free(&this->buckets, cache);
}

static MUST_USE struct kkv kkv_new(void)
{
	return (struct kkv){
		.inner = kkv_inner_new(),
		.initialized = false,
		.lock = __RW_LOCK_UNLOCKED(),
		.cache = NULL,
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
	/* shouldn't actually need the cache */
	_num_freed = kkv_inner_free(&inner, this->cache);
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
	e = (long)kkv_inner_free(&inner, this->cache);
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
	new_entry = kmem_cache_alloc(this->cache, GFP_KERNEL);
	if (!new_entry) {
		e = -ENOMEM;
		goto pair_free;
	}
	adding = false;

	trace();
	if (!kkv_lock(this, /* write */ false, /* expect init */ true)) {
		e = -EPERM;
		goto free_entry;
	}

	bucket = kkv_buckets_get(&this->inner.buckets, key);
	spin_lock(&bucket->lock);
	{
		/* Critical section: no allocs. */
		struct kkv_ht_entry *entry;

		entry = kkv_ht_bucket_find(bucket, key);
		if (!entry) {
			adding = true;
			entry = new_entry;
			kkv_ht_entry_init(entry, (u32)-1);
			kkv_ht_bucket_add(bucket, entry);
		}
		kkv_pair_swap(&pair, &entry->kv_pair);
		if (entry->q_count > 0) {
			trace();
			pr_info("key = %u, q_count = %u\n", key,
				entry->q_count);
			wake_up(&entry->q);
			trace();
		}
	}
	spin_unlock(&bucket->lock);
	read_unlock(&this->lock);
	trace();
	goto free_entry;

free_entry:
	if (!adding)
		kmem_cache_free(this->cache, new_entry);
pair_free:
	kkv_pair_free(&pair);
ret:
	trace();
	return e;
}

//static void free_detached_empty_entry(struct kkv_ht_entry *empty_entry,
//				      struct wait_queue_entry *wait,
//				      struct kmem_cache *cache)
//{
//	/**
//	 * `kkv_destroy` must've been called,
//	 * which skips freeing entries with `q_count`s
//	 * and wakes up the queue, so we have to do that.
//	 * Only one thread can free the entry, though,
//	 * so we have to use `q_count` as a reference count.
//	 * It's not atomic, though, so we have to synchronize access to it.
//	 * The entry doesn't have a lock either, but the queue does,
//	 * so co-opt that (not sure if it's safe,
//	 * but we're not allowed to modify the entry struct).
//	 * Also, we can't use the kkv rwlock,
//	 * since our entry is currently detached from the kkv.
//	 * We also have to free after unlocking, since the free frees the lock.
//	 * This is okay, though; the locking is for determining who frees.
//	 * Or can we call still call atomic functions on non-atomic integers?
//	 */
//	bool free;
//	// atomic_t *count;

//	trace();
//	finish_wait(&empty_entry->q, wait);
//	//count = (atomic_t *) &empty_entry->q_count;
//	//free = atomic_
//	free = --empty_entry->q_count == 0;
//	if (free)
//		kmem_cache_free(cache, empty_entry);
//}

static MUST_USE long kkv_get_(struct kkv *this, u32 key, void *user_val,
			      size_t user_size, int flags)
{
	long e;
	bool block;
	struct kkv_ht_bucket *bucket;
	struct kkv_ht_entry *new_entry;
	struct kkv_ht_entry *empty_entry;
	struct kkv_ht_entry *entry;
	struct kkv_pair pair;
	DEFINE_WAIT(wait);

	e = 0;

	if (flags & ~(KKV_NONBLOCK | KKV_BLOCK)) {
		e = -EINVAL;
		goto ret;
	}
	block = flags & KKV_BLOCK;

	if (!block) {
		new_entry = NULL;
		empty_entry = NULL;
	} else {
		new_entry = kmem_cache_alloc(this->cache, GFP_KERNEL);
		if (!new_entry) {
			e = -ENOMEM;
			goto ret;
		}
	}
	trace();

	if (!kkv_lock(this, /* write */ false, /* expect init */ true)) {
		e = -EPERM;
		goto ret;
	}
	trace();

	bucket = kkv_buckets_get(&this->inner.buckets, key);

	trace();
	spin_lock(&bucket->lock);
	trace();
	{
		entry = kkv_ht_bucket_find(bucket, key);
		if (entry) {
			if (entry->kv_pair.val) {
				/**
				 * `entry` removed but not freed;
				 * do that outside critical section.
				 * But since it's been removed,
				 * no one else has a reference to it anymore,
				 * so we can modify it more later.
				 */
				kkv_ht_bucket_remove(bucket, entry);
			} else if (block) {
				empty_entry = entry;
				entry = NULL;
			} else {
				/* `entry` is empty, but non blocking. */
				entry = NULL;
			}
		} else if (block) {
			/* Sets pair value to NULL. */
			empty_entry = new_entry;
			new_entry = NULL;
			kkv_ht_entry_init(empty_entry, key);
			kkv_ht_bucket_add(bucket, empty_entry);
		}
	}
	trace();
	spin_unlock(&bucket->lock);
	trace();

	if (empty_entry && block) {
		trace();
		pr_info("empty_entry = %p\n", empty_entry);
		empty_entry->q_count++;
		trace();
		prepare_to_wait(&empty_entry->q, &wait, TASK_INTERRUPTIBLE);
		trace();
		pr_info("entry = %p, key = %u, q_count = %u\n", empty_entry,
			empty_entry->kv_pair.key, empty_entry->q_count);
	}
	trace();
	read_unlock(&this->lock);
	trace();

	if (new_entry) {
		/**
		 * If `new_entry` was added to the kkv,
		 * it is assigned to `empty_entry` and set to NULL.
		 */
		trace();
		kmem_cache_free(this->cache, new_entry);
		trace();
	}

	pair = kkv_pair_empty_with_key(key);
	if (entry) {
		trace();
		kkv_pair_swap(&pair, &entry->kv_pair);
	} else {
		if (!block) {
			e = -ENOENT;
			trace();
			goto ret;
		}
		trace();

		/**
		 * `new_entry` is currently in the kkv,
		 * which means `kkv_destroy()` could free it at any moment
		 * unless we have a read lock.
		 * Assign to `entry` when it's no longer in the kkv,
		 * since `entry` can be accessed without any lock.
		 */
		for (;;) {
			bool break_loop;

			break_loop = false;
			trace();
			schedule();
			trace();
			if (!kkv_lock(this, /* write */ false,
				      /* expect init */ true)) {
				e = -EPERM;
				// free_detached_empty_entry(empty_entry, &wait,
				// 			  this->cache);
				trace();
				goto ret;
			}
			if (list_empty(&empty_entry->entries)) {
				/**
				 * kkv was destroyed and re-initialized,
				 * so we could get the lock,
				 * but we're detached and need to free ourselves.
				 */
				read_unlock(&this->lock);
				// free_detached_empty_entry(empty_entry, &wait,
				// 			  this->cache);
				e = -EPERM;
				trace();
				goto ret;
			}
			spin_lock(&bucket->lock);
			if (signal_pending(current)) {
				e = -EINTR;
				trace();
				goto break_loop;
			}

			if (empty_entry->kv_pair.val != NULL) {
				kkv_pair_swap(&pair, &empty_entry->kv_pair);
				trace();
				goto break_loop;
			}
			prepare_to_wait(&empty_entry->q, &wait,
					TASK_INTERRUPTIBLE);
			trace();
			goto unlock;

break_loop:
			break_loop = true;
			if (--empty_entry->q_count == 0) {
				kkv_ht_bucket_remove(bucket, empty_entry);
				/* will be freed */
				entry = empty_entry;
			}
			finish_wait(&empty_entry->q, &wait);
unlock:
			spin_unlock(&bucket->lock);
			read_unlock(&this->lock);
			if (break_loop)
				break;
		}
	}
	if (e < 0)
		goto free_entry;

	trace();
	e = kkv_pair_copy_to_user(&pair, user_val, user_size);
	goto free_entry;

free_entry:
	if (entry) {
		kkv_ht_entry_free(entry);
		kmem_cache_free(this->cache, entry);
	}
	kkv_pair_free(&pair);
ret:
	trace();
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

static MUST_USE int kkv_module_init(struct kkv *this)
{
	int e;

	e = 0;

	*this = kkv_new();
	this->cache = KMEM_CACHE(kkv_ht_entry, 0);
	if (!this->cache) {
		e = -ENOMEM;
		goto ret;
	}
	goto ret;

ret:
	return e;
}

static void kkv_module_free(struct kkv *this)
{
	int _e;

	/**
	 * Destroy in case the user forgot so we don't leak anything.
	 * It's also okay if it returns an error; we don't care.
	 */
	_e = kkv_free(this);
	kmem_cache_destroy(this->cache);
}

int fridge_init(void)
{
	int e;

	e = 0;

	pr_info("Installing fridge\n");
	e = kkv_module_init(&kkv);
	if (e < 0)
		goto ret;

	kkv_init_ptr = kkv_init;
	kkv_destroy_ptr = kkv_destroy;
	kkv_put_ptr = kkv_put;
	kkv_get_ptr = kkv_get;
	goto ret;

ret:
	return e;
}

void fridge_exit(void)
{
	pr_info("Removing fridge\n");
	kkv_get_ptr = NULL;
	kkv_put_ptr = NULL;
	kkv_destroy_ptr = NULL;
	kkv_init_ptr = NULL;
	kkv_module_free(&kkv);
}

module_init(fridge_init);
module_exit(fridge_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION(MODULE_NAME);
MODULE_AUTHOR("FireFerrises");
