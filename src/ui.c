#include <ncurses.h>
#include "ui.h"

void ui_setup()
{
	init_pair(COLOR_WHITE_BLACK,	COLOR_WHITE,	COLOR_BLACK);
	init_pair(COLOR_RED_BLACK,		COLOR_RED,		COLOR_BLACK);
	init_pair(COLOR_BLACK_BLACK,	COLOR_BLACK,	COLOR_BLACK);
	init_pair(COLOR_CYAN_BLACK,		COLOR_CYAN,		COLOR_BLACK);
	init_pair(COLOR_YELLOW_BLACK,	COLOR_YELLOW,	COLOR_BLACK);
	init_pair(COLOR_MAGENTA_BLACK,	COLOR_MAGENTA,	COLOR_BLACK);
	init_pair(COLOR_BLUE_BLACK,		COLOR_BLUE,		COLOR_BLACK);

	init_pair(COLOR_BLUE_WHITE,	COLOR_BLUE,		COLOR_WHITE);
	init_pair(COLOR_WHITE_BLUE,	COLOR_WHITE,	COLOR_BLUE);
	init_pair(COLOR_BLACK_CYAN,	COLOR_BLACK,	COLOR_CYAN);
	init_pair(COLOR_WHITE_RED,	COLOR_WHITE,	COLOR_RED);
	init_pair(COLOR_YELLOW_RED,	COLOR_YELLOW,	COLOR_RED);
	init_pair(COLOR_CYAN_BLUE,	COLOR_CYAN,		COLOR_BLUE);
	init_pair(COLOR_BLACK_WHITE,COLOR_BLACK,	COLOR_WHITE);
}

WINDOW* ui_new_center_win(int offset_row, int offset_col, int rows, int cols)
{
	int y, x, indent;
	getmaxyx(stdscr, y, x);
	indent = (x - cols) / 2 + offset_col;
	return newwin(rows, cols, offset_row, indent);
}

void ui_box_color(WINDOW* win, int colorpair)
{
	box(win, ACS_VLINE, ACS_HLINE);
	//box(win, ':', '-');
	//wborder(win, '|', '|', '-', '-', '+', '+', '+', '+');
	//wattrset(win, COLOR_PAIR(colorpair));
	//wborder(win, '|', '|', '=', '=', '@', '@', '@', '@');
	//wattrset(win, COLOR_PAIR(COLOR_WHITE_BLACK));
}

void ui_box(WINDOW* win)
{
	box(win, ACS_VLINE, ACS_HLINE);
	//ui_box_color(win, COLOR_MAGENTA_BLACK);
}

void ui_anchor_ur(WINDOW* win, int rows, int cols)
{
	int y, x;
	getmaxyx(stdscr, y, x);
	wresize(win, rows, cols);
	mvwin(win, 0, x - cols);
}

void ui_anchor_ul(WINDOW *win, int rows, int cols)
{
	wresize(win, rows, cols);
	mvwin(win, 0, 0);
}

void ui_anchor_br(WINDOW *win, int rows, int cols)
{
	int y, x;
	getmaxyx(stdscr, y, x);
	wresize(win, rows, cols);
	mvwin(win, y - rows, x - cols);
}

void ui_anchor_bl(WINDOW *win, int rows, int cols, int yoff, int xoff)
{
	int y, x;
	getmaxyx(stdscr, y, x);
	wresize(win, rows, cols);
	mvwin(win, y - rows + yoff, xoff);
}

void ui_anchor_center(WINDOW *win, int rows, int cols, int yoff, int xoff)
{
	int y, x;
	getmaxyx(stdscr, y, x);
	int oy = (y - rows) / 2 + yoff;
	int ox = (x - cols) / 2 + xoff;
	wresize(win, rows, cols);
	mvwin(win, oy, ox);
}

