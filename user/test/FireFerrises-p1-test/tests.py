import errno
from typing import Callable, Mapping, Sequence
import sys
from multiprocessing import Pool
from errno import EINVAL
from pathlib import Path

sys.path.append(str(Path(__file__).parent.parent / "py-in-fridge"))

from libfridge import (
    kkv_init,
    kkv_put,
    kkv_get,
    kkv_destroy,
    KKV_BLOCK,
    KKV_NONBLOCK,
)

error_name_map: Mapping[int, str] = {
    value: name for name, value in errno.__dict__.items() if name.startswith("E")}


def assert_errno_eq(actual: int, expected: int):
    assert actual == expected, f"{error_name_map[actual]} != {error_name_map[expected]}"


KEY = 1


def unknown_flags_test():
    key = KEY
    flags = (KKV_BLOCK, -1, 100)
    funcs: Sequence[Callable[[int], None]] = (
        lambda flag: kkv_init(flags=flag),
        lambda flag: kkv_destroy(flags=flag),
        lambda flag: kkv_put(key=KEY, value="", flags=flag),
        lambda flag: kkv_get(key=KEY, len=0, flags=flag),
    )

    for flag in flags:
        for func in funcs:
            try:
                func(flag)
            except OSError as e:
                assert_errno_eq(e.errno, EINVAL)


def len_test():
    key = KEY
    value = "TESTINGLENGTH"
    kkv_put(key, value, KKV_NONBLOCK)
    # know this is the incorrect size, need it to truncate
    n = 3
    response = kkv_get(key, n, KKV_NONBLOCK)
    assert response == value[:n]

    # testing with correct length
    kkv_put(key, value, KKV_NONBLOCK)
    response = kkv_get(key, len(value) + 1, KKV_NONBLOCK)
    assert response == value

    # testing with asking for more length than we put in
    kkv_put(key, value, KKV_NONBLOCK)
    response = kkv_get(key, 30, KKV_NONBLOCK)
    assert response == value


def get_and_put_test(i: int):
    key = KEY
    value = "TESTING"
    kkv_put(key=key, value=value, flags=KKV_NONBLOCK)
    response = kkv_get(key=key, len=len(value), flags=KKV_NONBLOCK)
    assert response == value


def get_and_put_parallel_test():
    num_processes = 100
    with Pool(processes=num_processes) as pool:
        pool.map(get_and_put_test, range(num_processes))


def main():
    unknown_flags_test()
    kkv_init()
    try:
        len_test()
        get_and_put_test(1)
        get_and_put_test(2)
        get_and_put_parallel_test()
    finally:
        kkv_destroy()


if __name__ == "__main__":
    main()
