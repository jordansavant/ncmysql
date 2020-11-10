#ifndef UI_H
#define UI_H

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

void ui_setup();
WINDOW* ui_new_center_win(int offset_row, int offset_col, int rows, int cols);
void ui_center_win(WINDOW* win, int offset_row, int offset_col, int rows, int cols);
void ui_clear_win(WINDOW *win);
void ui_clear_row(WINDOW *win, int row);
void ui_box_color(WINDOW* win, int colorpair);
void ui_box(WINDOW* win);
void ui_anchor_ur(WINDOW* win, int rows, int cols);
void ui_anchor_ul(WINDOW *win, int rows, int cols);
void ui_anchor_br(WINDOW *win, int rows, int cols);
void ui_anchor_bl(WINDOW *win, int rows, int cols, int yoff, int xoff);
void ui_anchor_center(WINDOW *win, int rows, int cols, int yoff, int xoff);

#endif
