#include <mysql/mysql.h>
#include <ncurses.h>
#include <menu.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "sqlops.h"

#define ARRAY_SIZE(a) (sizeof(a) / sizeof(a[0]))
#define KEY_RETURN	10
#define KEY_ESC		27
#define KEY_SPACE	32
#define KEY_TAB		9

//  dblist v |  query  |  exit
//  -------------------------------------------------
//  table    |  col   col     col     col     col
//  table    |
//  table    |
//  table    |

// modes
// - start
// - no conf
// - select connection
// - table view
// - query view
struct Connection {
	char *host;
	char *user;
	char *pass;
};
struct Connection app_cons[20];
bool conn_established = false;
bool db_selected = false;
MYSQL *selected_mysql_conn = NULL;

enum APP_STATE {
	APP_STATE_START,
	APP_STATE_CONNECTION_SELECT,
	APP_STATE_CONNECTION_CREATE,
	APP_STATE_CONNECT,
	APP_STATE_DB_SELECT,
	APP_STATE_DB_INTERACT,
	APP_STATE_DISCONNECT,
	APP_STATE_END,
	APP_STATE_DIE,
};
enum APP_STATE app_state = APP_STATE_START;

enum CON_STATE {
	CON_STATE_START,
	CON_STATE_SELECT_DB,
	CON_STATE_START_DB,
};
enum CON_STATE con_state = CON_STATE_START;

int maxi(int a, int b) {
	if (a > b)
		return a;
	return b;
}

// get a center a window in std
WINDOW* gui_new_center_win(int row, int width, int height, int offset_x)
{
	int y, x, indent;
	getmaxyx(stdscr, y, x);
	indent = (x - width) / 2 + offset_x;
	return newwin(height, width, row, indent);
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
}

void app_teardown() {
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

void on_db_select(char *database) {
	xlogf("db select %s\n", database);
	db_select(selected_mysql_conn, database);
	db_selected = true;
	app_state = APP_STATE_DB_INTERACT;
}

void run_db_select(MYSQL *con, struct Connection *app_con) {

	switch (con_state) {

		// TODO, should we detect if the database is still selected?
		// and if it changes (eg typed in USE command) we change our state to
		// STATE START?

		case CON_STATE_START:
			xlogf(" CON_STATE_START\n");
			refresh();
			con_state = CON_STATE_SELECT_DB;
			break;

		case CON_STATE_SELECT_DB: {
			xlogf(" CON_STATE_SELECT_DB\n");
			//die("this is a test");
			// Get DBs
			int num_fields, num_rows;
			MYSQL_RES *result = con_select(con, "SHOW DATABASES", &num_fields, &num_rows);
			// determine widest db name
			int dblen = 0;
			MYSQL_ROW row1;
			while (row1 = mysql_fetch_row(result)) {
				int d = (int)strlen(row1[0]);
				dblen = maxi(d, dblen);
			}
			// allocate window for db menu
			int frame_width = dblen + 7; // |_>_[label]_|
			int frame_height = num_rows + 4;
			int offset_rows = 10;
			WINDOW *db_win = gui_new_center_win(offset_rows, frame_width, frame_height, 0);
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
			set_menu_mark(my_menu, "> ");
			set_menu_win(my_menu, db_win);
			set_menu_sub(my_menu, derwin(db_win, frame_height - 2, frame_width - 2, 2, 2)); // (h, w, offy, offx) from parent window
			box(db_win, 0, 0);
			// post menu to render and draw it first
			post_menu(my_menu);
			wrefresh(db_win);
			// listen for input for the window selection
			while(!db_selected) {
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
							   con_state = CON_STATE_START_DB;
							   break;
						   }
					   }
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
			break;
		}
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
					fprintf(stderr, "%s\n", mysql_error(con));
					return 1;
				}
				if (mysql_real_connect(con, app_con->host, app_con->user, app_con->pass, NULL, 0, NULL, 0) == NULL) {
					fprintf(stderr, "%s\n", mysql_error(con));
					mysql_close(con);
					return 1;
				}

				conn_established = true;
				selected_mysql_conn = con;
				app_state = APP_STATE_DB_SELECT;
				break;
			case APP_STATE_DB_SELECT:
				xlogf("APP_STATE_DB_SELECT\n");
				// when the db is selected enter into run_db_interact
				run_db_select(con, app_con);
				break;

			case APP_STATE_DB_INTERACT:
				xlogf("APP_STATE_DB_INTERACT\n");

				//run_dn_interact(con, app_con);

				app_state = APP_STATE_DISCONNECT;
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

