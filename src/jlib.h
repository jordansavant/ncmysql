#ifndef JLIB_H
#define JLIB_H

#include <stdbool.h>
#include <ctype.h>
#include <stddef.h>

#include <ncurses.h>

// STRING FUNCTIONS
int j_strchrplc(char *str, char orig, char rep);
void j_strfill(char *string, int size, char c);
void j_strclr(char *string, int size);
size_t j_strtrim(char *out, size_t len, const char *str, bool trimlead, bool trimtrail);

// NCURSES FUNCTIONS
int nc_strtrimlen(chtype *buff, int size);
int nc_cutline(WINDOW* win, chtype *buff, int startpos, int len);
// expects null terminated buff
void nc_paste(WINDOW* win, chtype *buff);
int nc_mveol(WINDOW *win);

#endif
