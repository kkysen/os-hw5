#!/usr/bin/env python3

from multiprocessing import Pool
from errno import EPERM, EINTR, ENOENT
import sys
from pathlib import Path
from typing import Callable

sys.path.append(Path(__file__).parent.parent.__str__())

import kkv
from kkv import assert_errno_eq
import os

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

def blocking():
    value = "TEST"
    pid = os.fork()
    if pid > 0:
        kkv.get(key=KEY, len=10, flags = kkv.Flag.Block)
    else:
        kkv.put(key=KEY, value = value)

def destroy_test():
    value = "TEST"
    kkv.init()
    pid = os.fork()
    if pid > 0:
        kkv.get(key=KEY, len=10, flags = kkv.Flag.Block)
    else:
        try:
            kkv.destroy()
        except OSError as e:
            assert_errno_eq(e.errno, EPERM)

def main():
    destroy_test()
    kkv.init()
    try:
        basic_get_test()
        basic_nonblock()
        blocking()
    finally:
        kkv.destroy()


if __name__ == "__main__":
    main()
