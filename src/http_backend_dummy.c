#ifdef HTTP_BACKEND_DUMMY

#include "http.h"

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
