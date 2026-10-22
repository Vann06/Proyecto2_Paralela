CC = mpicc
CFLAGS = -std=c11 -O2 -Wall -Wextra -Wpedantic
LDLIBS = -lcrypto

.PHONY: all test
all: bruteforce_own

bruteforce_own: bruteforce_own.c
	$(CC) $(CFLAGS) $< -o $@ $(LDLIBS)

test: bruteforce_own
	python3 tests/test_workflow.py
