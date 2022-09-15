
/********** src/http.h **********/

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

/********** src/http.c **********/


SQLITE_EXTENSION_INIT1

#include <assert.h>
#include <ctype.h>
#include <string.h>

typedef struct http_vtab http_vtab;
struct http_vtab {
    sqlite3_vtab base;
    char* zMethod;
};

typedef struct http_cursor http_cursor;
struct http_cursor {
    sqlite3_vtab_cursor base;
    sqlite3_int64 iRowid;
    http_request req;
    http_response resp;
};

// If zHeaders contains headers for multiple responses, then this will strip
// headers from all but the last response.
void remove_all_but_last_headers(char* zHeaders) {
    int szHeaders = strlen(zHeaders);
    char* pLastTerminator = NULL;
    char* pSearch = zHeaders;

    for (;;) {
        char* p = strstr(pSearch, "\r\n\r\n");
        // If there isn't more terminators, we are done here
        if (!p) {
            break;
        }
        // If it's at the end of zHeaders, we are done here
        if (szHeaders - (p - zHeaders) == 4) {
            break;
        }
        pSearch = p + 4;
        pLastTerminator = p;
    }

    if (pLastTerminator) {
        pLastTerminator += 4;
        memmove(zHeaders, pLastTerminator, strlen(pLastTerminator) + 1);
    }
}

// Separate the status and header lines
void separate_status_and_headers(char** ppStatus, char* zHeaders) {
    char* cr = strchr(zHeaders, '\r');
    *ppStatus = sqlite3_mprintf("%.*s", cr - zHeaders, zHeaders);
    memmove(zHeaders, cr + 2, strlen(cr + 2) + 1);
}

static int httpConnect(sqlite3* db,
                       void* pAux,
                       int argc,
                       const char* const* argv,
                       sqlite3_vtab** ppVtab,
                       char** pzErr) {
    http_vtab* pNew;
    int rc;

    rc = sqlite3_declare_vtab(db,
                              "CREATE TABLE x(response_status TEXT, "
                              "response_status_code INT, response_headers TEXT, "
                              "response_body BLOB, request_method TEXT HIDDEN, "
                              "request_url TEXT HIDDEN, "
                              "request_headers TEXT HIDDEN, "
                              "request_body BLOB HIDDEN)");

#define HTTP_COL_RESPONSE_STATUS 0
#define HTTP_COL_RESPONSE_STATUS_CODE 1
#define HTTP_COL_RESPONSE_HEADERS 2
#define HTTP_COL_RESPONSE_BODY 3
#define HTTP_COL_REQUEST_METHOD 4
#define HTTP_COL_REQUEST_URL 5
#define HTTP_COL_REQUEST_HEADERS 6
#define HTTP_COL_REQUEST_BODY 7

    if (rc == SQLITE_OK) {
        pNew = sqlite3_malloc(sizeof(*pNew));
        *ppVtab = (sqlite3_vtab*)pNew;
        if (pNew == 0)
            return SQLITE_NOMEM;
        memset(pNew, 0, sizeof(*pNew));
        if (sqlite3_stricmp(argv[0], "http_get") == 0) {
            pNew->zMethod = sqlite3_mprintf("GET");
        } else if (sqlite3_stricmp(argv[0], "http_post") == 0) {
            pNew->zMethod = sqlite3_mprintf("POST");
        }
    }
    return rc;
}

static int httpDisconnect(sqlite3_vtab* pVtab) {
    http_vtab* p = (http_vtab*)pVtab;
    sqlite3_free(p->zMethod);
    sqlite3_free(p);
    return SQLITE_OK;
}

static int httpOpen(sqlite3_vtab* p, sqlite3_vtab_cursor** ppCursor) {
    http_cursor* pCur;
    http_vtab* pVtab = (http_vtab*)p;
    pCur = sqlite3_malloc(sizeof(*pCur));
    if (pCur == 0)
        return SQLITE_NOMEM;
    memset(pCur, 0, sizeof(*pCur));
    if (pVtab->zMethod) {
        pCur->req.zMethod = sqlite3_mprintf("%s", pVtab->zMethod);
    }
    *ppCursor = &pCur->base;
    return SQLITE_OK;
}

static int httpClose(sqlite3_vtab_cursor* cur) {
    http_cursor* pCur = (http_cursor*)cur;
    sqlite3_free(pCur->resp.pBody);
    sqlite3_free(pCur->req.zUrl);
    sqlite3_free(pCur->req.zMethod);
    sqlite3_free(pCur);
    return SQLITE_OK;
}

static int httpNext(sqlite3_vtab_cursor* cur) {
    http_cursor* pCur = (http_cursor*)cur;
    pCur->iRowid++;
    return SQLITE_OK;
}

static int httpColumn(sqlite3_vtab_cursor* cur, sqlite3_context* ctx, int i) {
    http_cursor* pCur = (http_cursor*)cur;

    switch (i) {
    case HTTP_COL_RESPONSE_STATUS:
        sqlite3_result_text(ctx, pCur->resp.zStatus, -1, SQLITE_TRANSIENT);
        break;

    case HTTP_COL_RESPONSE_STATUS_CODE:
        sqlite3_result_int(ctx, pCur->resp.iStatusCode);
        break;

    case HTTP_COL_RESPONSE_HEADERS:
        sqlite3_result_text(ctx, pCur->resp.zHeaders, -1, SQLITE_TRANSIENT);
        break;

    case HTTP_COL_RESPONSE_BODY:
        sqlite3_result_blob(ctx, pCur->resp.pBody, pCur->resp.szBody, SQLITE_TRANSIENT);
        break;

    case HTTP_COL_REQUEST_METHOD:
        sqlite3_result_text(ctx, pCur->req.zMethod, -1, SQLITE_TRANSIENT);
        break;

    case HTTP_COL_REQUEST_URL:
        sqlite3_result_text(ctx, pCur->req.zUrl, -1, SQLITE_TRANSIENT);
        break;

    case HTTP_COL_REQUEST_HEADERS:
        if (pCur->req.zHeaders) {
            sqlite3_result_text(ctx, pCur->req.zHeaders, -1, SQLITE_TRANSIENT);
        } else {
            sqlite3_result_null(ctx);
        }
        break;

    case HTTP_COL_REQUEST_BODY:
        if (pCur->req.pBody) {
            sqlite3_result_blob(ctx, pCur->req.pBody, pCur->req.szBody, SQLITE_TRANSIENT);
        } else {
            sqlite3_result_null(ctx);
        }
        break;
    }

    return SQLITE_OK;
}

static int httpRowid(sqlite3_vtab_cursor* cur, sqlite_int64* pRowid) {
    http_cursor* pCur = (http_cursor*)cur;
    *pRowid = pCur->iRowid;
    return SQLITE_OK;
}

static int httpEof(sqlite3_vtab_cursor* cur) {
    http_cursor* pCur = (http_cursor*)cur;
    return pCur->iRowid >= 1;
}

#define HTTP_FLAG_METHOD 1
#define HTTP_FLAG_URL 2
#define HTTP_FLAG_HEADERS 4
#define HTTP_FLAG_BODY 8

static int httpFilter(sqlite3_vtab_cursor* pVtabCursor,
                      int idxNum,
                      const char* idxStr,
                      int argc,
                      sqlite3_value** argv) {
    http_cursor* pCur = (http_cursor*)pVtabCursor;
    http_vtab* pVtab = (http_vtab*)pVtabCursor->pVtab;
    int bIsDo = pVtab->zMethod == NULL;
    char* zErrMsg = NULL;
    int rc;
    if (bIsDo) {
        pCur->req.zMethod = sqlite3_mprintf("%s", sqlite3_value_text(argv[0]));
        pCur->req.zUrl = sqlite3_mprintf("%s", sqlite3_value_text(argv[1]));
        if (idxNum & HTTP_FLAG_HEADERS) {
            pCur->req.zHeaders = sqlite3_mprintf("%s", sqlite3_value_text(argv[2]));
        }
        if (idxNum & HTTP_FLAG_BODY) {
            pCur->req.szBody = sqlite3_value_bytes(argv[3]);
            pCur->req.pBody = sqlite3_value_blob(argv[3]);
        }
    } else {
        pCur->req.zUrl = sqlite3_mprintf("%s", sqlite3_value_text(argv[0]));
        if (idxNum & HTTP_FLAG_HEADERS) {
            pCur->req.zHeaders = sqlite3_mprintf("%s", sqlite3_value_text(argv[1]));
        }
        if (idxNum & HTTP_FLAG_BODY) {
            pCur->req.szBody = sqlite3_value_bytes(argv[2]);
            pCur->req.pBody = sqlite3_value_blob(argv[2]);
        }
    }
    rc = http_do_request(&pCur->req, &pCur->resp, &zErrMsg);
    if (rc != SQLITE_OK) {
        sqlite3_free(pCur->base.pVtab->zErrMsg);
        pCur->base.pVtab->zErrMsg = zErrMsg;
    }
    return rc;
}

static int httpBestIndex(sqlite3_vtab* tab, sqlite3_index_info* pIdxInfo) {
    http_vtab* pTab = (http_vtab*)tab;
    int bIsDo = pTab->zMethod == NULL;
    int i;
    int idxNum = 0;
    const struct sqlite3_index_constraint* pConstraint;

    pConstraint = pIdxInfo->aConstraint;

    for (i = 0; i < pIdxInfo->nConstraint; ++i, ++pConstraint) {
        if (!pIdxInfo->aConstraint[i].usable) {
            return SQLITE_CONSTRAINT;
        }
        if (pConstraint->op != SQLITE_INDEX_CONSTRAINT_EQ) {
            return SQLITE_CONSTRAINT;
        };
        switch (pConstraint->iColumn) {
        case HTTP_COL_REQUEST_METHOD:
            if (bIsDo) {
                idxNum |= HTTP_FLAG_METHOD;
            } else {
                idxNum |= HTTP_FLAG_URL;
            }
            pIdxInfo->aConstraintUsage[i].argvIndex = 1;
            pIdxInfo->aConstraintUsage[i].omit = 1;
            break;

        case HTTP_COL_REQUEST_URL:
            if (bIsDo) {
                idxNum |= HTTP_FLAG_URL;
            } else {
                idxNum |= HTTP_FLAG_HEADERS;
            }
            pIdxInfo->aConstraintUsage[i].argvIndex = 2;
            pIdxInfo->aConstraintUsage[i].omit = 1;
            break;

        case HTTP_COL_REQUEST_HEADERS:
            if (bIsDo) {
                idxNum |= HTTP_FLAG_HEADERS;
            } else {
                idxNum |= HTTP_FLAG_BODY;
            }
            pIdxInfo->aConstraintUsage[i].argvIndex = 3;
            pIdxInfo->aConstraintUsage[i].omit = 1;
            break;

        case HTTP_COL_REQUEST_BODY:
            if (bIsDo) {
                idxNum |= HTTP_FLAG_BODY;
            } else {
                sqlite3_free(tab->zErrMsg);
                tab->zErrMsg = sqlite3_mprintf("too many arguments");
                return SQLITE_ERROR;
            }
            pIdxInfo->aConstraintUsage[i].argvIndex = 4;
            pIdxInfo->aConstraintUsage[i].omit = 1;
            break;
        }
    }

    if (bIsDo && !(idxNum & HTTP_FLAG_METHOD)) {
        sqlite3_free(tab->zErrMsg);
        tab->zErrMsg = sqlite3_mprintf("method missing");
        return SQLITE_ERROR;
    }

    if (!(idxNum & HTTP_FLAG_URL)) {
        sqlite3_free(tab->zErrMsg);
        tab->zErrMsg = sqlite3_mprintf("url missing");
        return SQLITE_ERROR;
    }

    pIdxInfo->estimatedCost = (double)1;
    pIdxInfo->estimatedRows = 1;
    pIdxInfo->idxNum = idxNum;

    return SQLITE_OK;
}

static sqlite3_module httpModule = {
    /* iVersion    */ 0,
    /* xCreate     */ 0,
    /* xConnect    */ httpConnect,
    /* xBestIndex  */ httpBestIndex,
    /* xDisconnect */ httpDisconnect,
    /* xDestroy    */ 0,
    /* xOpen       */ httpOpen,
    /* xClose      */ httpClose,
    /* xFilter     */ httpFilter,
    /* xNext       */ httpNext,
    /* xEof        */ httpEof,
    /* xColumn     */ httpColumn,
    /* xRowid      */ httpRowid,
    /* xUpdate     */ 0,
    /* xBegin      */ 0,
    /* xSync       */ 0,
    /* xCommit     */ 0,
    /* xRollback   */ 0,
    /* xFindMethod */ 0,
    /* xRename     */ 0,
    /* xSavepoint  */ 0,
    /* xRelease    */ 0,
    /* xRollbackTo */ 0,
    /* xShadowName */ 0,
};

static void httpSimpleFunc(sqlite3_context* ctx,
                           int argc,
                           sqlite3_value** argv,
                           const char* zTable,
                           const char* zColumn,
                           int bText) {
    char* zSql;
    int rc;
    sqlite3_stmt* pStmt;

    if (argc == 0) {
        sqlite3_result_error(ctx, "not enough arguments", -1);
        return;
    }

    if (argc == 1) {
        zSql = sqlite3_mprintf(
            "select \"%w\" from \"%w\"(%Q)", zColumn, zTable, sqlite3_value_text(argv[0]));
    }

    if (argc == 2) {
        zSql = sqlite3_mprintf("select \"%w\" from \"%w\"(%Q, %Q)",
                               zColumn,
                               zTable,
                               sqlite3_value_text(argv[0]),
                               sqlite3_value_text(argv[1]));
    }

    if (argc == 3) {
        zSql = sqlite3_mprintf("select \"%w\" from \"%w\"(%Q, %Q, %Q)",
                               zColumn,
                               zTable,
                               sqlite3_value_text(argv[0]),
                               sqlite3_value_text(argv[1]),
                               sqlite3_value_text(argv[2]));
    }

    if (argc >= 4) {
        zSql = sqlite3_mprintf("select \"%w\" from \"%w\"(%Q, %Q, %Q, %Q)",
                               zColumn,
                               zTable,
                               sqlite3_value_text(argv[0]),
                               sqlite3_value_text(argv[1]),
                               sqlite3_value_text(argv[2]),
                               sqlite3_value_text(argv[2]));
    }

    if (zSql == 0) {
        sqlite3_result_error_code(ctx, SQLITE_NOMEM);
        return;
    }

    rc = sqlite3_prepare_v2(sqlite3_context_db_handle(ctx), zSql, -1, &pStmt, 0);

    sqlite3_free(zSql);

    if (rc != SQLITE_OK) {
        sqlite3_result_error_code(ctx, rc);
        return;
    }

    rc = sqlite3_step(pStmt);

    if (rc == SQLITE_ROW) {
        int size = sqlite3_column_bytes(pStmt, 0);
        if (bText) {
            sqlite3_result_text(ctx, sqlite3_column_text(pStmt, 0), size, SQLITE_TRANSIENT);
        } else {
            sqlite3_result_blob(ctx, sqlite3_column_blob(pStmt, 0), size, SQLITE_TRANSIENT);
        }
        sqlite3_finalize(pStmt);
        return;
    }

    rc = sqlite3_finalize(pStmt);
    if (rc != SQLITE_OK) {
        sqlite3_result_error_code(ctx, rc);
    }
}

static void httpGetBodyFunc(sqlite3_context* ctx, int argc, sqlite3_value** argv) {
    httpSimpleFunc(ctx, argc, argv, "http_get", "response_body", 0);
}

static void httpPostBodyFunc(sqlite3_context* ctx, int argc, sqlite3_value** argv) {
    httpSimpleFunc(ctx, argc, argv, "http_post", "response_body", 0);
}

static void httpDoBodyFunc(sqlite3_context* ctx, int argc, sqlite3_value** argv) {
    httpSimpleFunc(ctx, argc, argv, "http_do", "response_body", 0);
}

static void httpGetHeadersFunc(sqlite3_context* ctx, int argc, sqlite3_value** argv) {
    httpSimpleFunc(ctx, argc, argv, "http_get", "response_headers", 1);
}

static void httpPostHeadersFunc(sqlite3_context* ctx, int argc, sqlite3_value** argv) {
    httpSimpleFunc(ctx, argc, argv, "http_post", "response_headers", 1);
}

static void httpDoHeadersFunc(sqlite3_context* ctx, int argc, sqlite3_value** argv) {
    httpSimpleFunc(ctx, argc, argv, "http_do", "response_headers", 1);
}

static void httpHeadersFunc(sqlite3_context* ctx, int argc, sqlite3_value** argv) {
    char* zHeaders = NULL;
    int i;
    int size = 0;
    int offset = 0;
    if (argc < 2) {
        sqlite3_result_error(ctx, "http_headers: expected at least 2 arguments", -1);
        return;
    }
    if (argc % 2 != 0) {
        sqlite3_result_error(ctx, "http_headers: even number of arguments expected", -1);
        return;
    }
    for (i = 0; i < argc; i += 2) {
        size += sqlite3_value_bytes(argv[i]) + 2;
        size += sqlite3_value_bytes(argv[i + 1]) + 2;
    }
    zHeaders = sqlite3_malloc(size);
    if (!zHeaders) {
        sqlite3_result_error_code(ctx, SQLITE_NOMEM);
        return;
    }
    for (i = 0; i < argc; i += 2) {
        memcpy(zHeaders + offset, sqlite3_value_text(argv[i]), sqlite3_value_bytes(argv[i]));
        offset += sqlite3_value_bytes(argv[i]);

        memcpy(zHeaders + offset, ": ", 2);
        offset += 2;

        memcpy(
            zHeaders + offset, sqlite3_value_text(argv[i + 1]), sqlite3_value_bytes(argv[i + 1]));
        offset += sqlite3_value_bytes(argv[i + 1]);

        memcpy(zHeaders + offset, "\r\n", 2);
        offset += 2;
    }
    assert(offset == size);
    sqlite3_result_text(ctx, zHeaders, size, sqlite3_free);
}

static void httpHeadersHasFunc(sqlite3_context* ctx, int argc, sqlite3_value** argv) {
    int zHeadersSize = sqlite3_value_bytes(argv[0]);
    const char* zHeaders = sqlite3_value_text(argv[0]);
    int zHeaderSize = sqlite3_value_bytes(argv[1]);
    const char* zHeader = sqlite3_value_text(argv[1]);
    while (zHeadersSize > 0) {
        int iParsed = 0;
        const char* pName;
        int iNameSize;
        int rc = http_next_header(zHeaders, zHeadersSize, &iParsed, &pName, &iNameSize, NULL, NULL);
        if (rc == SQLITE_ERROR) {
            sqlite3_result_error(ctx, "http_headers_has: malformed headers", -1);
            return;
        }
        if (rc == SQLITE_DONE) {
            sqlite3_result_int(ctx, 0);
            return;
        }
        zHeaders += iParsed;
        zHeadersSize -= iParsed;
        if (iNameSize == zHeaderSize && sqlite3_strnicmp(zHeader, pName, zHeaderSize) == 0) {
            sqlite3_result_int(ctx, 1);
            return;
        }
    }
    sqlite3_result_int(ctx, 0);
}

static void httpHeadersGetFunc(sqlite3_context* ctx, int argc, sqlite3_value** argv) {
    int zHeadersSize = sqlite3_value_bytes(argv[0]);
    const char* zHeaders = sqlite3_value_text(argv[0]);
    int zHeaderSize = sqlite3_value_bytes(argv[1]);
    const char* zHeader = sqlite3_value_text(argv[1]);
    while (zHeadersSize > 0) {
        int iParsed = 0;
        const char* pName;
        int iNameSize;
        const char* pValue;
        int iValueSize;
        int rc = http_next_header(
            zHeaders, zHeadersSize, &iParsed, &pName, &iNameSize, &pValue, &iValueSize);
        if (rc == SQLITE_ERROR) {
            sqlite3_result_error(ctx, "http_headers_get: malformed headers", -1);
            return;
        }
        if (rc == SQLITE_DONE) {
            sqlite3_result_int(ctx, 0);
            return;
        }
        zHeaders += iParsed;
        zHeadersSize -= iParsed;
        if (iNameSize == zHeaderSize && sqlite3_strnicmp(zHeader, pName, zHeaderSize) == 0) {
            sqlite3_result_text(ctx, pValue, iValueSize, SQLITE_TRANSIENT);
            return;
        }
    }
}

#define HTTP_HEADERS_EACH_COL_NAME 0
#define HTTP_HEADERS_EACH_COL_VALUE 1
#define HTTP_HEADERS_EACH_COL_HEADERS 2

typedef struct http_headers_each_vtab http_headers_each_vtab;
struct http_headers_each_vtab {
    sqlite3_vtab base;
};

typedef struct http_headers_each_cursor http_headers_each_cursor;
struct http_headers_each_cursor {
    sqlite3_vtab_cursor base;
    sqlite3_int64 iRowid;
    const char* zHeaders;
    int zHeadersSize;
    const char* p;
    const char* name;
    int nameSize;
    const char* value;
    int valueSize;
    int available;
    int done;
};

static int httpHeadersEachConnect(sqlite3* db,
                                  void* pAux,
                                  int argc,
                                  const char* const* argv,
                                  sqlite3_vtab** ppVtab,
                                  char** pzErr) {
    http_headers_each_vtab* pNew;
    int rc;
    rc = sqlite3_declare_vtab(db, "CREATE TABLE x(name TEXT, value TEXT, headers TEXT HIDDEN)");
    if (rc == SQLITE_OK) {
        pNew = sqlite3_malloc(sizeof(*pNew));
        *ppVtab = (sqlite3_vtab*)pNew;
        if (pNew == 0)
            return SQLITE_NOMEM;
        memset(pNew, 0, sizeof(*pNew));
    }
    return rc;
}

static int httpHeadersEachDisconnect(sqlite3_vtab* pVtab) {
    http_headers_each_vtab* p = (http_headers_each_vtab*)pVtab;
    sqlite3_free(p);
    return SQLITE_OK;
}

static int httpHeadersEachOpen(sqlite3_vtab* p, sqlite3_vtab_cursor** ppCursor) {
    http_headers_each_cursor* pCur;
    pCur = sqlite3_malloc(sizeof(*pCur));
    if (pCur == 0)
        return SQLITE_NOMEM;
    memset(pCur, 0, sizeof(*pCur));
    *ppCursor = &pCur->base;
    return SQLITE_OK;
}

static int httpHeadersEachClose(sqlite3_vtab_cursor* cur) {
    http_headers_each_cursor* pCur = (http_headers_each_cursor*)cur;
    sqlite3_free(pCur);
    return SQLITE_OK;
}

static int httpHeadersEachNext(sqlite3_vtab_cursor* cur) {
    http_headers_each_cursor* pCur = (http_headers_each_cursor*)cur;
    pCur->iRowid++;
    int nparsed = 0;
    int rc = http_next_header(pCur->p,
                              pCur->available,
                              &nparsed,
                              &pCur->name,
                              &pCur->nameSize,
                              &pCur->value,
                              &pCur->valueSize);
    pCur->p += nparsed;
    pCur->available -= nparsed;
    if (rc == SQLITE_ERROR) {
        sqlite3_free(cur->pVtab->zErrMsg);
        cur->pVtab->zErrMsg = sqlite3_mprintf("http_headers_each: malformed headers");
        return SQLITE_ERROR;
    }
    pCur->done = rc == SQLITE_DONE;
    return SQLITE_OK;
}

static int httpHeadersEachColumn(sqlite3_vtab_cursor* cur, sqlite3_context* ctx, int i) {
    http_headers_each_cursor* pCur = (http_headers_each_cursor*)cur;
    switch (i) {
    case HTTP_HEADERS_EACH_COL_NAME:
        sqlite3_result_text(ctx, pCur->name, pCur->nameSize, SQLITE_TRANSIENT);
        break;

    case HTTP_HEADERS_EACH_COL_VALUE:
        sqlite3_result_text(ctx, pCur->value, pCur->valueSize, SQLITE_TRANSIENT);
        break;

    case HTTP_HEADERS_EACH_COL_HEADERS:
        sqlite3_result_text(ctx, pCur->zHeaders, pCur->zHeadersSize, SQLITE_TRANSIENT);
        break;

    default:
        sqlite3_result_error(ctx, "unknown column", -1);
        break;
    }
    return SQLITE_OK;
}

static int httpHeadersEachRowid(sqlite3_vtab_cursor* cur, sqlite_int64* pRowid) {
    http_headers_each_cursor* pCur = (http_headers_each_cursor*)cur;
    *pRowid = pCur->iRowid;
    return SQLITE_OK;
}

static int httpHeadersEachEof(sqlite3_vtab_cursor* cur) {
    http_headers_each_cursor* pCur = (http_headers_each_cursor*)cur;
    return pCur->done;
}

static int httpHeadersEachFilter(sqlite3_vtab_cursor* pVtabCursor,
                                 int idxNum,
                                 const char* idxStr,
                                 int argc,
                                 sqlite3_value** argv) {
    http_headers_each_cursor* pCur = (http_headers_each_cursor*)pVtabCursor;
    pCur->zHeadersSize = sqlite3_value_bytes(argv[0]);
    pCur->available = pCur->zHeadersSize;
    pCur->zHeaders = sqlite3_value_text(argv[0]);
    pCur->p = pCur->zHeaders;
    int nparsed = 0;
    int rc = http_next_header(pCur->p,
                              pCur->available,
                              &nparsed,
                              &pCur->name,
                              &pCur->nameSize,
                              &pCur->value,
                              &pCur->valueSize);
    pCur->p += nparsed;
    pCur->available -= nparsed;
    pCur->iRowid = 1;
    if (rc == SQLITE_ERROR) {
        sqlite3_free(pVtabCursor->pVtab->zErrMsg);
        pVtabCursor->pVtab->zErrMsg = sqlite3_mprintf("http_headers_each: malformed headers");
        return SQLITE_ERROR;
    }
    pCur->done = rc == SQLITE_DONE;
    return SQLITE_OK;
}

static int httpHeadersEachBestIndex(sqlite3_vtab* tab, sqlite3_index_info* pIdxInfo) {
    http_vtab* pTab = (http_vtab*)tab;
    int bHeadersSeen = 0;

    for (int i = 0; i < pIdxInfo->nConstraint; ++i) {
        if (pIdxInfo->aConstraint[i].iColumn == HTTP_HEADERS_EACH_COL_HEADERS) {
            if (!pIdxInfo->aConstraint[i].usable) {
                return SQLITE_CONSTRAINT;
            }
            pIdxInfo->aConstraintUsage[i].argvIndex = 1;
            pIdxInfo->aConstraintUsage[i].omit = 1;
            bHeadersSeen = 1;
        }
    }

    if (!bHeadersSeen) {
        sqlite3_free(tab->zErrMsg);
        tab->zErrMsg = sqlite3_mprintf("headers missing");
        return SQLITE_ERROR;
    }

    pIdxInfo->estimatedCost = (double)1;
    // pIdxInfo->estimatedRows = 1;

    return SQLITE_OK;
}

static sqlite3_module httpHeadersEachModule = {
    /* iVersion    */ 0,
    /* xCreate     */ 0,
    /* xConnect    */ httpHeadersEachConnect,
    /* xBestIndex  */ httpHeadersEachBestIndex,
    /* xDisconnect */ httpHeadersEachDisconnect,
    /* xDestroy    */ 0,
    /* xOpen       */ httpHeadersEachOpen,
    /* xClose      */ httpHeadersEachClose,
    /* xFilter     */ httpHeadersEachFilter,
    /* xNext       */ httpHeadersEachNext,
    /* xEof        */ httpHeadersEachEof,
    /* xColumn     */ httpHeadersEachColumn,
    /* xRowid      */ httpHeadersEachRowid,
    /* xUpdate     */ 0,
    /* xBegin      */ 0,
    /* xSync       */ 0,
    /* xCommit     */ 0,
    /* xRollback   */ 0,
    /* xFindMethod */ 0,
    /* xRename     */ 0,
    /* xSavepoint  */ 0,
    /* xRelease    */ 0,
    /* xRollbackTo */ 0,
    /* xShadowName */ 0,
};

static const struct Func {
    const char* name;
    void (*xFunc)(sqlite3_context*, int, sqlite3_value**);
} funcs[] = {
    {"http_get_body", httpGetBodyFunc},
    {"http_post_body", httpPostBodyFunc},
    {"http_do_body", httpDoBodyFunc},
    {"http_get_headers", httpGetHeadersFunc},
    {"http_post_headers", httpPostHeadersFunc},
    {"http_do_headers", httpDoHeadersFunc},
    {"http_headers", httpHeadersFunc},
    {"http_headers_has", httpHeadersHasFunc},
    {"http_headers_get", httpHeadersGetFunc},
    {NULL, NULL},
};

static const struct Module {
    const char* name;
    const sqlite3_module* module;
} modules[] = {
    {"http_get", &httpModule},
    {"http_post", &httpModule},
    {"http_do", &httpModule},
    {"http_headers_each", &httpHeadersEachModule},
    {NULL, NULL},
};

#ifdef _WIN32
__declspec(dllexport)
#endif
    int sqlite3_http_init(sqlite3* db, char** pzErrMsg, const sqlite3_api_routines* pApi) {
    int rc = SQLITE_OK;
    SQLITE_EXTENSION_INIT2(pApi);
    int i;
    for (i = 0; funcs[i].name && rc == SQLITE_OK; ++i) {
        rc = sqlite3_create_function(
            db, funcs[i].name, -1, SQLITE_UTF8, NULL, funcs[i].xFunc, NULL, NULL);
    }
    for (i = 0; modules[i].name && rc == SQLITE_OK; ++i) {
        rc = sqlite3_create_module(db, modules[i].name, modules[i].module, 0);
    }
    return rc;
}

/********** src/http_backend_curl.c **********/

#ifdef HTTP_BACKEND_CURL


#include <assert.h>
#include <string.h>

SQLITE_EXTENSION_INIT3

#ifdef _WIN32
#include <windows.h>

void* http_dlopen(const char* zName) {
    return LoadLibraryA(zName);
}

void http_dlclose(void* library) {
    FreeLibrary(library);
}

void* http_dlsym(void* library, const char* zName) {
    return GetProcAddress(library, zName);
}
#else
#include <dlfcn.h>

void* http_dlopen(const char* zName) {
    return dlopen(zName, RTLD_NOW);
}

void http_dlclose(void* library) {
    dlclose(zName);
}

void* http_dlsym(void* library, const char* zName) {
    return dlsym(library, zName);
}
#endif

#ifndef MIN
#define MIN(A, B) ((A) < (B) ? (A) : (B))
#endif

typedef struct CURL CURL;
typedef int CURLcode;
typedef int CURLoption;
typedef int CURLINFO;
typedef int CURLversion;

#define CURL_ERROR_SIZE 256

#define CURLE_OK 0

#define CURLOPT_ERRORBUFFER (10000 + 10)
#define CURLOPT_URL (10000 + 2)
#define CURLOPT_FOLLOWLOCATION (52)
#define CURLOPT_HTTPGET (80)
#define CURLOPT_POST (47)
#define CURLOPT_SSL_OPTIONS (216)
#define CURLOPT_WRITEFUNCTION (20000 + 11)
#define CURLOPT_WRITEDATA (10000 + 1)
#define CURLOPT_READFUNCTION (20000 + 12)
#define CURLOPT_READDATA (10000 + 9)
#define CURLOPT_HEADERFUNCTION (20000 + 79)
#define CURLOPT_HEADERDATA (10000 + 29)
#define CURLOPT_POSTFIELDS (10000 + 15)
#define CURLOPT_POSTFIELDSIZE_LARGE (30000 + 120)
#define CURLOPT_HTTPHEADER (10000 + 23)
#define CURLOPT_INFILESIZE_LARGE (30000 + 115)
#define CURLOPT_NOBODY (44)
#define CURLOPT_CUSTOMREQUEST (10000 + 36)
#define CURLOPT_PUT (54)

#define CURLSSLOPT_NATIVE_CA (1 << 4)

#define CURLVERSION_NOW 9

#define CURLINFO_RESPONSE_CODE (0x200000 + 2)

// The struct is bigger but I don't need more information for now...
struct curl_version_info_data {
    CURLversion age;
    const char* version;
    unsigned int version_num;
};
typedef struct curl_version_info_data curl_version_info_data;

struct curl_slist;

typedef CURL* (*curl_easy_init_t)();
typedef void (*curl_easy_cleanup_t)(CURL*);
typedef CURLcode (*curl_easy_setopt_t)(CURL*, CURLoption, ...);
typedef CURLcode (*curl_easy_perform_t)(CURL*);
typedef CURLcode (*curl_easy_getinfo_t)(CURL*, CURLINFO, ...);
typedef char* (*curl_version_t)();
typedef curl_version_info_data* (*curl_version_info_t)(CURLversion);
typedef struct curl_slist* (*curl_slist_append_t)(struct curl_slist*, const char*);
typedef void (*curl_slist_free_all_t)(struct curl_slist*);
typedef const char* (*curl_easy_strerror_t)(CURLcode);

struct curl_api_routines {
    void* pLibrary;
    curl_easy_init_t easy_init;
    curl_easy_cleanup_t easy_cleanup;
    curl_easy_setopt_t easy_setopt;
    curl_easy_perform_t easy_perform;
    curl_easy_getinfo_t easy_getinfo;
    curl_version_t version;
    curl_version_info_t version_info;
    curl_slist_append_t slist_append;
    curl_slist_free_all_t slist_free_all;
    curl_easy_strerror_t easy_strerror;
};

static struct curl_api_routines curl_api;

#define curl_easy_init curl_api.easy_init
#define curl_easy_cleanup curl_api.easy_cleanup
#define curl_easy_setopt curl_api.easy_setopt
#define curl_easy_perform curl_api.easy_perform
#define curl_easy_getinfo curl_api.easy_getinfo
#define curl_version curl_api.version
#define curl_version_info curl_api.version_info
#define curl_slist_append curl_api.slist_append
#define curl_slist_free_all curl_api.slist_free_all
#define curl_easy_strerror curl_api.easy_strerror

static const char* aCurlLibNames[] = {
#ifdef _WIN32
    "libcurl-x64.dll",
    "libcurl.dll",
#else
    "libcurl.so",
    "libcurl.so.4",
#endif
};

static const int szCurlLibNames = sizeof(aCurlLibNames) / sizeof(aCurlLibNames[0]);

int http_backend_curl_load(char** zErrMsg) {
    assert(zErrMsg != NULL);

    *zErrMsg = NULL;

    if (!curl_api.pLibrary) {
        for (int i = 0; i < szCurlLibNames && !curl_api.pLibrary; ++i) {
            curl_api.pLibrary = http_dlopen(aCurlLibNames[i]);
        }
    }

    if (!curl_api.pLibrary) {
        *zErrMsg = sqlite3_mprintf("failed to load curl");
        goto error;
    }

    curl_easy_init = (curl_easy_init_t)http_dlsym(curl_api.pLibrary, "curl_easy_init");
    if (!curl_easy_init) {
        *zErrMsg = sqlite3_mprintf("failed to load curl_easy_init");
        goto error;
    }
    curl_easy_cleanup = (curl_easy_cleanup_t)http_dlsym(curl_api.pLibrary, "curl_easy_cleanup");
    if (!curl_easy_cleanup) {
        *zErrMsg = sqlite3_mprintf("failed to load curl_easy_cleanup");
        goto error;
    }
    curl_easy_setopt = (curl_easy_setopt_t)http_dlsym(curl_api.pLibrary, "curl_easy_setopt");
    if (!curl_easy_setopt) {
        *zErrMsg = sqlite3_mprintf("failed to load curl_easy_setopt");
        goto error;
    }
    curl_easy_perform = (curl_easy_perform_t)http_dlsym(curl_api.pLibrary, "curl_easy_perform");
    if (!curl_easy_perform) {
        *zErrMsg = sqlite3_mprintf("failed to load curl_easy_perform");
        goto error;
    }
    curl_easy_getinfo = (curl_easy_getinfo_t)http_dlsym(curl_api.pLibrary, "curl_easy_getinfo");
    if (!curl_easy_getinfo) {
        *zErrMsg = sqlite3_mprintf("failed to load curl_easy_getinfo");
        goto error;
    }
    curl_version = (curl_version_t)http_dlsym(curl_api.pLibrary, "curl_version");
    if (!curl_version) {
        *zErrMsg = sqlite3_mprintf("failed to load curl_version");
        goto error;
    }
    curl_version_info = (curl_version_info_t)http_dlsym(curl_api.pLibrary, "curl_version_info");
    if (!curl_version_info) {
        *zErrMsg = sqlite3_mprintf("failed to load curl_version_info");
        goto error;
    }
    curl_slist_append = (curl_slist_append_t)http_dlsym(curl_api.pLibrary, "curl_slist_append");
    if (!curl_slist_append) {
        *zErrMsg = sqlite3_mprintf("failed to load curl_slist_append");
        goto error;
    }
    curl_slist_free_all =
        (curl_slist_free_all_t)http_dlsym(curl_api.pLibrary, "curl_slist_free_all");
    if (!curl_slist_free_all) {
        *zErrMsg = sqlite3_mprintf("failed to load curl_slist_free_all");
        goto error;
    }
    curl_easy_strerror = (curl_easy_strerror_t)http_dlsym(curl_api.pLibrary, "curl_easy_strerror");
    if (!curl_easy_strerror) {
        *zErrMsg = sqlite3_mprintf("failed to load curl_easy_strerror");
        goto error;
    }

    return SQLITE_OK;

error:

    if (curl_api.pLibrary) {
        http_dlclose(curl_api.pLibrary);
        memset(&curl_api, 0, sizeof(curl_api));
    }

    return SQLITE_ERROR;
}

static size_t write_callback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    http_response* pResp = (http_response*)userdata;
    char* p;
    p = sqlite3_realloc(pResp->pBody, pResp->szBody + size * nmemb);
    if (!p) {
        return 0;
    }
    pResp->pBody = p;
    memcpy((char*)pResp->pBody + pResp->szBody, ptr, size * nmemb);
    pResp->szBody += size * nmemb;
    return size * nmemb;
}

static size_t header_callback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    http_response* pResp = (http_response*)userdata;
    char* p;
    p = sqlite3_realloc(pResp->zHeaders, pResp->szHeaders + size * nmemb + 1);
    if (!p) {
        return 0;
    }
    pResp->zHeaders = p;
    memcpy(pResp->zHeaders + pResp->szHeaders, ptr, size * nmemb);
    pResp->szHeaders += size * nmemb;
    pResp->zHeaders[pResp->szHeaders] = '\0';
    return size * nmemb;
}

struct readdata {
    const char* pBody;
    size_t szBody;
};

static size_t read_callback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    struct readdata* pData = (struct readdata*)userdata;
    size_t szToSend = MIN(size * nmemb, pData->szBody);
    memcpy(ptr, pData->pBody, szToSend);
    pData->pBody += szToSend;
    pData->szBody -= szToSend;
    return szToSend;
}

static int
headers_to_curl_headers(struct curl_slist** pHeaders, const char* zHeaders, int szHeaders) {
    const char* name;
    int szName;
    const char* value;
    int szValue;
    while (szHeaders > 0) {
        int nParsed = 0;
        int rc = http_next_header(zHeaders, szHeaders, &nParsed, &name, &szName, &value, &szValue);
        if (rc == SQLITE_DONE) {
            break;
        } else if (rc == SQLITE_ROW) {
            char* zHeader = sqlite3_mprintf("%.*s: %.*s", szName, name, szValue, value);
            if (!zHeader) {
                return 0;
            }
            void* tmp = curl_slist_append(*pHeaders, zHeader);
            sqlite3_free(zHeader);
            if (!tmp) {
                return 0;
            }
            *pHeaders = tmp;
            szHeaders -= nParsed;
            zHeaders += nParsed;
        } else if (rc == SQLITE_ERROR) {
            return 0;
        }
    }
    return 1;
}

static int set_curl_error_message(char** ppErrMsg, CURLcode rc, const char* message) {
    *ppErrMsg = sqlite3_mprintf("%s: %s (curl error code %d)", message, curl_easy_strerror(rc), rc);
    return SQLITE_ERROR;
}

int http_do_request(http_request* req, http_response* resp, char** ppErrMsg) {
    int rc;
    CURL* curl;
    char aErrorBuf[CURL_ERROR_SIZE];
    curl_version_info_data* pCurlVersionInfo;
    long responseCode;
    struct curl_slist* headers = NULL;
    CURLcode curlrc;
    struct readdata readdata;

    rc = http_backend_curl_load(ppErrMsg);
    if (rc != SQLITE_OK) {
        return rc;
    }

    pCurlVersionInfo = curl_version_info(CURLVERSION_NOW);

    curl = curl_easy_init();
    if (!curl) {
        *ppErrMsg = sqlite3_mprintf("curl_easy_init failed");
        rc = SQLITE_ERROR;
        goto error;
    }

    memset(aErrorBuf, 0, sizeof(aErrorBuf));
    if ((curlrc = curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, aErrorBuf)) != CURLE_OK) {
        rc = set_curl_error_message(ppErrMsg, curlrc, "curl_easy_setopt");
        goto error;
    }

    if ((curlrc = curl_easy_setopt(curl, CURLOPT_URL, req->zUrl)) != CURLE_OK) {
        rc = set_curl_error_message(ppErrMsg, curlrc, "curl_easy_setopt");
        goto error;
    }

    if ((curlrc = curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L)) != CURLE_OK) {
        rc = set_curl_error_message(ppErrMsg, curlrc, "curl_easy_setopt");
        goto error;
    }

    if (sqlite3_stricmp(req->zMethod, "GET") == 0) {
        if ((curlrc = curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L)) != CURLE_OK) {
            rc = set_curl_error_message(ppErrMsg, curlrc, "curl_easy_setopt");
            goto error;
        }
    } else if (sqlite3_stricmp(req->zMethod, "POST") == 0) {
        if ((curlrc = curl_easy_setopt(curl, CURLOPT_POST, 1L)) != CURLE_OK) {
            rc = set_curl_error_message(ppErrMsg, curlrc, "curl_easy_setopt");
            goto error;
        }
        if ((curlrc = curl_easy_setopt(curl, CURLOPT_POSTFIELDS, req->pBody)) != CURLE_OK) {
            rc = set_curl_error_message(ppErrMsg, curlrc, "curl_easy_setopt");
            goto error;
        }
        if ((curlrc = curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, req->szBody)) !=
            CURLE_OK) {
            rc = set_curl_error_message(ppErrMsg, curlrc, "curl_easy_setopt");
            goto error;
        }
    } else if (sqlite3_stricmp(req->zMethod, "PUT") == 0) {
        if ((curlrc = curl_easy_setopt(curl, CURLOPT_PUT, 1L)) != CURLE_OK) {
            rc = set_curl_error_message(ppErrMsg, curlrc, "curl_easy_setopt");
            goto error;
        }
    } else if (sqlite3_stricmp(req->zMethod, "HEAD") == 0) {
        if ((curlrc = curl_easy_setopt(curl, CURLOPT_NOBODY, 1L)) != CURLE_OK) {
            rc = set_curl_error_message(ppErrMsg, curlrc, "curl_easy_setopt");
            goto error;
        }
    } else {
        if ((curlrc = curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L)) != CURLE_OK) {
            rc = set_curl_error_message(ppErrMsg, curlrc, "curl_easy_setopt");
            goto error;
        }

        if (req->pBody && req->szBody) {
            if ((curlrc = curl_easy_setopt(curl, CURLOPT_PUT, 1L)) != CURLE_OK) {
                rc = set_curl_error_message(ppErrMsg, curlrc, "curl_easy_setopt");
                goto error;
            }
        }

        if ((curlrc = curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, req->zMethod)) != CURLE_OK) {
            rc = set_curl_error_message(ppErrMsg, curlrc, "curl_easy_setopt");
            goto error;
        }
    }

    if (req->pBody && req->szBody && sqlite3_stricmp(req->zMethod, "POST") != 0) {
        readdata.pBody = (const char*)req->pBody;
        readdata.szBody = (size_t)req->szBody;

        if ((curlrc = curl_easy_setopt(curl, CURLOPT_INFILESIZE_LARGE, req->szBody)) != CURLE_OK) {
            rc = set_curl_error_message(ppErrMsg, curlrc, "curl_easy_setopt");
            goto error;
        }

        if ((curlrc = curl_easy_setopt(curl, CURLOPT_READFUNCTION, read_callback)) != CURLE_OK) {
            rc = set_curl_error_message(ppErrMsg, curlrc, "curl_easy_setopt");
            goto error;
        }

        if ((curlrc = curl_easy_setopt(curl, CURLOPT_READDATA, &readdata)) != CURLE_OK) {
            rc = set_curl_error_message(ppErrMsg, curlrc, "curl_easy_setopt");
            goto error;
        }
    }

    // Since 7.71.0 (Jun 24 2020). Makes life easier on Windows at least.
    if (pCurlVersionInfo->version_num >= ((7 << 16) | (71 << 8))) {
        if ((curlrc = curl_easy_setopt(curl, CURLOPT_SSL_OPTIONS, CURLSSLOPT_NATIVE_CA)) !=
            CURLE_OK) {
            rc = set_curl_error_message(ppErrMsg, curlrc, "curl_easy_setopt");
            goto error;
        }
    }

    if ((curlrc = curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback)) != CURLE_OK) {
        rc = set_curl_error_message(ppErrMsg, curlrc, "curl_easy_setopt");
        goto error;
    }

    if ((curlrc = curl_easy_setopt(curl, CURLOPT_WRITEDATA, resp)) != CURLE_OK) {
        rc = set_curl_error_message(ppErrMsg, curlrc, "curl_easy_setopt");
        goto error;
    }

    if ((curlrc = curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, header_callback)) != CURLE_OK) {
        rc = set_curl_error_message(ppErrMsg, curlrc, "curl_easy_setopt");
        goto error;
    }

    if ((curlrc = curl_easy_setopt(curl, CURLOPT_HEADERDATA, resp)) != CURLE_OK) {
        rc = set_curl_error_message(ppErrMsg, curlrc, "curl_easy_setopt");
        goto error;
    }

    // Remove content-type header by default...
    headers = curl_slist_append(NULL, "content-type;");
    if (!headers) {
        *ppErrMsg = sqlite3_mprintf("curl_slist_append failed");
        rc = SQLITE_ERROR;
        goto error;
    }

    if (req->zHeaders) {
        if (!headers_to_curl_headers(&headers, req->zHeaders, strlen(req->zHeaders))) {
            *ppErrMsg = sqlite3_mprintf("failed to convert headers for curl");
            rc = SQLITE_ERROR;
            goto error;
        }
        if ((curlrc = curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers)) != CURLE_OK) {
            rc = set_curl_error_message(ppErrMsg, curlrc, "curl_easy_setopt");
            goto error;
        }
    }

    if (curl_easy_perform(curl) != CURLE_OK) {
        *ppErrMsg = sqlite3_mprintf("curl_easy_perform failed: %s", aErrorBuf);
        rc = SQLITE_ERROR;
        goto error;
    }

    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &responseCode);
    resp->iStatusCode = responseCode;

    remove_all_but_last_headers(resp->zHeaders);
    separate_status_and_headers(&resp->zStatus, resp->zHeaders);

    rc = SQLITE_OK;

error:

done:

    curl_easy_cleanup(curl);
    curl_slist_free_all(headers);

    return rc;
}

#endif // HTTP_BACKEND_CURL

/********** src/http_backend_dummy.c **********/

#ifdef HTTP_BACKEND_DUMMY


#include <assert.h>

SQLITE_EXTENSION_INIT3

static http_request sLastRequest;
static http_response* sResponse;
static char* sErrMsg;

void http_backend_dummy_set_response(http_response* response) {
    sResponse = response;
}

void http_backend_dummy_set_errmsg(const char* zErrMsg) {
    sErrMsg = sqlite3_mprintf("%s", zErrMsg);
}

void http_backend_dummy_reset_request() {
    sqlite3_free(sLastRequest.zMethod);
    sqlite3_free((void*)sLastRequest.pBody);
    sqlite3_free((void*)sLastRequest.zHeaders);
    memset(&sLastRequest, 0, sizeof(sLastRequest));
}

const http_request* http_backend_dummy_get_last_request() {
    return &sLastRequest;
}

int http_do_request(http_request* req, http_response* resp, char** ppErrMsg) {
    int rc = SQLITE_OK;
    if (sResponse) {
        *resp = *sResponse;
        sResponse = NULL;
    } else if (sErrMsg) {
        *ppErrMsg = sErrMsg;
        sErrMsg = NULL;
        rc = SQLITE_ERROR;
    } else {
        assert(0);
    }
    http_backend_dummy_reset_request();
    if (req->zMethod) {
        sLastRequest.zMethod = sqlite3_mprintf("%s", req->zMethod);
    }
    if (req->zUrl) {
        sLastRequest.zUrl = sqlite3_mprintf("%s", req->zUrl);
    }
    if (req->pBody) {
        sLastRequest.pBody = sqlite3_malloc(req->szBody);
        memcpy((void*)sLastRequest.pBody, req->pBody, req->szBody);
    }
    sLastRequest.szBody = req->szBody;
    if (req->zHeaders) {
        sLastRequest.zHeaders = sqlite3_mprintf("%s", req->zHeaders);
    }
    return rc;
}

#endif // HTTP_BACKEND_DUMMY

/********** src/http_backend_winhttp.c **********/

#ifdef HTTP_BACKEND_WINHTTP


#include <windows.h>
#include <winhttp.h>

#include <assert.h>

SQLITE_EXTENSION_INIT3

static char* unicode_to_utf8(LPCWSTR zWide) {
    DWORD nSize;
    char* zUtf8;
    nSize = WideCharToMultiByte(CP_UTF8, 0, zWide, -1, NULL, 0, NULL, NULL);
    if (nSize == 0) {
        return NULL;
    }
    zUtf8 = sqlite3_malloc(nSize);
    if (!zUtf8) {
        return NULL;
    }
    nSize = WideCharToMultiByte(CP_UTF8, 0, zWide, -1, zUtf8, nSize, NULL, NULL);
    if (nSize == 0) {
        sqlite3_free(zUtf8);
        return NULL;
    }
    return zUtf8;
}

static LPWSTR utf8_to_unicode(const char* zText) {
    DWORD nSize;
    LPWSTR zWide;
    nSize = MultiByteToWideChar(CP_UTF8, 0, zText, -1, NULL, 0);
    if (nSize == 0) {
        return NULL;
    }
    zWide = sqlite3_malloc(sizeof(*zWide) * nSize);
    if (!zWide) {
        return NULL;
    }
    nSize = MultiByteToWideChar(CP_UTF8, 0, zText, -1, zWide, nSize);
    if (nSize == 0) {
        sqlite3_free(zWide);
        return NULL;
    }
    return zWide;
}

static char* win32_get_last_error(DWORD code) {
    LPWSTR zWide = NULL;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                       FORMAT_MESSAGE_IGNORE_INSERTS,
                   NULL,
                   code,
                   0,
                   (LPWSTR)&zWide,
                   0,
                   NULL);
    char* zUtf8 = unicode_to_utf8(zWide);
    LocalFree(zWide);
    return zUtf8;
}

static int query_headers(HINTERNET request, DWORD dwInfoLevel, char** ppResult) {
    DWORD szWide = 0;
    LPWSTR zWide = NULL;

    WinHttpQueryHeaders(
        request, dwInfoLevel, WINHTTP_HEADER_NAME_BY_INDEX, NULL, &szWide, WINHTTP_NO_HEADER_INDEX);

    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
        return SQLITE_ERROR;
    }

    zWide = sqlite3_malloc(szWide);
    if (!zWide) {
        return SQLITE_NOMEM;
    }

    if (!WinHttpQueryHeaders(request,
                             dwInfoLevel,
                             WINHTTP_HEADER_NAME_BY_INDEX,
                             zWide,
                             &szWide,
                             WINHTTP_NO_HEADER_INDEX)) {
        sqlite3_free(zWide);
        return SQLITE_ERROR;
    }

    *ppResult = unicode_to_utf8(zWide);

    sqlite3_free(zWide);

    return SQLITE_OK;
}

int http_do_request(http_request* req, http_response* resp, char** ppErrMsg) {
    LPWSTR zUrlWide = NULL;
    LPWSTR zHostWide = NULL;
    LPWSTR zMethodWide = NULL;
    LPWSTR zHeadersWide = NULL;
    HINTERNET session = NULL;
    HINTERNET conn = NULL;
    HINTERNET request = NULL;
    URL_COMPONENTS components;
    DWORD lastErr = 0;
    const char* errFunc = NULL;
    char* zErrMsg = NULL;
    int rc = SQLITE_ERROR;
    LPWSTR zWideResponseHeaders = NULL;
    DWORD szWideResponseHeaders = 0;
    DWORD dwSize = 0;
    DWORD dwStatusCode;

    zUrlWide = utf8_to_unicode(req->zUrl);
    if (!zUrlWide) {
        rc = SQLITE_NOMEM;
        goto error;
    }

    memset(&components, 0, sizeof(components));
    components.dwStructSize = sizeof(components);
    components.dwHostNameLength = 1;
    components.dwUrlPathLength = 1;
    if (!WinHttpCrackUrl(zUrlWide, 0, 0, &components)) {
        lastErr = GetLastError();
        errFunc = "WinHttpCrackUrl";
        goto error;
    }

    session = WinHttpOpen(L"sqlite3-http",
                          WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                          WINHTTP_NO_PROXY_NAME,
                          WINHTTP_NO_PROXY_BYPASS,
                          0);

    if (!session) {
        lastErr = GetLastError();
        errFunc = "WinHttpCrackOpen";
        goto error;
    }

    // WinHttpSetStatusCallback(session,
    //     (WINHTTP_STATUS_CALLBACK)statusCallback,
    //     WINHTTP_CALLBACK_FLAG_ALL_NOTIFICATIONS,
    //     0);

    zHostWide = sqlite3_malloc(sizeof(wchar_t) * (components.dwHostNameLength + 1));
    if (!zHostWide) {
        rc = SQLITE_NOMEM;
        goto error;
    }
    memcpy(zHostWide, components.lpszHostName, sizeof(wchar_t) * components.dwHostNameLength);
    zHostWide[components.dwHostNameLength] = L'\0';

    conn = WinHttpConnect(session, zHostWide, components.nPort, 0);

    if (!conn) {
        lastErr = GetLastError();
        errFunc = "WinHttpConnect";
        goto error;
    }

    zMethodWide = utf8_to_unicode(req->zMethod);
    if (!zMethodWide) {
        rc = SQLITE_NOMEM;
        goto error;
    }

    request = WinHttpOpenRequest(conn,
                                 zMethodWide,
                                 components.lpszUrlPath,
                                 NULL,
                                 WINHTTP_NO_REFERER,
                                 WINHTTP_DEFAULT_ACCEPT_TYPES,
                                 components.nPort == 443 ? WINHTTP_FLAG_SECURE : 0);

    if (!request) {
        lastErr = GetLastError();
        errFunc = "WinHttpOpenRequest";
        goto error;
    }

    if (req->zHeaders) {
        zHeadersWide = utf8_to_unicode(req->zHeaders);
    }

    if (!WinHttpSendRequest(request,
                            zHeadersWide,
                            zHeadersWide ? -1 : 0,
                            (void*)req->pBody,
                            req->szBody,
                            req->szBody,
                            0)) {
        lastErr = GetLastError();
        errFunc = "WinHttpSendRequest";
        goto error;
    }

    if (!WinHttpReceiveResponse(request, NULL)) {
        lastErr = GetLastError();
        errFunc = "WinHttpReceiveResponse";
        goto error;
    }

    assert(resp->pBody == NULL);
    assert(resp->szBody == 0);
    for (;;) {
        DWORD dwSize = 0;
        DWORD nRead = 0;
        char* nb;
        if (!WinHttpQueryDataAvailable(request, &dwSize)) {
            lastErr = GetLastError();
            errFunc = "WinHttpQueryDataAvailable";
            goto error;
        }
        if (dwSize == 0) {
            break;
        }
        nb = sqlite3_realloc(resp->pBody, resp->szBody + dwSize);
        if (!nb) {
            rc = SQLITE_NOMEM;
            goto error;
        }
        resp->pBody = nb;
        if (!WinHttpReadData(request, (char*)resp->pBody + resp->szBody, dwSize, &nRead)) {
            lastErr = GetLastError();
            errFunc = "WinHttpReadData";
            goto error;
        }
        resp->szBody += dwSize;
        if (nRead == 0) {
            break;
        }
    }

    rc = query_headers(request, WINHTTP_QUERY_RAW_HEADERS_CRLF, &resp->zHeaders);
    if (rc != SQLITE_OK) {
        if (rc == SQLITE_ERROR) {
            lastErr = GetLastError();
            errFunc = "WinHttpQueryHeaders(WINHTTP_QUERY_RAW_HEADERS_CRLF)";
        }
        goto error;
    }
    rc = SQLITE_ERROR; // need to reset this back. Otherwise a jump to error below
                       // could leave rc as SQLITE_OK...

    dwSize = sizeof(dwStatusCode);
    if (!WinHttpQueryHeaders(request,
                             WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX,
                             &dwStatusCode,
                             &dwSize,
                             WINHTTP_NO_HEADER_INDEX)) {
        lastErr = GetLastError();
        errFunc = "WinHttpQueryHeaders(WINHTTP_QUERY_STATUS_CODE)";
        goto error;
    }

    resp->iStatusCode = dwStatusCode;

    remove_all_but_last_headers(resp->zHeaders);
    separate_status_and_headers(&resp->zStatus, resp->zHeaders);

    rc = SQLITE_OK;

    goto done;

error:

    if (rc == SQLITE_ERROR && errFunc) {
        *ppErrMsg = win32_get_last_error(lastErr);
    }

done:

    sqlite3_free(zUrlWide);
    sqlite3_free(zHostWide);
    sqlite3_free(zMethodWide);
    sqlite3_free(zHeadersWide);
    WinHttpCloseHandle(request);
    WinHttpCloseHandle(conn);
    WinHttpCloseHandle(session);

    return rc;
}

#endif // HTTP_BACKEND_WINHTTP

/********** src/http_next_header.c **********/

#include <ctype.h>
#include <sqlite3.h>

static int is_token_char(int c) {
    return isalnum(c) || (c != 0 && strchr("!#$%&'*+-.^_`|~", c));
}

static int is_header_ws(int c) {
    return c == ' ' || c == '\t';
}

static int is_vchar(int c) {
    return c >= 0x21 && c <= 0x7e;
}

// Return SQLITE_ROW if a header was extracted
// Return SQLITE_DONE if headers has been exhausted
// Return SQLITE_ERROR on malformed input
int http_next_header(const char* headers,
                     int size,
                     int* pParsed,
                     const char** ppName,
                     int* pNameSize,
                     const char** ppValue,
                     int* pValueSize) {
    const char* headersEnd = headers + size;
    const char* nameStart = NULL;
    const char* nameEnd = NULL;
    const char* valueStart = NULL;
    const char* valueEnd = NULL;
    const char* last = NULL;
    const char* p = headers;

    enum {
        StateInit,
        StateName,
        StateNameTrailingWs,
        StateValueLeadingWs,
        StateValue,
        StateCR,
        StateCheckFold,
        StateDone,
        StateError,
    };

    int state = StateInit;

    *ppName = NULL;
    *pNameSize = 0;
    *ppValue = NULL;
    *pValueSize = 0;

    if (size == 0) {
        *pParsed = 0;
        return SQLITE_DONE;
    }

    for (; p != headersEnd && state != StateDone && state != StateError; ++p) {
        switch (state) {
        case StateInit:
            // printf("StateInit %02x %c\n", *p, isalnum(*p) ? *p : ' ');
            if (*p == '\r') {
                state = StateCR;
            } else if (is_header_ws(*p)) {
                // There shouldn't be whitespace here. Folding is handled in
                // StateCR/StateCheckFold.
                state = StateError;
            } else {
                nameStart = p;
                state = StateName;
            }
            break;

        case StateName:
            // printf("StateName %02x %c\n", *p, isalnum(*p) ? *p : ' ');
            if (is_token_char(*p)) {
                // accept
            } else if (*p == ':') {
                nameEnd = p;
                state = StateValueLeadingWs;
            } else if (is_header_ws(*p)) {
                nameEnd = p;
                state = StateNameTrailingWs;
            } else {
                state = StateError;
            }
            break;

        case StateNameTrailingWs:
            // printf("StateNameTrailingWs %02x %c\n", *p, isalnum(*p) ? *p : ' ');
            if (is_header_ws(*p)) {
                // accept and skip
            } else if (*p == ':') {
                state = StateValueLeadingWs;
            } else {
                state = StateError;
            }
            break;

        case StateValueLeadingWs:
            // printf("StateValueLeadingWs %02x %c\n", *p, isalnum(*p) ? *p : ' ');
            if (is_header_ws(*p)) {
                // accept and skip
            } else if (*p == '\r') {
                // empty value
                state = StateCR;
            } else if (is_vchar(*p)) {
                valueStart = p;
                last = p;
                state = StateValue;
            } else {
                state = StateError;
            }
            break;

        case StateValue:
            // printf("StateValue %02x %c\n", *p, isalnum(*p) ? *p : ' ');
            if (is_header_ws(*p)) {
                // accept but keep separate so that we can track trailing ws
            } else if (*p == '\r') {
                valueEnd = last + 1;
                state = StateCR;
            } else if (is_vchar(*p)) {
                // accept
                last = p;
            }
            break;

        case StateCR:
            // printf("StateCr %02x %c\n", *p, isalnum(*p) ? *p : ' ');
            if (*p == '\n') {
                // If there's more data, check for folding.
                if (p + 1 == headers + size) {
                    state = StateDone;
                } else {
                    state = StateCheckFold;
                }
            } else {
                state = StateError;
            }
            break;

        case StateCheckFold:
            // printf("StateCheckFold %02x %c\n", *p, isalnum(*p) ? *p : ' ');
            if (is_header_ws(*p)) {
                state = StateValue;
            } else {
                state = StateDone;
                --p;
            }
            break;
        }
    }

    *pParsed = p - headers;

    if (state != StateDone || state == StateError) {
        return SQLITE_ERROR;
    }

    if (!nameStart) {
        return SQLITE_DONE;
    }

    *ppName = nameStart;
    *pNameSize = nameEnd - nameStart;

    if (valueStart) {
        *ppValue = valueStart;
        *pValueSize = valueEnd - valueStart;
    }

    return SQLITE_ROW;
}
