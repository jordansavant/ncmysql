#include <mysql/mysql.h>
#include <ncurses.h>
#include <menu.h>
#include <stdio.h>
#include <stdbool.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>
#include <limits.h>
#include "jlib.h"
#include "pass.h"

#define ARRAY_SIZE(a) (sizeof(a) / sizeof(a[0]))

//////////////////////////////////////
// DB CONNECTION INFORMATION
// We need to rely on that these values, be them populated from args or from
// file that the strings are malloc'd values
struct Connection {
	bool isset;
	char *name;
	char *host;
	char *port;
	int iport;
	char *user;
	char *pass;
	char *ssh_tunnel;
};
void init_conn(struct Connection *conn) {
	conn->isset = false;
	conn->name = NULL;
	conn->host = NULL;
	conn->port = NULL;
	conn->iport = 0;
	conn->user = NULL;
	conn->pass = NULL;
	conn->ssh_tunnel = NULL;
}
void free_conn(struct Connection *conn) {
	free(conn->name);
	free(conn->host);
	free(conn->port);
	free(conn->user);
	free(conn->pass);
	free(conn->ssh_tunnel);
}
#define CONNECTION_COUNT 20
struct Connection *app_con = NULL;
struct Connection* app_cons[CONNECTION_COUNT];
int app_con_count = 0;
bool conn_established = false;
bool db_selected = false;
MYSQL *selected_mysql_conn = NULL;
#define LOCALHOST "127.0.0.1"
#define DB_NAME_LEN 64
char db_name[DB_NAME_LEN];

#define QUERY_HIST_LEN	10
#define QUERY_MAX	4096
char query_history[QUERY_HIST_LEN][QUERY_MAX];
char the_query[QUERY_MAX];
MYSQL_RES* the_result = NULL;
int the_num_fields=0, the_num_rows=0, the_aff_rows=0;
bool result_rerender_full = true, result_rerender_focus = false;
int last_focus_cell_r = 0;
int focus_cell_r = 0, focus_cell_c = 0, focus_cell_c_pos = 0, focus_cell_c_width = 0, focus_cell_r_pos = 0;


//////////////////////////////////////
// STATE MACHINES

enum APP_STATE {
	APP_STATE_START,
	APP_STATE_ARGS_TO_CONNECTION,
	APP_STATE_LOAD_CONNECTION_FILE,
	APP_STATE_CONNECTIONS_PARSED,
	APP_STATE_CONNECTION_SELECT,
	APP_STATE_FORK_SSH_TUNNEL,
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

enum RESULT_STATE {
	RESULT_STATE_NAVIGATE,
	RESULT_STATE_EDIT_CELL,
	RESULT_STATE_VIEW_CELL,
};
enum RESULT_STATE result_state = RESULT_STATE_NAVIGATE;


//////////////////////////////////////
// GLOBAL WINDOWS

WINDOW* error_window;
WINDOW* cmd_window;
WINDOW* str_window;
WINDOW* query_window;
WINDOW* result_pad;
WINDOW* cell_pad;
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


//////////////////////////////////////
// APP SETUP METHODS

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
	set_escdelay(100); // lower time to detect escape delay
	curs_set(0); // hide cursor
	noecho();
	keypad(stdscr, true); // turn on F key listening
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
	result_pad = newpad(1,1);
	cell_pad = newpad(1,1);

	app_ui_layout();

	strclr(the_query, QUERY_MAX);

	// listen to os signal
	signal(SIGWINCH, on_term_resize);
}

void app_teardown() {
	delwin(cell_pad);
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


//////////////////////////////////////
// WINDOW RENDER METHODS

void display_cmdf(char *mode, int arglen, ...) {
	int sx, sy;
	getmaxyx(stdscr, sy, sx);

	// render mode
	wmove(cmd_window, 0, 0);
	wclrtoeol(cmd_window);
	wattrset(cmd_window, COLOR_PAIR(COLOR_CYAN_BLACK) | A_BOLD);
	waddstr(cmd_window, mode);
	wattrset(cmd_window, COLOR_PAIR(COLOR_WHITE_BLACK));

	// render args
	wattrset(cmd_window, COLOR_PAIR(COLOR_WHITE_BLACK));
	va_list argptr;
	va_start(argptr, arglen);
	int pos = 12;
	for (int i=0; i < arglen; i++) {
		char* cmd = va_arg(argptr, char*);
		wmove(cmd_window, 0, pos);
		waddstr(cmd_window, cmd);
		pos += maxi(12, strlen(cmd) + 2);
	}
	va_end(argptr);

	// render title on right side
	int tsz = strlen(app_con->host) + 1 + strlen(db_name);
	char title[tsz + 1];
	sprintf(title, "%s %s", app_con->host, db_name);
	int size = strlen(title);
	wmove(cmd_window, 0, sx - size);
	waddstr(cmd_window, title);

	wrefresh(cmd_window);
}

void display_strf(char *format, ...) {
	int sx, sy;
	getmaxyx(stdscr, sy, sx);
	char str[sx];
	strclr(str, sx);

	va_list argptr;
	va_start(argptr, format);
	vsnprintf(str, sx - 1, format, argptr);
	va_end(argptr);

	wbkgd(str_window, COLOR_PAIR(COLOR_BLACK_WHITE));
	wmove(str_window, 0, 0);
	wclrtoeol(str_window);
	wattrset(str_window, COLOR_PAIR(COLOR_BLACK_WHITE) | A_BOLD);
	waddstr(str_window, str);

	wrefresh(str_window);
}

void display_str(char *str) {
	display_strf("%s", str);
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

	// because the mac version of ncurses does not handle
	// wbkgd commands correctly i will need to fill it manually
	wattrset(error_window, COLOR_PAIR(COLOR_YELLOW_RED));
	int my, mx;
	getmaxyx(error_window, my, mx);
	for (int r=0; r<my; r++) {
		for (int c=0; c<mx; c++) {
			wmove(error_window, r, c);
			waddch(error_window, ' ');
		}
	}

	// render the error message
	wmove(error_window, 1, 2);
	waddstr(error_window, "ERROR");
	errlinepos = 0;
	wordwrap(string, ERR_WIN_W - 4, display_error_on_line); // avoid gcc nested function supporta

	// render x: close
	wmove(error_window, ERR_WIN_H - 2, 2);
	waddstr(error_window, "x:close");
	//waddch(error_window, 'c');
	//wattrset(error_window, COLOR_PAIR(COLOR_YELLOW_RED));
	//waddstr(error_window, "lose");

	do {
		wrefresh(error_window);
	} while (getch() != KEY_x);
	clear(); refresh();
}


//////////////////////////////////////
// CONNECTION SELECT OPERATION

bool con_selected = true;
int con_select_index = 0;
void on_con_select(char* label) {
	con_selected = true;
}

void run_con_select() {

	int sy,sx;
	getmaxyx(stdscr, sy, sx);
	move(8, sx / 2 - 5);
	attrset(COLOR_PAIR(COLOR_WHITE_BLACK) | A_DIM);
	addstr("CONNECTION");

	con_selected = false;
	refresh();

	// determine widest connection name
	int width = 0;
	for (int i=0; i<app_con_count; i++) {
		width = maxi(width, strlen(app_cons[i]->name));
	}

	// allocate window for db menu
	int frame_width = width + 5;// 7; // |_>_[label]_|
	int frame_height = app_con_count + 2;
	int offset_rows = 10;

	WINDOW *con_win = ui_new_center_win(offset_rows, 0, frame_height, frame_width);
	wbkgd(con_win, COLOR_PAIR(COLOR_WHITE_BLUE));
	ui_clear_win(con_win);
	keypad(con_win, TRUE);

	// allocate menu
	MENU *my_menu;
	ITEM **my_items = (ITEM **)calloc(app_con_count + 1, sizeof(ITEM *));

	// iterate over dbs and add them to the menu as items
	for (int i=0; i<app_con_count; i++) {
		my_items[i] = new_item(app_cons[i]->name, NULL);
		set_item_userptr(my_items[i], on_con_select);
	}
	my_items[app_con_count] = (ITEM*)NULL; // final element should be null
	my_menu = new_menu((ITEM **)my_items);

	// set menu styles and into parent
	set_menu_fore(my_menu, COLOR_PAIR(COLOR_BLACK_CYAN));
	set_menu_back(my_menu, COLOR_PAIR(COLOR_WHITE_BLUE));
	set_menu_grey(my_menu, COLOR_PAIR(COLOR_YELLOW_RED));
	set_menu_mark(my_menu, "");
	set_menu_win(my_menu, con_win);
	set_menu_sub(my_menu, derwin(con_win, frame_height - 2, frame_width - 4, 1, 2)); // (h, w, offy, offx) from parent window

	// post menu to render and draw it first
	post_menu(my_menu);
	wrefresh(con_win);

	// reposition the menu from last selection
	for (int j=0; j < con_select_index; j++) {
		menu_driver(my_menu, REQ_DOWN_ITEM);
	}
	wrefresh(con_win);

	// listen for input for the window selection
	bool done = false;
	while(!done) {
		int c = getch();
		switch(c) {
			case KEY_DOWN:
				menu_driver(my_menu, REQ_DOWN_ITEM);
				con_select_index = clampi(con_select_index + 1, 0, app_con_count - 1);
				break;
			case KEY_UP:
				menu_driver(my_menu, REQ_UP_ITEM);
				con_select_index = clampi(con_select_index - 1, 0, app_con_count - 1);
				break;
			case KEY_RETURN: {
				   void (*callback)(char *);
				   ITEM *cur_item = current_item(my_menu);
				   callback = item_userptr(cur_item);
				   callback((char *)item_name(cur_item));
				   pos_menu_cursor(my_menu);
				   if (con_selected) {
					   done = true;
					   app_con = app_cons[con_select_index]; // selct the connection
					   break;
				   }
			   }
			   break;
			case KEY_x:
			   done = true;
			   break;
		}
		wrefresh(con_win);
	}
	// with DB selected free menu memory
	unpost_menu(my_menu);
	for(int i = 0; i < app_con_count; i++) {
		free_item(my_items[i]); // new_item is only called on 0 thru num_rows
	}
	free_menu(my_menu);
	free(my_items);
	delwin(con_win);

	clear();
}


//////////////////////////////////////
// DATABASE SELECT OPERATION

void on_db_select(char *database) {
	//xlogf("db select %s\n", database);
	db_select(selected_mysql_conn, database);
	db_selected = true;

	// set our db name
	strclr(db_name, DB_NAME_LEN);
	strncpy(db_name, database, DB_NAME_LEN - 1);
	db_name[DB_NAME_LEN - 1] = '\0';
}

int menu_select_pos = 0;
void run_db_select(MYSQL *con, struct Connection *app_con) {

	int sy,sx;
	getmaxyx(stdscr, sy, sx);
	move(8, sx / 2 - 4);
	attrset(COLOR_PAIR(COLOR_WHITE_BLACK) | A_DIM);
	addstr("DATABASE");

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
			while ((row1 = mysql_fetch_row(result))) {
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
			ui_clear_win(db_win);
			keypad(db_win, TRUE);
			// allocate menu
			MENU *my_menu;
			ITEM **my_items = (ITEM **)calloc(num_rows + 1, sizeof(ITEM *));
			// iterate over dbs and add them to the menu as items
			int i=0;
			MYSQL_ROW row2;
			mysql_data_seek(result, 0);
			while ((row2 = mysql_fetch_row(result))) {
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
			for(int i = 0; i < num_rows; i++) {
				free_item(my_items[i]); // new_item is only called on 0 thru num_rows
			}
			free_menu(my_menu);
			free(my_items);
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


//////////////////////////////////////
// TABLE AND QUERY EXECUTION OPERATIONS

void refresh_tables() {

	if (tbl_result != NULL)
		mysql_free_result(tbl_result);

	tbl_index = 0; // default to first table
	tbl_count = 0;
	tbl_result = NULL;

	// populate the db table listing
	int num_fields, num_rows, aff_rows, errcode;
	tbl_result = db_query(selected_mysql_conn, "SHOW TABLES", &num_fields, &num_rows, &aff_rows, &errcode);
	// TODO handle errocode failure
	//xlogf("SHOW TABLES: %d, %d, %d\n", num_rows, num_fields, errcode);
	tbl_count = num_rows;
}

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

int calc_result_pad_width();
int calc_result_pad_height();

void execute_query(bool clear) {

	// shift on to history
	//for (int i=1; i < QUERY_HIST_LEN; i++) {
	//	strncpy(query_history[i - 1], query_history[i], QUERY_MAX);
	//}
	//strncpy(query_history[QUERY_HIST_LEN - 1], the_query, QUERY_MAX);

	int errcode;
	xlog(the_query);
	the_result = db_query(selected_mysql_conn, the_query, &the_num_fields, &the_num_rows, &the_aff_rows, &errcode);
	if (!the_result && errcode > 0) { // inserts have null responses
		display_error(mysql_error(selected_mysql_conn));
	}

	// resize result pad to match size of result set plus its dividers etc
	// so, as it is the widths and heights work great for resizing the pad
	// however when rendering the resized pad ncurses draws old areas
	// so because of this limitation I am going to make the min sizes
	// no smaller than the screen sizes
	int result_h=calc_result_pad_height(), result_w = calc_result_pad_width();
	int sy,sx;
	getmaxyx(stdscr, sy, sx);
	result_h = maxi(result_h, sy);
	result_w = maxi(result_w, sx);
	wresize(result_pad, result_h, result_w);

	// there is also the potential that the query changed the database with USE database
	// so lets refresh tables when we execute

	// we need to see if the database has changed from our previous selection
	// and if so refresh the tables, we don't want to do this if the database
	// has not changed as that can affect the table set
	char buffer[DB_NAME_LEN];
	db_get_db(selected_mysql_conn, buffer, DB_NAME_LEN);
	if (strcmp(db_name, buffer) != 0) {
		strncpy(db_name, buffer, DB_NAME_LEN - 1);
		db_name[DB_NAME_LEN - 1] = '\0';
		refresh_tables();
	}

	// reset our properties for the execution
	result_rerender_full = true;
	result_rerender_focus = false;

	//if (clear) {
	//	last_focus_cell_r = 0;
	//	focus_cell_r = 0;
	//	focus_cell_c = 0;
	//	focus_cell_c_width = 0;
	//	focus_cell_c_pos = 0;
	//	focus_cell_r_pos = 0;
	//} else {
	//	last_focus_cell_r = mini(last_focus_cell_r, the_num_rows + 1 - 1);
	//	focus_cell_r = mini(focus_cell_r, the_num_rows + 1 - 1); // +1 for header row, -1 to make index isntead osize
	//	focus_cell_c = mini(focus_cell_c, the_num_fields - 1);
	//}
	last_focus_cell_r = mini(last_focus_cell_r, the_num_rows + 1 - 1);
	focus_cell_r = mini(focus_cell_r, the_num_rows + 1 - 1); // +1 for header row, -1 to make index isntead osize
	focus_cell_c = mini(focus_cell_c, the_num_fields - 1);
}

void clear_query(bool clear_results) {
	the_num_fields = 0;
	the_num_rows = 0;
	the_aff_rows = 0;
	if (the_result) {
		mysql_free_result(the_result); // free sql memory
		the_result = NULL;
	}
	strclr(the_query, QUERY_MAX);
	if (clear_results) {
		result_rerender_full = true;
		result_rerender_focus = false;
		last_focus_cell_r = 0;
		focus_cell_r = 0;
		focus_cell_c = 0;
		focus_cell_c_width = 0;
		focus_cell_c_pos = 0;
		focus_cell_r_pos = 0;

		ui_clear_win(result_pad);
	}
}

bool get_focus_row_val(char *fieldname, char *buffer, int len) {
	if (!the_result || the_num_rows == 0 || the_num_fields == 0)
		return false;
	if (focus_cell_r == 0) {
		return false;
	} else {
		mysql_data_seek(the_result, focus_cell_r - 1);
		MYSQL_ROW row = mysql_fetch_row(the_result);
		MYSQL_FIELD *f; int c=0;
		mysql_field_seek(the_result, 0);
		while ((f = mysql_fetch_field(the_result))) {
			if (strcmp(f->name, fieldname) == 0) {
				strncpy(buffer, row[c], len - 1);
				buffer[len - 1] = '\0';
				return true;
			}
			c++;
		}
		return false;
	}
	return false;
}

bool get_focus_data(char *buffer, int len, bool clean) {
	if (!the_result || the_num_rows == 0 || the_num_fields == 0)
		return false;
	if (focus_cell_r == 0) {
		mysql_field_seek(the_result, focus_cell_c);
		MYSQL_FIELD *fh = mysql_fetch_field(the_result);
		strncpy(buffer, fh->name, len - 1);
		buffer[len - 1] = '\0';
		return true;
	} else {
		mysql_data_seek(the_result, focus_cell_r - 1);
		MYSQL_ROW row = mysql_fetch_row(the_result);
		if (row[focus_cell_c] != NULL && strlen(row[focus_cell_c]) > 0) {
			strncpy(buffer, row[focus_cell_c], len - 1);
			buffer[len - 1] = '\0';
			if (clean) {
				strflat(buffer);
			}
			return true;
		}
		strclr(buffer, len);
		return true;
	}
	return true;
}

MYSQL_FIELD* get_focus_field() {
	if (!the_result || the_num_rows == 0 || the_num_fields == 0)
		return NULL;
	mysql_field_seek(the_result, focus_cell_c);
	MYSQL_FIELD *fh = mysql_fetch_field(the_result);
	return fh;
}

int get_min_cell_size(MYSQL_FIELD *f) {
	unsigned long max_field_length = maxi(5, maxi(f->max_length, f->name_length)); // size of biggest value in column
	// max size of the field is 32 characters rendered
	if (max_field_length > 32)
		max_field_length = 32;

	// min size is 6 so we can support "empty" data
	if (max_field_length < 5)
		max_field_length = 5;

	return (int)max_field_length;
}

int calc_result_pad_width() {
	// looks at result to determine size of result pad width
	if (!the_result) {
		// if no result then we may have run an execute so lets just give it room
		// to rendere affected rows
		return 64;
	}

	// if we have results we need to calcaulate the width of the columns + dividers
	int width = 0;
	MYSQL_FIELD *fh;
	mysql_field_seek(the_result, 0);
	while ((fh = mysql_fetch_field(the_result))) {
		int imaxf = get_min_cell_size(fh); // size of string data to print
		width += (imaxf + 3); // plus size of divider
	}

	return width;
}

int calc_result_pad_height() {
	// looks at result to determine size of result pad width
	if (!the_result) {
		// if no result then we may have run an execute so lets just give it room
		// to rendere affected rows
		return 1;
	}

	// if we have results we need to calcaulate the width of the rowsize + header
	if (the_num_rows == 0)
		return 1 + 1; // header plus "no results" line

	return the_num_rows + 1; // rows and header
}


//////////////////////////////////////
// RESULT RENDER METHODS

void render_result_divider() {
	wattrset(result_pad, COLOR_PAIR(COLOR_WHITE_BLACK));
	waddch(result_pad, ' ');
	wattrset(result_pad, COLOR_PAIR(COLOR_BLACK_BLUE));
	waddch(result_pad, ' ');
	wattrset(result_pad, COLOR_PAIR(COLOR_WHITE_BLACK));
	waddch(result_pad, ' ');
}

void render_result_header(int *render_row) {

	int result_row = *render_row;
	int result_field = 0;
	wmove(result_pad, result_row, 0);
	MYSQL_FIELD *fh;
	mysql_field_seek(the_result, 0);
	while ((fh = mysql_fetch_field(the_result))) {

		int cury,curx;
		getyx(result_pad, cury, curx);

		int imaxf = get_min_cell_size(fh);
		char buffer[imaxf + 1]; // plus 1 for for guaranteeing terminating null character
		strclr(buffer, imaxf + 1);
		snprintf(buffer, imaxf + 1, "%*s", imaxf, fh->name);

		// cell coloring
		int attrs;
		if (interact_state == INTERACT_STATE_RESULTS) {
			if (focus_cell_r == result_row && focus_cell_c == result_field) {
				attrs = COLOR_PAIR(COLOR_BLACK_CYAN) | A_UNDERLINE;
				focus_cell_c_width = imaxf;
				focus_cell_c_pos = curx;
				focus_cell_r_pos = result_row;
			} else {
				attrs = COLOR_PAIR(COLOR_WHITE_BLACK) | A_UNDERLINE | A_BOLD;
			}
		} else {
			attrs = COLOR_PAIR(COLOR_WHITE_BLACK) | A_UNDERLINE;
		}
		wattrset(result_pad, attrs);
		waddstr(result_pad, buffer);

		// column divider
		render_result_divider();

		result_field++;
	}

	result_field = 0;
	result_row++;
}

void render_result_row(int row_index, int *render_row) {

	int result_row = *render_row;
	int result_field = 0;

	MYSQL_ROW row;
	mysql_data_seek(the_result, row_index);
	if ((row = mysql_fetch_row(the_result))) {
		wmove(result_pad, result_row, 0);

		int i=-1;
		MYSQL_FIELD *f;
		mysql_field_seek(the_result, 0);
		while ((f = mysql_fetch_field(the_result)) && ++i > -1) {

			int cury,curx;
			getyx(result_pad, cury, curx);

			int imaxf = get_min_cell_size(f);
			bool isnull = !row[i];
			bool isempty = !isnull && strlen(row[i]) == 0;

			// determine cell style
			int attrs;
			if (focus_cell_r == result_row && focus_cell_c == result_field) {
				attrs = COLOR_PAIR(COLOR_BLACK_CYAN);
				focus_cell_c_width = imaxf;
				focus_cell_c_pos = curx;
				focus_cell_r_pos = result_row;
			} else if (isnull) {
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
						attrs = COLOR_PAIR(COLOR_CYAN_BLACK);
						break;
					case MYSQL_TYPE_DATETIME:
					case MYSQL_TYPE_DATE:
					case MYSQL_TYPE_TIME:
					case MYSQL_TYPE_TIMESTAMP:
						attrs = COLOR_PAIR(COLOR_MAGENTA_BLACK);
						break;
					default:
						attrs = COLOR_PAIR(COLOR_WHITE_BLACK);
						break;
				}
			}
			wattrset(result_pad, attrs);

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
				snprintf(buffer, imaxf + 1, "%*s", imaxf, row[i]);
				strflat(buffer);
			}
			waddstr(result_pad, buffer);

			// column divider
			render_result_divider();

			result_field++;

		} // eo column loop

		result_row++;
		result_field = 0;
	} // eo if row
}


//////////////////////////////////////
// CELL INSPECTION METHODS

void run_edit_focused_cell() {

	if (!the_result || the_num_rows == 0 || focus_cell_r <= 0)
		return;

	MYSQL_FIELD *f = get_focus_field();
	if (f == NULL) {
		display_error("Unable to locate field");
		return;
	}

	if (!f->table) {
		display_error("Unable to determine table associated with this result field");
		return;
	}

	// Get primary key data: key name, key value for this row
	// SHOW KEYS FROM test_table WHERE Key_name = "PRIMARY"
	// get field value for "Column_name"
	// is this possible? we cant actually edit cells from SELECT results
	// TODO support the MUL key types where there are multiple values that are the primary key
	// TODO support multiple primary keys too, this one is dangerously hardcoded to a single one
	char primary_key[64];
	strclr(primary_key, 64);
	bool pk_found = db_get_primary_key(selected_mysql_conn, f->table, primary_key, 64);
	if (!pk_found) {
		display_error("No primary key could be determined for this field's table");
		return;
	}

	// Get primary key value from this row of data...
	char primary_val[256];
	strclr(primary_val, 256);
	bool pk_val_found = get_focus_row_val(primary_key, primary_val, 256);
	if (!pk_val_found) {
		display_error("No primary key value be determined for this field's row");
		return;
	}

	int imaxf = f->max_length + 1; // max length of data in row
	char buffer[imaxf];
	strclr(buffer, imaxf);
	if (!get_focus_data(buffer, imaxf, false)) {
		display_error("Unable to fetch field contents for editing");
		return;
	}

	// render the pad to edit
	clear();
	refresh();
	ui_clear_win(cell_pad);
	int sy,sx;
	getmaxyx(stdscr, sy, sx);
	wresize(cell_pad, sy - 2, sx);
	wattrset(cell_pad, COLOR_PAIR(COLOR_WHITE_BLUE));
	wmove(cell_pad, 0, 0);
	waddstr(cell_pad, buffer);

	char edited[imaxf];
	//int pad_y=0, pad_x=0, scr_y=1, scr_x=2, scr_y_end=sy-2, scr_x_end=sx-3;
	int pad_y=0, pad_x=0, scr_y=0, scr_x=0, scr_y_end=sy-1, scr_x_end=sx-1;
	enum M {
		TEXT_EDITOR,
		CONFIRM
	};
	enum M mode = TEXT_EDITOR;
	bool editing=true;
	while (editing) {
		switch (mode) {
			case TEXT_EDITOR: {
				xlog("   - TEXT_EDITOR");
				display_strf("UPDATE %s SET %s='%%s' WHERE %s='%s'", f->table, f->name, primary_key, primary_val);
				display_cmdf("EDITING", 1, "[^x][esc]=done");

				// prefresh(pad, y-inpad,x-inpad, upper-left:y-inscreen,x-inscreen, lower-right:y-inscreen,x-onscreen)
				prefresh(cell_pad, pad_y, pad_x, scr_y, scr_x, scr_y_end, scr_x_end);

				xlog(buffer);
				strclr(edited, imaxf);
				wmove(cell_pad, 0, 0);
				nc_text_editor_pad(cell_pad, edited, imaxf, pad_y, pad_x, scr_y, scr_x, scr_y_end, scr_x_end);
				xlogf("[%s]\n", edited);

				mode = CONFIRM;
				break;
			}
			case CONFIRM:
				xlog("   - CONFIRM");
				display_cmdf("SAVE?", 3, "[y]=yes", "[n]=no", "[e]=edit");
				switch (getch()) {
					case KEY_e:
						mode = TEXT_EDITOR;
						break;
					case KEY_y: {
						// execute query in the background, then we need to refresh the existing query
						unsigned long e_imaxf = imaxf * 2 + 1; //https://dev.mysql.com/doc/c-api/8.0/en/mysql-real-escape-string-quote.html
						char escaped[e_imaxf]; // escape string version
						strclr(escaped, e_imaxf);
						unsigned long p = mysql_real_escape_string_quote(selected_mysql_conn, escaped, edited, imaxf - 1, '\'');
						//xlogf("EDITED: [%s]\n", edited);
						//xlogf("ESCAPE: [%s]\n", escaped);
						// detect if "NULL" was entered
						int nf, nr, ar, ec;
						MYSQL_RES *result = NULL;
						//xlogf("NULL CHECK: %s NULL=%d null=%d\n", edited, strcmp(edited, "NULL"), strcmp(edited, "null"));
						if (strcmp(edited, "NULL") == 0 || strcmp(edited, "null") == 0) {
							// null
							result = db_queryf(
								selected_mysql_conn, &nf, &nr, &ar, &ec,
								"UPDATE `%s` SET `%s`=NULL WHERE `%s` = '%s'\n", f->table, f->name, primary_key, primary_val
							);
						} else {
							// value
							result = db_queryf(
								selected_mysql_conn, &nf, &nr, &ar, &ec,
								"UPDATE `%s` SET `%s`='%s' WHERE `%s` = '%s'\n", f->table, f->name, escaped, primary_key, primary_val
							);
						}
						if (!result && ec > 0) { // inserts have null responses
							display_error(mysql_error(selected_mysql_conn));
						}
						// refresh result query
						execute_query(false);
						editing = false;
						clear();
						refresh();
						break;
					}
					case KEY_n:
					case KEY_x:
						editing = false;
						clear();
						refresh();
						break;
				}
				break;
		} // eo mode switch
	} // eo edit loop
}

void run_view_focused_cell() {

	if (!the_result || the_num_rows == 0 || focus_cell_r <= 0)
		return;

	MYSQL_FIELD *f = get_focus_field();
	if (f == NULL) {
		display_error("Unable to locate field");
		return;
	}

	// get the data
	// TODO make the pad height variable to fit longer text

	int imaxf = f->max_length + 1; // max length of data in row
	char buffer[imaxf];
	strclr(buffer, imaxf);
	if (!get_focus_data(buffer, imaxf, false)) {
		display_error("Unable to fetch field contents for viewing");
		return;
	}

	clear();
	refresh();
	ui_clear_win(cell_pad);
	int sy,sx;
	getmaxyx(stdscr, sy, sx);
	wresize(cell_pad, sy - 2, sx);
	wattrset(cell_pad, COLOR_PAIR(COLOR_WHITE_BLACK));
	wmove(cell_pad, 0, 0);
	waddstr(cell_pad, buffer);

	//int pad_y=0, pad_x=0, scr_y=1, scr_x=2, scr_y_end=sy-2, scr_x_end=sx-3;
	int pad_y=0, pad_x=0, scr_y=0, scr_x=0, scr_y_end=sy-1, scr_x_end=sx-1;
	do {
		display_strf("");
		display_cmdf("VIEW DATA", 1, "[x]=done");
		// prefresh(pad, y-inpad,x-inpad, upper-left:y-inscreen,x-inscreen, lower-right:y-inscreen,x-onscreen)
		prefresh(cell_pad, pad_y, pad_x, scr_y, scr_x, scr_y_end, scr_x_end);
	} while(getch() != KEY_x);

	clear();
	refresh();
}


//////////////////////////////////////
// MAIN STATE MACHINE FOR INTERACTING
// WITH SELECTED DATABASE

bool cell_sort_asc = true;
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

			refresh_tables();

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

			clear();
			db_state = DB_STATE_INTERACT;

			break;

		case DB_STATE_INTERACT:
			xlog(" DB_STATE_INTERACT");
			refresh();
			// draw the panels

			////////////////////////////////////////////////
			// PRINT THE TABLES
			ui_clear_win(tbl_pad);
			int r = 0, c = 0;
			if (tbl_result && tbl_count > 0) {
				MYSQL_ROW row;
				//wbkgdset(tbl_pad, COLOR_PAIR(COLOR_WHITE_BLUE)); // on OSX this does not play well with wattrset for background colors
				mysql_data_seek(tbl_result, 0);
				while ((row = mysql_fetch_row(tbl_result))) {
					wmove(tbl_pad, r, c);
					// highlight focused table
					if (r == tbl_index) {
						if (interact_state == INTERACT_STATE_TABLE_LIST){
							wattrset(tbl_pad, COLOR_PAIR(COLOR_BLACK_CYAN));
						}else{
							wattrset(tbl_pad, COLOR_PAIR(COLOR_CYAN_BLUE));
						}
					} else {
						wattrset(tbl_pad, COLOR_PAIR(COLOR_WHITE_BLUE));
					}
					// print table name with padding for focused background coloring
					char buffer[tbl_pad_w];
					sprintf(buffer, TBL_STR_FMT, row[0]);
					waddstr(tbl_pad, buffer);
					r++;
				}
			}

			// wbkgdset does not play well with wattrset backgrounds on top of them on OS X
			// so I am filling backgrounds manually when I need them
			while (r < TBL_PAD_H) {
				wmove(tbl_pad, r, c);
				ui_color_row(tbl_pad, COLOR_PAIR(COLOR_WHITE_BLUE));
				r++;
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
			if (interact_state == INTERACT_STATE_QUERY)
				wattrset(query_window, COLOR_PAIR(COLOR_WHITE_BLACK) | A_BOLD);
			else
				wattrset(query_window, COLOR_PAIR(COLOR_WHITE_BLACK));
			wmove(query_window, 0, 0);
			waddstr(query_window, the_query);
			wrefresh(query_window);

			//////////////////////////////////////////////
			// PRINT THE RESULTS PANEL
			if (result_rerender_full || result_rerender_focus) {
				if (the_result) {

					if (result_rerender_focus) {
						// This is a performance improvement, if we are changing what
						// cell we are focused on we need to rerender those rows only for performance
						// improvements
						xlogf("  - RERENDER FOCUSED %d %d\n", last_focus_cell_r, focus_cell_r);
						int result_row = 0;
						if (last_focus_cell_r == 0 || focus_cell_r == 0) {
							render_result_header(&result_row);
						}
						if (last_focus_cell_r > 0 || focus_cell_r > 0) {
							result_row = last_focus_cell_r;
							render_result_row(result_row - 1, &result_row);
							result_row = focus_cell_r;
							render_result_row(result_row - 1, &result_row);
						}
					} else {
						xlog("  - RERENDER FULL");
						// Do a full rerender of the result set
						//wbkgd(result_pad, COLOR_PAIR(COLOR_WHITE_BLACK));
						ui_clear_win(result_pad);
						// print header
						int result_row = 0;
						render_result_header(&result_row);
						result_row++;

						int result_field = 0;
						if (the_num_rows == 0) {
							// print empty if no rows
							wmove(result_pad, result_row++, 0);
							wattrset(result_pad, COLOR_PAIR(COLOR_YELLOW_BLACK));
							waddstr(result_pad, "no results");
						} else {
							// print rows
							for (int i=0; i < the_num_rows; i++) {
								render_result_row(i, &result_row);
								result_row++;
							}
						}
					}
				} // eo if results
				else if (!the_result && the_aff_rows) {
					// no results but a query did affect some rows so print the info for that
					ui_clear_win(result_pad);
					char buffer[64];
					strclr(buffer, 64);
					sprintf(buffer, "%d %s", the_aff_rows, "affected row(s)");

					wmove(result_pad, 0, 0);
					wattrset(result_pad, COLOR_PAIR(COLOR_YELLOW_BLACK));
					waddstr(result_pad, buffer);
				} // eo if affected rows

				result_rerender_focus = false;
				result_rerender_full = false;
			} // eo if rerender


			//////////////////////////////////////////////
			// PRINT THE STRING BAR
			// PRINT THE COMMAND BAR
			switch (interact_state) {
				case INTERACT_STATE_TABLE_LIST: {
					// string bar
					// print the table title fully since they can be cut off
					if (tbl_result && tbl_count > 0) {
						mysql_data_seek(tbl_result, tbl_index);
						MYSQL_ROW r = mysql_fetch_row(tbl_result);
						display_str(r[0]);
					} else {
						display_str("");
					}

					// command bar
					display_cmdf("TABLES", 6, "[enter]=select-100", "[k]=keys", "[d]=describe", "[c]=show-create", "[tab]=mode", "[x]=exit");// "s/enter:select-100 | k:keys | d:describe | tab:next | x:close");
					break;
				}
				case INTERACT_STATE_QUERY:
					// string bar (none)
					display_str("");

					// command bar
					if (query_state == QUERY_STATE_COMMAND)
						display_cmdf("QUERY", 5, "[e][i]=edit", "[enter]=execute", "[del]=clear", "[tab]=mode", "[x]=exit");// "e/i:edit | enter:execute | del:clear | tab:next | x:close");
					else
						display_cmdf("EDIT QUERY", 1, "[^x/esc]=done");// "ctrl+x/esc:no-edit | tab:next | x:close");
					break;
				case INTERACT_STATE_RESULTS: {
					// string bar (none)
					char buffer[256];
					if (get_focus_data(buffer, 256, true)) {
						strstripspaces(buffer);
						display_str(buffer);
					} else
						display_str("");

					// command bar
					if (the_num_rows > 0 && focus_cell_r > 0)
						display_cmdf("RESULTS", 4, "[v]=view", "[e]=edit", "[tab]=mode", "[x]=exit");
					else if (the_num_rows > 0 && focus_cell_r == 0) // header
						display_cmdf("RESULTS", 3, "[s]=sort", "[tab]=mode", "[x]=exit");
					else
						display_cmdf("RESULTS", 2, "[tab]=mode", "[x]=exit");
					break;
				}
			}


			// pads need to be refreshed after windows
			// prefresh(pad, y-inpad,x-inpad, upper-left:y-inscreen,x-inscreen, lower-right:t-inscreen,x-onscreen)
			int sx, sy;
			getmaxyx(stdscr, sy, sx);
			prefresh(tbl_pad, pad_row_offset,0, 0,0, tbl_render_h,tbl_render_w);

			// NEW spacing
			// get focus cell x + focus_cell width
			// if wider than tbl_render_w then shift pad the difference
			int rp_width = sx - tbl_render_w - 3; // gutter + len/index swap
			int rp_height = sy - QUERY_WIN_H - 1 - 2 - 1; // gutter - bars - len/index swap
			// pad coords to render, shifted to ensure our focused cell is visible
			int rp_shift_c = 0;
			int x_overhang_amt = (focus_cell_c_pos + focus_cell_c_width + 3) - rp_width; // plus 3 for spacing + vert bar divider
			if (x_overhang_amt > 0)
				rp_shift_c = x_overhang_amt;
			int rp_shift_r = 0;
			int y_overhang_amt = focus_cell_r_pos - rp_height;
			if (y_overhang_amt > 0)
				rp_shift_r = y_overhang_amt;
			// screen coords to render at, top left and bottom right
			int scrn_y = QUERY_WIN_H + 1;
			int scrn_x = tbl_render_w + 1 + 2 + 1;
			int scrn_y_end = sy-3;
			int scrn_x_end = sx-1;
			prefresh(result_pad, rp_shift_r,rp_shift_c, scrn_y,scrn_x, scrn_y_end,scrn_x_end);


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
							result_rerender_full = true;
							break;
						case KEY_d:
							if (tbl_result && tbl_count > 0) {
								mysql_data_seek(tbl_result, tbl_index);
								MYSQL_ROW r = mysql_fetch_row(tbl_result);
								set_queryf("DESCRIBE %s", r[0]);
								execute_query(true);
							}
							break;
						case KEY_c:
							if (tbl_result && tbl_count > 0) {
								mysql_data_seek(tbl_result, tbl_index);
								MYSQL_ROW r = mysql_fetch_row(tbl_result);
								set_queryf("SHOW CREATE TABLE %s", r[0]);
								execute_query(true);
							}
							break;
						case KEY_k:
							if (tbl_result && tbl_count > 0) {
								mysql_data_seek(tbl_result, tbl_index);
								MYSQL_ROW r = mysql_fetch_row(tbl_result);
								set_queryf("SHOW KEYS FROM %s", r[0]);
								execute_query(true);
							}
							break;
						case KEY_RETURN: {
							if (tbl_result && tbl_count > 0) {
								mysql_data_seek(tbl_result, tbl_index);
								MYSQL_ROW r = mysql_fetch_row(tbl_result);
								set_queryf("SELECT * FROM %s LIMIT 100", r[0]);
								execute_query(true);
							}
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
								execute_query(true);
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
								result_rerender_full = true;
								break;
							case KEY_DC:
							case KEY_BACKSPACE:
								// Clear the editor
								clear_query(false);
								break;
							case KEY_i:
							case KEY_e:
								// Insert mode
								query_state = QUERY_STATE_EDIT;
								break;
						}
					} else if(query_state == QUERY_STATE_EDIT) {
						xlog("   QUERY_STATE_EDIT");

						char buffer[QUERY_MAX];
						nc_text_editor_win(query_window, buffer, QUERY_MAX);

						// capture query
						set_query(buffer);

						// editor has been escaped
						query_state = QUERY_STATE_COMMAND;
					}
					break;
				}
				case INTERACT_STATE_RESULTS: {
					xlog("  INTERACT_STATE_RESULTS");

					switch (result_state) {
						case RESULT_STATE_NAVIGATE: {
							xlog("   RESULT_STATE_NAVIGATE");
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
									result_rerender_full = true;
									break;
								case KEY_UP:
									last_focus_cell_r = focus_cell_r;
									focus_cell_r = maxi(0, focus_cell_r - 1);
									result_rerender_focus = true;
									break;
								case KEY_DOWN:
									last_focus_cell_r = focus_cell_r;
									focus_cell_r = mini(the_num_rows, focus_cell_r + 1);
									result_rerender_focus = true;
									break;
								case KEY_LEFT:
									focus_cell_c = maxi(0, focus_cell_c - 1);
									result_rerender_focus = true;
									break;
								case KEY_RIGHT:
									focus_cell_c = mini(the_num_fields - 1, focus_cell_c + 1);
									result_rerender_focus = true;
									break;
								case KEY_PPAGE: // pageup
									last_focus_cell_r = focus_cell_r;
									focus_cell_r = maxi(0, focus_cell_r - 20);
									result_rerender_focus = true;
									break;
								case KEY_NPAGE:
									last_focus_cell_r = focus_cell_r;
									focus_cell_r = mini(the_num_rows, focus_cell_r + 20);
									result_rerender_focus = true;
									break;
								case KEY_HOME:
									focus_cell_c = 0;
									result_rerender_focus = true;
									break;
								case KEY_END:
									focus_cell_c = maxi(0, the_num_fields - 1);
									result_rerender_focus = true;
									break;
								case KEY_e:
									// open editor for selected cell if this is not a header and we ahve rows
									if (the_num_rows > 0 && focus_cell_r > 0)
										result_state = RESULT_STATE_EDIT_CELL;
									break;
								case KEY_v:
									if (the_num_rows > 0 && focus_cell_r > 0)
										result_state = RESULT_STATE_VIEW_CELL;
									break;
								case KEY_s:
									if (the_num_rows > 0 && focus_cell_r == 0) {
										// technically in workbench a sql call is not re run, but instead the
										// result set is directly sorted
										// this would require us copying the result set into a new array when we execute
										// and then using this copy with all result inspections, loops, etc
										// then this would sort the result set based on the position
										// The other option is to attempt to find the current query and do string
										// manipulation to update or add an ORDER BY clause to it
										// The final option is to do a default select 100 on the fields table
										// with an ORDER BY field ASC/DESC etc on it
										if (tbl_result && tbl_count > 0) {
											mysql_data_seek(tbl_result, tbl_index);
											MYSQL_ROW r = mysql_fetch_row(tbl_result);
											char field_name[256];
											if (get_focus_data(field_name, 256, false)) {
												if (cell_sort_asc)
													set_queryf("SELECT * FROM %s ORDER BY `%s` ASC LIMIT 100", r[0], field_name);
												else
													set_queryf("SELECT * FROM %s ORDER BY `%s` DESC LIMIT 100", r[0], field_name);
												execute_query(true);
												cell_sort_asc = !cell_sort_asc;
											}
										}
									}
							} // eo navigate KEY switch
							break;
						} // eo RESULT_STATE_NAVIGATE case
						case RESULT_STATE_EDIT_CELL:
							xlog("   RESULT_STATE_EDIT_CELL");

							run_edit_focused_cell();

							result_state = RESULT_STATE_NAVIGATE;
							break;
						case RESULT_STATE_VIEW_CELL:
							xlog("   RESULT_STATE_VIEW_CELL");

							run_view_focused_cell();

							result_state = RESULT_STATE_NAVIGATE;
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
			clear_query(true);
			// clear table list query
			if (tbl_result)
				mysql_free_result(tbl_result); // free sql memory
			tbl_result = NULL;
			// unset db
			db_selected = false;
			app_state = APP_STATE_DB_INTERACT_END;
			break;
	}
}


//////////////////////////////////////
// MAIN

char *program_name;
void cli_usage(FILE* f) {
	fprintf(f, "Usage:\n");
	fprintf(f, "  %s -i help\n", program_name);
	fprintf(f, "  %s -h mysql-host [-l port=3306] -u mysql-user [-p mysql-pass] [-s ssh-tunnel-host] [-g log-file]\n", program_name);
	fprintf(f, "  %s -f connection-file=connections.csv [-d delimeter=,] [-g log-file]\n", program_name);
}

void cli_error(char *err) {
	char *red = "\033[0;31m";
	char *green = "\033[0;32m";
	char *blue = "\033[0;34m";
	char *def = "\033[0m";
	if (err && strlen(err)) {
		fprintf(stderr, "%sError:\n  %s: %s %s\n", red, program_name, err, def);
	}
	cli_usage(stderr);
	exit(1);
}

// note, im just referencing argv, not copying them into new buffers, and argc/argv
// "array shall be modifiable by the program, and retain their last-stored values between program startup and program termination."
char *arg_host=NULL, *arg_port=NULL, *arg_user=NULL, *arg_pass=NULL, *arg_tunnel=NULL, *arg_confile=NULL, *arg_logfile=NULL;
char arg_delimeter = ',';
bool arg_info = false;
int parseargs(int argc, char **argv) {
	opterr = 0; // hide error output
	int c;
	while ((c = getopt(argc, argv, "ih:l:u:p:s:f:d:g:")) != -1) {
		switch (c) {
			case 'i': arg_info = true; break;
			case 'h': arg_host = optarg; break;
			case 'l': arg_port = optarg; break;
			case 'u': arg_user = optarg; break;
			case 'p': arg_pass = optarg; break;
			case 's': arg_tunnel = optarg; break;
			case 'f': arg_confile = optarg; break;
			case 'd': arg_delimeter = optarg[0]; break;
			case 'g': arg_logfile = optarg; break;
			case '?': // appears if unknown option when opterr=0
				if (optopt == 'h')
					cli_error("-h missing mysql-host");
				else if (optopt == 'l')
					cli_error("-l missing port");
				else if (optopt == 'u')
					cli_error("-u missing mysql-user");
				else if (optopt == 'p')
					cli_error("-p missing mysql-pass");
				else if (optopt == 's')
					cli_error("-s missing ssh-tunnel-host");
				else if (isprint(optopt)) {
					char b[256]; strclr(b, 256);
					sprintf(b, "unknown option '-%c'", optopt);
					cli_error(b);
				} else
					cli_error("unknown option");
				return 1;
			default:
				cli_error("unknown getopt error");
				return 1;
		}
	}
	return 0;
}

char *scan_pass = NULL;
int s_portmax = 2216;
int s_port = 2200;
int main(int argc, char **argv) {

	// parse our arguments into data
	program_name = argv[0];
	if (parseargs(argc, argv)) {
		return 1;
	}

	if (arg_info) {
		cli_usage(stdout);
		return 0;
	}

	//printf("h:%s l:%s u:%s p:%s s:%s\n", arg_host, arg_port, arg_user, arg_pass, arg_tunnel);
	if (arg_logfile && strlen(arg_logfile))
		xlogopen(arg_logfile, "w+");

	xlog("....... START .......");
	xlogf("MySQL client version: %s\n", mysql_get_client_info());

	selected_mysql_conn = NULL;
	bool run = true;
	while (run) {
		switch (app_state) {
			case APP_STATE_START:
				xlogf("APP_STATE_START\n");

				// init our connection structs
				//app_cons = (struct Connection**)malloc(sizeof(struct Connection*) * CONNECTION_COUNT);
				for (int i=0; i < CONNECTION_COUNT; i++) {
					app_cons[i] = (struct Connection*)malloc(sizeof(struct Connection));
					init_conn(app_cons[i]);
				}

				// If we had some args that specify loading a connection directly then run that
				// otherwise go to file connection loader
				if (arg_host)
					app_state = APP_STATE_ARGS_TO_CONNECTION;
				else
					app_state = APP_STATE_LOAD_CONNECTION_FILE;
				break;

			case APP_STATE_ARGS_TO_CONNECTION:
				xlog("APP_STATE_ARGS_TO_CONNECTION");

				if (arg_host == NULL)
					cli_error("mysql-host is required");
				if (arg_user == NULL)
					cli_error("mysql-user is required");

				// strdups are used to copy global argv values into heap
				// so we can free the connection the same way for cmd line arg strings
				// and file strings from the connection file
				app_con_count++;
				app_cons[0]->isset = true;
				app_cons[0]->name = strdup(arg_host);
				app_cons[0]->host = strdup(arg_host);
				app_cons[0]->user = strdup(arg_user);

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
					app_cons[0]->pass = scan_pass;
				} else {
					app_cons[0]->pass = strdup(arg_pass);
				}

				if (arg_port != NULL && strlen(arg_port) > 0) {
					app_cons[0]->iport = atoi(arg_port);
					app_cons[0]->port = strdup(arg_port);
				} else {
					app_cons[0]->iport = 3306; // default 3306
					app_cons[0]->port = strdup("3306");
				}

				if (arg_tunnel)
					app_cons[0]->ssh_tunnel = strdup(arg_tunnel);

				app_state = APP_STATE_CONNECTIONS_PARSED;

				break;

			case APP_STATE_LOAD_CONNECTION_FILE:
				xlog("APP_STATE_LOAD_CONNECTION_FILE");

				FILE* fp = NULL;
				if (arg_confile) {
					fp = fopen(arg_confile, "r");
				} else {
					char cwd[PATH_MAX];
					if (getcwd(cwd, sizeof(cwd)) == NULL) {
						cli_error("failed to get cwd");
						return 1;
					}
					char *fname = "connections.csv";
					int flen = strlen(cwd) + 1 + strlen(fname);
					char fpath[flen];
					sprintf(fpath, "%s/%s", cwd, fname);
					fp = fopen(fpath, "r");
				}

				if (!fp) {
					// TODO we could someday do a wizard to create the file here, or just do instructions on how to format csv
					cli_error("unable to locate connection file and no connection arguments provided");
				}

				char line[1024];
				int i = 0;
				while (fgets(line, 1024, fp)) {

					// the getfield uses strtok which fubars the data so i copy the line for as many fields, not ideal
					// also we put in the heap so substrings found have stable addresses
					char *tmp_for_name = strdup(line);
					char *tmp_for_host = strdup(line);
					char *tmp_for_port = strdup(line);
					char *tmp_for_user = strdup(line);
					char *tmp_for_pass = strdup(line);
					char *tmp_for_tunnel = strdup(line);

					const char *f_name = scantok(tmp_for_name, 1, arg_delimeter);
					const char *f_host = scantok(tmp_for_host, 2, arg_delimeter);
					const char *f_port = scantok(tmp_for_port, 3, arg_delimeter);
					const char *f_user = scantok(tmp_for_user, 4, arg_delimeter);
					const char *f_pass = scantok(tmp_for_pass, 5, arg_delimeter);
					const char *f_tunnel = scantok(tmp_for_tunnel, 6, arg_delimeter);
					//xlogf("Fields:\n  name=%s\n  host=%s\n  port=%s\n  user=%s\n  pass=%s\n  tunnel=%s\n", f_name, f_host, f_port, f_user, f_pass, f_tunnel);

					// TODO There are memory leaks coming from strdup
					// even though we are running free on the app_conn property
					if (f_name && f_host && f_user && f_pass) {
						xlogf("setting connection entry %d\n", i);
						app_con_count++;
						app_cons[i]->isset = true;
						app_cons[i]->name = strdup(f_name);
						app_cons[i]->host = strdup(f_host);
						if (f_port)
							app_cons[i]->port = strdup(f_port);
						app_cons[i]->user = strdup(f_user);
						app_cons[i]->pass = strdup(f_pass);
						if (f_tunnel)
							app_cons[i]->ssh_tunnel = strdup(f_tunnel);
						if (f_port)
							app_cons[i]->iport = atoi(f_port);
					} else {
						xlogf("skipping connection entry %d\n", i);
					}

					free(tmp_for_pass);
					free(tmp_for_name);
					free(tmp_for_host);
					free(tmp_for_port);
					free(tmp_for_user);
					free(tmp_for_tunnel);

					i++;
				}

				fclose(fp);

				app_state = APP_STATE_CONNECTIONS_PARSED;

				break;

			case APP_STATE_CONNECTIONS_PARSED:
				xlog("APP_STATE_CONNECTIONS_PARSED");

				// All args and connections have been parsed from CLI or File
				// However application and ncurses has not started
				// This is the place to do that prior to starting the connection state machine

				app_setup(); // setup ncurses control

				if (app_con_count == 0) {
					display_error("No connections defined");
					app_state = APP_STATE_END;
				} else {
					app_state = APP_STATE_CONNECTION_SELECT;
				}

				break;

			case APP_STATE_CONNECTION_SELECT:
				xlogf("APP_STATE_CONNECTION_SELECT\n");

				// if only one connection then use it
				// otherwise create a menu to display them

				if (app_con_count == 1) {
					app_con = app_cons[0];
				} else {
					run_con_select();
				}

				if (!app_con) {
					// we exited the connection without a selection
					app_state = APP_STATE_END;
					break;
				}

				// once one has been established run the connect
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
								"ssh -oStrictHostKeyChecking=no -fL :%d:%s:%d -o ExitOnForwardFailure=yes %s sleep 2 > /dev/null 2>&1",
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
				selected_mysql_conn = mysql_init(NULL);
				if (selected_mysql_conn == NULL) {
					display_error(mysql_error(selected_mysql_conn));
					app_state = APP_STATE_END;
					break;
				}

				// If we are connecting through an established tunnel, then target localhost at our local port
				// otherwise connect to server naturally
				unsigned int timeout = 3;
				mysql_options(selected_mysql_conn, MYSQL_OPT_CONNECT_TIMEOUT, &timeout);
				if (app_con->ssh_tunnel)
					selected_mysql_conn = mysql_real_connect(selected_mysql_conn, LOCALHOST, app_con->user, app_con->pass, NULL, s_port, NULL, 0);
				else
					selected_mysql_conn = mysql_real_connect(selected_mysql_conn, app_con->host, app_con->user, app_con->pass, NULL, app_con->iport, NULL, 0);

				if (selected_mysql_conn == NULL) {
					display_error(mysql_error(selected_mysql_conn));
					mysql_close(selected_mysql_conn);
					app_state = APP_STATE_END;
					break;
				}

				conn_established = true;
				app_state = APP_STATE_DB_SELECT;
				break;

			case APP_STATE_DB_SELECT:
				xlogf("APP_STATE_DB_SELECT\n");
				// show menu to select db for the connection
				if (selected_mysql_conn != NULL) {
					xlog("CON STILL PRESENT BEFORE DB SELECT");
					xlog(mysql_get_host_info(selected_mysql_conn));
				}
				run_db_select(selected_mysql_conn, app_con);

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
				run_db_interact(selected_mysql_conn);

				break;

			case APP_STATE_DB_INTERACT_END:
				xlog("APP_STATE_DB_INTERACT_END");
				db_state = DB_STATE_START; // reset db state
				app_state = APP_STATE_DB_SELECT; // go to select db
				break;

			case APP_STATE_DISCONNECT:
				xlogf("APP_STATE_DISCONNECT %s@%s\n", app_con->user, app_con->host);

				// close connection
				mysql_close(selected_mysql_conn);
				selected_mysql_conn = NULL;

				conn_established = false;
				if (app_con_count == 1)
					app_state = APP_STATE_END; // if we ran with only one connection, nothing to choose from
				else
					app_state = APP_STATE_CONNECTION_SELECT; // if more than one connection option, prompt for it again
				app_con = NULL;
				break;

			case APP_STATE_END:
			case APP_STATE_DIE:
				xlogf("APP_STATE_END|DIE\n");
				run = false;
				app_teardown();

				// no longer freeing this directly as it is referenced by the app_con
				// and freed below
				// if (scan_pass)
				// 	free(scan_pass);

				// free connection memory
				for (int i=0; i < CONNECTION_COUNT; i++) {
					if (app_cons[i]->isset) {
						free_conn(app_cons[i]);
					}
					free(app_cons[i]);
				}

				break;
		} // end app fsm
	} // end run loop

	xlog("........ END ........");
	xlogclose();
	return 0;
} // end main

