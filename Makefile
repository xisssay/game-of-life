CC      = gcc
CFLAGS  = -O2 -Wall
TARGET  = life

$(TARGET): life.c
	$(CC) $(CFLAGS) -o $(TARGET) life.c

clean:
	rm -f $(TARGET)
