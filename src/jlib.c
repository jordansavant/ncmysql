#include "jlib.h"
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include <stddef.h>
#include <ncurses.h>
#include <mysql/mysql.h>


//////////////////////////////////////
// MATH FUNCTIONS START

int maxi(int a, int b) {
	if (a > b)
		return a;
	return b;
}
int mini(int a, int b) {
	if (a < b)
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

// MATH FUNCTIONS END
//////////////////////////////////////


///////////////////////////////////////
// STRING FUNCTIONS START

int strchrplc(char *str, char orig, char rep) {
	char *ix = str;
	int n = 0;
	while((ix = strchr(ix, orig)) != NULL) {
		*ix++ = rep;
		n++;
	}
	return n;
}

void strstripspaces(char* str) {
	int i, x;
	for(i=x=0; str[i]; ++i)
		if(!isspace(str[i]) || (i > 0 && !isspace(str[i-1])))
			str[x++] = str[i];
	str[x] = '\0';
}

void strflat(char *str) {
	// replace rando whitespace with single spaces
	strchrplc(str, '\t', ' ');
	strchrplc(str, '\n', ' ');
	strchrplc(str, '\r', ' ');
}


void strfill(char *string, int size, char c) {
	for (int i = 0; i < size - 1; i++) {
		string[i] = c;
	}
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

void strsplit(const char *text, char splitter, int m, int n, char words[m][n], int *wordlen) {
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
void strlines(int m, int n, char words[m][n], int sentence_size, int o, int p, char lines[o][p], int *linelen) {
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
void wordwrap(const char *text, int size, void (*on_line)(const char *line)) {
	int wordlen = 0;
	char words[1056][32];
	strsplit(text, ' ', 1056, 32, words, &wordlen);

	int linelen = 0;
	char lines[64][1056];
	strlines(wordlen, 32, words, size, 64, 1056, lines, &linelen);

	for (int i=0; i<linelen; i++) {
		char *line = lines[i]; // null terminated
		on_line(line);
	}
}

/**
 * Takes a line and \0 terminates the delimeters
 * and returns a pointer to the position expected
 * field_pos is the position, not index, eg "1" is first field
 * TODO: does not handle escaped delimiters or quoted values
 * TODO: could make it fancy like strtok and make delim a str of values
 * This was built to replace scanning CSVs to overcome the limits of strtok:
 * - if a blank value is present such as "foo;bar;;zan;" the empty entries are skipped
 * - this is because strtok will scan for the first non-delimeter character as its starting position
 */
char* scantok(char *line, int field_pos, char delim) {
	if (field_pos < 0)
		return 0;

	int i=0;
	int scanpos=0;
	char scanchar;
	int inspected_pos = 0;
	while (inspected_pos < field_pos) {

		scanpos = i;
		do {
			scanchar = line[i];
			i++;
		} while (scanchar != '\n' && scanchar != '\0' && scanchar != delim);
		//xlogf("terminate pos [%d]=%c\n", i-1, line[i-1]);
		line[i-1] = '\0'; // replace delim with terminating character

		inspected_pos++;
	}

	if (strlen(&line[scanpos]) == 0)
		return NULL;

	return &line[scanpos];
}

// STRING FUNCTIONS END
///////////////////////////////////////



///////////////////////////////////////
// NCURSES FUNCTIONS START

void nc_text_editor_pad(WINDOW *pad, char *buffer, int buffer_len, int pad_y, int pad_x, int scr_y, int scr_x, int scr_y_end, int scr_x_end) {
	nc_text_editor(pad, buffer, buffer_len, true, pad_y, pad_x, scr_y, scr_x, scr_y_end, scr_x_end);
}

void nc_text_editor_win(WINDOW *win, char *buffer, int buffer_len) {
	nc_text_editor(win, buffer, buffer_len, false, 0, 0, 0, 0, 0, 0);
}

void nc_text_editor(WINDOW *window, char *buffer, int buffer_len, bool is_pad, int pad_y, int pad_x, int scr_y, int scr_x, int scr_y_end, int scr_x_end) {
	curs_set(1);

	// beg and end are screen coords of editor window
	// min and max are internal window coords
	int begy,begx, endx,endy, miny=0,minx=0, maxy,maxx;
	getbegyx(window, begy, begx);
	getmaxyx(window, maxy, maxx);
	endy = begy + maxy - 1; endx = begx + maxx; // add beginning coordinates to max sizes

	// cur is the position of the cursor within the window
	int cury,curx;
	getyx(window, cury,curx);
	cury += begy; curx += begx;
	if (cury < begy || curx < begx || cury > endy || curx > endx)
		cury=begy, curx=begx; // default to beggining

	move(cury, curx);
	wmove(window, cury - begy, curx - begx);

	bool editing = true;
	while (editing) {
		// Lock down editing to this position
		int key = getch();
		switch (key) {
			case KEY_ctrl_x:
			case KEY_ESC: {
				editing = false;

				// capture and trim the contents of the window
				// convert to character buffer for the line
				chtype chtype_buffers[maxy][maxx];
				strclr(buffer, buffer_len);
				int buffi = 0;
				for (int r=0; r < maxy; r++) {
					wmove(window, r, 0);
					winchstr(window, chtype_buffers[r]);
					// to trim the line we should count backwards until the first character
					// you see maxx + 1 because maxx is the final index, not the size

					// convert chtypes to characters
					int ccount = nc_strtrimlen(chtype_buffers[r], maxx);
					if (maxx + 1 == ccount)
						continue; // all characters are empty

					// ccount is the number of characters on the end
					int strsize = maxx - ccount + 1;
					for (int c=0; c < strsize; c++) {
						buffer[buffi++] = chtype_buffers[r][c] & A_CHARTEXT;
					}
					buffer[buffi++] = '\n';
				}
				// trim final newline
				buffer[buffi-1] = '\0';

				break;
			}
			// MOVE CURSOR
			case KEY_LEFT:
				curx = clampi(curx - 1, begx, endx);
				move(cury, curx);
				wmove(window, cury - begy, curx - begx);
				break;
			case KEY_RIGHT:
				curx = clampi(curx + 1, begx, endx);
				move(cury, curx);
				wmove(window, cury - begy, curx - begx);
				break;
			case KEY_UP:
				cury = clampi(cury - 1, begy, endy);
				move(cury, curx);
				wmove(window, cury - begy, curx - begx);
				break;
			case KEY_DOWN:
				cury = clampi(cury + 1, begy, endy);
				move(cury, curx);
				wmove(window, cury - begy, curx - begx);
				break;
				// TEXT EDITOR
			default:
				// ELSE WE ARE LISTENING TO INPUT AND EDITING THE WINDOW
				if (key > 31 && key < 127) { // ascii input
					int winy, winx;
					getyx(window, winy, winx); // winx is position in line
					int sizeleft = maxx - winx - 1;
					// we need to shift everthing after this position over without overflowing the text area
					chtype contents[maxx];
					winchstr(window, contents); // this captures everything after the cursor
					// insert the character into the line
					waddch(window, key);
					// reinsert the remaining characters
					for (int i=0; i < sizeleft; i++) {
						char c = contents[i] & A_CHARTEXT;
						if (c > 31 && c < 127)
							waddch(window, contents[i]);
					}
					wmove(window, winy, winx+1);
				}
				if (key == '\n') { // new line
					waddch(window, '\n');
				}
				if (key == '\t') {
					// TODO this does not deal well when injecting into middle of string
					waddch(window, '\t');
				}
				if (key == KEY_BACKSPACE || key == KEY_DELETE) { // regular delete
					int winy, winx;
					getyx(window, winy, winx); // winx is position in line

					if (curx - begx == 0) {
						// already at beginning of line
						// clear line and copy contents up to previous line
						int start_winy=winy, start_winx=winx;

						// copy line and clear line
						chtype contents[maxx];
						nc_cutline(window, contents, 0, maxx);

						// go to line above
						winy = clampi(winy - 1, 0, maxy);
						wmove(window, winy, winx);
						int target_winy = winy;

						// go to end of line
						int target_winx = nc_mveol(window);

						// append contents to end of line
						nc_paste(window, contents);

						// repeat for every Y below our original position
						for (int y=start_winy; y<maxy - 1; y++) {
							// move next line to our current line
							winy = clampi(y + 1, 0, maxy);
							winx = 0;
							wmove(window, winy, winx);

							// get line contents
							chtype linecontents[maxx];
							int sz = nc_cutline(window, linecontents, 0, maxx);
							if (sz == 0)
								continue;

							// copy to line above
							winy = clampi(y, 0, maxy);
							wmove(window, winy, winx);
							nc_paste(window, linecontents);
						}
						// reset back to original position
						wmove(window, target_winy, target_winx);
					} else {
						winx = clampi(winx - 1, 0, maxx);
						wmove(window, winy, winx);
						wdelch(window);
					}
				}
				if (key == KEY_DC) { // forward delete
					wdelch(window);
				}

				if (!is_pad)
					wrefresh(window);
				else
					prefresh(window, pad_y, pad_x, scr_y, scr_x, scr_y_end, scr_x_end);

				getyx(window, cury,curx);
				cury += begy; curx += begx;
				break;
		} // eo key switch
	} // eo editing loop

	curs_set(0);
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

	wclrtoeol(win); // delete remainder of line

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

// NCURSES FUNCTIONS END
///////////////////////////////////////



///////////////////////////////////////
// NCURSES UI FUNCTIONS START

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
	init_pair(COLOR_BLACK_BLUE,	COLOR_BLACK,	COLOR_BLUE);
	init_pair(COLOR_BLACK_YELLOW,COLOR_BLACK,	COLOR_YELLOW);
	init_pair(COLOR_BLACK_MAGENTA,COLOR_BLACK,	COLOR_MAGENTA);
	init_pair(COLOR_BLACK_RED,	COLOR_BLACK,	COLOR_RED);
}

WINDOW* ui_new_center_win(int offset_row, int offset_col, int rows, int cols)
{
	int y, x, indent;
	getmaxyx(stdscr, y, x);
	indent = (x - cols) / 2 + offset_col;
	return newwin(rows, cols, offset_row, indent);
}

void ui_center_win(WINDOW* win, int offset_row, int offset_col, int rows, int cols)
{
	int y, x, indent;
	getmaxyx(stdscr, y, x);
	indent = (x - cols) / 2 + offset_col;
	wresize(win, rows, cols);
	mvwin(win, offset_row, indent);
}

void ui_clear_win(WINDOW *win)
{
	for (int i=0; i<getmaxy(win); i++) {
		wmove(win, i,0);
		wclrtoeol(win);
	}
	wrefresh(win);
}


void ui_clear_row(WINDOW *win, int row)
{
	wmove(win, row + 1, 0);
	wclrtoeol(win);
}

void _strfill(char *string, int size, char c) {
	for (int i = 0; i < size - 1; i++) {
		string[i] = c;
	}
}
void ui_color_row(WINDOW *win, int colorpair)
{
	int y,x;
	getmaxyx(win, y, x);
	wattrset(win, colorpair);
	char buffer[x - 1];
	_strfill(buffer, x - 1, ' ');
	waddstr(win, buffer);
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

// NCURSES UI FUNCTIONS END
///////////////////////////////////////



///////////////////////////////////////
// MYSQL FUNCTIONS START

MYSQL_RES* con_select(MYSQL *con, int *num_fields, int *num_rows) {
	if (mysql_query(con, "SHOW DATABASES")) {
		die(mysql_error(con));
	}

	MYSQL_RES *result = mysql_store_result(con);
	if (result == NULL) {
		die(mysql_error(con));
	}

	*num_fields = mysql_num_fields(result);
	*num_rows = mysql_num_rows(result);
	return result;
}

MYSQL_RES* db_queryf(MYSQL *con, int *num_fields, int *num_rows, int *num_affect_rows, int *errcode, char *format, ...) {
	char query[2056]; // max query size hope we dont go over this
	va_list argptr;
	va_start(argptr, format);
	vsprintf(query, format, argptr);
	va_end(argptr);

	if ((*errcode = mysql_query(con, query))) {
		*num_fields = 0;
		*num_rows = 0;
		*num_affect_rows = 0;
		return NULL;
	}

	MYSQL_RES *result = mysql_store_result(con);
	if ((*errcode = mysql_errno(con))) {
		*num_fields = 0;
		*num_rows = 0;
		*num_affect_rows = 0;
		return NULL;
	}

	// inserts etc return null
	if (result == NULL) {
		*num_fields = 0;
		*num_rows = 0;
		*num_affect_rows = mysql_affected_rows(con);
		return NULL;
	}

	*num_fields = mysql_num_fields(result);
	*num_rows = mysql_num_rows(result);
	*num_affect_rows = mysql_affected_rows(con);
	return result;
}

MYSQL_RES* db_query(MYSQL *con, char *query, int *num_fields, int *num_rows, int *num_affect_rows, int *errcode) {
	return db_queryf(con, num_fields, num_rows, num_affect_rows, errcode, "%s", query);
}

void db_select(MYSQL *con, char *db) {
	if (mysql_select_db(con, db)) {
		die(mysql_error(con));
	}
}

void db_get_db(MYSQL *con, char *buffer, int len) {
	int nf, nr, ar, ec;
	MYSQL_RES *result = db_query(con, "SELECT DATABASE()", &nf, &nr, &ar, &ec);
	if (!result && ec > 0) {
		strclr(buffer, len);
		return;
	}
	MYSQL_ROW row = mysql_fetch_row(result);
	strncpy(buffer, row[0], len - 1);
	buffer[len - 1] = '\0';
	return;
}

/**
 * This function has an errant assumption for one primary
 * key when it is possible in MySQL to have more than
 * one, so it will return FALSE if more than one is
 * detected
 */
bool db_get_primary_key(MYSQL *con, char *table, char *buffer, int len) {
	int nf, nr, ar, ec;
	MYSQL_RES *result = db_queryf(con, &nf, &nr, &ar, &ec, "SHOW KEYS FROM `%s` WHERE `Key_name` = 'PRIMARY'", table);
	if (!result && ec > 0) {
		// error
		strclr(buffer, len);
		return false;
	}
	if (nr != 1) {
		// either no primary key or more than one
		return false;
	}
	// loop over fields until one is found called Column_name
	MYSQL_ROW row = mysql_fetch_row(result);
	if (row) {
		MYSQL_FIELD *f; int c=0;
		mysql_field_seek(result, 0);
		while ((f = mysql_fetch_field(result))) {
			if (strcmp(f->name, "Column_name") == 0) {
				strncpy(buffer, row[c], len - 1);
				buffer[len - 1] = '\0';
				return true;
			}
			c++;
		}
	}
	strclr(buffer, len);
	return false;
}

int col_size(MYSQL_RES* result, int index) {
	MYSQL_ROW row;
	mysql_data_seek(result, 0);
	int len = 0;
	while ((row = mysql_fetch_row(result))) {
		int d = (int)strlen(row[index]);
		len = maxi(d, len);
	}
	mysql_data_seek(result, 0);
	return len;
}

//bool get_database(MYSQL *con, char *buf) {
//	int nf, nr;
//	MYSQL_RES* result = con_select(con, "SELECT DATABASE()", &nf, &nr);
//	if (result == NULL)
//		return false;
//	if (nr == 0 || nf == 0)
//		return false;
//	MYSQL_ROW row = mysql_fetch_row(result);
//	if (row == NULL)
//		return false;
//	buf = row[0];
//	return true;
//}

// MYSQL FUNCTIONS END
///////////////////////////////////////
