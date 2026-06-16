CC = gcc
CFLAGS = -Wall -Wextra -O2

TARGET = lwte
SRC = lwte.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET)

clean:
	rm -f $(TARGET)

install: $(TARGET)
	mkdir -p ~/.local/bin
	cp $(TARGET) ~/.local/bin/

uninstall:
	rm -f ~/.local/bin/$(TARGET)
