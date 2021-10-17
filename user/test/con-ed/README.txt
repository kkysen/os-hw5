Con Ed Test Suite
=================

This test suite hosts a collection of standalone test programs for the Fridge
system calls, written in C.


Directory structure
-------------------

-   `README.txt`: this document. Contains information about organization of
    this test suite.

-   `Makefile`: used to build unit tests. See section on "Make commands" below

-   `con_ed.h`: common header file for all unit tests. `#include`s useful
    standard header files for writing unit tests for the Fridge syscalls.

-   `con_ed_util.c`: utility "library" which contains the implementation of
    useful library functions for the Con Ed test suite.


Make commands
-------------

-   `make`/`make default`/`make compile`: the default rule builds everything

    -   Set the `VERBOSE` variable to `1` or `0` to force everything to be
        compiled with verbose `DEBUG()` statements, e.g.:

            $ make VERBOSE=1

-   `make clean`: this removes all build products, including object files

-   `make distclean`: same as `make clean`

-   `make <TEST-NAME>`: this builds `<TEST-NAME>`

-   `make help`: lists all targets and possible build targets


Writing and adding new tests
----------------------------

1.  Choose creative `<TEST-NAME>`

2.  Create source file, `<TEST-NAME>.c`

3.  Add `<TEST-NAME>` to the `TEST_OBJS` variable in the `Makefile`

4.  Write your test program in `<TEST-NAME>.c`

    -   `#include "fridge.h"` in your test program

    -   Define a test function named `<TEST-NAME>` that serves as the entry
        point for your test case

        -   The test function may take any number of arguments

        -   The test function should assert the correct behavior of the Fridge
            system calls using `assert()`

        -   If the tests were successful, the test function should ensure that
            `kkv_destroy()` was called before returning, just like one should
            ensure that one's fridge door is closed after using it

    -   Make sure to define a `main()` function for your test program, which
        will handle argument parsing etc., before calling your test function:

            RUN_TEST(<TEST-NAME>, <args>...);

5.  Build your program with `make <TEST-NAME>` (or just `make`)

6.  Run using `./<TEST-NAME>`


Using Con Ed utilities
----------------------

-   `DEBUG(fmt, ...)`: only print these statements if `VERBOSE` is defined,
    useful for debugging

-   `char *random_string(size_t max_len)`: allocates a random-size `char` array
    of at most `max_len + 1` elements, populates it with random printable ASCII
    characters, null terminates it, and returns it

-   `void free_string(char *rstring)`: deallocates `rstring`

-   `unsigned int *random_buf(size_t max_len)`: allocates a random-size
    `unsigned int` array of at most `max_len` elements, populates with random
    non-zero integers, and returns it

-   `void free_buf(unsigned int *rbuf)`: deallocates `rbuf`

-   `void random_sleep(useconds_t max_time)`: sleeps for a random amount of
    time, at most `max_time`

-   `CON_ED_SIGNAL`: signal to be handled, by default `SIGUSR1`

-   `int install_signal_handler(void)`: install the signal handler for
    `CON_ED_SIGNAL`

-   `int raise_signal(pid_t pid)`: raise `CON_ED_SIGNAL` to `pid`

-   `die(msg)`: calls `perror(msg)` before `exit()`ing

-   `RUN_TEST(test, ...)`: seeds randomness, then runs `test` with all rest of
    the arguments passed in; includes pretty printing to indicate which test
    ran and that it passed

