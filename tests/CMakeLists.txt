# ── Test: HTML Sanitizer ───────────────────────────────────────────────────
add_executable(test_sanitizer
    test_sanitizer.cpp
    ${CMAKE_SOURCE_DIR}/src/html_sanitizer.cpp
)
target_include_directories(test_sanitizer PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_options(test_sanitizer PRIVATE -Wall -Wextra)
add_test(NAME test_sanitizer COMMAND test_sanitizer)

# ── Test: EPUB Builder ─────────────────────────────────────────────────────
add_executable(test_epub
    test_epub.cpp
    ${CMAKE_SOURCE_DIR}/src/epub_builder.cpp
    ${CMAKE_SOURCE_DIR}/src/html_sanitizer.cpp
)
target_include_directories(test_epub PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${LIBZIP_INCLUDE_DIRS}
    ${SQLITE3_INCLUDE_DIRS}
)
target_link_libraries(test_epub PRIVATE
    ${LIBZIP_LIBRARIES}
    ${SQLITE3_LIBRARIES}
)
target_link_directories(test_epub PRIVATE
    ${LIBZIP_LIBRARY_DIRS}
    ${SQLITE3_LIBRARY_DIRS}
)
target_compile_options(test_epub PRIVATE -Wall -Wextra)
add_test(NAME test_epub COMMAND test_epub)
# ── Test: RSS Fetcher ──────────────────────────────────────────────────────
find_package(pugixml REQUIRED)
add_executable(test_rss
    test_rss.cpp
    ${CMAKE_SOURCE_DIR}/src/rss_fetcher.cpp
    ${CMAKE_SOURCE_DIR}/src/database.cpp
    ${CMAKE_SOURCE_DIR}/src/config.cpp
)
target_include_directories(test_rss PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${SQLITE3_INCLUDE_DIRS}
)
target_link_libraries(test_rss PRIVATE
    ${SQLITE3_LIBRARIES}
    pugixml::pugixml
    Qt6::Network
)
target_link_directories(test_rss PRIVATE
    ${SQLITE3_LIBRARY_DIRS}
)
target_compile_options(test_rss PRIVATE -Wall -Wextra)
add_test(NAME test_rss COMMAND test_rss)
