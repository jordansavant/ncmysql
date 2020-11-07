#ifndef UI_H
#define UI_H

#define TCOLOR_NORMAL	2
#define TCOLOR_RED	3
#define TCOLOR_BLACK	4
#define TCOLOR_CYAN	5
#define TCOLOR_YELLOW	6
#define TCOLOR_PURPLE   9

void ui_setup();
void ui_box_color(WINDOW* win, int colorpair);
void ui_box(WINDOW* win);
void ui_anchor_ur(WINDOW* win, int rows, int cols);
void ui_anchor_ul(WINDOW *win, int rows, int cols);
void ui_anchor_br(WINDOW *win, int rows, int cols);
void ui_anchor_bl(WINDOW *win, int rows, int cols, int yoff, int xoff);
void ui_anchor_center(WINDOW *win, int rows, int cols, int yoff, int xoff);

#endif
