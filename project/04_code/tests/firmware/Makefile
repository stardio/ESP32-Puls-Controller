# Host tests of the firmware logic (no ESP-IDF). Usage:  make -C project/04_code/tests/firmware   (build + run all)
FW = ../../firmware/components
INC = -I$(FW)/fw_core/include -I$(FW)/fw_hal/include -I$(FW)/fw_motion/include -I.
CFLAGS = -std=c11 -Wall -Wextra -O1 -g -fno-math-errno $(INC)
CORE = $(FW)/fw_core/fw_profile.c $(FW)/fw_core/fw_stop.c $(FW)/fw_core/fw_params.c $(FW)/fw_core/fw_regmap.c $(FW)/fw_core/fw_unit.c

all: run
test_core: test_core.c fw_test.h $(CORE)
	gcc $(CFLAGS) -o $@ test_core.c $(CORE) -lm
test_regmap: test_regmap.c fw_test.h $(CORE)
	gcc $(CFLAGS) -o $@ test_regmap.c $(CORE) -lm
test_axis: test_axis.c fake_hal.c fake_hal.h fw_test.h $(CORE) $(FW)/fw_motion/fw_axis.c
	gcc $(CFLAGS) -o $@ test_axis.c fake_hal.c $(CORE) $(FW)/fw_motion/fw_axis.c -lm
test_beginner: test_beginner.c fake_hal.c fake_hal.h fw_test.h $(CORE) $(FW)/fw_motion/fw_axis.c
	gcc $(CFLAGS) -o $@ test_beginner.c fake_hal.c $(CORE) $(FW)/fw_motion/fw_axis.c -lm
run: test_core test_regmap test_axis test_beginner
	./test_core && ./test_regmap && ./test_axis && ./test_beginner
pytest:
	python3 -I -m pytest -q test_cfg_syntax.py
clean:
	rm -f test_core test_regmap test_axis test_beginner
.PHONY: all run pytest clean
