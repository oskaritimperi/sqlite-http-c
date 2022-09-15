#ifdef HTTP_BACKEND_CURL

#include "http.h"

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
    dlclose(library);
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
