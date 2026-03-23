cmake_minimum_required(VERSION 3.16)
project(tgbot C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── Compiler flags ────────────────────────────────────────────────────────────
add_compile_options(
    -Wall -Wextra -Wpedantic -Werror
    -O2
    -fstack-protector-strong
    -ffunction-sections -fdata-sections
    -D_GNU_SOURCE
)
add_link_options(-Wl,--gc-sections)

# ── Dependencies ─────────────────────────────────────────────────────────────
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
    ${CMAKE_SOURCE_DIR}/src/utils.c
    ${CMAKE_SOURCE_DIR}/src/db_sqlite.c
    ${CMAKE_SOURCE_DIR}/src/storage_local.c
    ${CJSON_DIR}/cJSON.c
)

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
        CURL::libcurl
    )

    target_sources(tgbot_tests PRIVATE
        ${CMAKE_SOURCE_DIR}/src/storage_local.c
        ${CMAKE_SOURCE_DIR}/src/db_sqlite.c
        ${CMAKE_SOURCE_DIR}/src/config.c
        ${CMAKE_SOURCE_DIR}/src/gemini.c
        ${CMAKE_SOURCE_DIR}/src/utils.c
    )

    add_test(NAME tgbot_tests COMMAND tgbot_tests)
endif()

# ── Lint ──────────────────────────────────────────────────────────────────────
find_program(CLANG_TIDY_EXE clang-tidy)
if(CLANG_TIDY_EXE)
    add_custom_target(lint
        COMMAND ${CLANG_TIDY_EXE}
            -p ${CMAKE_BINARY_DIR}
            ${SOURCES}
        COMMENT "Running clang-tidy on source files"
        VERBATIM
    )
    message(STATUS "clang-tidy found - 'make lint' target available")
else()
    message(STATUS "clang-tidy not found - 'make lint' target not available")
endif()

find_program(CPPCHECK_EXE cppcheck)
if(CPPCHECK_EXE)
    add_custom_target(cppcheck
        COMMAND ${CPPCHECK_EXE}
            --enable=warning,performance,portability
            --error-exitcode=1
            --suppress=missingIncludeSystem
            -I ${CMAKE_SOURCE_DIR}/src
            ${CMAKE_SOURCE_DIR}/src/
        COMMENT "Running cppcheck on source files"
        VERBATIM
    )
    message(STATUS "cppcheck found - 'make cppcheck' target available")
endif()

# ── Format ────────────────────────────────────────────────────────────────────
find_program(CLANG_FORMAT_EXE clang-format)
if(CLANG_FORMAT_EXE)
    set(ALL_C_FILES
        ${SOURCES}
        ${TEST_SOURCES}
        ${CMAKE_SOURCE_DIR}/src/utils.h
        ${CMAKE_SOURCE_DIR}/src/utils.c
        ${CMAKE_SOURCE_DIR}/tests/test_helpers.h
        ${CMAKE_SOURCE_DIR}/src/processor.h
        ${CMAKE_SOURCE_DIR}/src/bot.h
        ${CMAKE_SOURCE_DIR}/src/gemini.h
        ${CMAKE_SOURCE_DIR}/src/config.h
        ${CMAKE_SOURCE_DIR}/src/db.h
        ${CMAKE_SOURCE_DIR}/src/db_backend.h
        ${CMAKE_SOURCE_DIR}/src/storage.h
        ${CMAKE_SOURCE_DIR}/src/storage_backend.h
    )

    add_custom_target(format
        COMMAND ${CLANG_FORMAT_EXE} -i ${ALL_C_FILES}
        COMMENT "Formatting all source files with clang-format"
        VERBATIM
    )

    add_custom_target(format-check
        COMMAND ${CLANG_FORMAT_EXE} --dry-run --Werror ${ALL_C_FILES}
        COMMENT "Checking formatting with clang-format"
        VERBATIM
    )

    message(STATUS "clang-format found - 'make format' and 'make format-check' targets available")
endif()

# ── Test Coverage ─────────────────────────────────────────────────────────────
option(ENABLE_COVERAGE "Enable code coverage instrumentation" OFF)

if(ENABLE_COVERAGE AND BUILD_TESTS)
    target_compile_options(tgbot_tests PRIVATE --coverage -O0 -g)
    target_link_options(tgbot_tests PRIVATE --coverage)

    find_program(LCOV_EXE lcov)
    find_program(GENHTML_EXE genhtml)

    if(LCOV_EXE AND GENHTML_EXE)
        add_custom_target(coverage
            COMMAND ${LCOV_EXE} --capture --directory ${CMAKE_BINARY_DIR} --output-file coverage.info --no-external
            COMMAND ${LCOV_EXE} --remove coverage.info '*/tests/*' '*/third_party/*' --output-file coverage_filtered.info
            COMMAND ${GENHTML_EXE} coverage_filtered.info --output-directory coverage_report
            COMMENT "Generating code coverage report"
            VERBATIM
        )
        message(STATUS "lcov/genhtml found - 'make coverage' target available")
    endif()
endif()

# ── Install ───────────────────────────────────────────────────────────────────
install(TARGETS tgbot DESTINATION bin)
