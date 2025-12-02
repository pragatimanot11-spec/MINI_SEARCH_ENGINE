CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -O2
OBJ = main.o indexer.o search.o

search_engine: $(OBJ)
	$(CC) $(CFLAGS) -o search_engine $(OBJ) -lm

main.o: main.c indexer.h
	$(CC) $(CFLAGS) -c main.c

indexer.o: indexer.c indexer.h
	$(CC) $(CFLAGS) -c indexer.c

search.o: search.c indexer.h
	$(CC) $(CFLAGS) -c search.c

clean:
	rm -f $(OBJ) search_engine
