This file should contain:

-	Your name & UNI (or those of all group members for group assignments)
-	Homework assignment number
-	Description for each part

The description should indicate whether your solution for the part is working
or not. You may also want to include anything else you would like to
communicate to the grader, such as extra functionality you implemented or how
you tried to fix your non-working code.

Names:

Khyber Sen, ks3343
Aliza Rabinovitz, ajr2252
Isabelle Arevalo, ia2422

HW: 5

PART 1 -
Code: We set our function pointers at the beginning connecting the system call and the module.
Our init function creates an array of buckets which are initialized with spinlocks and counts equal to 0.
We also init the list head using linux library functions for each bucket. In destroy, for each bucket (we iterate through), we go iterate through this linked list of entries and free all the elements, deleting them as we go (using the safe function).
In put, we figure out which bucket the key should go in by modding it by the number of buckets, and then we add it to the linked list for that bucket if it is not already there (allocating using malloc, iterating through the linked list to replace it if it is already there).
We have to lock the spinlock before changing the linked list value.
In get, we do the opposite, finding the bucket in the same way, iterating through the linked list to find the key, and removing it the same way we did in destroy.

Tests:
We added tests to ensure that the spin lock is accurate between getting and putting the same key into the bucket/linked list and that they do not conflict with each other.
We have another test to make sure that get is working correctly in terms of the size that it is (if size is less than the length of the string, the string should truncate without the null character).
We also have a test to test the flags that we are using in our function and make sure that the errno codes are set appropriately.
