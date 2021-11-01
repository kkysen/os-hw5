#!/usr/bin/env python3

from multiprocessing import Pool
from errno import EPERM, EINTR, ENOENT
import sys
from pathlib import Path
import os
import time

sys.path.append(Path(__file__).parent.parent.__str__())

from kkv import assert_errno_eq
import kkv

KEY = 1

def non_blocking_enoent():
    value = "TEST"
    try:
        _response = kkv.get(key=KEY, len=len(value), flags=kkv.Flag.NonBlock)
    except OSError as e:
        assert_errno_eq(e.errno, ENOENT)

def non_blocking():
    value = "TEST"
    kkv.put(key=KEY, value=value)
    response = kkv.get(key=KEY, len=len(value), flags=kkv.Flag.NonBlock)
    assert response == value

def blocking_1():
    value = "TEST"
    pid = os.fork()
    if pid > 0:
        time.sleep(.1)
        kkv.put(key=KEY, value = value)
        os.wait()
    else:
        response = kkv.get(key=KEY, len=10, flags = kkv.Flag.Block)
        assert response == value
        os._exit(status=0)


def blocking_2():
    pass

def blocking_destroy():
    pass


def main():
    kkv.init()
    try:
        non_blocking_enoent()
        non_blocking()
        blocking_1()
        blocking_2()
        blocking_destroy()
    finally:
        kkv.destroy()


if __name__ == "__main__":
    main()
