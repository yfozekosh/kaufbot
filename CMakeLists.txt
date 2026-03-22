cmake_minimum_required(VERSION 3.16)
project(tgbot C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# ── Compiler flags ────────────────────────────────────────────────────────────
add_compile_options(
    -Wall -Wextra -Wpedantic
    -O2
    -fstack-protector-strong
    # Reduce binary size on embedded targets
    -ffunction-sections -fdata-sections
)
add_link_options(-Wl,--gc-sections)

# ── Sources ───────────────────────────────────────────────────────────────────
# cJSON location — injected via -DCJSON_DIR=... (relative or absolute)
if(NOT DEFINED CJSON_DIR)
    set(CJSON_DIR "${CMAKE_SOURCE_DIR}/third_party/cjson")
endif()
# Resolve to absolute path so it works regardless of where cmake -B build is run
get_filename_component(CJSON_DIR "${CJSON_DIR}" ABSOLUTE BASE_DIR "${CMAKE_SOURCE_DIR}")

set(SOURCES
    ${CMAKE_SOURCE_DIR}/src/main.c
    ${CMAKE_SOURCE_DIR}/src/config.c
    ${CMAKE_SOURCE_DIR}/src/storage.c
    ${CMAKE_SOURCE_DIR}/src/db.c
    ${CMAKE_SOURCE_DIR}/src/gemini.c
    ${CMAKE_SOURCE_DIR}/src/processor.c
    ${CMAKE_SOURCE_DIR}/src/bot.c
    ${CJSON_DIR}/cJSON.c
)

add_executable(tgbot ${SOURCES})

target_include_directories(tgbot PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CJSON_DIR}
)

# ── Dependencies ──────────────────────────────────────────────────────────────
find_package(CURL REQUIRED)
find_package(SQLite3 REQUIRED)

target_link_libraries(tgbot PRIVATE
    CURL::libcurl
    SQLite::SQLite3
)

# ── Install ───────────────────────────────────────────────────────────────────
install(TARGETS tgbot DESTINATION bin)
