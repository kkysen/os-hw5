#ifndef _LIBFRIDGE_H_
#define _LIBFRIDGE_H_

#include <stdint.h>
#include <stddef.h>
#include <unistd.h>

#define __NR_kkv_init      501
#define __NR_kkv_destroy   502
#define __NR_kkv_put       503
#define __NR_kkv_get       504

/*
 * Initialize the Kernel Key-Value store.
 *
 * Returns 0 on success.
 * Returns -1 on failure, with errno set accordingly.
 * The flags parameter is currently unused.
 *
 * The result of initializing twice (without an intervening
 * kvv_destroy() call) is undefined.
 */
int kkv_init(int flags);

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
int kkv_destroy(int flags);

/*
 * If a key-value pair is found for the given key, the pair is
 * REMOVED from the Kernel Key-Value store.
 *
 * The value is copied into the buffer pointed to by the "val" parameter,
 * up to "size" bytes. Note that if the value was a string of
 * length >= size, the string will be truncated and it will not be
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
int kkv_get(uint32_t key, void *val, size_t size, int flags);

/*
 * Insert a new key-value pair. The previous value for the key, if any, is
 * replaced by the new value.
 *
 * The "size" bytes from the buffer pointed to by the "val" parameter
 * will be copied into the Kernel Key-Value store.
 *
 * If the value is a string, make sure that size == strlen(val) + 1,
 * so that the null character is stored as part of the value.
 *
 * Returns 0 on success.
 * Returns -1 on failure, with errno set accordingly.
 * The flags parameter is currently unused.
 *
 * The result of calling kkv_put() before initializing the Kernel
 * Key-Value store is undefined.
 */
int kkv_put(uint32_t key, void *val, size_t size, int flags);

#define KKV_NONBLOCK 0
#define KKV_BLOCK 1

#endif
