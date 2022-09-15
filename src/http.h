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
