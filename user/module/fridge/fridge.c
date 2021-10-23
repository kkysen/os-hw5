/*
 * fridge.c
 *
 * A kernel-level key-value store. Accessed via user-defined
 * system calls. This is the module implementation.
 */

#include <linux/module.h>
#include <linux/printk.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include "fridge_data_structures.h"

#define MODULE_NAME "Fridge"

static struct kkv_ht_bucket buckets[HASH_TABLE_LENGTH];

int kkv_init(int flags){

	struct kkv_ht_bucket *currbucket;
	int i = 0;

	for(; i < HASH_TABLE_LENGTH; i++){
		currbucket = &buckets[i];
		INIT_LIST_HEAD(&currbucket->entries);
		currbucket->count = 0;
		spin_lock_init(&currbucket->lock);
	}

	return 0;
	
}

int kkv_destroy(int flags){

	int i = 0;
	int entries_removed = 0;
	struct kkv_ht_bucket *currbucket;
	struct kkv_ht_entry *e;
	struct kkv_ht_entry *next;

	for(; i < HASH_TABLE_LENGTH; i++){
		currbucket = &buckets[i];

		list_for_each_entry_safe(e, next, &currbucket->entries, entries){
			list_del(&e->entries);
			kfree(e);
			entries_removed++;
		}

	}

	return entries_removed;
}

int kkv_get(uint32_t key, void *val, size_t size, int flags){

	//buffer
	//look for in list 

	//copy into buffer
	return 0;
}
int kkv_put(uint32_t key, void *val, size_t size, int flags){

	int bucket_num = key % HASH_TABLE_LENGTH;
	struct kkv_ht_entry *e;

	list_for_each_entry(e, &buckets[bucket_num].entries, entries){
		if(e->kv_pair.key == key){
			e->kv_pair.val = krealloc(e->kv_pair.val, size, GFP_KERNEL);
			if(!e->kv_pair.val){
				return -ENOMEM;
			}
			if(copy_from_user(e->kv_pair.val, val, size) != 0){
				return -EFAULT;
			}

			e->kv_pair.size = size;
			return 0;
		}
	}

	e = kmalloc(sizeof(*e), GFP_KERNEL);
	if(!e){
		return -ENOMEM;
	}
	if(copy_from_user(e->kv_pair.val, val, size) != 0){
		return -EFAULT;
	}
	e->kv_pair.key = key;
	e->kv_pair.size = size;
	list_add(&e->entries, &buckets[bucket_num].entries);
	return 0;

}

int fridge_init(void)
{
	pr_info("Installing fridge\n");
	return 0;
}

void fridge_exit(void)
{
	pr_info("Removing fridge\n");
}

module_init(fridge_init);
module_exit(fridge_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION(MODULE_NAME);
MODULE_AUTHOR("cs4118");
