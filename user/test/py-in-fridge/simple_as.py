#!/usr/bin/env python3

#
# A simple sequential test written in Python to demonstrate how to use the
# fridge Python bindings.
#

# flags parameter is optional, default is 0
from libfridge import (
    kkv_init,
    kkv_put,
    kkv_get,
    kkv_destroy,
    KKV_BLOCK,
    KKV_NONBLOCK,
)

def main():
    kkv_init()

    kkv_put(4, 'orange')

    assert(kkv_get(4) == 'orange')

    kkv_put(1, 'apple')
    kkv_put(1, 'banana')
    kkv_put(8, 'milk')

    assert(kkv_get(8) == 'milk')
    assert(kkv_get(1, flags=KKV_NONBLOCK) == 'banana')

    # you can also pass flags=KKV_BLOCK to kkv_get(), but that would hold up the
    # fridge line. We'll leave that to you to figure out. Hint: check out
    # Python's pthread library!

    empty_correct = False
    try:
        kkv_get(1)
    except OSError:
        empty_correct = True

    assert(empty_correct is True)

    kkv_destroy()
    print('Test passed. See? Simple as Py!')

if __name__ == '__main__':
    main()
