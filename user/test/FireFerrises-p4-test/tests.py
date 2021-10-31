#!/usr/bin/env python3

from multiprocessing import Pool
from errno import EPERM
import sys
from pathlib import Path

sys.path.append(Path(__file__).parent.parent.__str__())

import kkv
from kkv import assert_errno_eq

def main():
    pass


if __name__ == "__main__":
    main()
