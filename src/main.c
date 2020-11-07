#include <mysql/mysql.h>
#include <ncurses.h>
#include <menu.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "sqlops.h"
#include "ui.h"

#define ARRAY_SIZE(a) (sizeof(a) / sizeof(a[0]))
#define KEY_RETURN	10
#define KEY_ESC		27
#define KEY_SPACE	32
#define KEY_TAB		9

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
	char *user;
	char *pass;
};
struct Connection app_cons[20];
bool conn_established = false;
bool db_selected = false;
MYSQL *selected_mysql_conn = NULL;

// STATE MACHINES
//
enum APP_STATE {
	APP_STATE_START,
	APP_STATE_CONNECTION_SELECT,
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

// GLOBAL WINDOWS
//
WINDOW* error_window;
#define ERR_WIN_W	48
#define ERR_WIN_H	12


// FUNCTIONS
//
int maxi(int a, int b) {
	if (a > b)
		return a;
	return b;
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

void app_setup() {
	if (!ncurses_setup())
		exit(1);
	xlogopen("logs/log", "w+");
	ui_setup();

	//error_window = newwir(0, 0, 0, 0);
	error_window = ui_new_center_win(16, 0, ERR_WIN_H, ERR_WIN_W);
}

void app_teardown() {
	delwin(error_window);
	xlogclose();
	ncurses_teardown();
}

void die(const char *msg) {
	xlogf("DIE %s\n", msg);
	clear();
	app_teardown();
	printf("DIE %s\n", msg);
	exit(1);
}

int errlinepos = 0;
void display_error_on_line(const char *line) {
	wmove(error_window, 3 + errlinepos++, 2);
	waddstr(error_window, line);
};
void display_error(const char *string) {
	// clear all of the application
	clear();
	wbkgd(error_window, COLOR_PAIR(COLOR_YELLOW_RED));

	// render the error message
	wmove(error_window, 1, 2);
	waddstr(error_window, "ERROR");
	errlinepos = 0;
	dm_wordwrap(string, ERR_WIN_W - 4, display_error_on_line); // avoid gcc nested function supporta

	// render x: close
	wmove(error_window, ERR_WIN_H - 2, 2);
	waddstr(error_window, "x: close");

	do {
		refresh();
		wrefresh(error_window);
	} while (getch() != KEY_x);
}





void on_db_select(char *database) {
	xlogf("db select %s\n", database);
	db_select(selected_mysql_conn, database);
	db_selected = true;
}

void run_db_select(MYSQL *con, struct Connection *app_con) {

	switch (db_select_state) {

		// TODO, should we detect if the database is still selected?
		// and if it changes (eg typed in USE command) we change our state to
		// STATE START?

		case DB_SELECT_STATE_START:
			xlog(" DB_SELECT_STATE_START");
			refresh();
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
			// listen for input for the window selection
			bool done = false;
			while(!done) {
				int c = getch();
				switch(c) {
					case KEY_DOWN:
						menu_driver(my_menu, REQ_DOWN_ITEM);
						break;
					case KEY_UP:
						menu_driver(my_menu, REQ_UP_ITEM);
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
WINDOW* tbl_pad = NULL;
int tbl_index = 0;
int tbl_count = 0;
MYSQL_RES* tbl_result = NULL;
#define TBL_PAD_H		256
#define TBL_PAD_W		256
#define TBL_STR_FMT		"%-256s"

void run_db_interact(MYSQL *con) {
	int sx, sy;
	getmaxyx(stdscr,sy,sx);
	int tbl_pad_h = TBL_PAD_H;
	int tbl_pad_w = TBL_PAD_W;
	int tbl_render_h = sy - 3; // -1 for index, -1 for command bar, -1 for string bar
	int tbl_render_w = 32;

	switch (db_state) {
		case DB_STATE_START: {
			xlog(" DB_STATE_START");
			// build the curses windows
			// left side is the table menu
			// center top is the query window
			// center bottom is the result window
			//tbl_pad = newwin(0,0,0,0);
			tbl_pad = newpad(tbl_pad_h, tbl_pad_w); // h, w
			tbl_index = 0; // default to first table
			tbl_count = 0;
			tbl_result = NULL;

			// inventory window
			//int rows = sy; int cols = 28;
			//ui_anchor_ul(tbl_pad, rows, cols);
			//ui_box(tbl_pad);

			// populate the db table listing
			int num_fields, num_rows, errcode;
			tbl_result = db_query(con, "SHOW TABLES", &num_fields, &num_rows, &errcode);
			// TODO handle errocode failure
			int max_table_name = col_size(tbl_result, 0);
			xlogf("SHOW TABLES: %d, %d, %d\n", num_rows, num_fields, errcode);
			tbl_count = num_rows;

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
			// draw the panels
			refresh();

			// print the tables
			MYSQL_ROW row;
			int r = 0; int c = 0; int i = 0;
			wbkgd(tbl_pad, COLOR_PAIR(COLOR_WHITE_BLUE));
			//wattrset(tbl_pad, COLOR_PAIR(COLOR_WHITE_BLUE));
			mysql_data_seek(tbl_result, 0);
			while (row = mysql_fetch_row(tbl_result)) {
				wmove(tbl_pad, r, c);
				// highlight focused table
				if (i == tbl_index) {
					wattrset(tbl_pad, COLOR_PAIR(COLOR_BLACK_CYAN));
				} else {
					wattrset(tbl_pad, COLOR_PAIR(0));
				}
				// print table name with padding for focused background coloring
				char buffer[tbl_pad_w];
				sprintf(buffer, TBL_STR_FMT, row[0]);
				waddstr(tbl_pad, buffer);
				r++; i++;
			}
			//wrefresh(tbl_pad);
			// draw the pad, position within pad, to position within screen
			// shift the listing to show the highlighted table
			int pad_row_offset = 0;
			int bottom_padding = 0;
			if (tbl_index > tbl_render_h - bottom_padding) {
				pad_row_offset = tbl_index - tbl_render_h + bottom_padding;
			}
			prefresh(tbl_pad, pad_row_offset, 0, 0, 0, tbl_render_h, tbl_render_w);


			// print the query panel


			// print the results panel

			// print the command bar
			switch (interact_state) {
				case INTERACT_STATE_TABLE_LIST: {
					// string bar
					// print the table title fully since they can be cut off
					move(sy - 2, 0);
					clrtoeol();
					attrset(COLOR_PAIR(COLOR_WHITE_BLACK) | A_BOLD);
					mysql_data_seek(tbl_result, tbl_index);
					MYSQL_ROW r = mysql_fetch_row(tbl_result);
					addstr(r[0]);

					// command bar
					move(sy - 1, 0);
					clrtoeol();
					attrset(COLOR_PAIR(COLOR_YELLOW_BLACK) | A_BOLD);
					addstr("TABLE MODE");
					attrset(COLOR_PAIR(COLOR_WHITE_BLACK));
					move(sy - 1, 12);
					addstr("x: close | enter: select table | d: describe");
					break;
				}
				case INTERACT_STATE_QUERY:
					// string bar
					move(sy - 2, 0);
					clrtoeol();

					// command bar
					move(sy - 1, 0);
					clrtoeol();
					attrset(COLOR_PAIR(COLOR_YELLOW_BLACK) | A_BOLD);
					addstr("QUERY MODE");
					attrset(COLOR_PAIR(COLOR_WHITE_BLACK));
					move(sy - 1, 12);
					addstr("tab: next | x: close | i: insert");
					break;
			}


			// print the string bar

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
							interact_state = INTERACT_STATE_QUERY;
							break;
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
					int key = getch();
					switch (key) {
						case KEY_x:
							db_state = DB_STATE_END;
							break;
						case KEY_TAB:
							interact_state = INTERACT_STATE_TABLE_LIST;
							break;
					}
					break;
				}
			}

			break;

		case DB_STATE_END:
			xlog(" DB_STATE_END");
			// tear down curses windows
			delwin(tbl_pad);
			clear();
			db_selected = false;
			app_state = APP_STATE_DB_INTERACT_END;
			break;
	}
}

int main(int argc, char **argv) {

	app_setup();

	xlog("------- START -------");
	xlogf("MySQL client version: %s\n", mysql_get_client_info());

	if (argc < 4) {
		xlogf("ERROR: missing connection arg\n");
		return 1;
	}

	struct Connection *app_con = NULL;
	MYSQL *con = NULL;

	bool run = true;
	while (run) {
		switch (app_state) {
			case APP_STATE_START:
				xlogf("APP_STATE_START\n");
				// See if we have any listed connections
				// TODO read from conf file
				// TODO malloc the list to make it variable length
				app_cons[0].host = argv[1];
				app_cons[0].user = argv[2];
				app_cons[0].pass = argv[3];
				app_state = APP_STATE_CONNECTION_SELECT;
				break;

			case APP_STATE_CONNECTION_SELECT:
				xlogf("APP_STATE_CONNECTION_SELECT\n");

				// ncurses menu to show a list of connections to choose from
				app_con = &app_cons[0];
				app_state = APP_STATE_CONNECT;
				break;

			case APP_STATE_CONNECT:
				xlogf("APP_STATE_CONNECT %s@%s\n", app_con->user, app_con->host);

				// create mysql connection
				con = mysql_init(NULL);
				if (con == NULL) {
                    display_error(mysql_error(con));
                    app_state = APP_STATE_END;
				}
				if (mysql_real_connect(con, app_con->host, app_con->user, app_con->pass, NULL, 0, NULL, 0) == NULL) {
                    display_error(mysql_error(con));
					mysql_close(con);
                    app_state = APP_STATE_END;
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

				//app_state = APP_STATE_DISCONNECT;
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
				xlogf("APP_STATE_END\n");
				run = false;
				break;

			case APP_STATE_DIE:
				xlogf("APP_STATE_DIE\n");
				run = false;
				break;
		} // end app fsm
	} // end run loop

	app_teardown();
	return 0;
} // end main

