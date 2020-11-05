#include <mysql/mysql.h>
#include <stdio.h>
#include <stdbool.h>

// implemented by main application
void die(const char *msg);

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
MYSQL_RES* con_select(MYSQL *con, char *query, int *num_fields, int *num_rows) {
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

void db_select(MYSQL *con, char *db) {
    if (mysql_select_db(con, db)) {
        die(mysql_error(con));
    }
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
