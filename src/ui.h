#ifndef UI_H
#define UI_H

#define COLOR_WHITE_BLACK	2
#define COLOR_RED_BLACK		3
#define COLOR_BLACK_BLACK	4
#define COLOR_CYAN_BLACK	5
#define COLOR_YELLOW_BLACK	6
#define COLOR_PURPLE_BLACK	9

#define COLOR_BLUE_WHITE	10
#define COLOR_WHITE_BLUE	11

void ui_setup();
void ui_box_color(WINDOW* win, int colorpair);
void ui_box(WINDOW* win);
void ui_anchor_ur(WINDOW* win, int rows, int cols);
void ui_anchor_ul(WINDOW *win, int rows, int cols);
void ui_anchor_br(WINDOW *win, int rows, int cols);
void ui_anchor_bl(WINDOW *win, int rows, int cols, int yoff, int xoff);
void ui_anchor_center(WINDOW *win, int rows, int cols, int yoff, int xoff);

#endif
