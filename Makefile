CC = gcc
CFLAGS = -Wall -Wextra -Wpedantic $(shell pkg-config --cflags gtk+-3.0)
LIBS = $(shell pkg-config --libs gtk+-3.0)
SRC = src/railway_reservation_v4_responsive_no_top_bar.c
TARGET = railway_v4

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET) $(LIBS)

run: $(TARGET)
	env -i HOME="$$HOME" PATH="/usr/bin:/bin" DISPLAY="$$DISPLAY" \
	XAUTHORITY="$$XAUTHORITY" XDG_RUNTIME_DIR="$$XDG_RUNTIME_DIR" \
	XDG_DATA_DIRS="/usr/local/share:/usr/share" \
	XDG_CONFIG_DIRS="/etc/xdg" ./$(TARGET)

clean:
	rm -f $(TARGET)

.PHONY: all run clean
