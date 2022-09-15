#include <stdio.h>
#include <stdlib.h>
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

    static const int bufsize = 16*1024;
    char* buffer = malloc(bufsize);

    for (int i = 0; i < nFilenames; ++i) {
        FILE* fp = fopen(aFilenames[i], "rb");
        if (!fp) {
            fprintf(stderr, "error opening %s: %s\n", aFilenames[i], strerror(errno));
            return 1;
        }
        snprintf(buffer, bufsize, "\n/********** %s **********/\n\n", aFilenames[i]);
        if (fwrite(buffer, 1, strlen(buffer), fout) != strlen(buffer)) {
            fprintf(stderr, "error writing http.c: %s\n", strerror(errno));
            return 1;
        }
        while (fgets(buffer, bufsize, fp)) {
            size_t l = strlen(buffer);
            if (strncmp(buffer, "#include \"http.h\"", 17) != 0) {
                if (fwrite(buffer, 1, l, fout) != l) {
                    fprintf(stderr, "error writing http.c: %s\n", strerror(errno));
                    return 1;
                }
            }
        }
        fclose(fp);
    }

    fclose(fout);

    return 0;
}