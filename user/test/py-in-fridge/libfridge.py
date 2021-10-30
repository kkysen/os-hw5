import ctypes, os

from ctypes import (
        c_long,
        c_uint,
        c_void_p,
        c_size_t,
        c_int,
        create_string_buffer,
        sizeof,
        CDLL,
        get_errno,
)

KKV_NONBLOCK, KKV_BLOCK = 0, 1

__NR_kkv_init, __NR_kkv_destroy = 501, 502
__NR_kkv_put, __NR_kkv_get = 503, 504

BUF_SIZE = 1024
ENCODING = 'UTF-8'


sys_kkv_init = CDLL(None, use_errno=True).syscall
sys_kkv_init.restype = c_int
sys_kkv_init.argtypes = c_long, c_int

sys_kkv_destroy = CDLL(None, use_errno=True).syscall
sys_kkv_destroy.restype = c_int
sys_kkv_destroy.argtypes = c_long, c_int

sys_kkv_put = CDLL(None, use_errno=True).syscall
sys_kkv_put.restype = c_int
sys_kkv_put.argtypes = c_long, c_uint, c_void_p, c_size_t, c_int

sys_kkv_get = CDLL(None, use_errno=True).syscall
sys_kkv_get.restype = c_int
sys_kkv_get.argtypes = c_long, c_uint, c_void_p, c_size_t, c_int

def kkv_init(flags=0):
    ret = sys_kkv_init(__NR_kkv_init, flags)
    if ret != 0:
        raise OSError(get_errno(), 'kkv_init()')

def kkv_destroy(flags=0):
    ret = sys_kkv_destroy(__NR_kkv_destroy, flags)
    if ret < 0:
        raise OSError(get_errno(), 'kkv_destroy()')

def kkv_put(key, value, flags=0):
    buf = create_string_buffer(value.encode(ENCODING))
    ret = sys_kkv_put(__NR_kkv_put, key, buf, sizeof(buf), flags)
    if ret != 0:
        raise OSError(get_errno(), 'kkv_put()')

def kkv_get(key, flags=KKV_NONBLOCK):
    buf = create_string_buffer(BUF_SIZE)
    ret = sys_kkv_get(__NR_kkv_get, key, buf, BUF_SIZE, flags)
    if ret != 0:
        raise OSError(get_errno(), 'kkv_get()')
    return buf.value.decode(ENCODING)
