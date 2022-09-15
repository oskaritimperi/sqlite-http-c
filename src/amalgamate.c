#include <stdio.h>
#include <string.h>

int main(int argc, char const* argv[]) {
    static const char* aFilenames[] = {
        "src/http.h",
        "src/http.c",
        "src/http_backend_curl.c",
        "src/http_backend_dummy.c",
        "src/http_backend_winhttp.c",
        "src/http_next_header.c",
    };

    static const int nFilenames = sizeof(aFilenames) / sizeof(aFilenames[0]);

    FILE* fout = fopen("http.c", "wb");
    if (!fout) {
        fprintf(stderr, "error opening http.c: %s\n", strerror(errno));
        return 1;
    }

    for (int i = 0; i < nFilenames; ++i) {
        FILE* fp = fopen(aFilenames[i], "rb");
        if (!fp) {
            fprintf(stderr, "error opening %s: %s\n", aFilenames[i], strerror(errno));
            return 1;
        }
        char buffer[512];
        size_t nread =
            snprintf(buffer, sizeof(buffer), "\n/********** %s **********/\n\n", aFilenames[i]);
        if (fwrite(buffer, 1, nread, fout) != nread) {
            fprintf(stderr, "error writing http.c: %s\n", strerror(errno));
            return 1;
        }
        while (1) {
            nread = fread(buffer, 1, sizeof(buffer), fp);
            if (!nread) {
                if (ferror(fp)) {
                    fprintf(stderr, "error reading %s: %s\n", aFilenames[i], strerror(errno));
                    return 1;
                }
                break;
            }
            if (fwrite(buffer, 1, nread, fout) != nread) {
                fprintf(stderr, "error writing http.c: %s\n", strerror(errno));
                return 1;
            }
        }
        fclose(fp);
    }

    fclose(fout);

    return 0;
}