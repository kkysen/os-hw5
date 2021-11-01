PHONY += default
default:

PHONY += test
test:
	./tests.py

PHONY += clean
clean:
	rm -rf *.pyc __pycache__ ../__pycache__

PHONY += test-all
test-all:
	make -C ../con-ed/
	../con-ed/simple_sequential
	../con-ed/hot_potato 1000
	../con-ed/blocking_simple
	../con-ed/blocking_signal
	../py-in-fridge/simple_as.py
	../FireFerrises-p1-test/tests.py
	../FireFerrises-p2-test/tests.py
	../FireFerrises-p3-test/tests.py
	../FireFerrises-p4-test/tests.py

.PHONY: $(PHONY)
