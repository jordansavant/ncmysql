#include <mysql/mysql.h>
#include <ncurses.h>
#include <menu.h>
#include <stdio.h>
#include <string.h>

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

enum APP_STATE {
	APP_STATE_START,
	APP_STATE_CONNECTION_SELECT,
	APP_STATE_CONNECTION_CREATE,
	APP_STATE_CONNECT,
	APP_STATE_CONNECTION_INTERACT,
	APP_STATE_DISCONNECT,
	APP_STATE_END,
};
enum APP_STATE app_state = APP_STATE_START;

enum CON_STATE {
    CON_STATE_START,
    CON_STATE_SELECT_DB,
    CON_STATE_START_DB,
};
enum CON_STATE con_state = CON_STATE_START;

struct Connection {
	char *host;
	char *user;
	char *pass;
};
struct Connection app_cons[20];

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

MYSQL_RES* con_select(MYSQL *con, char *query, int *num_fields, int *num_rows) {
	if (mysql_query(con, "SHOW DATABASES")){
		// TODO how do we handle errors?
		fprintf(stderr, "%s\n", mysql_error(con));
		return NULL;
	}

	MYSQL_RES *result = mysql_store_result(con);
	if (result == NULL) {
		// TODO how do we handle errors?
		fprintf(stderr, "%s\n", mysql_error(con));
		return NULL;
	}

	*num_fields = mysql_num_fields(result);
	*num_rows = mysql_num_rows(result);
	return result;
}


void on_db_select(char *database) {
	xlogf("db select %s\n", database);
}

void run_con_interact(MYSQL *con, struct Connection *app_con) {

	char *db = NULL;
	con_state = CON_STATE_START;

	int run = 1;
	while (run) {
		switch (con_state) {
			case CON_STATE_START:
				refresh();
				db = NULL;
				con_state = CON_STATE_SELECT_DB;
				break;
			case CON_STATE_SELECT_DB: {
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
				int c;
				while((c = getch()) != KEY_F(1)) {
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
}

int main(int argc, char **argv) {
	if (!ncurses_setup())
		return 1;
	xlogopen("logs/log", "w+");
	xlog("------- START -------");

	xlogf("MySQL client version: %s\n", mysql_get_client_info());

	if (argc < 4) {
		xlogf("ERROR: missing connection arg\n");
		return 1;
	}

	struct Connection *app_con = NULL;
	MYSQL *con = NULL;

	unsigned short run = 1;
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

				app_state = APP_STATE_CONNECTION_INTERACT;
				break;
			case APP_STATE_CONNECTION_INTERACT:
				xlogf("APP_STATE_CONNECTION_INTERACT\n");

                // when the db is selected enter into run_db_interact
                run_con_interact(con, app_con);

				app_state = APP_STATE_DISCONNECT;
				break;
			case APP_STATE_DISCONNECT:
				xlogf("APP_STATE_DISCONNECT %s@%s\n", app_con->user, app_con->host);

				// close connection
				mysql_close(con);

				app_state = APP_STATE_END;
				break;
			case APP_STATE_END:
				xlogf("APP_STATE_END\n");
				run = 0;
				break;
		} // end app fsm
	} // end run loop

	xlogclose();
	ncurses_teardown();
	return 0;
} // end main

