#!/usr/bin/env python3

from functools import wraps
from typing import Callable
from multiprocessing import Pool
from errno import EPERM
import sys
from pathlib import Path
from datetime import datetime

sys.path.append(Path(__file__).parent.parent.__str__())

import kkv
from kkv import assert_errno_eq

KEY = 1

# Test 1: Concurency

# 1a. Call put() and destroy() at the same time

# 1a. Call put() and get() same time -- make sure still works


def benchmark(f: Callable) -> Callable:
    @wraps(f)
    def wrapped(*args, **kwargs):
        start = datetime.now()
        try:
            f(*args, **kwargs)
        finally:
            end = datetime.now()
            elapsed = end - start
            print(f"{f.__name__} took {elapsed}")

    return wrapped


def get_and_put_test(i: int):
    key = i
    value = "hello world\n" * i
    # print(f"key = {key}")
    kkv.put(key=key, value=value)
    response = kkv.get(key=key, len=len(value))
    assert response == value


@benchmark
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
    kkv.init()
    kkv.put(key=key, value=value)
    response = kkv.get(key=key, len=len(value))
    assert response == value


@benchmark
def _1b_parallel_tests():
    num_processes = 100
    with Pool(processes=num_processes) as pool:
        pool.map(init_and_put_test, range(num_processes))
    kkv.destroy()

# 1c. Call init() and destroy() at same time


def init_and_destroy(i: int):
    kkv.init()
    kkv.destroy()


@benchmark
def _1c_parallel_tests():
    num_processes = 100
    with Pool(processes=num_processes) as pool:
        pool.map(init_and_destroy, range(num_processes))

# 1d. Call all four at the same time


def four_same_time(i: int):
    key = i
    value = "hello world\n" * i
    # print(f"key = {key}")
    kkv.init()
    kkv.put(key=key, value=value)
    response = kkv.get(key=key, len=len(value))
    kkv.destroy()
    assert response == value


@benchmark
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
