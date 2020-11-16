#!/bin/bash

#gcc src/main.c src/sqlops.c src/ui.c -lmysqlclient -lncurses -lmenu -lm -o bin/a.out
gcc -I/usr/local/opt/ncurses/include -L/usr/local/opt/ncurses/lib src/main.c src/sqlops.c src/ui.c -o bin/a.out -lmysqlclient -lmenu -lncurses -lm

