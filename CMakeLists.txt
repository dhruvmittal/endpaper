cmake_minimum_required(VERSION 3.25)
project(endpaper VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── Build Options ──────────────────────────────────────────────────────────
option(BUILD_TUI "Build TUI application" ON)
option(BUILD_GUI "Build GUI application (Qt Widgets)" ON)

# ── Qt6 ────────────────────────────────────────────────────────────────────
if (BUILD_GUI)
    find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)
else()
    find_package(Qt6 REQUIRED COMPONENTS Core Gui)
endif()
set(CMAKE_AUTOMOC ON)

# ── Extra CMake Modules (required by KDE Frameworks) ──────────────────────
find_package(ECM REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH} ${CMAKE_MODULE_PATH})

# ── KDE Frameworks 6 ──────────────────────────────────────────────────────
find_package(KF6Mime QUIET)
if(KF6Mime_FOUND)
    set(KMIME_TARGET KF6::Mime)
else()
    find_package(KPim6Mime REQUIRED)
    set(KMIME_TARGET KPim6::Mime)
endif()
find_package(KPim6Akonadi REQUIRED)
find_package(KPim6AkonadiMime REQUIRED)

find_package(Threads REQUIRED)

# ── FTXUI ──────────────────────────────────────────────────────────────────
if (BUILD_TUI)
    find_package(ftxui QUIET)
    if(NOT ftxui_FOUND)
        include(FetchContent)
        FetchContent_Declare(
          ftxui
          GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI
          GIT_TAG main
        )
        FetchContent_MakeAvailable(ftxui)

        if(TARGET screen AND NOT TARGET ftxui::screen)
            add_library(ftxui::screen ALIAS screen)
        endif()
        if(TARGET dom AND NOT TARGET ftxui::dom)
            add_library(ftxui::dom ALIAS dom)
        endif()
        if(TARGET component AND NOT TARGET ftxui::component)
            add_library(ftxui::component ALIAS component)
        endif()
    endif()
endif()

# ── SQLite3 ────────────────────────────────────────────────────────────────
find_package(PkgConfig REQUIRED)
pkg_check_modules(SQLITE3 REQUIRED sqlite3)

# ── libzip ─────────────────────────────────────────────────────────────────
pkg_check_modules(LIBZIP REQUIRED libzip)

# ── pugixml (for RSS parsing) ──────────────────────────────────────────────
find_package(pugixml REQUIRED)

# ── Lexbor (for HTML sanitation & extraction) ──────────────────────────────
find_path(LEXBOR_INCLUDE_DIR NAMES lexbor/html/parser.h)
find_library(LEXBOR_LIBRARY NAMES lexbor)
if (LEXBOR_INCLUDE_DIR AND LEXBOR_LIBRARY)
    set(LEXBOR_FOUND TRUE)
    set(LEXBOR_INCLUDE_DIRS ${LEXBOR_INCLUDE_DIR})
    set(LEXBOR_LIBRARIES ${LEXBOR_LIBRARY})
else()
    message(FATAL_ERROR "Lexbor library or headers not found")
endif()

# ── CURL (for networking) ──────────────────────────────────────────────────
find_package(CURL REQUIRED)

# ── nlohmann_json (for JSON parsing) ───────────────────────────────────────
find_package(nlohmann_json REQUIRED)

# ── Shared Core Library ────────────────────────────────────────────────────
add_library(endpaper_core STATIC
    src/config.cpp
    src/database.cpp
    src/html_sanitizer.cpp
    src/epub_builder.cpp
    src/http_client.cpp
    src/html_extractor.cpp
    src/build_pipeline.cpp
    src/rss_fetcher.cpp
    src/miniflux_fetcher.cpp
    src/akonadi_fetcher.cpp
)

target_include_directories(endpaper_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${LEXBOR_INCLUDE_DIRS}
    ${SQLITE3_INCLUDE_DIRS}
    ${LIBZIP_INCLUDE_DIRS}
)

target_link_libraries(endpaper_core PUBLIC
    Qt6::Core
    Qt6::Gui
    CURL::libcurl
    nlohmann_json::nlohmann_json
    pugixml::pugixml
    ${KMIME_TARGET}
    KPim6::AkonadiCore
    KPim6::AkonadiMime
    ${SQLITE3_LIBRARIES}
    ${LIBZIP_LIBRARIES}
    ${LEXBOR_LIBRARIES}
    Threads::Threads
)

target_link_directories(endpaper_core PUBLIC
    ${SQLITE3_LIBRARY_DIRS}
    ${LIBZIP_LIBRARY_DIRS}
)

target_compile_options(endpaper_core PRIVATE -Wall -Wextra -Wpedantic)

# ── TUI executable ─────────────────────────────────────────────────────────
if (BUILD_TUI)
    add_executable(endpaper
        src/main.cpp
        src/tui_app.cpp
    )

    target_link_libraries(endpaper PRIVATE
        endpaper_core
        ftxui::component
        ftxui::dom
        ftxui::screen
    )

    target_compile_options(endpaper PRIVATE -Wall -Wextra -Wpedantic)
    install(TARGETS endpaper DESTINATION bin)
endif()

# ── GUI executable ─────────────────────────────────────────────────────────
if (BUILD_GUI)
    add_executable(endpaper-qt
        src/qt_main.cpp
        src/main_window.cpp
        src/main_window.h
        src/settings_dialog.cpp
        src/settings_dialog.h
    )

    target_link_libraries(endpaper-qt PRIVATE
        endpaper_core
        Qt6::Widgets
    )

    target_compile_options(endpaper-qt PRIVATE -Wall -Wextra -Wpedantic)
    install(TARGETS endpaper-qt DESTINATION bin)
endif()

# ── Tests ──────────────────────────────────────────────────────────────────
option(BUILD_TESTS "Build tests" ON)
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
