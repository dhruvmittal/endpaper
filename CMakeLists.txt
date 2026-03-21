cmake_minimum_required(VERSION 3.25)
project(mail2book VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── Qt6 (required by Akonadi + Network for RSS) ──────────────────────────────
find_package(Qt6 REQUIRED COMPONENTS Core Network)
set(CMAKE_AUTOMOC ON)

# ── Extra CMake Modules (required by KDE Frameworks) ──────────────────────
find_package(ECM REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH} ${CMAKE_MODULE_PATH})

# ── KDE Frameworks 6 ──────────────────────────────────────────────────────
find_package(KPim6Mime REQUIRED)
find_package(KPim6Akonadi REQUIRED)
find_package(KPim6AkonadiMime REQUIRED)

# ── FTXUI ──────────────────────────────────────────────────────────────────
find_package(ftxui REQUIRED)

# ── SQLite3 ────────────────────────────────────────────────────────────────
find_package(PkgConfig REQUIRED)
pkg_check_modules(SQLITE3 REQUIRED sqlite3)

# ── libzip ─────────────────────────────────────────────────────────────────
pkg_check_modules(LIBZIP REQUIRED libzip)

# ── pugixml (for RSS parsing) ──────────────────────────────────────────────
find_package(pugixml REQUIRED)

# ── Source files ───────────────────────────────────────────────────────────
set(MAIL2BOOK_SOURCES
    src/main.cpp
    src/config.cpp
    src/database.cpp
    src/html_sanitizer.cpp
    src/epub_builder.cpp
    src/akonadi_fetcher.cpp
    src/rss_fetcher.cpp
    src/miniflux_fetcher.cpp
    src/tui_app.cpp
)

add_executable(mail2book ${MAIL2BOOK_SOURCES})

target_include_directories(mail2book PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${SQLITE3_INCLUDE_DIRS}
    ${LIBZIP_INCLUDE_DIRS}
)

target_link_libraries(mail2book PRIVATE
    Qt6::Core
    Qt6::Network
    KPim6::Mime
    KPim6::AkonadiCore
    KPim6::AkonadiMime
    ftxui::screen
    ftxui::dom
    ftxui::component
    pugixml::pugixml
    ${SQLITE3_LIBRARIES}
    ${LIBZIP_LIBRARIES}
)

target_link_directories(mail2book PRIVATE
    ${SQLITE3_LIBRARY_DIRS}
    ${LIBZIP_LIBRARY_DIRS}
)

target_compile_options(mail2book PRIVATE -Wall -Wextra -Wpedantic)

# ── Tests ──────────────────────────────────────────────────────────────────
option(BUILD_TESTS "Build tests" ON)
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ── Install ────────────────────────────────────────────────────────────────
install(TARGETS mail2book DESTINATION bin)
