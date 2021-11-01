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
        kkv.put(key=KEY, value = value)
        os.wait()
    else:
        response = kkv.get(key=KEY, len=10, flags = kkv.Flag.Block)
        assert response == value
        exit()

def main():
    kkv.init()
    try:
        basic_nonblock()
        blocking()
    finally:
        kkv.destroy()


if __name__ == "__main__":
    main()
