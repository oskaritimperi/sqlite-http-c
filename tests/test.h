#ifndef TEST_H
#define TEST_H

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ASSERT_INT_EQ(X, Y)                                                                        \
    do {                                                                                           \
        int xxx = (X);                                                                             \
        int yyy = (Y);                                                                             \
        if (xxx != yyy) {                                                                          \
            fprintf(stderr,                                                                        \
                    "*** %s ***\n%s is not equal to %s\nwhere\n%s is %d\nand\n%s "                 \
                    "is %d\n",                                                                     \
                    __FUNCTION__,                                                                  \
                    #X,                                                                            \
                    #Y,                                                                            \
                    #X,                                                                            \
                    xxx,                                                                           \
                    #Y,                                                                            \
                    yyy);                                                                          \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

#define ASSERT_STR_EQ(X, Y)                                                                        \
    do {                                                                                           \
        const char* xxx = (X);                                                                     \
        const char* yyy = (Y);                                                                     \
        if (strcmp(xxx, yyy) != 0) {                                                               \
            fprintf(stderr,                                                                        \
                    "*** %s ***\n%s is not equal to %s\nwhere\n%s is %s\nand\n%s "                 \
                    "is %s\n",                                                                     \
                    __FUNCTION__,                                                                  \
                    #X,                                                                            \
                    #Y,                                                                            \
                    #X,                                                                            \
                    xxx,                                                                           \
                    #Y,                                                                            \
                    yyy);                                                                          \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

#define ASSERT_MEM_EQ(X, Y, SIZE)                                                                  \
    do {                                                                                           \
        const char* xxx = (X);                                                                     \
        const char* yyy = (Y);                                                                     \
        if (memcmp(xxx, yyy, SIZE) != 0) {                                                         \
            fprintf(stderr,                                                                        \
                    "*** %s ***\n%s is not equal to %s\nwhere\n%s is %.*s\nand\n%s "               \
                    "is %.*s\n",                                                                   \
                    __FUNCTION__,                                                                  \
                    #X,                                                                            \
                    #Y,                                                                            \
                    #X,                                                                            \
                    SIZE,                                                                          \
                    xxx,                                                                           \
                    #Y,                                                                            \
                    SIZE,                                                                          \
                    yyy);                                                                          \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

#define ASSERT_NULL(X)                                                                             \
    do {                                                                                           \
        const void* xxx = (X);                                                                     \
        if (xxx != NULL) {                                                                         \
            fprintf(stderr, "*** %s ***\n%s is not NULL (was %p)\n", __FUNCTION__, #X, xxx);       \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

#endif
