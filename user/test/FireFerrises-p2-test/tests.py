#!/usr/bin/env python3

from multiprocessing import Pool
from errno import ENOENT, EPERM
import sys
from pathlib import Path

sys.path.append(Path(__file__).parent.parent.__str__())

from kkv import assert_errno_eq
import kkv

KEY = 1

# Test 1: Concurency

# 1a. Call put() and destroy() at the same time

# 1a. Call put() and get() same time -- make sure still works


def get_and_put_test(i: int):
    key = i
    value = "hello world\n" * i
    # print(f"key = {key}")
    kkv.put(key=key, value=value)
    response = kkv.get(key=key, len=len(value))
    assert response == value


def _1a_parallel_tests():
    kkv.init()
    num_processes = 100
    with Pool(processes=num_processes) as pool:
        pool.map(get_and_put_test, range(num_processes))
    kkv.destroy()

# 1b. Call get() and put() with init()


def init_and_put_test(i: int):
    key = i
    value = "hello world\n" * i
    # print(f"key = {key}")
    try:
        kkv.init()
        kkv.put(key=key, value=value)
        response = kkv.get(key=key, len=len(value))
        assert response == value
    except OSError as e:
        assert_errno_eq(e.errno, EPERM)


def _1b_parallel_tests():
    num_processes = 100
    with Pool(processes=num_processes) as pool:
        pool.map(init_and_put_test, range(num_processes))
    kkv.destroy()

# 1c. Call init() and destroy() at same time


def init_and_destroy(i: int):
    try:
        kkv.init()
        kkv.destroy()
    except OSError as e:
        assert_errno_eq(e.errno, EPERM)

def _1c_parallel_tests():
    num_processes = 100
    with Pool(processes=num_processes) as pool:
        pool.map(init_and_destroy, range(num_processes))

# 1d. Call all four at the same time


def four_same_time(i: int):
    key = i
    value = "hello world\n" * i
    # print(f"key = {key}")
    try:
        kkv.init()
        kkv.put(key=key, value=value)
        response = kkv.get(key=key, len=len(value))
        kkv.destroy()
        assert response == value
    except OSError as e:
        assert_errno_eq(e.errno, EPERM, ENOENT)


def _1d_parallel_tests():
    num_processes = 100
    with Pool(processes=num_processes) as pool:
        pool.map(four_same_time, range(num_processes))

# Test 2: Call destroy() before init


def destroy_before_init():
    try:
        kkv.destroy()
    except OSError as e:
        assert_errno_eq(e.errno, EPERM)

# Test 3: Call put() before init


def put_before_init():
    try:
        kkv.put(key=KEY, value="TESTING")
    except OSError as e:
        assert_errno_eq(e.errno, EPERM)

# Test 4: Call get() before init


def get_before_init():
    try:
        kkv.get(key=KEY, len=30)
    except OSError as e:
        assert_errno_eq(e.errno, EPERM)

# Test 5: Call init() twice


def init_twice():
    kkv.init()
    try:
        kkv.init()
    except OSError as e:
        assert_errno_eq(e.errno, EPERM)
        kkv.destroy()

# Test 6: Call get() after destroy


def get_after_destroy():
    kkv.init()
    kkv.put(key=KEY, value="TESTING")
    kkv.destroy()
    try:
        kkv.get(key=KEY, len=30)
    except OSError as e:
        assert_errno_eq(e.errno, EPERM)

# Test 7: Call put() after destroy


def put_after_destroy():
    kkv.init()
    kkv.destroy()
    try:
        kkv.put(key=KEY, value="TESTING")
    except OSError as e:
        assert_errno_eq(e.errno, EPERM)

# Test 8: Call destroy() twice


def destroy_twice():
    kkv.init()
    kkv.destroy()
    try:
        kkv.destroy()
    except OSError as e:
        assert_errno_eq(e.errno, EPERM)

# Calling everything, main function


def eperm_tests():
    destroy_before_init()
    get_before_init()
    put_before_init
    init_twice()
    get_after_destroy()
    put_after_destroy()
    destroy_twice()


def main():
    eperm_tests()
    _1a_parallel_tests()
    _1b_parallel_tests()
    _1c_parallel_tests()
    _1d_parallel_tests()


if __name__ == "__main__":
    main()
