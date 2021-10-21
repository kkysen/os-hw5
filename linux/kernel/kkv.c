#include <linux/syscalls.h>
#include <linux/printk.h>

long (*kkv_init_ptr)(int flags) = NULL;
EXPORT_SYMBOL(kkv_init_ptr);

long (*kkv_destroy_ptr)(int flags) = NULL;
EXPORT_SYMBOL(kkv_destroy_ptr);

long (*kkv_put_ptr)(uint32_t key, void *val, size_t size, int flags) = NULL;
EXPORT_SYMBOL(kkv_put_ptr);

long (*kkv_get_ptr)(uint32_t key, void *val, size_t size, int flags) = NULL;
EXPORT_SYMBOL(kkv_get_ptr);

SYSCALL_DEFINE1(kkv_init, int, flags)
{
	if (kkv_init_ptr)
		return kkv_init_ptr(flags);

	pr_err("fridge module not running. init exiting.\n");
	return -ENOSYS;
}

SYSCALL_DEFINE1(kkv_destroy, int, flags)
{
	if (kkv_destroy_ptr)
		return kkv_destroy_ptr(flags);

	pr_err("fridge module not running. destroy exiting.\n");
	return -ENOSYS;
}

SYSCALL_DEFINE4(kkv_put, uint32_t, key, void *, val, size_t, size, int, flags)
{
	if (kkv_put_ptr)
		return kkv_put_ptr(key, val, size, flags);

	pr_err("fridge module not running. put exiting.\n");
	return -ENOSYS;
}

SYSCALL_DEFINE4(kkv_get, uint32_t, key, void *, val, size_t, size, int, flags)
{
	if (kkv_get_ptr)
		return kkv_get_ptr(key, val, size, flags);

	pr_err("fridge module not running. get exiting.\n");
	return -ENOSYS;
}
