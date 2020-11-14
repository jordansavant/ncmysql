#include <mysql/mysql.h>
#include <ncurses.h>
#include <menu.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <signal.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/wait.h>
#include "sqlops.h"
#include "ui.h"
#include "pass.h"

#define ARRAY_SIZE(a) (sizeof(a) / sizeof(a[0]))
#define KEY_RETURN	10
#define KEY_ESC		27
#define KEY_SPACE	32
#define KEY_TAB		9

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

// DB CONNECTION INFORMATION
//
struct Connection {
	char *host;
	char *port;
	int iport;
	char *user;
	char *pass;
	char *ssh_tunnel;
};
#define LOCALHOST "127.0.0.1"

struct Connection app_cons[20];
bool conn_established = false;
bool db_selected = false;
MYSQL *selected_mysql_conn = NULL;

#define QUERY_MAX	4096
char the_query[QUERY_MAX];
MYSQL_RES* the_result = NULL;
int the_num_fields=0, the_num_rows=0, the_aff_rows=0;
bool result_rerender = true;
int result_shift_r=0, result_shift_c=0;

// STATE MACHINES
//
enum APP_STATE {
	APP_STATE_START,
	APP_STATE_ARGS_TO_CONNECTION,
	APP_STATE_LOAD_CONNECTION_FILE,
	APP_STATE_CONNECTION_SELECT,
	APP_STATE_FORK_SSH_TUNNEL,
	APP_STATE_CONNECTION_CREATE,
	APP_STATE_CONNECT,
	APP_STATE_DB_SELECT,
	APP_STATE_DB_SELECT_END,
	APP_STATE_DB_INTERACT,
	APP_STATE_DB_INTERACT_END,
	APP_STATE_DISCONNECT,
	APP_STATE_END,
	APP_STATE_DIE,
};
enum APP_STATE app_state = APP_STATE_START;

enum DB_SELECT_STATE {
	DB_SELECT_STATE_START,
	DB_SELECT_STATE_SELECT_DB,
	DB_SELECT_STATE_END,
};
enum DB_SELECT_STATE db_select_state = DB_SELECT_STATE_START;

enum DB_STATE {
	DB_STATE_START,
	DB_STATE_NOTABLES,
	DB_STATE_INTERACT,
	DB_STATE_END,
};
enum DB_STATE db_state = DB_STATE_START;

enum INTERACT_STATE {
	INTERACT_STATE_TABLE_LIST,
	INTERACT_STATE_QUERY,
	INTERACT_STATE_RESULTS,
};
enum INTERACT_STATE interact_state = INTERACT_STATE_TABLE_LIST;

enum QUERY_STATE {
	QUERY_STATE_COMMAND,
	QUERY_STATE_EDIT,
};
enum QUERY_STATE query_state = QUERY_STATE_COMMAND;

// GLOBAL WINDOWS
//
WINDOW* error_window;
WINDOW* cmd_window;
WINDOW* str_window;
WINDOW* query_window;
WINDOW* result_pad;
#define ERR_WIN_W	48
#define ERR_WIN_H	12
#define QUERY_WIN_H	12

WINDOW* tbl_pad = NULL;
int tbl_index = 0;
int tbl_count = 0;
MYSQL_RES* tbl_result = NULL;
#define TBL_PAD_H		256
#define TBL_PAD_W		256
#define TBL_STR_FMT		"%-256s"
#define TBL_LIST_W		32


// FUNCTIONS
//
int maxi(int a, int b) {
	if (a > b)
		return a;
	return b;
}
int clampi(int v, int min, int max) {
	if (v > max)
		return max;
	else if (v < min)
		return min;
	return v;
}


FILE* _fp;
void xlogopen(char *location, char *mode) {
	_fp = fopen(location, mode);
}
void xlogclose() {
	fclose(_fp);
}
void xlog(char *msg) {
	fprintf(_fp, "%s\n", msg);
	fflush(_fp);
}
void xlogf(char *format, ...) {
	va_list argptr;
	va_start(argptr, format);
	vfprintf(_fp, format, argptr);
	//fprintf(_fp, "\n");
	va_end(argptr);
	fflush(_fp);
}

int charreplace(char *str, char orig, char rep) {
	char *ix = str;
	int n = 0;
	while((ix = strchr(ix, orig)) != NULL) {
		*ix++ = rep;
		n++;
	}
	return n;
}

void strclr(char *string, int size) {
	for (int i = 0; i < size; i++) {
		string[i] = '\0';
	}
}

size_t strtrim(char *out, size_t len, const char *str, bool trimlead, bool trimtrail) {
	if(len == 0)
		return 0;

	const char *end;
	size_t out_size;

	// Trim leading space
	while(isspace((unsigned char)*str)) str++;

	if(*str == 0)  // All spaces?
	{
		*out = 0;
		return 1;
	}

	// Trim trailing space
	end = str + strlen(str) - 1;
	while(end > str && isspace((unsigned char)*end)) end--;
	end++;

	// Set output size to minimum of trimmed string length and buffer size minus 1
	out_size = (end - str) < len-1 ? (end - str) : len-1;

	// Copy trimmed string and add null terminator
	memcpy(out, str, out_size);
	out[out_size] = 0;

	return out_size;
}

int nc_strtrimlen(chtype *buff, int size) {
	int ccount = 0;
	for (int rc=size; rc >= 0; rc--) {
		char character = buff[rc] & A_CHARTEXT;
		if (character < 33 || character > 126)
			ccount++;
		else
			break;
	}
	return ccount;
}

int nc_cutline(WINDOW* win, chtype *buff, int startpos, int len) {
	// get winsize for line lengths
	int maxy, maxx, winy, winx;
	getmaxyx(win, maxy, maxx);
	getyx(win, winy, winx);

	// move cursor to x
	wmove(win, winy, startpos);

	// capture line
	winchstr(win, buff);

	// replace trailing whitespace with null characters up to len
	int untrimmed_len = maxx - startpos;
	int trimcount = nc_strtrimlen(buff, untrimmed_len);
	int linesize = untrimmed_len - trimcount + 1;
	//xlogf("%d,%d args:(start=%d, len=%d) ut=%d tr=%d lsz=%d\n", winy,winx, startpos, len, untrimmed_len, trimcount, linesize);
	if (linesize > 0)
		for (int i=linesize; i<len; i++)
			buff[i] = '\0'; // fill the remainder with null

	wclrtoeol(query_window); // delete remainder of line

	return linesize;
}

// expects null terminated buff
void nc_paste(WINDOW* win, chtype *buff) {
	chtype c;
	int i=0;
	while ((c = buff[i]) != '\0') {
		//xlogf("paste %d %c\n", i, c & A_CHARTEXT);
		waddch(win, c);
		i++;
	}
}

int nc_mveol(WINDOW *win) {
	int maxy, maxx, winy, winx;
	getmaxyx(win, maxy, maxx);
	getyx(win, winy, winx);

	// move to start of line
	wmove(win, winy, 0);

	// get contents of line
	chtype buff[maxx];
	winchstr(win, buff);

	// get end of text pos
	int trimlen = nc_strtrimlen(buff, maxx);
	int endx = maxx - trimlen + 1;
	wmove(win, winy, endx);

	return endx;
}

void dm_splitstr(const char *text, char splitter, int m, int n, char words[m][n], int *wordlen) {
	//this is the mother who likes to speak and use the language of the underground to be able to tell players what they can and cannot do
	// split the text int a bunch of sub strings that represent the words
	int wordcount = 0;
	int spacepos = 0;
	for (int i=0; i < strlen(text) + 1; i++) {
		if (text[i] == splitter || text[i] == '\n' || text[i] == '\0') {
			// copy characters from last space pos to current position
			int wordsize = i - spacepos;
			memcpy(&words[wordcount], &text[spacepos], wordsize);
			words[wordcount][wordsize] = '\0';
			//xlogf("wordcount %d wordsize %d spacepos %d i %d [%s]\n", wordcount, wordsize, spacepos, i, words[wordcount]);
			spacepos = i + 1;
			wordcount++;
		}
		if (text[i] == '\n') {
			// we ran into a newline, we want to split and make the prior contents a word, then also inject another word for newline after it
			// special newline word
			words[wordcount][0] = '\n';
			words[wordcount][1] = '\0';
			wordcount++;
		}
	}
	*wordlen = wordcount;
}
void dm_lines(int m, int n, char words[m][n], int sentence_size, int o, int p, char lines[o][p], int *linelen) {
	// take a collection of words that are null terminated and a sentence size
	// and copy them into the lines buffer
	int cursize = 0;
	int linepos = 0;
	for (int i=0; i < m; i++) {
		char *word = words[i];
		int wordsize = strlen(word);
		//xlogf("compare %d + %d < %d\n", cursize, wordsize, sentence_size);
		if (word[0] == '\n') {
			// its a newline word, move to next line, reset space pos
			cursize = 0;
			linepos++;
		}
		else if (cursize + wordsize < sentence_size) {
			// append word to line
			//xlogf("append word [%s]\n", word);
			memcpy(&lines[linepos][cursize], word, wordsize);
			lines[linepos][cursize + wordsize] = ' ';
			lines[linepos][cursize + wordsize + 1] = '\0';
			cursize += wordsize + 1;
		} else {
			// move to next line, copy the word there
			//xlogf("overflow sentence, start word [%s]\n", word);
			linepos++;
			memcpy(&lines[linepos], word, wordsize);
			lines[linepos][wordsize] = ' ';
			lines[linepos][wordsize + 1] = '\0';
			cursize = wordsize + 1;
		}
	}
	*linelen = linepos + 1;
}
void dm_wordwrap(const char *text, int size, void (*on_line)(const char *line)) {
	int wordlen = 0;
	char words[1056][32];
	dm_splitstr(text, ' ', 1056, 32, words, &wordlen);

	int linelen = 0;
	char lines[64][1056];
	dm_lines(wordlen, 32, words, size, 64, 1056, lines, &linelen);

	for (int i=0; i<linelen; i++) {
		char *line = lines[i]; // null terminated
		on_line(line);
	}
}



int W = 0;
int H = 0;

bool ncurses_setup() {
	// setup ncurses window
	initscr();
	if (has_colors()) {
		start_color();
	} else {
		xlogf("%s\n", "Colors not supported on this terminal");
		endwin(); // cleanup
		return false;
	}
	curs_set(0); // hide cursor
	noecho();
	keypad(stdscr, true); // turn on F key listening

	getmaxyx(stdscr, H, W);
	return true;
}

void ncurses_teardown() {
	// teardown ncurses
	keypad(stdscr,FALSE);
	curs_set(1);
	echo();
	endwin();
}

void app_ui_layout() {
	int sx, sy;
	getmaxyx(stdscr, sy, sx);

	ui_center_win(error_window, 16,0, ERR_WIN_H,ERR_WIN_W);
	wresize(str_window, 1, sx);
	mvwin(str_window, sy - 2, 0);
	wresize(cmd_window, 1, sx);
	mvwin(cmd_window, sy - 1, 0);
	wresize(query_window, QUERY_WIN_H, sx - TBL_LIST_W - 1 - 2 - 1);
	mvwin(query_window, 0, TBL_LIST_W + 1 + 2 + 1); // plus 2 for gutter + 1 for spacing
}

bool term_resized = false;
void on_term_resize(int dummy) {
	term_resized = true;

	clear();
	endwin();
	refresh();
	app_ui_layout();
	refresh();
}

void app_setup() {
	if (!ncurses_setup())
		exit(1);

	ui_setup();

	error_window = newwin(0,0,0,0);
	str_window = newwin(0,0,0,0);
	cmd_window = newwin(0,0,0,0);
	query_window = newwin(0,0,0,0);
	result_pad = newpad(2056,4112); // TODO resize dynamically based on the result size of cells,rows and padding

	app_ui_layout();

	strclr(the_query, QUERY_MAX);

	// listen to os signal
	signal(SIGWINCH, on_term_resize);
}

void app_teardown() {
	delwin(result_pad);
	delwin(query_window);
	delwin(str_window);
	delwin(cmd_window);
	delwin(error_window);

	ncurses_teardown();
}

void die(const char *msg) {
	xlogf("DIE %s\n", msg);
	clear();
	app_teardown();
	printf("DIE %s\n", msg);
	exit(1);
}


void display_cmd(char *mode, char *cmd) {
	int sx, sy;
	getmaxyx(stdscr, sy, sx);

	wmove(cmd_window, 0, 0);
	wclrtoeol(cmd_window);
	wattrset(cmd_window, COLOR_PAIR(COLOR_CYAN_BLACK) | A_BOLD);
	waddstr(cmd_window, mode);
	wattrset(cmd_window, COLOR_PAIR(COLOR_WHITE_BLACK));
	wmove(cmd_window, 0, 12);
	waddstr(cmd_window, cmd);

	wrefresh(cmd_window);
}

void display_str(char *str) {
	int sx, sy;
	getmaxyx(stdscr, sy, sx);
	wbkgd(str_window, COLOR_PAIR(COLOR_BLACK_WHITE));

	wmove(str_window, 0, 0);
	wclrtoeol(str_window);
	wattrset(str_window, COLOR_PAIR(COLOR_BLACK_WHITE) | A_BOLD);
	waddstr(str_window, str);

	wrefresh(str_window);
}

int errlinepos = 0;
void display_error_on_line(const char *line) {
	wmove(error_window, 3 + errlinepos++, 2);
	waddstr(error_window, line);
};
void display_error(const char *string) {
	// clear all of the application
	clear();
	refresh();
	wbkgd(error_window, COLOR_PAIR(COLOR_YELLOW_RED));

	// render the error message
	wmove(error_window, 1, 2);
	waddstr(error_window, "ERROR");
	errlinepos = 0;
	dm_wordwrap(string, ERR_WIN_W - 4, display_error_on_line); // avoid gcc nested function supporta

	// render x: close
	wmove(error_window, ERR_WIN_H - 2, 2);
	waddstr(error_window, "x:close");
	//waddch(error_window, 'c');
	//wattrset(error_window, COLOR_PAIR(COLOR_YELLOW_RED));
	//waddstr(error_window, "lose");

	do {
		wrefresh(error_window);
	} while (getch() != KEY_x);
}





void on_db_select(char *database) {
	//xlogf("db select %s\n", database);
	db_select(selected_mysql_conn, database);
	db_selected = true;
}

int menu_select_pos = 0;
void run_db_select(MYSQL *con, struct Connection *app_con) {

	refresh();
	switch (db_select_state) {

		// TODO, should we detect if the database is still selected?
		// and if it changes (eg typed in USE command) we change our state to
		// STATE START?

		case DB_SELECT_STATE_START:
			xlog(" DB_SELECT_STATE_START");
			db_select_state = DB_SELECT_STATE_SELECT_DB;
			break;

		case DB_SELECT_STATE_SELECT_DB: {
			xlog(" DB_SELECT_STATE_SELECT_DB");
			//die("this is a test");
			// Get DBs
			int num_fields, num_rows;
			MYSQL_RES *result = con_select(con, &num_fields, &num_rows);
			// determine widest db name
			int dblen = 0;
			MYSQL_ROW row1;
			while (row1 = mysql_fetch_row(result)) {
				int d = (int)strlen(row1[0]);
				dblen = maxi(d, dblen);
			}
			// allocate window for db menu
			int frame_width = dblen + 5;// 7; // |_>_[label]_|
			int frame_height = num_rows + 2;
			int offset_rows = 10;
			//WINDOW *db_win = ui_new_center_win(offset_rows, frame_height, frame_width, 0);
			WINDOW *db_win = ui_new_center_win(offset_rows, 0, frame_height, frame_width);
			wbkgd(db_win, COLOR_PAIR(COLOR_WHITE_BLUE));
			keypad(db_win, TRUE);
			// allocate menu
			ITEM **my_items;
			MENU *my_menu;
			my_items = (ITEM **)calloc(num_rows + 1, sizeof(ITEM *));
			// iterate over dbs and add them to the menu as items
			int i=0;
			MYSQL_ROW row2;
			mysql_data_seek(result, 0);
			while (row2 = mysql_fetch_row(result)) {
				my_items[i] = new_item(row2[0], "");
				set_item_userptr(my_items[i], on_db_select);
				i++;
			}
			my_items[num_rows] = (ITEM*)NULL; // final element should be null
			my_menu = new_menu((ITEM **)my_items);
			// set menu styles and into parent
			set_menu_fore(my_menu, COLOR_PAIR(COLOR_BLACK_CYAN));
			set_menu_back(my_menu, COLOR_PAIR(COLOR_WHITE_BLUE));
			set_menu_grey(my_menu, COLOR_PAIR(COLOR_YELLOW_RED));
			set_menu_mark(my_menu, "");
			set_menu_win(my_menu, db_win);
			set_menu_sub(my_menu, derwin(db_win, frame_height - 2, frame_width - 4, 1, 2)); // (h, w, offy, offx) from parent window
			// post menu to render and draw it first
			post_menu(my_menu);
			wrefresh(db_win);

			// reposition the menu from last selection
			for (int j=0; j < menu_select_pos; j++) {
				menu_driver(my_menu, REQ_DOWN_ITEM);
			}
			wrefresh(db_win);

			// listen for input for the window selection
			bool done = false;
			while(!done) {
				int c = getch();
				switch(c) {
					case KEY_DOWN:
						menu_driver(my_menu, REQ_DOWN_ITEM);
						menu_select_pos = clampi(menu_select_pos + 1, 0, num_rows - 1);
						break;
					case KEY_UP:
						menu_driver(my_menu, REQ_UP_ITEM);
						menu_select_pos = clampi(menu_select_pos - 1, 0, num_rows - 1);
						break;
					case KEY_RETURN: {
						   void (*callback)(char *);
						   ITEM *cur_item = current_item(my_menu);
						   callback = item_userptr(cur_item);
						   callback((char *)item_name(cur_item));
						   pos_menu_cursor(my_menu);
						   if (db_selected) {
							   done = true;
							   db_select_state = DB_SELECT_STATE_END;
							   break;
						   }
					   }
					   break;
					case KEY_x:
					   // db_selected is false
					   done = true;
					   db_select_state = DB_SELECT_STATE_END;
					   break;
				}
				wrefresh(db_win);
			}
			// with DB selected free menu memory
			unpost_menu(my_menu);
			for(int i = 0; i < num_rows; i++)
				free_item(my_items[i]);
			free_menu(my_menu);
			delwin(db_win);
			mysql_free_result(result); // free sql memory
			clear();
			break;
		}

		case DB_SELECT_STATE_END:
			xlog(" DB_SELECT_STATE_END");
			app_state = APP_STATE_DB_SELECT_END;
			break;
	}
}

// DB PANELS

void set_query(char *query) {
	strclr(the_query, QUERY_MAX);
	strcpy(the_query, query);
}

void set_queryf(char *format, ...) {
	strclr(the_query, QUERY_MAX);

	va_list argptr;
	va_start(argptr, format);
	vsprintf(the_query, format, argptr);
	va_end(argptr);
}

void execute_query() {
	int errcode;
	xlog(the_query);
	the_result = db_query(selected_mysql_conn, the_query, &the_num_fields, &the_num_rows, &the_aff_rows, &errcode);
	if (!the_result && errcode > 0) { // inserts have null responses
		display_error(mysql_error(selected_mysql_conn));
	}
	result_rerender = true;
	result_shift_r = 0;
	result_shift_c = 0;
}

void clear_query() {
	the_num_fields = 0;
	the_num_rows = 0;
	the_aff_rows = 0;
	if (the_result) {
		mysql_free_result(the_result); // free sql memory
		the_result = NULL;
	}
	strclr(the_query, QUERY_MAX);
	result_rerender = true;
	result_shift_r = 0;
	result_shift_c = 0;
}


void run_db_interact(MYSQL *con) {

	int sx, sy;
	getmaxyx(stdscr,sy,sx);
	int tbl_pad_h = TBL_PAD_H;
	int tbl_pad_w = TBL_PAD_W;
	int tbl_render_h = sy - 3; // -1 for index, -1 for command bar, -1 for string bar
	int tbl_render_w = TBL_LIST_W;

	switch (db_state) {
		case DB_STATE_START: {
			xlog(" DB_STATE_START");

			tbl_pad = newpad(tbl_pad_h, tbl_pad_w); // h, w
			tbl_index = 0; // default to first table
			tbl_count = 0;
			tbl_result = NULL;

			// populate the db table listing
			int num_fields, num_rows, aff_rows, errcode;
			tbl_result = db_query(con, "SHOW TABLES", &num_fields, &num_rows, &aff_rows, &errcode);
			// TODO handle errocode failure
			int max_table_name = col_size(tbl_result, 0);
			//xlogf("SHOW TABLES: %d, %d, %d\n", num_rows, num_fields, errcode);
			tbl_count = num_rows;

			interact_state = INTERACT_STATE_TABLE_LIST;
			query_state = QUERY_STATE_COMMAND;

			if (tbl_count == 0)
				db_state = DB_STATE_NOTABLES;
			else
				db_state = DB_STATE_INTERACT;
			break;
		}

		case DB_STATE_NOTABLES:
			xlog(" DB_STATE_NOTABLES");
			// if there are no tables we need to print an interrupt error
			// and return to db selection
			display_error("No tables were found in the selected database");

			db_state = DB_STATE_END;

			break;

		case DB_STATE_INTERACT:
			xlog(" DB_STATE_INTERACT");
			refresh();
			// draw the panels

			////////////////////////////////////////////////
			// PRINT THE TABLES
			MYSQL_ROW row;
			int r = 0; int c = 0; int i = 0;
			wbkgd(tbl_pad, COLOR_PAIR(COLOR_WHITE_BLUE));
			mysql_data_seek(tbl_result, 0);
			while (row = mysql_fetch_row(tbl_result)) {
				wmove(tbl_pad, r, c);
				// highlight focused table
				if (i == tbl_index) {
					if (interact_state == INTERACT_STATE_TABLE_LIST)
						wattrset(tbl_pad, COLOR_PAIR(COLOR_BLACK_CYAN));
					else
						wattrset(tbl_pad, COLOR_PAIR(COLOR_CYAN_BLUE));
				} else {
					wattrset(tbl_pad, COLOR_PAIR(0));
				}
				// print table name with padding for focused background coloring
				char buffer[tbl_pad_w];
				sprintf(buffer, TBL_STR_FMT, row[0]);
				waddstr(tbl_pad, buffer);
				r++; i++;
			}

			// draw the pad, position within pad, to position within screen
			// shift the listing to show the highlighted table
			int pad_row_offset = 0;
			int bottom_padding = 0;
			if (tbl_index > tbl_render_h - bottom_padding) {
				pad_row_offset = tbl_index - tbl_render_h + bottom_padding;
			}

			//////////////////////////////////////////////
			// PRINT THE QUERY PANEL
			ui_clear_win(query_window);
			if (interact_state == INTERACT_STATE_QUERY) {
				wbkgd(query_window, COLOR_PAIR(COLOR_WHITE_BLACK) | A_BOLD);
			} else {
				wbkgd(query_window, COLOR_PAIR(COLOR_WHITE_BLACK));
			}
			wmove(query_window, 0, 0);
			waddstr(query_window, the_query);
			wrefresh(query_window);

			//////////////////////////////////////////////
			// PRINT THE RESULTS PANEL
			if (result_rerender) {
				xlog("  - RERENDER");
				result_rerender = false;
				ui_clear_win(result_pad);
				wbkgd(result_pad, COLOR_PAIR(COLOR_WHITE_BLACK));
				if (the_result) {

					int result_row = 0;

					// print header
					wmove(result_pad, result_row++, 0);
					MYSQL_FIELD *fh;
					mysql_field_seek(the_result, 0);
					while (fh = mysql_fetch_field(the_result)) {
						unsigned long max_field_length = maxi(5, maxi(fh->max_length, fh->name_length)); // size of biggest value in column
						if (max_field_length > 32)
							max_field_length = 32;
						int imaxf = (int)max_field_length;
						char buffer[imaxf + 1]; // plus 1 for for guaranteeing terminating null character
						strclr(buffer, imaxf + 1);
						snprintf(buffer, imaxf + 1, "%*s", imaxf, fh->name);
						int attrs = COLOR_PAIR(COLOR_WHITE_BLACK) | A_UNDERLINE;
						if (interact_state == INTERACT_STATE_RESULTS)
							attrs |= A_BOLD;
						wattrset(result_pad, attrs);
						waddstr(result_pad, buffer);

						// column divider
						wattrset(result_pad, COLOR_PAIR(COLOR_WHITE_BLACK));
						waddch(result_pad, ' ');
						wattrset(result_pad, COLOR_PAIR(COLOR_BLACK_BLUE));
						waddch(result_pad, ' ');
						wattrset(result_pad, COLOR_PAIR(COLOR_WHITE_BLACK));
						waddch(result_pad, ' ');
					}

					if (the_num_rows == 0) {
						// print empty if no rows
						wmove(result_pad, result_row++, 0);
						wattrset(result_pad, COLOR_PAIR(COLOR_YELLOW_BLACK));
						waddstr(result_pad, "no results");
					} else {
						// print rows
						MYSQL_ROW row;
						mysql_data_seek(the_result, 0);
						while ((row = mysql_fetch_row(the_result))) {
							wmove(result_pad, result_row++, 0);

							int i=-1;
							MYSQL_FIELD *f;
							// TODO FIELD TITLES ON ROW TOP
							mysql_field_seek(the_result, 0);
							while ((f = mysql_fetch_field(the_result)) && ++i > -1) {

								unsigned long max_field_length = maxi(5, maxi(f->max_length, f->name_length)); // size of biggest value in column, 5 for EMPTY
								bool isnull = !row[i];
								bool isempty = !isnull && strlen(row[i]) == 0;

								// determine cell style
								int attrs;
								if (isnull) {
									attrs = COLOR_PAIR(COLOR_YELLOW_BLACK);
								} else if (isempty) {
									attrs = COLOR_PAIR(COLOR_YELLOW_BLACK);
								} else {
									switch (f->type) {
										case MYSQL_TYPE_TINY:
										case MYSQL_TYPE_SHORT:
										case MYSQL_TYPE_LONG:
										case MYSQL_TYPE_INT24:
										case MYSQL_TYPE_LONGLONG:
											attrs = COLOR_PAIR(COLOR_CYAN_BLACK);
											break;
										case MYSQL_TYPE_DECIMAL:
										case MYSQL_TYPE_NEWDECIMAL:
										case MYSQL_TYPE_FLOAT:
										case MYSQL_TYPE_DOUBLE:
										case MYSQL_TYPE_BIT:
											attrs = COLOR_PAIR(COLOR_MAGENTA_BLACK);
											break;
										case MYSQL_TYPE_DATETIME:
										case MYSQL_TYPE_DATE:
										case MYSQL_TYPE_TIME:
											attrs = COLOR_PAIR(COLOR_BLUE_BLACK);
											break;
										default:
											attrs = COLOR_PAIR(COLOR_WHITE_BLACK);
											break;
									}
								}
								//if (interact_state == INTERACT_STATE_RESULTS)
								//	attrs |= A_BOLD;
								wattrset(result_pad, attrs);

								// print into the cell
								if (max_field_length > 32)
									max_field_length = 32;

								int imaxf;
								if (isnull)
									imaxf = maxi(max_field_length, 4); // "NULL"
								else if (isempty)
									imaxf = maxi(max_field_length, 5); // "EMPTY"
								else
									imaxf = (int)max_field_length; // plus 3 for padding
								//xlogf("%s:%d %s imaxf=%d\n", __FILE__, __LINE__, f->name, imaxf);

								// data in the field is not a null terminated string, its a fixed size since binary data can contain null characters
								// but they do null terminate where they data ends, so its a mixed bag, i am going to just ignore anything
								// after a random null character because im not that concerned about rendering out contents of BLOBs with that
								// shitty data in it
								char buffer[imaxf + 1]; // plus 1 for for guaranteeing terminating null character
								strclr(buffer, imaxf + 1);
								if (isnull) {
									// NULL
									snprintf(buffer, imaxf + 1, "%*s", imaxf, "NULL");
								} else if (isempty) {
									// EMPTY STRING
									snprintf(buffer, imaxf + 1, "%*s", imaxf, "EMPTY");
								} else {
									// CONTENTS
									charreplace(row[i], '\t', ' ');
									charreplace(row[i], '\n', ' ');
									charreplace(row[i], '\r', ' ');
									snprintf(buffer, imaxf + 1, "%*s", imaxf, row[i]);
								}
								waddstr(result_pad, buffer);

								// column divider
								wattrset(result_pad, COLOR_PAIR(COLOR_WHITE_BLACK));
								waddch(result_pad, ' ');
								wattrset(result_pad, COLOR_PAIR(COLOR_BLACK_BLUE));
								waddch(result_pad, ' ');
								wattrset(result_pad, COLOR_PAIR(COLOR_WHITE_BLACK));
								waddch(result_pad, ' ');
							}
						} // eo while row
					} // eo if rows
				} // eo if results
				else if (the_aff_rows) {
					char buffer[64];
					strclr(buffer, 64);
					sprintf(buffer, "%d %s", the_aff_rows, "affected row(s)");

					wmove(result_pad, 0, 0);
					wattrset(result_pad, COLOR_PAIR(COLOR_YELLOW_BLACK));
					waddstr(result_pad, buffer);
				} // eo if affected rows
			} // eo if rerender


			//////////////////////////////////////////////
			// PRINT THE STRING BAR
			// PRINT THE COMMAND BAR
			switch (interact_state) {
				case INTERACT_STATE_TABLE_LIST: {
					// string bar
					// print the table title fully since they can be cut off
					mysql_data_seek(tbl_result, tbl_index);
					MYSQL_ROW r = mysql_fetch_row(tbl_result);
					display_str(r[0]);

					// command bar
					display_cmd("TABLES", "s/enter:select-100 | d:describe | tab:next | x:close");
					break;
				}
				case INTERACT_STATE_QUERY:
					// string bar (none)
					display_str("");

					// command bar
					if (query_state == QUERY_STATE_COMMAND)
						display_cmd("QUERY", "e/i:edit | enter:execute | del:clear | tab:next | x:close");
					else
						display_cmd("EDIT QUERY", "ctrl+x/esc:no-edit | tab:next | x:close");
					break;
				case INTERACT_STATE_RESULTS:
					// string bar (none)
					display_str("");

					// command bar
					display_cmd("RESULTS", "tab:next | x:close");
					break;
			}


			// pads need to be refreshed after windows
			// prefresh(pad, y-inpad,x-inpad, upper-left:y-inscreen,x-inscreen, lower-right:x-inscreen,w-onscreen)
			int sx, sy;
			getmaxyx(stdscr, sy, sx);
			prefresh(tbl_pad, pad_row_offset,0, 0,0, tbl_render_h,tbl_render_w);

			int rp_shift_r = result_shift_r;
			int rp_shift_c = result_shift_c;
			int rp_y = QUERY_WIN_H + 1;
			int rp_x = tbl_render_w + 1 + 2 + 1;
			prefresh(result_pad, rp_shift_r,rp_shift_c, rp_y,rp_x, sy-3,sx-1); // with gutter spacing


			////////////////////////////////////////////
			// GUTTERS / DIVIDERS
			attrset(COLOR_PAIR(COLOR_BLACK_WHITE));
			// vert
			move(0,tbl_render_w + 1);
			vline(' ', tbl_render_h + 1);
			// horiz
			move(QUERY_WIN_H,tbl_render_w + 1 + 1);
			hline(' ',256);
			// focus lines
			if (interact_state == INTERACT_STATE_QUERY) {
				if (query_state == QUERY_STATE_COMMAND)
					attrset(COLOR_PAIR(COLOR_BLACK_CYAN));
				else
					attrset(COLOR_PAIR(COLOR_BLACK_RED));
				move(0, tbl_render_w + 2);
				vline(' ', QUERY_WIN_H);
			} else {
				attrset(COLOR_PAIR(COLOR_BLACK_WHITE));
				move(0, tbl_render_w + 2);
				vline(' ', QUERY_WIN_H);
			}
			if (interact_state == INTERACT_STATE_RESULTS) {
				attrset(COLOR_PAIR(COLOR_BLACK_CYAN));
				move(QUERY_WIN_H + 1, tbl_render_w + 2);
				vline(' ', sy - QUERY_WIN_H - 3);
			} else {
				attrset(COLOR_PAIR(COLOR_BLACK_WHITE));
				move(QUERY_WIN_H + 1, tbl_render_w + 2);
				vline(' ', sy - QUERY_WIN_H - 3);
			}

			// depending on which context I am in:
			// - table list, query, results, interact differently

			switch (interact_state) {
				case INTERACT_STATE_TABLE_LIST: {
					xlog("  INTERACT_STATE_TABLE_LIST");
					// listen for input
					int key = getch();
					switch (key) {
						case KEY_x:
							db_state = DB_STATE_END;
							break;
						case KEY_TAB:
						case KEY_BTAB:
							if (key == KEY_TAB)
								interact_state = INTERACT_STATE_QUERY;
							else
								interact_state = INTERACT_STATE_RESULTS;
							result_rerender = true;
							break;
						case KEY_d:
							mysql_data_seek(tbl_result, tbl_index);
							MYSQL_ROW r = mysql_fetch_row(tbl_result);
							set_queryf("DESCRIBE %s", r[0]);
							execute_query();
							break;
						case KEY_s:
						case KEY_RETURN: {
							mysql_data_seek(tbl_result, tbl_index);
							MYSQL_ROW r = mysql_fetch_row(tbl_result);
							set_queryf("SELECT * FROM %s LIMIT 100", r[0]);
							execute_query();
							break;
						}
						case KEY_UP:
						case KEY_DOWN:
							if (key == KEY_UP)
								tbl_index = (tbl_index - 1) % tbl_count;
							if (key == KEY_DOWN)
								tbl_index = (tbl_index + 1) % tbl_count;
							if (tbl_index < 0)
								tbl_index = tbl_count + tbl_index;
							break;
					}
					break;
				}
				case INTERACT_STATE_QUERY: {
					xlog("  INTERACT_STATE_QUERY");
					if (query_state == QUERY_STATE_COMMAND) {
						xlog("   QUERY_STATE_COMMAND");
						curs_set(0);
						int key = getch();
						switch (key) {
							case KEY_RETURN:
								execute_query();
								break;
							case KEY_x:
								// Exit database editor
								db_state = DB_STATE_END;
								break;
							case KEY_BTAB:
							case KEY_TAB:
								// Shift windows
								if (key == KEY_TAB)
									interact_state = INTERACT_STATE_RESULTS;
								else
									interact_state = INTERACT_STATE_TABLE_LIST;
								result_rerender = true;
								break;
							case KEY_DC:
							case KEY_BACKSPACE:
								// Clear the editor
								clear_query();
								break;
							case KEY_i:
							case KEY_e:
								// Insert mode
								query_state = QUERY_STATE_EDIT;
								break;
						}
					} else if(query_state == QUERY_STATE_EDIT) {
						xlog("   QUERY_STATE_EDIT");
						curs_set(1);

						// beg and end are screen coords of editor window
						// min and max are internal window coords
						int begy,begx, endx,endy, miny=0,minx=0, maxy,maxx;
						getbegyx(query_window, begy, begx);
						getmaxyx(query_window, maxy, maxx);
						endy = begy + maxy - 1; endx = begx + maxx; // add beginning coordinates to max sizes

						// cur is the position of the cursor within the window
						int cury,curx;
						getyx(query_window, cury,curx);
						cury += begy; curx += begx;
						//xlogf("cury=%d curx=%d begy=%d begx=%d maxy=%d maxx=%d\n", cury, curx, begy, begx, maxy, maxx);
						if (cury < begy || curx < begx || cury > endy || curx > endx)
							cury=begy, curx=begx; // default to beggining

						move(cury, curx);
						wmove(query_window, cury - begy, curx - begx);

						bool editing = true;
						while (editing) {
							// Lock down editing to this position
							int key = getch();
							switch (key) {
								case KEY_ctrl_x:
								case KEY_ESC: {
									query_state = QUERY_STATE_COMMAND;
									editing = false;

									// capture and trim the contents of the window
									// convert to character buffer for the line
									chtype chtype_buffers[maxy][maxx];
									char buffer[QUERY_MAX];
									strclr(buffer, QUERY_MAX);
									int buffi = 0;
									for (int r=0; r < maxy; r++) {
										wmove(query_window, r, 0);
										winchstr(query_window, chtype_buffers[r]);
										// to trim the line we should count backwards until the first character
										// you see maxx + 1 because maxx is the final index, not the size

										// convert chtypes to characters
										int ccount = nc_strtrimlen(chtype_buffers[r], maxx);
										if (maxx + 1 == ccount)
											continue; // all characters are empty

										// ccount is the number of characters on the end
										int strsize = maxx - ccount + 1;
										//xlogf("row %d  ccount %d\n", r, ccount);
										for (int c=0; c < strsize; c++) {
											buffer[buffi++] = chtype_buffers[r][c] & A_CHARTEXT;
										}
										buffer[buffi++] = '\n';
									}
									// trim final newline
									buffer[buffi-1] = '\0';
									// capture query
									set_query(buffer);
									xlog(buffer);

									break;
								}
								// MOVE CURSOR
								case KEY_LEFT:
									curx = clampi(curx - 1, begx, endx);
									move(cury, curx);
									wmove(query_window, cury - begy, curx - begx);
									break;
								case KEY_RIGHT:
									curx = clampi(curx + 1, begx, endx);
									move(cury, curx);
									wmove(query_window, cury - begy, curx - begx);
									break;
								case KEY_UP:
									cury = clampi(cury - 1, begy, endy);
									move(cury, curx);
									wmove(query_window, cury - begy, curx - begx);
									break;
								case KEY_DOWN:
									cury = clampi(cury + 1, begy, endy);
									move(cury, curx);
									wmove(query_window, cury - begy, curx - begx);
									break;
								// TEXT EDITOR
								default:
									// ELSE WE ARE LISTENING TO INPUT AND EDITING THE WINDOW
									if (key > 31 && key < 127) { // ascii input
										// TODO there is a bug, when i insert a character it pushes a space over the next line
										int winy, winx;
										getyx(query_window, winy, winx); // winx is position in line
										int sizeleft = maxx - winx - 1;
										// we need to shift everthing after this position over without overflowing the text area
										chtype contents[maxx];
										winchstr(query_window, contents); // this captures everything after the cursor
										// insert the character into the line
										waddch(query_window, key);
										// reinsert the remaining characters
										for (int i=0; i < sizeleft; i++) {
											char c = contents[i] & A_CHARTEXT;
											if (c > 31 && c < 127)
												waddch(query_window, contents[i]);
										}
										wmove(query_window, winy, winx+1);
									}
									if (key == '\n') { // new line
										waddch(query_window, '\n');
									}
									if (key == '\t') {
										// TODO this does not deal well when injecting into middle of string
										waddch(query_window, '\t');
									}
									if (key == KEY_BACKSPACE) { // regular delete
										int winy, winx;
										getyx(query_window, winy, winx); // winx is position in line

										if (curx - begx == 0) {
											// already at beginning of line
											// clear line and copy contents up to previous line
											int start_winy=winy, start_winx=winx;

											// copy line and clear line
											chtype contents[maxx];
											nc_cutline(query_window, contents, 0, maxx);

											// go to line above
											winy = clampi(winy - 1, 0, maxy);
											wmove(query_window, winy, winx);
											int target_winy = winy;

											// go to end of line
											int target_winx = nc_mveol(query_window);

											// append contents to end of line
											nc_paste(query_window, contents);

											// repeat for every Y below our original position
											for (int y=start_winy; y<maxy - 1; y++) {
												//xlogf("LOOP ON %d\n", y);
												// move next line to our current line
												winy = clampi(y + 1, 0, maxy);
												winx = 0;
												wmove(query_window, winy, winx);

												// get line contents
												chtype linecontents[maxx];
												int sz = nc_cutline(query_window, linecontents, 0, maxx);
												//xlogf("copy from %d,%d size=%d\n", winy, winx, sz);
												if (sz == 0)
													continue;

												// copy to line above
												winy = clampi(y, 0, maxy);
												wmove(query_window, winy, winx);
												nc_paste(query_window, linecontents);
												//xlogf("copy to %d,%d size=%d\n", winy, winx, sz);
											}
											// reset back to original position
											wmove(query_window, target_winy, target_winx);
										} else {
											winx = clampi(winx - 1, 0, maxx);
											wmove(query_window, winy, winx);
											wdelch(query_window);
										}
									}
									if (key == KEY_DC) { // forward delete
										wdelch(query_window);
									}
									wrefresh(query_window);
									getyx(query_window, cury,curx);
									cury += begy; curx += begx;
									break;
							}
						}
					}
					break;
				}
				case INTERACT_STATE_RESULTS: {
					xlog("  INTERACT_STATE_RESULTS");
					int key = getch();
					switch (key) {
						case KEY_x:
							db_state = DB_STATE_END;
							break;
						case KEY_BTAB:
						case KEY_TAB:
							if (key == KEY_TAB)
								interact_state = INTERACT_STATE_TABLE_LIST;
							else
								interact_state = INTERACT_STATE_QUERY;
							result_rerender = true;
							break;
						case KEY_UP:
							result_shift_r--;
							result_shift_r = maxi(0, result_shift_r);
							break;
						case KEY_DOWN:
							result_shift_r++;
							break;
						case KEY_LEFT:
							result_shift_c -= 2;
							result_shift_c = maxi(0, result_shift_c);
							break;
						case KEY_RIGHT:
							result_shift_c += 2;
							break;
						case KEY_PPAGE: // pageup
							result_shift_r -= 20;
							result_shift_r = maxi(0, result_shift_r);
							break;
						case KEY_NPAGE:
							result_shift_r += 20;
							break;
						case KEY_HOME:
							result_shift_c -= 40;
							result_shift_c = maxi(0, result_shift_c);
							break;
						case KEY_END:
							result_shift_c += 40;
							break;
					}
					break;
				}
			}

			break;

		case DB_STATE_END:
			xlog(" DB_STATE_END");
			delwin(tbl_pad);
			clear();
			// clear main query
			clear_query();
			// clear table list query
			mysql_free_result(tbl_result); // free sql memory
			tbl_result = NULL;
			// unset db
			db_selected = false;
			app_state = APP_STATE_DB_INTERACT_END;
			break;
	}
}


// TODO LIST
// - text editor needs a lot of quol work
// - connection list, file and menu
// - cell editor
// - usage message
// - dynamic pad size for result set, right now its fixed
// - build on os x

/*
 * USAGE
 * ./a.out -h mysqlhost [-l port] -u mysqluser [-p mysqlpass] [-s sshtunnelhost]
 */
// note, im just referencing argv, not copying them into new buffers, and argc/argv
// "array shall be modifiable by the program, and retain their last-stored values between program startup and program termination."
char *arg_host=NULL, *arg_port=NULL, *arg_user=NULL, *arg_pass=NULL, *arg_tunnel=NULL;
int parseargs(int argc, char **argv) {
	opterr = 0; // hide error output
	int c;
	// TODO help arg?
	while ((c = getopt(argc, argv, "h:l:u:p:s:")) != -1) {
		switch (c) {
			case 'h': arg_host = optarg; break;
			case 'l': arg_port = optarg; break;
			case 'u': arg_user = optarg; break;
			case 'p': arg_pass = optarg; break;
			case 's': arg_tunnel = optarg; break;
			case '?': // appears if unknown option when opterr=0
				if (optopt == 'h')
					fprintf(stderr, "Error: -h missing hostname\n");
				else if (optopt == 'l')
					fprintf(stderr, "Error: -l missing port\n");
				else if (optopt == 'u')
					fprintf(stderr, "Error: -u missing user\n");
				else if (optopt == 'p')
					fprintf(stderr, "Error: -p missing password\n");
				else if (optopt == 's')
					fprintf(stderr, "Error: -s missing ssh tunnel string\n");
				else if (isprint(optopt))
					fprintf(stderr, "Error: unknown option '-%c'\n", optopt);
				else
					fprintf(stderr, "Error: unknown option ['\\x%x']\n", optopt);
				return 1;
			default:
				fprintf(stderr, "Error: unknown getopt error");
				return 1;
		}
	}
	// TODO print usage on error
	return 0;
}

char *scan_pass = NULL;
int s_portmax = 2216;
int s_port = 2200;
int main(int argc, char **argv) {

	xlogopen("logs/log", "w+");

	xlog("....... START .......");
	xlogf("MySQL client version: %s\n", mysql_get_client_info());

	// parse our arguments into data
	if (parseargs(argc, argv)) {
		return 1;
	}
	//printf("h:%s l:%s u:%s p:%s s:%s\n", arg_host, arg_port, arg_user, arg_pass, arg_tunnel);

	struct Connection *app_con = NULL;
	MYSQL *con = NULL;

	bool run = true;
	while (run) {
		switch (app_state) {
			case APP_STATE_START:
				xlogf("APP_STATE_START\n");

				// If we had some args that specify loading a connection directly then run that
				// otherwise go to file connection loader
				if (arg_host)
					app_state = APP_STATE_ARGS_TO_CONNECTION;
				else
					app_state = APP_STATE_LOAD_CONNECTION_FILE;
				break;

			case APP_STATE_ARGS_TO_CONNECTION:
				xlog("APP_STATE_ARGS_TO_CONNECTION");

				bool invalid = false;
				if (arg_host == NULL) {
					fprintf(stderr, "Error: hostname is required\n");
					invalid = true;
				}
				if (arg_user == NULL) {
					fprintf(stderr, "Error: user is required\n");
					invalid = true;
				}
				if (invalid)
					return 1;

				app_cons[0].host = arg_host;
				app_cons[0].user = arg_user;

				if (arg_pass == NULL) {
					// ask for password
					scan_pass = (char*)malloc(256);
					strclr(scan_pass, 256);
					int nchr = 0;
					while (nchr == 0) {
						printf("Enter password: ");
						nchr = getpasswd(&scan_pass, 255, '*', stdin);
						if (nchr == 0)
							printf("\n");
					}
					app_cons[0].pass = scan_pass;
				} else {
					app_cons[0].pass = arg_pass;
				}

				if (arg_port != NULL && strlen(arg_port) > 0) {
					app_cons[0].iport = atoi(arg_port);
					app_cons[0].port = arg_port;
				} else {
					app_cons[0].iport = 3306; // default 3306
					app_cons[0].port = "3306";
				}

				app_cons[0].ssh_tunnel = arg_tunnel;

				app_setup(); // setup ncurses control

				app_state = APP_STATE_CONNECTION_SELECT;

				break;

			case APP_STATE_LOAD_CONNECTION_FILE:
				xlog("APP_STATE_LOAD_CONNECTION_FILE");

				// TODO
				// - was a path to a conf file provided?
				// - if not is there a hidden local conf file?
				// - if no file detected TODO build a create mode?
				// - if file found, load connections into menu for selection
				// - on selection, choose it as the app_con

				app_state = APP_STATE_END;

				break;

			case APP_STATE_CONNECTION_SELECT:
				xlogf("APP_STATE_CONNECTION_SELECT\n");

				// TODO this was a state to show a list of saved connections, not in use
				// ncurses menu to show a list of connections to choose from
				app_con = &app_cons[0];

				if (app_con->ssh_tunnel != NULL)
					app_state = APP_STATE_FORK_SSH_TUNNEL;
				else
					app_state = APP_STATE_CONNECT;

				break;

			case APP_STATE_FORK_SSH_TUNNEL: {
				xlogf("APP_STATE_FORK_SSH_TUNNEL\n");

				// switch target mysql host to localhost
				// If an ssh tunnel was requested lets fork one
				int prc = fork();
				if (prc < 0) {
					display_error("Unable to create child process for SSH tunnel");
					app_state = APP_STATE_END;
					break;
				}
				pid_t cpid;
				if (prc == 0) {
					// child process: create ssh tunnel in background, allowing for 2 seconds of time for us to connect, if we do not it closes
					// "ssh -fL 127.0.0.1:$LOCALPORT:$HOSTNAME:$HOSTPORT $TUNNEL sleep 2"
					bool established = false;
					while (!established && s_port < s_portmax) {
						xlogf("- CHILD TUNNEL ATTEMPTS ON %d\n", s_port);
						char buffer[256];
						snprintf(buffer, 256,
								"ssh -oStrictHostKeyChecking=no -fL :%d:%s:%d -o ExitOnForwardFailure=yes %s sleep 2",
								s_port, app_con->host, app_con->iport, app_con->ssh_tunnel);
						int sys_exit = system(buffer);
						int ssh_exit = WEXITSTATUS(sys_exit);
						xlogf("- CHILD SYSTEM r=%d W=%d\n", sys_exit, ssh_exit);
						// system will return a -1 if it straight up failed to fork the child process for system (not the same as the exit code of the command)
						// ssh will return a 255 if the commmand failed because of the ExitOnForwardFailure=yes arg
						if (sys_exit == -1 || ssh_exit == 255) {
							s_port++;
							xlogf("- CHILD TUNNEL CONNECT FAILURE\n", s_port);
						} else {
							established = true;
						}
					}
					if (!established)
						return 1;
					return 0;
				} else {
					// parent/main: continues on its way, waiting on child to establish its connection
					int child_exit;
					cpid = wait(&child_exit);
					child_exit = WEXITSTATUS(child_exit);
					xlogf("- PARENT SEES CHILD DONE %d\n", child_exit);
					if (child_exit == 1) {
						// ssh tunnel failed
						display_error("Failed to establish SSH tunnel: all ports are taken or invalid ssh host");
						app_state = APP_STATE_END;
					} else {
						app_state = APP_STATE_CONNECT;
					}
				}
				//printf("parent pid %d\n", getpid());
				//printf("child pid %d\n", cpid);
				break;
			}

			case APP_STATE_CONNECT:
				xlogf("APP_STATE_CONNECT %s@%s:%d\n", app_con->user, app_con->host, app_con->iport);

				// create mysql connection
				con = mysql_init(NULL);
				if (con == NULL) {
					display_error(mysql_error(con));
					app_state = APP_STATE_END;
					break;
				}

				// If we are connecting through an established tunnel, then target localhost at our local port
				// otherwise connect to server naturally
				unsigned int timeout = 3;
				mysql_options(con, MYSQL_OPT_CONNECT_TIMEOUT, &timeout);
				if (app_con->ssh_tunnel)
					con = mysql_real_connect(con, LOCALHOST, app_con->user, app_con->pass, NULL, s_port, NULL, 0);
				else
					con = mysql_real_connect(con, app_con->host, app_con->user, app_con->pass, NULL, app_con->iport, NULL, 0);

				if (con == NULL) {
					display_error(mysql_error(con));
					mysql_close(con);
					app_state = APP_STATE_END;
					break;
				}

				conn_established = true;
				selected_mysql_conn = con;
				app_state = APP_STATE_DB_SELECT;
				break;

			case APP_STATE_DB_SELECT:
				xlogf("APP_STATE_DB_SELECT\n");
				// show menu to select db for the connection
				run_db_select(con, app_con);

				break;

			case APP_STATE_DB_SELECT_END:
				xlog("APP_STATE_DB_SELECT_END");
				db_select_state = DB_SELECT_STATE_START; // reset select state for future
				if (db_selected)
					app_state = APP_STATE_DB_INTERACT;
				else
					app_state = APP_STATE_DISCONNECT;
				break;

			case APP_STATE_DB_INTERACT:
				xlogf("APP_STATE_DB_INTERACT\n");

				// with a db selected lets run the selected db state
				run_db_interact(con);

				break;

			case APP_STATE_DB_INTERACT_END:
				xlog("APP_STATE_DB_INTERACT_END");
				db_state = DB_STATE_START; // reset db state
				app_state = APP_STATE_DB_SELECT; // go to select db
				break;

			case APP_STATE_DISCONNECT:
				xlogf("APP_STATE_DISCONNECT %s@%s\n", app_con->user, app_con->host);

				// close connection
				mysql_close(con);

				conn_established = false;
				selected_mysql_conn = NULL;
				app_state = APP_STATE_END;
				break;

			case APP_STATE_END:
			case APP_STATE_DIE:
				xlogf("APP_STATE_END|DIE\n");
				run = false;
				app_teardown();
				if (scan_pass)
					free(scan_pass);
				break;
		} // end app fsm
	} // end run loop

	xlog("........ END ........");
	xlogclose();
	return 0;
} // end main

