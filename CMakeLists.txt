cmake_minimum_required(VERSION 3.21 FATAL_ERROR)

project(sqlite-http-c VERSION 0.1.0 LANGUAGES C)

if(UNIX)
    set(HTTP_BACKEND_CURL_DEFAULT TRUE)
endif()

if(WIN32)
    set(HTTP_BACKEND_WINHTTP_DEFAULT TRUE)
endif()

option(HTTP_BACKEND_CURL "Enable curl backend" ${HTTP_BACKEND_CURL_DEFAULT})
option(HTTP_BACKEND_WINHTTP "Enable WinHTTP backend" ${HTTP_BACKEND_WINHTTP_DEFAULT})
option(HTTP_BUILD_STATIC "Build a static library" OFF)
option(HTTP_BUILD_SHARED "Build a shared library" ON)

if(PROJECT_IS_TOP_LEVEL)
    add_executable(amalgamate src/amalgamate.c)

    add_custom_command(
        OUTPUT "${CMAKE_CURRENT_SOURCE_DIR}/http.c"
        COMMAND amalgamate
        DEPENDS
            src/amalgamate.c
            src/http.c
            src/http.h
            src/http_backend_curl.c
            src/http_backend_dummy.c
            src/http_backend_winhttp.c
            src/http_next_header.c
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    )
endif()

include(FetchContent)

FetchContent_Declare(
    sqlite
    URL https://sqlite.org/2022/sqlite-amalgamation-3390200.zip
    URL_HASH SHA3_256=deb2abef617b6305525e3b1a2b39a5dc095ffb62f243b4d1b468ba5f41900ce7
)

FetchContent_MakeAvailable(sqlite)

if(HTTP_BUILD_STATIC)
    add_library(sqlite-http-c-static STATIC "http.c")
    target_include_directories(sqlite-http-c-static PRIVATE ${sqlite_SOURCE_DIR})
    if(HTTP_BACKEND_CURL)
        target_compile_definitions(sqlite-http-c-static PRIVATE HTTP_BACKEND_CURL)
    endif()
    if(HTTP_BACKEND_WINHTTP)
        target_compile_definitions(sqlite-http-c-static PRIVATE HTTP_BACKEND_WINHTTP)
        target_link_libraries(sqlite-http-c-static PUBLIC winhttp)
    endif()
    target_compile_definitions(sqlite-http-c-static PRIVATE SQLITE_CORE)
endif()

if(HTTP_BUILD_SHARED)
    add_library(http SHARED "http.c")
    target_include_directories(http PRIVATE ${sqlite_SOURCE_DIR})
    if(HTTP_BACKEND_CURL)
        target_compile_definitions(http PRIVATE HTTP_BACKEND_CURL)
    endif()
    if(HTTP_BACKEND_WINHTTP)
        target_compile_definitions(http PRIVATE HTTP_BACKEND_WINHTTP)
        target_link_libraries(http PRIVATE winhttp)
    endif()
endif()

if(PROJECT_IS_TOP_LEVEL)
    include(CTest)
    if(BUILD_TESTING)
        add_subdirectory(tests)
    endif()
endif()
