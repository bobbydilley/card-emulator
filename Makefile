.PHONY = default

BUILD_DIR = build
BUILD_DAEMON = cardd
BUILD_CLIENT = cardctl
SRC = src

default: $(SRC)/cardd.c $(SRC)/cardctl.c $(SRC)/common.h
	mkdir -p $(BUILD_DIR)
	gcc $(SRC)/cardd.c -o $(BUILD_DIR)/$(BUILD_DAEMON)
	gcc $(SRC)/cardctl.c -o $(BUILD_DIR)/$(BUILD_CLIENT)

clean:
	rm -r $(BUILD_DIR)
