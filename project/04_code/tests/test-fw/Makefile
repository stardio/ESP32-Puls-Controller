# Host build of tfw_logic tests: make -C project/04_code/tests/test-fw
L = ../../test-fw/components/tfw_logic
test_logic: test_logic.c $(L)/tfw_core.c $(L)/tfw_regwin.c $(L)/tfw_seq.c $(L)/tfw_mb.c
	gcc -std=c11 -Wall -Wextra -O1 -I$(L)/include -o $@ $^ -lm
run: test_logic
	./test_logic
clean:
	rm -f test_logic
