#include <sqlite3.h>
#include <stdio.h>

typedef void (*entrypoint)(void);

int sqlite3_http_init(sqlite3*, char**, const sqlite3_api_routines*);

void test_init() {
    sqlite3_initialize();
    sqlite3_auto_extension((entrypoint)sqlite3_http_init);
}
