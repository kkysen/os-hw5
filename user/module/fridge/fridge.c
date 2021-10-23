/*
 * fridge.c
 *
 * A kernel-level key-value store. Accessed via user-defined
 * system calls. This is the module implementation.
 */

#include <linux/module.h>
#include <linux/printk.h>

#include "fridge_data_structures.h"

#define MODULE_NAME "Fridge"

long kkv_init(int flags);
long kkv_destroy(int flags);
long kkv_put(uint32_t key, void *val, size_t size, int flags);
long kkv_get(uint32_t key, void *val, size_t size, int flags);

extern long (*kkv_init_ptr)(int flags);
extern long (*kkv_destroy_ptr)(int flags);
extern long (*kkv_put_ptr)(uint32_t key, void *val, size_t size, int flags);
extern long (*kkv_get_ptr)(uint32_t key, void *val, size_t size, int flags);

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

long kkv_init(int flags)
{
	return -ENOSYS;
}

long kkv_destroy(int flags)
{
	return -ENOSYS;
}

long kkv_put(uint32_t key, void *val, size_t size, int flags)
{
	return -ENOSYS;
}

long kkv_get(uint32_t key, void *val, size_t size, int flags)
{
	return -ENOSYS;
}
