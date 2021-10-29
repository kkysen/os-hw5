import errno
from typing import Callable, Mapping, Sequence
import sys
from multiprocessing import Pool
from errno import EINVAL, EPERM
from pathlib import Path

#part 2 tests

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

#Test 1: Concurency

#1a. Call put() and destroy() at the same time

#1a. Call put() and get() same time -- make sure still works
def get_and_put_test(i: int):
    key = i
    value = "hello world\n" * i
    print(f"key = {key}")
    kkv_put(key=key, value=value, flags=KKV_NONBLOCK)
    response = kkv_get(key=key, len=len(value), flags=KKV_NONBLOCK)
    assert response == value

def _1a_parallel_tests():
    kkv_init()
    num_processes = 100
    with Pool(processes=num_processes) as pool:
        pool.map(get_and_put_test, range(num_processes))
    kkv_destroy()

#1b. Call get() and put() with init()
def init_and_put_test(i: int):
    key = i
    value = "hello world\n" * i
    print(f"key = {key}")
    kkv_init()
    kkv_put(key=key, value=value, flags=KKV_NONBLOCK)
    response = kkv_get(key=key, len=len(value), flags=KKV_NONBLOCK)
    assert response == value

def _1b_parallel_tests():
    num_processes = 100
    with Pool(processes=num_processes) as pool:
        pool.map(init_and_put_test, range(num_processes))
    kkv_destroy()

#1c. Call init() and destroy() at same time
def init_and_destroy(i: int):
    kkv_init()
    kkv_destroy()

def _1c_parallel_tests():
    num_processes = 100
    with Pool(processes=num_processes) as pool:
        pool.map(init_and_destroy, range(num_processes))

#1d. Call all four at the same time
def four_same_time(i: int):
    key = i
    value = "hello world\n" * i
    print(f"key = {key}")
    kkv_init()
    kkv_put(key=key, value=value, flags=KKV_NONBLOCK)
    response = kkv_get(key=key, len=len(value), flags=KKV_NONBLOCK)
    kkv_destroy()
    assert response == value

def _1d_parallel_tests():
    num_processes = 100
    with Pool(processes=num_processes) as pool:
        pool.map(four_same_time, range(num_processes))

#Test 2: Call destroy() before init
def destroy_before_init():
    try:
        kkv_destroy()
    except OSError as e:
        assert_errno_eq(e.errno, EPERM)

#Test 3: Call put() before init
def put_before_init():
    try:
        kkv_put(KEY, "TESTING", KKV_NONBLOCK)
    except OSError as e:
        assert_errno_eq(e.errno, EPERM)

#Test 4: Call get() before init
def get_before_init():
    try:
        kkv_get(KEY, 30, KKV_NONBLOCK)
    except OSError as e:
        assert_errno_eq(e.errno, EPERM)

#Test 5: Call init() twice
def init_twice():
    kkv_init()
    try:
        kkv_init()
    except OSError as e:
        assert_errno_eq(e.errno, EPERM)
        kkv_destroy()

#Test 6: Call get() after destroy
def get_after_destroy():
    kkv_init()
    kkv_put(KEY, "TESTING", KKV_NONBLOCK)
    kkv_destroy()
    try:
        kkv_get(KEY, 30, KKV_NONBLOCK)
    except OSError as e:
        assert_errno_eq(e.errno, EPERM)

#Test 7: Call put() after destroy
def put_after_destroy():
    kkv_init()
    kkv_destroy()
    try:
        kkv_put(KEY, "TESTING", KKV_NONBLOCK)
    except OSError as e:
        assert_errno_eq(e.errno, EPERM)

#Test 8: Call destory() twice
def destroy_twice():
    kkv_init()
    kkv_destroy()
    try:
        kkv_destroy()
    except OSError as e:
        assert_errno_eq(e.errno, EPERM)

#Calling everything, main function
def main():
    #First EPERM tests
    try:
        destroy_before_init()
        get_before_init()
        put_before_init
        init_twice()
        get_after_destroy()
        put_after_destroy()
        destroy_twice()
    except:
        print("One of the EPERM tests :( ")
    print("EPERM tests completed.")

    t0 = time.clock()
    try:
        _1a_parallel_tests()
    except:
        "1a failed"
    t1 = time.clock()
    print("1a succeeded, total time =", t1-t0)
    t0 = time.clock()
    try:
        _1b_parallel_tests()
    except:
        "1b failed"
    t1 = time.clock()
    print("1b succeeded, total time =", t1-t0)
    t0 = time.clock()
    try:
        _1c_parallel_tests()
    except:
        "1c failed"
    t1 = time.clock()
    print("1c succeeded, total time =", t1-t0)
    t0 = time.clock()
    try:
        _1d_parallel_tests()
    except:
        "1d failed"
    t1 = time.clock()
    print("1d succeeded, total time =", t1-t0)

if __name__ == "__main__":
    main()