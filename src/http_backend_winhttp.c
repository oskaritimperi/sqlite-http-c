#ifdef HTTP_BACKEND_WINHTTP

#include "http.h"

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
