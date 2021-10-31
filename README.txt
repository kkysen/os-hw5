hw5

Khyber Sen, ks3343
Aliza Rabinovitz, ajr2252
Isabelle Arevalo, ia2422

### part1

This part is working.

##### Module

In `fridge_init` and `fridge_exit`, we set and unset the kkv syscall pointers, respectively.

For the kkv, we created a `struct kkv` for the entire store,
which contains a `struct kkv_buckets`, which owns the bucket array (dynamically allocated).
`kkv_init` and `kkv_destroy` init and free this global `struct kkv`
(see the `*_init` and `*_free` functions for most structs),
but without any checks since it's part1.

In `kkv_put`, we made sure to allocate and copy the `kv_pair` fromt he user first,
as well as the possible new entry.
This let us keep any allocations or other potentially blocking calls out of the critical section.
Unfortunately, this means potentially needless allocations for the new entry
if the key already exists, but that's necessary to keep the allocation out of the critical section.
We also use the existing `hash_32` to pre-hash the key before hashing it into a bucket index with `%`.

In `kkv_get`, we do largely the opposite,
except here we do all the frees after the critical section.

##### Tests

We added Python (3) tests in `tests.py` to ensure that the spin lock is accurate
between getting and putting the same key into the bucket/linked list
and that they do not conflict with each other.

We have another test to make sure that get is working correctly in terms of the size that it is
(if size is less than the length of the string, the string should truncate without the null character).

We also have a test to test the flags that we are using in our function
and make sure that the errno codes are set appropriately.

We also have a test that only puts to check if destroy can free everything correctly.

We also used a shared `user/test/kkv.py` instead of `user/test/py-in-fridge/libfridge.py`
because we wanted to modify a few things, such as adding type annotations
and adding a variable length field to `kkv_get` instead of always using the same `BUF_SIZE`.

To run the tests, run `make test` in `user/test/FireFerrises-p1-test/`.


### part2

This part is working.

##### Module

TODO

##### Tests

See part1 for our shared python test code.

TODO

To run the tests, run `make test` in `user/test/FireFerrises-p2-test/`.


### part3

This part is TODO.

##### Module

TODO

##### Tests

See part1 for our shared python test code.

TODO

To run the tests, run `make test` in `user/test/FireFerrises-p3-test/`.


### part4

This part is TODO.

##### Module

TODO

##### Tests

See part1 for our shared python test code.

TODO

To run the tests, run `make test` in `user/test/FireFerrises-p4-test/`.
