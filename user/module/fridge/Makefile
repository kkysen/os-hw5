MAKE_DIR := /lib/modules/$(shell uname -r)/build

obj-m += fridge.o

CFLAGS_fridge.o += -Wall -Werror -Wextra

KASAN_SANITIZE_fridge.o := $(CONFIG_KASAN)
UBSAN_SANITIZE_fridge.o := $(CONFIG_UBSAN)

all:
	$(MAKE) -C $(MAKE_DIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(MAKE_DIR) M=$(PWD) clean
