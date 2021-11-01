#!/usr/bin/env python3

from multiprocessing import Pool
from errno import EPERM, EINTR, ENOENT
import sys
from pathlib import Path
from typing import Callable

sys.path.append(Path(__file__).parent.parent.__str__())

import kkv
from kkv import assert_errno_eq

KEY = 1

def basic_get_test():
    value = "TEST"
    response = kkv.get(key=KEY, len=len(value), flags=kkv.Flag.Block)
    print(f"get {response}")
    kkv.put(key=KEY, value=value)
    response = kkv.get(key=KEY, len=len(value))
    assert response == value

def basic_nonblock():
    value = "TEST"
    try:
        _response = kkv.get(key=KEY, len=len(value), flags=kkv.Flag.NonBlock)
    except OSError as e:
        assert_errno_eq(e.errno, ENOENT)

def blocking_test(i: int):
    #Using get and put in any order
    key = KEY
    value = "TESTING"
    kkv.put(key=key, value=value)
    print(f"{i}: put {value}")
    response = kkv.get(key=key, len=len(value), flags=kkv.Flag.Block)
    print(f"{i}: get {response}")
    assert response == value

def parallel_tests(num_procs: int, num_reps: int, f: Callable[[int], None]):
    with Pool(processes=num_procs) as pool:
        pool.map(f, range(num_reps))

def main():
    kkv.init()
    try:
        basic_get_test()
        basic_nonblock()
        parallel_tests(num_procs=10, num_reps=10, f=blocking_test)
    finally:
        kkv.destroy()


if __name__ == "__main__":
    main()
