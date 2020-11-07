
MYSQL_RES* con_select(MYSQL *con, int *num_fields, int *num_rows);
MYSQL_RES* db_query(MYSQL *con, char *query, int *num_fields, int *num_rows, int *errcode);
void db_select(MYSQL *con, char *db);
int col_size(MYSQL_RES* result, int index);
//bool get_database(MYSQL *con, char *buf);

