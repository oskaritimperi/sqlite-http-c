#include "http.h"

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
