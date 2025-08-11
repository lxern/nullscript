CC = gcc
TARGET = nullscript
CFLAGS = -Wall -Wextra
SRC = main.c
BUILD_DIR = build
EXECUTABLE = $(BUILD_DIR)/$(TARGET)

all: $(EXECUTABLE)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(EXECUTABLE): $(SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -rf $(BUILD_DIR)

rebuild: clean all
