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

To make init and destroy thread-safe, too,
without changing get and put's concurrency,
we used a rwlock to guard an `initialized` field,
which indicates if a new `struct kkv_inner` is initialized or not.
The `struct kkv_inner` contains the buckets,
but could contain other protected fields later (like the cache),
which is why it's refactored out.

Thus, in init and destroy, we acquire a write lock,
since we're writing to the initialized field there,
while we only acquire a read lock in get and put,
since we're only reading the initialized field there
and the data inside the bucket array (not the bucket array itself).
This allows multiple gets and puts (on separate buckets)
to still run concurrently since they can acquire multiple read locks.
Meanwhile, init and destroy have to acquire the exclusive write lock,
so they have exclusive access when they run and can modify the bucket array.

However, just like in get and put in part1, we had to be careful
to avoid (de)allocations in the critical sections in init and destroy.
Thus, in the critical sections we only swap out the `struct kkv_inner`
and set the `initialized` field, all simple `memcpy`s.
We do the actual allocation before and deallocation after.
This does mean sometimes we can waste a buckets allocation in init
if it gets initialized between then and acquiring the write lock.

We refactored the rw-locking into
```
bool kkv_lock(struct kkv *this, bool write, bool expecting_initialized);
```
since we do double-checked locking (to greatly reduce the times we need to lock).

We also converted most fallible functions to use goto for error-handling,
since that made it much easier to keep track of the multiple error states.

##### Tests

See part1 for our shared python test code.

To run the tests, run `make test` in `user/test/FireFerrises-p2-test/`.

We added python tests in `tests.py` to test multiple conditions.
One part of this (tests 2-8) is testing to the EPERM errors.
If calls that should strictly not be happening,
    a get or a put after a destroy,
    multiple inits or multiple destroys,
    or a get or a put before an init,
occur in sequential order, then we return EPERM.
This is testing without the race conditions.

We have also added in the previous get and put test to ensure that
our race conditions still work on this part and that our additions
for part 2 did not mess up the concurrency between get and put.

We also added multiple tests in part2 that test the concurrency between
gets, puts, inits, and destroys. These tests are split up
to add in a different system call each time so
we were more closely ableto identify where the errors may be.
The tests also account for EPERM
(and in the instance of get/destroy, ENOENT) errors,
because although get and put will wait for the lock to be acquired,
they may still acquire the lock before an init or after the destroy,
and that should trigger that response.


### part3

This part is TODO.

##### Module

TODO

##### Tests

See part1 for our shared python test code.

We used our tests to benchmark with a larger number of memory allocations.
We used set the user value length to 0 in order to avoid value allocation,
so that we would primarily be looking at the `struct kkv_ht_entry` memory allocations,
and regular `kmalloc` allocations for the values wouldn't hide anything.
Even with entry allocations dominating, though,
sometimes the part2 module would be slightly faster
and sometimes the part3 module would be faster.
`kmalloc` is probably already optimized for small allocations like for
`struct kkv_ht_entry`, which is only 40 bytes, so we're guessing that's why
there wasn't a major difference in performance.

To run the tests, run `make test` in `user/test/FireFerrises-p3-test/`.
This just times the entry allocation heavy test.
We also tried benchmarking it through `hyperfine` to avoid outliers.


### part4

This part is TODO.

##### Module

TODO

##### Tests

See part1 for our shared python test code.

TODO

To run the tests, run `make test` in `user/test/FireFerrises-p4-test/`.
