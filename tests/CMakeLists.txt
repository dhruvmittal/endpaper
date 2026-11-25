add_executable(test_sanitizer test_sanitizer.cpp)
target_link_libraries(test_sanitizer PRIVATE endpaper_core)
target_compile_options(test_sanitizer PRIVATE -Wall -Wextra)
add_test(NAME test_sanitizer COMMAND test_sanitizer)

add_executable(test_epub test_epub.cpp)
target_link_libraries(test_epub PRIVATE endpaper_core)
target_compile_options(test_epub PRIVATE -Wall -Wextra)
add_test(NAME test_epub COMMAND test_epub)

add_executable(test_rss test_rss.cpp)
target_link_libraries(test_rss PRIVATE endpaper_core)
target_compile_options(test_rss PRIVATE -Wall -Wextra)
add_test(NAME test_rss COMMAND test_rss)

add_executable(test_append_comprehensive test_append_comprehensive.cpp)
target_link_libraries(test_append_comprehensive PRIVATE endpaper_core)
target_compile_options(test_append_comprehensive PRIVATE -Wall -Wextra)
add_test(NAME test_append_comprehensive COMMAND test_append_comprehensive)

add_executable(test_database_books test_database_books.cpp)
target_link_libraries(test_database_books PRIVATE endpaper_core)
target_compile_options(test_database_books PRIVATE -Wall -Wextra)
add_test(NAME test_database_books COMMAND test_database_books)

add_executable(test_html_extractor test_html_extractor.cpp)
target_link_libraries(test_html_extractor PRIVATE endpaper_core)
target_compile_options(test_html_extractor PRIVATE -Wall -Wextra)
add_test(NAME test_html_extractor COMMAND test_html_extractor)

add_executable(test_build_pipeline test_build_pipeline.cpp)
target_link_libraries(test_build_pipeline PRIVATE endpaper_core)
target_compile_options(test_build_pipeline PRIVATE -Wall -Wextra)
add_test(NAME test_build_pipeline COMMAND test_build_pipeline)
