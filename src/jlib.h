#ifndef JLIB_H
#define JLIB_H

#include <stdbool.h>
#include <ctype.h>
#include <stddef.h>
#include <ncurses.h>
#include <mysql/mysql.h>

#define KEY_RETURN	10
#define KEY_ESC		27
#define KEY_SPACE	32
#define KEY_TAB		9
#define KEY_DELETE  127

#define KEY_ctrl_e	5
#define KEY_ctrl_x	24

#define KEY_a		97
#define KEY_b		98
#define KEY_c		99
#define KEY_d		100
#define KEY_e		101
#define KEY_f		102
#define KEY_g		103
#define KEY_h		104
#define KEY_i		105
#define KEY_j		106
#define KEY_k		107
#define KEY_l		108
#define KEY_m		109
#define KEY_n		110
#define KEY_o		111
#define KEY_p		112
#define KEY_q		113
#define KEY_r		114
#define KEY_s		115
#define KEY_t		116
#define KEY_u		117
#define KEY_v		118
#define KEY_w		119
#define KEY_x		120
#define KEY_y		121
#define KEY_z		122

#define KEY_0		48
#define KEY_1		49
#define KEY_2		50
#define KEY_3		51
#define KEY_4		52
#define KEY_5		53
#define KEY_6		54
#define KEY_7		55
#define KEY_8		56
#define KEY_9		57

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
#define COLOR_BLACK_GREEN	22
#define COLOR_WHITE_GREEN	23

// LOG HELPERS
void xlogopen(const char *location, char *mode);
void xlogclose();
void xlog(const char *msg);
void xlogf(const char *format, ...);

// SYSTEM FUNCTIONS
bool sysexists(const char *program);
int syscode(const char *cmd);

// MATH FUNCTIONS
int maxi(int a, int b);
int mini(int a, int b);
int clampi(int v, int min, int max);

// STRING FUNCTIONS
int strchrplc(char *str, char orig, char rep);
void strstripspaces(char* str);
void strflat(char *str);
void strfill(char *string, int size, char c);
void strclr(char *string, int size);
size_t strtrim(char *out, size_t len, const char *str, bool trimlead, bool trimtrail);
void strsplit(const char *text, char splitter, int m, int n, char words[m][n], int *wordlen);
void strlines(int m, int n, char words[m][n], int sentence_size, int o, int p, char lines[o][p], int *linelen);
void wordwrap(const char *text, int size, void (*on_line)(const char *line));
char* scantok(char *line, int field_pos, char delim);

// NCURSES FUNCTIONS
void nc_text_editor_pad(WINDOW *pad, char *buffer, int buffer_len, bool tab_exits, int pad_y, int pad_x, int scr_y, int scr_x, int scr_y_end, int src_x_end);
void nc_text_editor_win(WINDOW *win, char *buffer, int buffer_len, bool tab_exits);
void nc_text_editor(WINDOW *window, char *buffer, int buffer_len, bool tab_exits, bool is_pad, int pad_y, int pad_x, int scr_y, int scr_x, int scr_y_end, int src_x_end);
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
void ui_bgline(WINDOW *win, int line, int cpair);
void ui_bgwin(WINDOW *win, int cpair);

// MYSQL FUNCTIONS
MYSQL_RES* db_queryf(MYSQL *con, int *num_fields, int *num_rows, int *num_affect_rows, int *errcode, char *format, ...);
MYSQL_RES* db_query(MYSQL *con, char *query, int *num_fields, int *num_rows, int *num_affect_rows, int *errcode);
bool db_select(MYSQL *con, char *db);
void db_get_db(MYSQL* con, char *buff, int len);
bool db_get_primary_key(MYSQL *con, char *table, char *buffer, int len);
int col_size(MYSQL_RES* result, int index);

#endif
