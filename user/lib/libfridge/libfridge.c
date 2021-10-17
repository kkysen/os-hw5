#include <sys/syscall.h>
#include "fridge.h"

int kkv_init(int flags)
{
	return syscall(__NR_kkv_init, flags);
}

int kkv_destroy(int flags)
{
	return syscall(__NR_kkv_destroy, flags);
}

int kkv_put(uint32_t key, void *val, size_t size, int flags)
{
	return syscall(__NR_kkv_put, key, val, size, flags);
}

int kkv_get(uint32_t key, void *val, size_t size, int flags)
{
	return syscall(__NR_kkv_get, key, val, size, flags);
}
