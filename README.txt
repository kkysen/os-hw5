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

This part is working.

##### Module

A cache is created on module init and destroyed on module destroy (stored in `struct kkv`).
We replaced our calls to `kmalloc` and `kfree` for each of the entries
with calls to `kmem_cache_alloc` and `kmem_cache_free` using the cache.
This was done to improve the speed and efficiency of the program
because we are able to allocate directly from the pre-allocated cache,
which knows the size of the entry already, instead of needing to
specify the size each time with `kmalloc` from the kernel,
meaning it can't size-optimize it as well.

We also needed to avoid calling `kmem_cache_free(NULL)` (`kfree(NULL)` works).

##### Tests

See part1 for our shared python test code.

We used our tests to benchmark with a larger number of memory allocations.
We used set the user value length to 0 in order to avoid value allocation,
so that we would primarily be looking at the `struct kkv_ht_entry` memory allocations,
and regular `kmalloc` allocations for the values wouldn't hide anything.
Even with entry allocations dominating, though,
sometimes the part2 module would be slightly faster
and sometimes the part3 module would be faster.

We thought this might be because `kmalloc` is probably already optimized
for small allocations like for `struct kkv_ht_entry`, which is only 72 bytes,
so we were guessing that's why there wasn't a major difference in performance.
But we tried adding a `char big[4000]` field to `struct kkv_ht_entry`
so that it's almost as large as a page (4072 vs. 4096),
but that didn't even make any difference.

Thus we are guessing it's either from syscall (context-switch) or locking overhead.
More likely the context-switch, since a whole context-switch
to allocate just 72 bytes is very wasteful.

To run the tests, run `make test` in `user/test/FireFerrises-p3-test/`.
This just times the entry allocation heavy test.
We also tried benchmarking it through `hyperfine` to avoid outliers.


### part4

This part is working.

##### Module

In `kkv_get`, when `KKV_BLOCK` is passed, the code path is similar to
a mix of non-blocking `kkv_get` and `kkv_put`.
We allocate an empty entry before the critical section
in case we need to insert it.
While in the bucket critical section, we either
remove a normal entry (normal get behavior),
find an existing empty entry (which we need to wait on),
or find nothing, in case we insert our new empty entry.
In the non-blocking case, we just do as before.

Then after we've unlocked the bucket but still hold the kkv read lock,
we increment the `q_count` and call `prepare_to_wait` (if blocking).
We need to do this while still in the read lock since the empty entry
is currently in the kkv, and a `kkv_destroy()`
could free it at any moment (if not locked).

Then we unlock the read lock before entering the `schedule` loop.
After `schedule` returns, we first acquire the read lock again
so that we can access the empty entry again.
If we can't, like if `kkv_destroy` was called, then we return with `-EPERM`.

Then we acquire the bucket lock and check if there were pending signals,
in which case we return with `-EINTR`.
We need the bucket lock because we have to
decrement the `q_count` and call `finish_wait` on the empty entry,
which we can only access with the bucket lock since otherwise other threads could, too.
(That's why we check for `EPERM` before `EINTR`,
because we return from `EINTR` if we couldn't get the read lock
(i.e., the kkv wasn't initialized anymore)).

Then if the empty entry is no longer empty (a non-null value),
then we swap it out to our local (on the stack) `kv_pair`,
which was empty again.  This way we get an exclusive reference to the non-empty pair,
while the shared empty entry now is empty again for other blocking gets waiting.

If the entry is still empty, like if another blocking get got it first,
then we `prepare_to_wait` again, unlock the locks, and continue the loop,
scheduling again.

Note that we only remove the empty entry (and free it outside the locks)
if the `q_count` goes to 0, meaning no one else is using it anymore.


Then in `kkv_put`, we just call `wake_up` on the entry's queue if
it has a positive `q_count`, indicating there are blocking gets waiting on it.

When the user size is 0, we use a global zero-length array as the value,
which is different from `NULL`, which we reserve to mean no value,
like when blocking get adds an empty entry.
Thus, we can always differentiate between the two cases.

In `kkv_destroy`, we wake up the queue for any entries
that have a positive `q_count`, which allows them to wake up,
try to acquire the kkv read lock, and then see that
the kkv is no longer initialized and return with `-EPERM`.

##### Tests

See part1 for our shared python test code.
We ran a test to confirm that our non-blocking was working as normal with the blocking added.
We also created a test to get and put with blocking. The test uses fork to work with the put and get and
tests whether the blocking works with get and then it returns when the put value is in the entry
(and able to be/is then removed).

To run the tests, run `make test` in `user/test/FireFerrises-p4-test/`.
