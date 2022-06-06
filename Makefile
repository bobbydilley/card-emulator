.PHONY = default

BUILD_DIR = build
BUILD = card-emulator
SRC = src

default: $(SRC)/card-emulator.c
	mkdir -p $(BUILD_DIR)
	gcc $^ -o $(BUILD_DIR)/$(BUILD)

clean:
	rm -r $(BUILD_DIR)
