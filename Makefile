.PHONY = default

BUILD = card-emulator

default: card-emulator.c
	gcc $^ -o $(BUILD)

clean:
	rm $(BUILD)
