#!/usr/bin/env python3

from typing import Callable, Sequence
from multiprocessing import Pool
from errno import EINVAL, ENOENT
import sys
from pathlib import Path

sys.path.append(Path(__file__).parent.parent.__str__())

from kkv import assert_errno_eq
import kkv

KEY = 1


def unknown_flags_test():
    key = KEY
    flags = (kkv.Flag.Block, kkv.Flag(-1), kkv.Flag(100))
    funcs: Sequence[Callable[[int], None]] = (
        lambda flag: kkv.init(flags=flag),
        lambda flag: kkv.destroy(flags=flag),
        lambda flag: kkv.put(key=KEY, value="", flags=flag),
        lambda flag: kkv.get(key=KEY, len=0, flags=flag),
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
    kkv.put(key=key, value=value)
    # know this is the incorrect size, need it to truncate
    n = 3
    response = kkv.get(key=key, len=n)
    assert response == value[:n]

    # testing with correct length
    kkv.put(key=key, value=value)
    response = kkv.get(key=key, len=len(value) + 1)
    assert response == value

    # testing with asking for more length than we put in
    kkv.put(key=key, value=value)
    response = kkv.get(key=key, len=30)


def get_and_put_separate_key(i: int):
    key = i
    value = "hello world\n" * i
    #print(f"key = {key}")
    kkv.put(key=key, value=value)
    # try:
    response = kkv.get(key=key, len=len(value))
    # except OSError as e:
    #     assert_errno_eq(e.errno, ENOENT)
    #     return
    assert response == value


def get_and_put_test(i: int):
    key = KEY
    value = "TESTING"
    kkv.put(key=key, value=value)
    response = kkv.get(key=key, len=len(value))
    assert response == value


def put_and_get_other():
    kkv.put(key=0, value="hello")
    try:
        kkv.get(key=1, len=len("world"))
    except OSError as e:
        assert_errno_eq(e.errno, ENOENT)


def parallel_tests(num_procs: int, num_reps: int, f: Callable[[int], None]):
    with Pool(processes=num_procs) as pool:
        pool.map(f, range(num_reps))


def put_a_lot(n: int):
    for i in range(n):
        key = i
        value = "hello world\n" * i
        kkv.put(key=key, value=value)

def test_put_a_lot():
    try:
        kkv.init()
        put_a_lot(n=10)
    finally:
        kkv.destroy()

def main():
    # unknown_flags_test()
    test_put_a_lot()
    kkv.init()
    try:
        len_test()
        get_and_put_test(1)
        get_and_put_test(2)
        put_and_get_other()
        parallel_tests(num_procs=17, num_reps=100, f=get_and_put_separate_key)
    finally:
        kkv.destroy()


if __name__ == "__main__":
    main()
