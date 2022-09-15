#include <sqlite3.h>

#include "test.h"

int http_next_header(const char* headers,
                     int size,
                     int* pParsed,
                     const char** ppName,
                     int* pNameSize,
                     const char** ppValue,
                     int* pValueSize);

void single_header() {
    const char* headers = "Foo: Bar\r\n";
    int nParsed;
    const char* name;
    int nameSize;
    const char* value;
    int valueSize;
    int rc =
        http_next_header(headers, strlen(headers), &nParsed, &name, &nameSize, &value, &valueSize);
    ASSERT_INT_EQ(rc, SQLITE_ROW);
    ASSERT_INT_EQ(nameSize, 3);
    ASSERT_MEM_EQ(name, "Foo", 3);
    ASSERT_INT_EQ(valueSize, 3);
    ASSERT_MEM_EQ(value, "Bar", 3);
    ASSERT_INT_EQ(nParsed, 10);
}

void empty_string() {
    const char* headers = "";
    int nParsed;
    const char* name;
    int nameSize;
    const char* value;
    int valueSize;
    int rc =
        http_next_header(headers, strlen(headers), &nParsed, &name, &nameSize, &value, &valueSize);
    ASSERT_INT_EQ(rc, SQLITE_DONE);
    ASSERT_INT_EQ(nameSize, 0);
    ASSERT_NULL(name);
    ASSERT_INT_EQ(valueSize, 0);
    ASSERT_NULL(value);
    ASSERT_INT_EQ(nParsed, 0);
}

void terminating_crnl() {
    const char* headers = "\r\n";
    int nParsed;
    const char* name;
    int nameSize;
    const char* value;
    int valueSize;
    int rc =
        http_next_header(headers, strlen(headers), &nParsed, &name, &nameSize, &value, &valueSize);
    ASSERT_INT_EQ(rc, SQLITE_DONE);
    ASSERT_INT_EQ(nameSize, 0);
    ASSERT_NULL(name);
    ASSERT_INT_EQ(valueSize, 0);
    ASSERT_NULL(value);
    ASSERT_INT_EQ(nParsed, 2);
}

void multiple_headers() {
    const char* headers = "Content-Type: application/json\r\nContent-Length: "
                          "343\r\nAccess-Control-Allow-Origin: *\r\n\r\n";
    int nParsed;
    const char* name;
    int nameSize;
    const char* value;
    int valueSize;
    int rc;

    rc = http_next_header(headers, strlen(headers), &nParsed, &name, &nameSize, &value, &valueSize);
    ASSERT_INT_EQ(rc, SQLITE_ROW);
    ASSERT_INT_EQ(nameSize, 12);
    ASSERT_MEM_EQ(name, "Content-Type", 12);
    ASSERT_INT_EQ(valueSize, 16);
    ASSERT_MEM_EQ(value, "application/json", 16);
    ASSERT_INT_EQ(nParsed, 32);

    headers += nParsed;

    rc = http_next_header(headers, strlen(headers), &nParsed, &name, &nameSize, &value, &valueSize);
    ASSERT_INT_EQ(rc, SQLITE_ROW);
    ASSERT_INT_EQ(nameSize, 14);
    ASSERT_MEM_EQ(name, "Content-Length", 14);
    ASSERT_INT_EQ(valueSize, 3);
    ASSERT_MEM_EQ(value, "343", 3);
    ASSERT_INT_EQ(nParsed, 21);

    headers += nParsed;

    rc = http_next_header(headers, strlen(headers), &nParsed, &name, &nameSize, &value, &valueSize);
    ASSERT_INT_EQ(rc, SQLITE_ROW);
    ASSERT_INT_EQ(nameSize, 27);
    ASSERT_MEM_EQ(name, "Access-Control-Allow-Origin", 27);
    ASSERT_INT_EQ(valueSize, 1);
    ASSERT_MEM_EQ(value, "*", 1);
    ASSERT_INT_EQ(nParsed, 32);

    headers += nParsed;

    rc = http_next_header(headers, strlen(headers), &nParsed, &name, &nameSize, &value, &valueSize);
    ASSERT_INT_EQ(rc, SQLITE_DONE);
    ASSERT_INT_EQ(nameSize, 0);
    ASSERT_NULL(name);
    ASSERT_INT_EQ(valueSize, 0);
    ASSERT_NULL(value);
    ASSERT_INT_EQ(nParsed, 2);
}

void empty_value() {
    const char* headers = "Foo:\r\n";
    int nParsed;
    const char* name;
    int nameSize;
    const char* value;
    int valueSize;
    int rc =
        http_next_header(headers, strlen(headers), &nParsed, &name, &nameSize, &value, &valueSize);
    ASSERT_INT_EQ(rc, SQLITE_ROW);
    ASSERT_INT_EQ(nameSize, 3);
    ASSERT_MEM_EQ(name, "Foo", 3);
    ASSERT_INT_EQ(valueSize, 0);
    ASSERT_NULL(value);
    ASSERT_INT_EQ(nParsed, 6);
}

void empty_value_leading_ws() {
    const char* headers = "Foo:     \r\n";
    int nParsed;
    const char* name;
    int nameSize;
    const char* value;
    int valueSize;
    int rc =
        http_next_header(headers, strlen(headers), &nParsed, &name, &nameSize, &value, &valueSize);
    ASSERT_INT_EQ(rc, SQLITE_ROW);
    ASSERT_INT_EQ(nameSize, 3);
    ASSERT_MEM_EQ(name, "Foo", 3);
    ASSERT_INT_EQ(valueSize, 0);
    ASSERT_NULL(value);
    ASSERT_INT_EQ(nParsed, 11);
}

void value_with_leading_ws() {
    const char* headers = "Foo:    \t  Bar\r\n";
    int nParsed;
    const char* name;
    int nameSize;
    const char* value;
    int valueSize;
    int rc =
        http_next_header(headers, strlen(headers), &nParsed, &name, &nameSize, &value, &valueSize);
    ASSERT_INT_EQ(rc, SQLITE_ROW);
    ASSERT_INT_EQ(nameSize, 3);
    ASSERT_MEM_EQ(name, "Foo", 3);
    ASSERT_INT_EQ(valueSize, 3);
    ASSERT_MEM_EQ(value, "Bar", 3);
    ASSERT_INT_EQ(nParsed, 16);
}

void value_with_trailing_ws() {
    const char* headers = "Foo: Bar   \t  \r\n";
    int nParsed;
    const char* name;
    int nameSize;
    const char* value;
    int valueSize;
    int rc =
        http_next_header(headers, strlen(headers), &nParsed, &name, &nameSize, &value, &valueSize);
    ASSERT_INT_EQ(rc, SQLITE_ROW);
    ASSERT_INT_EQ(nameSize, 3);
    ASSERT_MEM_EQ(name, "Foo", 3);
    ASSERT_INT_EQ(valueSize, 3);
    ASSERT_MEM_EQ(value, "Bar", 3);
    ASSERT_INT_EQ(nParsed, strlen(headers));
}

void value_with_leading_and_trailing_ws() {
    const char* headers = "Foo:        Bar   \t  \r\n";
    int nParsed;
    const char* name;
    int nameSize;
    const char* value;
    int valueSize;
    int rc =
        http_next_header(headers, strlen(headers), &nParsed, &name, &nameSize, &value, &valueSize);
    ASSERT_INT_EQ(rc, SQLITE_ROW);
    ASSERT_INT_EQ(nameSize, 3);
    ASSERT_MEM_EQ(name, "Foo", 3);
    ASSERT_INT_EQ(valueSize, 3);
    ASSERT_MEM_EQ(value, "Bar", 3);
    ASSERT_INT_EQ(nParsed, strlen(headers));
}

void value_with_no_leading_ws() {
    const char* headers = "Foo:Bar\r\n";
    int nParsed;
    const char* name;
    int nameSize;
    const char* value;
    int valueSize;
    int rc =
        http_next_header(headers, strlen(headers), &nParsed, &name, &nameSize, &value, &valueSize);
    ASSERT_INT_EQ(rc, SQLITE_ROW);
    ASSERT_INT_EQ(nameSize, 3);
    ASSERT_MEM_EQ(name, "Foo", 3);
    ASSERT_INT_EQ(valueSize, 3);
    ASSERT_MEM_EQ(value, "Bar", 3);
    ASSERT_INT_EQ(nParsed, strlen(headers));
}

void value_with_spaces_inside() {
    const char* headers = "Foo: This is     a  value\r\n";
    int nParsed;
    const char* name;
    int nameSize;
    const char* value;
    int valueSize;
    int rc =
        http_next_header(headers, strlen(headers), &nParsed, &name, &nameSize, &value, &valueSize);
    ASSERT_INT_EQ(rc, SQLITE_ROW);
    ASSERT_INT_EQ(nameSize, 3);
    ASSERT_MEM_EQ(name, "Foo", 3);
    ASSERT_INT_EQ(valueSize, 20);
    ASSERT_MEM_EQ(value, "This is     a  value", 20);
    ASSERT_INT_EQ(nParsed, strlen(headers));
}

void value_with_spaces_inside_trailing_ws() {
    const char* headers = "Foo: This is     a  value      \r\n";
    int nParsed;
    const char* name;
    int nameSize;
    const char* value;
    int valueSize;
    int rc =
        http_next_header(headers, strlen(headers), &nParsed, &name, &nameSize, &value, &valueSize);
    ASSERT_INT_EQ(rc, SQLITE_ROW);
    ASSERT_INT_EQ(nameSize, 3);
    ASSERT_MEM_EQ(name, "Foo", 3);
    ASSERT_INT_EQ(valueSize, 20);
    ASSERT_MEM_EQ(value, "This is     a  value", 20);
    ASSERT_INT_EQ(nParsed, strlen(headers));
}

void value_with_spaces_inside_leading_ws() {
    const char* headers = "Foo:          This is     a  value\r\n";
    int nParsed;
    const char* name;
    int nameSize;
    const char* value;
    int valueSize;
    int rc =
        http_next_header(headers, strlen(headers), &nParsed, &name, &nameSize, &value, &valueSize);
    ASSERT_INT_EQ(rc, SQLITE_ROW);
    ASSERT_INT_EQ(nameSize, 3);
    ASSERT_MEM_EQ(name, "Foo", 3);
    ASSERT_INT_EQ(valueSize, 20);
    ASSERT_MEM_EQ(value, "This is     a  value", 20);
    ASSERT_INT_EQ(nParsed, strlen(headers));
}

void value_with_spaces_inside_leading_and_trailing_ws() {
    const char* headers = "Foo:          This is     a  value        \r\n";
    int nParsed;
    const char* name;
    int nameSize;
    const char* value;
    int valueSize;
    int rc =
        http_next_header(headers, strlen(headers), &nParsed, &name, &nameSize, &value, &valueSize);
    ASSERT_INT_EQ(rc, SQLITE_ROW);
    ASSERT_INT_EQ(nameSize, 3);
    ASSERT_MEM_EQ(name, "Foo", 3);
    ASSERT_INT_EQ(valueSize, 20);
    ASSERT_MEM_EQ(value, "This is     a  value", 20);
    ASSERT_INT_EQ(nParsed, strlen(headers));
}

void value_with_spaces_inside_no_leading_ws() {
    const char* headers = "Foo:This is     a  value\r\n";
    int nParsed;
    const char* name;
    int nameSize;
    const char* value;
    int valueSize;
    int rc =
        http_next_header(headers, strlen(headers), &nParsed, &name, &nameSize, &value, &valueSize);
    ASSERT_INT_EQ(rc, SQLITE_ROW);
    ASSERT_INT_EQ(nameSize, 3);
    ASSERT_MEM_EQ(name, "Foo", 3);
    ASSERT_INT_EQ(valueSize, 20);
    ASSERT_MEM_EQ(value, "This is     a  value", 20);
    ASSERT_INT_EQ(nParsed, strlen(headers));
}

void trailing_ws_after_name_is_ignored() {
    const char* headers = "Foo\t : Bar\r\n";
    int nParsed;
    const char* name;
    int nameSize;
    const char* value;
    int valueSize;
    int rc =
        http_next_header(headers, strlen(headers), &nParsed, &name, &nameSize, &value, &valueSize);
    ASSERT_INT_EQ(rc, SQLITE_ROW);
    ASSERT_INT_EQ(nameSize, 3);
    ASSERT_MEM_EQ(name, "Foo", 3);
    ASSERT_INT_EQ(valueSize, 3);
    ASSERT_MEM_EQ(value, "Bar", 3);
    ASSERT_INT_EQ(nParsed, strlen(headers));
}

void folding() {
    const char* headers = "Foo: Bar\r\n  Baz\r\n\tAnd more\r\n";
    int nParsed;
    const char* name;
    int nameSize;
    const char* value;
    int valueSize;
    int rc =
        http_next_header(headers, strlen(headers), &nParsed, &name, &nameSize, &value, &valueSize);
    ASSERT_INT_EQ(rc, SQLITE_ROW);
    ASSERT_INT_EQ(nameSize, 3);
    ASSERT_MEM_EQ(name, "Foo", 3);
    ASSERT_INT_EQ(valueSize, 21);
    ASSERT_MEM_EQ(value, "Bar\r\n  Baz\r\n\tAnd more", 21);
    ASSERT_INT_EQ(nParsed, strlen(headers));
}

int main(int argc, char const* argv[]) {
    single_header();
    empty_string();
    terminating_crnl();
    multiple_headers();
    empty_value();
    empty_value_leading_ws();
    value_with_leading_ws();
    value_with_trailing_ws();
    value_with_leading_and_trailing_ws();
    value_with_no_leading_ws();
    value_with_spaces_inside();
    value_with_spaces_inside_trailing_ws();
    value_with_spaces_inside_leading_ws();
    value_with_spaces_inside_leading_and_trailing_ws();
    value_with_spaces_inside_no_leading_ws();
    trailing_ws_after_name_is_ignored();
    folding();
    return 0;
}
