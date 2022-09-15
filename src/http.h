/**
 * HTTP client extension for SQLite.
 *
 * https://github.com/oskaritimperi/sqlite-http-c
 *
 * MIT License
 *
 * Copyright (c) 2022 Oskari Timperi
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef HTTP_H
#define HTTP_H

#include "sqlite3ext.h"

typedef struct http_request http_request;
struct http_request {
    char* zMethod;
    char* zUrl;
    const void* pBody;
    sqlite3_int64 szBody;
    const char* zHeaders;
};

typedef struct http_response http_response;
struct http_response {
    void* pBody;
    sqlite3_int64 szBody;
    char* zHeaders;
    int szHeaders;
    int iStatusCode;
    char* zStatus;
};

int http_do_request(http_request* req, http_response* resp, char** ppErrMsg);

int http_next_header(const char* headers,
                     int size,
                     int* pParsed,
                     const char** ppName,
                     int* pNameSize,
                     const char** ppValue,
                     int* pValueSize);

void remove_all_but_last_headers(char* zHeaders);
void separate_status_and_headers(char** ppStatus, char* zHeaders);

#endif
