import errno
from typing import Mapping, Tuple, Type
from enum import IntFlag
from ctypes import (
    _SimpleCData,
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


class Flag(IntFlag):
    NonBlock = 0
    Block = 1


ENCODING = 'UTF-8'


def wrap_syscall(name: str, syscall_num: int, arg_types: Tuple[Type[_SimpleCData], ...], return_type: Type[_SimpleCData]):
    syscall = CDLL(name=None, use_errno=True).syscall
    syscall.restype = return_type
    syscall.argtypes = arg_types

    def wrapper(*args) -> None:
        ret_val = syscall(syscall_num, *args)
        if ret_val == -1:
            raise OSError(get_errno(), name)
        return ret_val

    return wrapper


sys_init = wrap_syscall(name="kkv_init", syscall_num=501,
                        arg_types=(c_long, c_int), return_type=c_int)
sys_destroy = wrap_syscall(name="kkv_destroy", syscall_num=502, arg_types=(
    c_long, c_int), return_type=c_int)
sys_put = wrap_syscall(name="kkv_put", syscall_num=503, arg_types=(
    c_long, c_uint, c_void_p, c_size_t, c_int), return_type=c_int)
sys_get = wrap_syscall(name="kkv_get", syscall_num=504, arg_types=(
    c_long, c_uint, c_void_p, c_size_t, c_int), return_type=c_int)


def init(flags: Flag = Flag.NonBlock):
    sys_init(flags.value)


def destroy(flags: Flag = Flag.NonBlock) -> int:
    return sys_destroy(flags.value)


def put(key: int, value: str, flags: Flag = Flag.NonBlock):
    buf = create_string_buffer(value.encode(ENCODING))
    sys_put(key, buf, sizeof(buf), flags.value)


def get(key: int, len: int, flags: Flag = Flag.NonBlock) -> str:
    buf = create_string_buffer(len)
    sys_get(key, buf, sizeof(buf), flags.value)
    return buf.value.decode(ENCODING)


error_name_map: Mapping[int, str] = {
    value: name for name, value in errno.__dict__.items() if name.startswith("E")}


def assert_errno_eq(actual: int, expected: int):
    assert actual == expected, f"{error_name_map[actual]} != {error_name_map[expected]}"
