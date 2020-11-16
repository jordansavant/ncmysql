#include <mysql/mysql.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>

// implemented by main application
void die(const char *msg);

int _maxi(int a, int b) {
	if (a > b)
		return a;
	return b;
}

//void die_error(int error_code) {
//    switch (error_code) {
//        case 2014://CR_COMMANDS_OUT_OF_SYNC:
//            die("commands out of sync");
//            break;
//        case 2006://CR_SERVER_GONE_ERROR:
//            die("server gone");
//            break;
//        case 2013://CR_SERVER_LOST:
//            die("server lost");
//            break;
//        default:
//        case 2000://CR_UNKNOWN_ERROR:
//            die("unknown error");
//            break;
//    }
//}
//
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

MYSQL_RES* db_query(MYSQL *con, char *query, int *num_fields, int *num_rows, int *num_affect_rows, int *errcode) {
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

void db_select(MYSQL *con, char *db) {
    if (mysql_select_db(con, db)) {
        die(mysql_error(con));
    }
}

int col_size(MYSQL_RES* result, int index) {
    MYSQL_ROW row;
    mysql_data_seek(result, 0);
    int len = 0;
    while ((row = mysql_fetch_row(result))) {
        int d = (int)strlen(row[index]);
        len = _maxi(d, len);
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
