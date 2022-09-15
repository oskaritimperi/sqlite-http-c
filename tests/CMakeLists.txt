add_library(sqlite3 STATIC "${sqlite_SOURCE_DIR}/sqlite3.c")
target_include_directories(sqlite3 PUBLIC "${sqlite_SOURCE_DIR}")

add_executable(t_http_next_header t_http_next_header.c ../http.c)
target_link_libraries(t_http_next_header PRIVATE sqlite3)
target_compile_definitions(t_http_next_header PRIVATE HTTP_BACKEND_DUMMY)
add_test(NAME http_next_header COMMAND t_http_next_header)

add_executable(t_http t_http.c ../http.c)
target_link_libraries(t_http PRIVATE sqlite3)
target_compile_definitions(t_http PRIVATE HTTP_BACKEND_DUMMY SQLITE_CORE)
target_include_directories(t_http PRIVATE ../src)
add_test(NAME http COMMAND t_http)
