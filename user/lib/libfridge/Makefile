# I know this is a long Makefile, but at least it's not black magic like
# everyone's favorite build system, the GNU Build System.
#
# We don't have ldconfig but that's okay because there's no point creating a
# dynamically linked library from this simple syscall wrapper.

CC=gcc
CFLAGS=-g -Wall -Werror
LDFLAGS=
LDLIBS=
AR=ar
ARFLAGS=rvU

FRIDGE_DEST?=/usr/local
DEST_LIB=$(FRIDGE_DEST)/lib
DEST_INCLUDE=$(FRIDGE_DEST)/include

headers=fridge.h
sources=libfridge.c
objects=$(sources:.c=.o)
lib=libfridge.a

PHONY+=default
default: build

PHONY+=build
build: $(lib)

$(lib): $(lib)($(objects))
$(objects): $(headers)

PHONY+=install
install: install_lib install_headers

_PHONY+=install_lib
install_lib: $(lib)
	mkdir -p $(DEST_LIB)
	cp $^ $(DEST_LIB)

_PHONY+=install_headers
install_headers: $(headers)
	mkdir -p $(DEST_INCLUDE)
	cp $^ $(DEST_INCLUDE)

PHONY+=uninstall
uninstall: uninstall_lib uninstall_headers

_PHONY+=uninstall_lib
uninstall_lib:
	rm -f $(foreach l, $(lib),$(DEST_LIB)/$(l))

_PHONY+=uninstall_headers
uninstall_headers:
	rm -f $(foreach h, $(headers),$(DEST_INCLUDE)/$(h))

PHONY+=clean
clean:
	rm -rf $(objects) $(lib)

PHONY+=help
help:
	@echo available targets: $(PHONY)

.PHONY: $(PHONY) $(_PHONY)
