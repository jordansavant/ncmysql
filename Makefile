# http://www.cs.colby.edu/maxwell/courses/tutorials/maketutor/
CC=gcc
CFLAGS=-I.
BUILD = src/main.c src/sqlops.c src/ui.c
LIBS = -lmysqlclient -lncurses -lmenu -lm
DEPS = src/ui.h src/pass.h src/sqlops.h

%.o: %.c $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)

make: $(BUILD) $(DEPS)
	gcc $(BUILD) $(LIBS) $(CFLAGS)

run: $(BUILD) $(DEPS)
	gcc $(BUILD) $(LIBS) $(CFLAGS) -o bin/main.out && ./bin/main.out $(ARGS)

valgrind: $(BUILD) $(DEPS)
	gcc -g -O0 $(BUILD) $(LIBS) $(CFLAGS) -o bin/main.out && /usr/bin/valgrind --leak-check=full --show-leak-kinds=all --log-file=logs/valgrind --suppressions=valgrind.suppression ./bin/main.out $(ARGS)

valgrind-full: $(BUILD) $(DEPS)
	gcc -g -O0 $(BUILD) $(LIBS) $(CFLAGS) -o bin/main.out && /usr/bin/valgrind --leak-check=full --show-leak-kinds=all ./bin/main.out $(ARGS)
