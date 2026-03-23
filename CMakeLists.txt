cmake_minimum_required(VERSION 3.16)
project(tgbot C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# ── Compiler flags ────────────────────────────────────────────────────────────
add_compile_options(
    -Wall -Wextra -Wpedantic -Werror
    -O2
    -fstack-protector-strong
    # Reduce binary size on embedded targets
    -ffunction-sections -fdata-sections
)
add_link_options(-Wl,--gc-sections)

# ── Dependencies (must be before Sources so PostgreSQL_FOUND is available) ─────
find_package(CURL REQUIRED)
find_package(SQLite3 REQUIRED)
find_package(PostgreSQL)

# ── Sources ───────────────────────────────────────────────────────────────────
if(NOT DEFINED CJSON_DIR)
    set(CJSON_DIR "${CMAKE_SOURCE_DIR}/third_party/cjson")
endif()
get_filename_component(CJSON_DIR "${CJSON_DIR}" ABSOLUTE BASE_DIR "${CMAKE_SOURCE_DIR}")

set(SOURCES
    ${CMAKE_SOURCE_DIR}/src/main.c
    ${CMAKE_SOURCE_DIR}/src/config.c
    ${CMAKE_SOURCE_DIR}/src/gemini.c
    ${CMAKE_SOURCE_DIR}/src/processor.c
    ${CMAKE_SOURCE_DIR}/src/bot.c
    ${CMAKE_SOURCE_DIR}/src/db_sqlite.c
    ${CMAKE_SOURCE_DIR}/src/storage_local.c
    ${CJSON_DIR}/cJSON.c
)

# Add PostgreSQL sources if available
if(PostgreSQL_FOUND)
    list(APPEND SOURCES
        ${CMAKE_SOURCE_DIR}/src/db_postgres.c
        ${CMAKE_SOURCE_DIR}/src/storage_supabase.c
    )
endif()

add_executable(tgbot ${SOURCES})

target_include_directories(tgbot PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CJSON_DIR}
)

# ── Linker ────────────────────────────────────────────────────────────────────

target_link_libraries(tgbot PRIVATE
    CURL::libcurl
    SQLite::SQLite3
)

if(PostgreSQL_FOUND)
    target_link_libraries(tgbot PRIVATE PostgreSQL::PostgreSQL)
    target_compile_definitions(tgbot PRIVATE HAVE_POSTGRES)
    message(STATUS "PostgreSQL support enabled")
else()
    message(STATUS "PostgreSQL not found - building without PostgreSQL support")
endif()

# ── Tests ─────────────────────────────────────────────────────────────────────
option(BUILD_TESTS "Build test suite" OFF)

if(BUILD_TESTS)
    enable_testing()

    set(TEST_SOURCES
        ${CMAKE_SOURCE_DIR}/tests/main.c
        ${CMAKE_SOURCE_DIR}/tests/test_storage.c
        ${CMAKE_SOURCE_DIR}/tests/test_db.c
        ${CMAKE_SOURCE_DIR}/tests/test_json.c
        ${CMAKE_SOURCE_DIR}/tests/test_config.c
        ${CMAKE_SOURCE_DIR}/tests/test_edge_cases.c
        ${CJSON_DIR}/cJSON.c
    )

    add_executable(tgbot_tests ${TEST_SOURCES})

    target_include_directories(tgbot_tests PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/tests
        ${CJSON_DIR}
    )

    target_link_libraries(tgbot_tests PRIVATE
        SQLite::SQLite3
    )

    target_sources(tgbot_tests PRIVATE
        ${CMAKE_SOURCE_DIR}/src/storage_local.c
        ${CMAKE_SOURCE_DIR}/src/db_sqlite.c
        ${CMAKE_SOURCE_DIR}/src/config.c
        ${CMAKE_SOURCE_DIR}/src/gemini.c
    )

    target_link_libraries(tgbot_tests PRIVATE
        CURL::libcurl
    )

    add_test(NAME tgbot_tests COMMAND tgbot_tests)
endif()

# ── Install ───────────────────────────────────────────────────────────────────
install(TARGETS tgbot DESTINATION bin)
