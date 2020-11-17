#include "jlib.h"
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include <stddef.h>

#include <ncurses.h>

///////////////////////////////////////
// STRING FUNCTIONS START

int j_strchrplc(char *str, char orig, char rep) {
	char *ix = str;
	int n = 0;
	while((ix = strchr(ix, orig)) != NULL) {
		*ix++ = rep;
		n++;
	}
	return n;
}

void j_strfill(char *string, int size, char c) {
	for (int i = 0; i < size - 1; i++) {
		string[i] = c;
	}
}

void j_strclr(char *string, int size) {
	for (int i = 0; i < size; i++) {
		string[i] = '\0';
	}
}

size_t j_strtrim(char *out, size_t len, const char *str, bool trimlead, bool trimtrail) {
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

// STRING FUNCTIONS END
///////////////////////////////////////



///////////////////////////////////////
// NCURSES FUNCTIONS START

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

