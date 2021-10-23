MAKE_DIR := /lib/modules/$(shell uname -r)/build

obj-m += fridge.o

all:
	$(MAKE) -C $(MAKE_DIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(MAKE_DIR) M=$(PWD) clean
