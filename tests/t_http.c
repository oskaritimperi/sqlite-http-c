#include "http.h"

#include <sqlite3.h>

#include "test.h"

SQLITE_EXTENSION_INIT3

void http_backend_dummy_set_errmsg(const char* zErrMsg);
void http_backend_dummy_set_response(http_response* response);
void http_backend_dummy_reset_request();
const http_request* http_backend_dummy_get_last_request();

int sqlite3_http_init(sqlite3*, char**, const sqlite3_api_routines*);

void new_text_response(http_response* response,
                       const char* zBody,
                       const char* zHeaders,
                       int iStatusCode,
                       const char* zStatus) {
    memset(response, 0, sizeof(*response));
    assert(zStatus != NULL);
    if (zBody) {
        response->pBody = sqlite3_mprintf("%s", zBody);
        response->szBody = strlen(zBody);
    }
    if (zHeaders) {
        response->zHeaders = sqlite3_mprintf("%s", zHeaders);
        response->szHeaders = strlen(zHeaders);
    }
    response->iStatusCode = iStatusCode;
    response->zStatus = sqlite3_mprintf("%s", zStatus);
}

static sqlite3* db;

void test_http_get() {
    sqlite3_stmt* stmt;
    http_response response;
    new_text_response(&response, "hello, world!", "Foo: Bar\r\n\r\n", 200, "HTTP/1.0 200 OK");
    http_backend_dummy_set_response(&response);
    ASSERT_INT_EQ(
        sqlite3_prepare_v2(db, "select * from http_get('http://example.com')", -1, &stmt, NULL),
        SQLITE_OK);
    ASSERT_INT_EQ(sqlite3_step(stmt), SQLITE_ROW);
    ASSERT_INT_EQ(sqlite3_column_count(stmt), 4);
    ASSERT_STR_EQ(sqlite3_column_text(stmt, 0), "HTTP/1.0 200 OK");
    ASSERT_INT_EQ(sqlite3_column_int(stmt, 1), 200);
    ASSERT_STR_EQ(sqlite3_column_text(stmt, 2), "Foo: Bar\r\n\r\n");
    ASSERT_STR_EQ(sqlite3_column_text(stmt, 3), "hello, world!");
    ASSERT_INT_EQ(sqlite3_finalize(stmt), SQLITE_OK);
}

void test_http_get_hidden_columns() {
    sqlite3_stmt* stmt;
    http_response response;
    new_text_response(&response, "hello, world!", "Foo: Bar\r\n\r\n", 200, "HTTP/1.0 200 OK");
    http_backend_dummy_set_response(&response);
    ASSERT_INT_EQ(
        sqlite3_prepare_v2(db,
                           "select response_status, response_status_code, response_headers, "
                           "response_body, request_method, request_url, request_headers, "
                           "request_body from http_get('http://example.com/foo')",
                           -1,
                           &stmt,
                           NULL),
        SQLITE_OK);
    ASSERT_INT_EQ(sqlite3_step(stmt), SQLITE_ROW);
    ASSERT_INT_EQ(sqlite3_column_count(stmt), 8);
    ASSERT_STR_EQ(sqlite3_column_text(stmt, 0), "HTTP/1.0 200 OK");
    ASSERT_INT_EQ(sqlite3_column_int(stmt, 1), 200);
    ASSERT_STR_EQ(sqlite3_column_text(stmt, 2), "Foo: Bar\r\n\r\n");
    ASSERT_STR_EQ(sqlite3_column_text(stmt, 3), "hello, world!");
    ASSERT_STR_EQ(sqlite3_column_text(stmt, 4), "GET");
    ASSERT_STR_EQ(sqlite3_column_text(stmt, 5), "http://example.com/foo");
    ASSERT_NULL(sqlite3_column_text(stmt, 6));
    ASSERT_NULL(sqlite3_column_text(stmt, 7));
    ASSERT_INT_EQ(sqlite3_finalize(stmt), SQLITE_OK);
}

void test_http_get_request_headers() {
    sqlite3_stmt* stmt;
    http_response response;
    new_text_response(&response, "hello, world!", "Foo: Bar\r\n\r\n", 200, "HTTP/1.0 200 OK");
    http_backend_dummy_set_response(&response);
    ASSERT_INT_EQ(
        sqlite3_prepare_v2(db,
                           "select response_status, response_status_code, response_headers, "
                           "response_body, request_method, request_url, request_headers, "
                           "request_body from http_get('http://example.com/foo', "
                           "http_headers('Req1', 'Val1', 'Req2', 'Val2'))",
                           -1,
                           &stmt,
                           NULL),
        SQLITE_OK);
    ASSERT_INT_EQ(sqlite3_step(stmt), SQLITE_ROW);
    ASSERT_INT_EQ(sqlite3_column_count(stmt), 8);
    ASSERT_STR_EQ(sqlite3_column_text(stmt, 0), "HTTP/1.0 200 OK");
    ASSERT_INT_EQ(sqlite3_column_int(stmt, 1), 200);
    ASSERT_STR_EQ(sqlite3_column_text(stmt, 2), "Foo: Bar\r\n\r\n");
    ASSERT_STR_EQ(sqlite3_column_text(stmt, 3), "hello, world!");
    ASSERT_STR_EQ(sqlite3_column_text(stmt, 4), "GET");
    ASSERT_STR_EQ(sqlite3_column_text(stmt, 5), "http://example.com/foo");
    ASSERT_STR_EQ(sqlite3_column_text(stmt, 6), "Req1: Val1\r\nReq2: Val2\r\n");
    ASSERT_NULL(sqlite3_column_text(stmt, 7));
    ASSERT_INT_EQ(sqlite3_finalize(stmt), SQLITE_OK);
}

void test_http_post() {
    sqlite3_stmt* stmt;
    http_response response;
    new_text_response(&response, "hello, world!", "Foo: Bar\r\n\r\n", 200, "HTTP/1.0 200 OK");
    http_backend_dummy_set_response(&response);
    ASSERT_INT_EQ(
        sqlite3_prepare_v2(db, "select * from http_post('http://example.com')", -1, &stmt, NULL),
        SQLITE_OK);
    ASSERT_INT_EQ(sqlite3_step(stmt), SQLITE_ROW);
    ASSERT_INT_EQ(sqlite3_column_count(stmt), 4);
    ASSERT_STR_EQ(sqlite3_column_text(stmt, 0), "HTTP/1.0 200 OK");
    ASSERT_INT_EQ(sqlite3_column_int(stmt, 1), 200);
    ASSERT_STR_EQ(sqlite3_column_text(stmt, 2), "Foo: Bar\r\n\r\n");
    ASSERT_STR_EQ(sqlite3_column_text(stmt, 3), "hello, world!");
    ASSERT_INT_EQ(sqlite3_finalize(stmt), SQLITE_OK);
}

void test_http_post_hidden_columns() {
    sqlite3_stmt* stmt;
    http_response response;
    new_text_response(&response, "hello, world!", "Foo: Bar\r\n\r\n", 200, "HTTP/1.0 200 OK");
    http_backend_dummy_set_response(&response);
    ASSERT_INT_EQ(
        sqlite3_prepare_v2(db,
                           "select response_status, response_status_code, response_headers, "
                           "response_body, request_method, request_url, request_headers, "
                           "request_body from http_post('http://example.com/foo')",
                           -1,
                           &stmt,
                           NULL),
        SQLITE_OK);
    ASSERT_INT_EQ(sqlite3_step(stmt), SQLITE_ROW);
    ASSERT_INT_EQ(sqlite3_column_count(stmt), 8);
    ASSERT_STR_EQ(sqlite3_column_text(stmt, 0), "HTTP/1.0 200 OK");
    ASSERT_INT_EQ(sqlite3_column_int(stmt, 1), 200);
    ASSERT_STR_EQ(sqlite3_column_text(stmt, 2), "Foo: Bar\r\n\r\n");
    ASSERT_STR_EQ(sqlite3_column_text(stmt, 3), "hello, world!");
    ASSERT_STR_EQ(sqlite3_column_text(stmt, 4), "POST");
    ASSERT_STR_EQ(sqlite3_column_text(stmt, 5), "http://example.com/foo");
    ASSERT_NULL(sqlite3_column_text(stmt, 6));
    ASSERT_NULL(sqlite3_column_text(stmt, 7));
    ASSERT_INT_EQ(sqlite3_finalize(stmt), SQLITE_OK);
}

void test_http_post_request_headers() {
    sqlite3_stmt* stmt;
    http_response response;
    new_text_response(&response, "hello, world!", "Foo: Bar\r\n\r\n", 200, "HTTP/1.0 200 OK");
    http_backend_dummy_set_response(&response);
    ASSERT_INT_EQ(
        sqlite3_prepare_v2(db,
                           "select response_status, response_status_code, response_headers, "
                           "response_body, request_method, request_url, request_headers, "
                           "request_body from http_post('http://example.com/foo', "
                           "http_headers('Req1', 'Val1', 'Req2', 'Val2'))",
                           -1,
                           &stmt,
                           NULL),
        SQLITE_OK);
    ASSERT_INT_EQ(sqlite3_step(stmt), SQLITE_ROW);
    ASSERT_INT_EQ(sqlite3_column_count(stmt), 8);
    ASSERT_STR_EQ(sqlite3_column_text(stmt, 0), "HTTP/1.0 200 OK");
    ASSERT_INT_EQ(sqlite3_column_int(stmt, 1), 200);
    ASSERT_STR_EQ(sqlite3_column_text(stmt, 2), "Foo: Bar\r\n\r\n");
    ASSERT_STR_EQ(sqlite3_column_text(stmt, 3), "hello, world!");
    ASSERT_STR_EQ(sqlite3_column_text(stmt, 4), "POST");
    ASSERT_STR_EQ(sqlite3_column_text(stmt, 5), "http://example.com/foo");
    ASSERT_STR_EQ(sqlite3_column_text(stmt, 6), "Req1: Val1\r\nReq2: Val2\r\n");
    ASSERT_NULL(sqlite3_column_text(stmt, 7));
    ASSERT_INT_EQ(sqlite3_finalize(stmt), SQLITE_OK);
}

void test_http_post_request_body() {
    sqlite3_stmt* stmt;
    http_response response;
    new_text_response(&response, "hello, world!", "Foo: Bar\r\n\r\n", 200, "HTTP/1.0 200 OK");
    http_backend_dummy_set_response(&response);
    ASSERT_INT_EQ(
        sqlite3_prepare_v2(db,
                           "select response_status, response_status_code, response_headers, "
                           "response_body, request_method, request_url, request_headers, "
                           "request_body from http_post('http://example.com/foo', "
                           "http_headers('Req1', 'Val1', 'Req2', 'Val2'), 'hello')",
                           -1,
                           &stmt,
                           NULL),
        SQLITE_OK);
    ASSERT_INT_EQ(sqlite3_step(stmt), SQLITE_ROW);
    ASSERT_INT_EQ(sqlite3_column_count(stmt), 8);
    ASSERT_STR_EQ(sqlite3_column_text(stmt, 0), "HTTP/1.0 200 OK");
    ASSERT_INT_EQ(sqlite3_column_int(stmt, 1), 200);
    ASSERT_STR_EQ(sqlite3_column_text(stmt, 2), "Foo: Bar\r\n\r\n");
    ASSERT_STR_EQ(sqlite3_column_text(stmt, 3), "hello, world!");
    ASSERT_STR_EQ(sqlite3_column_text(stmt, 4), "POST");
    ASSERT_STR_EQ(sqlite3_column_text(stmt, 5), "http://example.com/foo");
    ASSERT_STR_EQ(sqlite3_column_text(stmt, 6), "Req1: Val1\r\nReq2: Val2\r\n");
    ASSERT_STR_EQ(sqlite3_column_text(stmt, 7), "hello");
    ASSERT_INT_EQ(sqlite3_finalize(stmt), SQLITE_OK);
}

int main(int argc, char const* argv[]) {
    sqlite3_initialize();
    sqlite3_auto_extension((void (*)(void))sqlite3_http_init);
    ASSERT_INT_EQ(sqlite3_open(":memory:", &db), SQLITE_OK);
    test_http_get();
    test_http_get_hidden_columns();
    test_http_get_request_headers();
    test_http_post();
    test_http_post_hidden_columns();
    test_http_post_request_headers();
    test_http_post_request_body();
    return 0;
}
