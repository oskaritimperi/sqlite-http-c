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
