#!/usr/bin/env python3

from functools import wraps
from typing import Callable
from multiprocessing import Pool
import sys
from pathlib import Path
from datetime import datetime

sys.path.append(Path(__file__).parent.parent.__str__())

import kkv

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


def entry_allocation_test(i: int):
    key = i
    kkv.sys_put(key, None, 0, 0)
    kkv.sys_get(key, None, 0, 0)


@benchmark
def entry_allocation_tests():
    kkv.init()
    with Pool(processes=100) as pool:
        pool.map(entry_allocation_test, range(100000))
    kkv.destroy()


def main():
    entry_allocation_tests()


if __name__ == "__main__":
    main()
