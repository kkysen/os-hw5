# Makefile for con ed test suite

CC = gcc
CFLAGS = -Wall -O2
LDFLAGS =
LDLIBS = -lfridge -pthread

TESTS = simple_sequential hot_potato blocking_simple blocking_signal
TEST_OBJS = $(foreach exe, $(TESTS), $(exe).o)

COMMON_SRC = con_ed_util.c
COMMON_OBJ = $(COMMON_SRC:.c=.o)

HEADER = con_ed.h

ifeq "$(VERBOSE)" "1"
CFLAGS += -DVERBOSE
$(info compiling with VERBOSE)
$(shell touch $(HEADER))
endif

ifeq "$(VERBOSE)" "0"
$(info compiling without VERBOSE)
$(shell touch $(HEADER))
endif


PHONY += default
default: compile

PHONY += compile
compile: $(TESTS)

$(TESTS): $(COMMON_OBJ)

$(TEST_OBJS) $(COMMON_OBJ): $(HEADER)

PHONY += test
test: $(foreach exe, $(TESTS), $(exe).test)

TESTS_TARGS += $(foreach exe, $(TESTS), $(exe).test)
%.test: %
	./$<

PHONY += clean
clean:
	rm -rf $(TESTS) *.o core*

PHONY += distclean
distclean: clean

PHONY += help
help:
	@echo "Available targets:"
	@echo "   " $(PHONY)
	@echo
	@echo "Available tests (make <test>.test):"
	@echo "   " $(TESTS)
	@echo
	@echo "To run a test, run:"
	@echo "    make <test>.test"
	@echo

.PHONY = $(PHONY) $(TESTS_TARGS)
