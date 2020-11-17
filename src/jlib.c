#include "jlib.h"
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include <stddef.h>

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
