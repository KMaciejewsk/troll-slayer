CC = mpicc
CFLAGS = -Wall -Wextra -std=c99 -g
TARGET = troll_slayer
SOURCES = main.c troll_slayer.c

all: $(TARGET)

$(TARGET): $(SOURCES)
	$(CC) $(CFLAGS) -o $(TARGET) $(SOURCES)

clean:
	rm -f $(TARGET)

run: $(TARGET)
	mpirun -np 4 ./$(TARGET) 5

run8: $(TARGET)
	mpirun -np 8 ./$(TARGET) 10

.PHONY: all clean run run8