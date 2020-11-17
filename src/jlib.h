#ifndef JLIB_H
#define JLIB_H

#include <stdbool.h>
#include <ctype.h>
#include <stddef.h>
#include <ncurses.h>
#include <mysql/mysql.h>

#define COLOR_WHITE_BLACK	2
#define COLOR_RED_BLACK		3
#define COLOR_BLACK_BLACK	4
#define COLOR_CYAN_BLACK	5
#define COLOR_YELLOW_BLACK	6
#define COLOR_MAGENTA_BLACK	9
#define COLOR_BLUE_BLACK	10

#define COLOR_BLUE_WHITE	11
#define COLOR_WHITE_BLUE	12
#define COLOR_BLACK_CYAN	13
#define COLOR_WHITE_RED		14
#define COLOR_YELLOW_RED	15
#define COLOR_CYAN_BLUE		16
#define COLOR_BLACK_WHITE	17
#define COLOR_BLACK_BLUE	18
#define COLOR_BLACK_YELLOW	19
#define COLOR_BLACK_MAGENTA	20
#define COLOR_BLACK_RED		21

// MATH FUNCTIONS
int maxi(int a, int b);
int mini(int a, int b);
int clampi(int v, int min, int max);

// STRING FUNCTIONS
int strchrplc(char *str, char orig, char rep);
void strfill(char *string, int size, char c);
void strclr(char *string, int size);
size_t strtrim(char *out, size_t len, const char *str, bool trimlead, bool trimtrail);
void strsplit(const char *text, char splitter, int m, int n, char words[m][n], int *wordlen);
void strlines(int m, int n, char words[m][n], int sentence_size, int o, int p, char lines[o][p], int *linelen);
void wordwrap(const char *text, int size, void (*on_line)(const char *line));

// NCURSES FUNCTIONS
int nc_strtrimlen(chtype *buff, int size);
int nc_cutline(WINDOW* win, chtype *buff, int startpos, int len);
void nc_paste(WINDOW* win, chtype *buff); // expects null terminated buff
int nc_mveol(WINDOW *win);

// NCURSES UI FUNCTIONS
void ui_setup();
WINDOW* ui_new_center_win(int offset_row, int offset_col, int rows, int cols);
void ui_center_win(WINDOW* win, int offset_row, int offset_col, int rows, int cols);
void ui_clear_win(WINDOW *win);
void ui_clear_row(WINDOW *win, int row);
void ui_color_row(WINDOW *win, int colorpair);
void ui_box_color(WINDOW* win, int colorpair);
void ui_box(WINDOW* win);
void ui_anchor_ur(WINDOW* win, int rows, int cols);
void ui_anchor_ul(WINDOW *win, int rows, int cols);
void ui_anchor_br(WINDOW *win, int rows, int cols);
void ui_anchor_bl(WINDOW *win, int rows, int cols, int yoff, int xoff);
void ui_anchor_center(WINDOW *win, int rows, int cols, int yoff, int xoff);

// MYSQL FUNCTIONS
void die(const char *msg); // must be defined in consuming code
MYSQL_RES* con_select(MYSQL *con, int *num_fields, int *num_rows);
MYSQL_RES* db_query(MYSQL *con, char *query, int *num_fields, int *num_rows, int *num_affect_rows, int *errcode);
void db_select(MYSQL *con, char *db);
int col_size(MYSQL_RES* result, int index);

#endif
