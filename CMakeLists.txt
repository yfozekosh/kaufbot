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

    set(CJSON_SRC ${CJSON_DIR}/cJSON.c)

    # Common test include paths
    set(TEST_INCLUDES
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/tests
        ${CJSON_DIR}
    )

    # Per-module test executables — each registered separately with CTest
    macro(add_test_module NAME TEST_SRC)
        add_executable(${NAME}
            ${CMAKE_SOURCE_DIR}/tests/main.c
            ${TEST_SRC}
            ${CJSON_SRC}
        )
        target_include_directories(${NAME} PRIVATE ${TEST_INCLUDES})
        target_link_libraries(${NAME} PRIVATE SQLite::SQLite3 CURL::libcurl)
        target_sources(${NAME} PRIVATE ${ARGN})
        add_test(NAME ${NAME} COMMAND ${NAME})
    endmacro()

    add_test_module(test_storage
        ${CMAKE_SOURCE_DIR}/tests/test_storage.c
        ${CMAKE_SOURCE_DIR}/src/storage_local.c
        ${CMAKE_SOURCE_DIR}/src/utils.c
    )

    add_test_module(test_db
        ${CMAKE_SOURCE_DIR}/tests/test_db.c
        ${CMAKE_SOURCE_DIR}/src/db_sqlite.c
        ${CMAKE_SOURCE_DIR}/src/utils.c
    )

    add_test_module(test_json
        ${CMAKE_SOURCE_DIR}/tests/test_json.c
        ${CMAKE_SOURCE_DIR}/src/gemini.c
        ${CMAKE_SOURCE_DIR}/src/utils.c
    )

    add_test_module(test_config
        ${CMAKE_SOURCE_DIR}/tests/test_config.c
        ${CMAKE_SOURCE_DIR}/src/config.c
    )

    add_test_module(test_edge_cases
        ${CMAKE_SOURCE_DIR}/tests/test_edge_cases.c
        ${CMAKE_SOURCE_DIR}/src/storage_local.c
        ${CMAKE_SOURCE_DIR}/src/db_sqlite.c
        ${CMAKE_SOURCE_DIR}/src/utils.c
    )
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
        ${CMAKE_SOURCE_DIR}/tests/main.c
        ${CMAKE_SOURCE_DIR}/tests/test_storage.c
        ${CMAKE_SOURCE_DIR}/tests/test_db.c
        ${CMAKE_SOURCE_DIR}/tests/test_json.c
        ${CMAKE_SOURCE_DIR}/tests/test_config.c
        ${CMAKE_SOURCE_DIR}/tests/test_edge_cases.c
        ${CMAKE_SOURCE_DIR}/tests/test_helpers.h
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
    target_compile_options(test_storage PRIVATE --coverage -O0 -g)
    target_link_options(test_storage PRIVATE --coverage)
    target_compile_options(test_db PRIVATE --coverage -O0 -g)
    target_link_options(test_db PRIVATE --coverage)
    target_compile_options(test_json PRIVATE --coverage -O0 -g)
    target_link_options(test_json PRIVATE --coverage)
    target_compile_options(test_config PRIVATE --coverage -O0 -g)
    target_link_options(test_config PRIVATE --coverage)
    target_compile_options(test_edge_cases PRIVATE --coverage -O0 -g)
    target_link_options(test_edge_cases PRIVATE --coverage)

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

        add_custom_target(test-coverage
            COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
            COMMAND ${LCOV_EXE} --capture --directory ${CMAKE_BINARY_DIR} --output-file coverage.info --no-external
            COMMAND ${LCOV_EXE} --remove coverage.info '*/tests/*' '*/third_party/*' --output-file coverage_filtered.info
            COMMAND ${GENHTML_EXE} coverage_filtered.info --output-directory coverage_report
            COMMENT "Running tests and generating coverage report"
            VERBATIM
        )

        add_custom_target(coverage-check
            COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
            COMMAND ${LCOV_EXE} --capture --directory ${CMAKE_BINARY_DIR} --output-file coverage.info --no-external
            COMMAND ${LCOV_EXE} --remove coverage.info '*/tests/*' '*/third_party/*' --output-file coverage_filtered.info
            COMMAND ${GENHTML_EXE} coverage_filtered.info --output-directory coverage_report
            COMMAND ${CMAKE_COMMAND} -E env bash -c
                "PERCENT=$$(${LCOV_EXE} --summary coverage_filtered.info 2>&1 | grep lines | sed 's/.*: \\([0-9.]*\\)%.*/\\1/'); \
                 echo \"Line coverage: $${PERCENT}%\"; \
                 if [ \"$$(echo \"$${PERCENT} < ${COVERAGE_MIN_PERCENT}\" | bc -l)\" -eq 1 ]; then \
                   echo \"ERROR: Coverage $${PERCENT}% is below ${COVERAGE_MIN_PERCENT}% minimum\"; \
                   exit 1; \
                 fi"
            COMMENT "Checking minimum coverage threshold (${COVERAGE_MIN_PERCENT}%)"
            VERBATIM
        )

        message(STATUS "lcov/genhtml found - 'make coverage', 'make test-coverage', 'make coverage-check' targets available")
    endif()
endif()

set(COVERAGE_MIN_PERCENT 70 CACHE STRING "Minimum line coverage percentage threshold")

# ── Install ───────────────────────────────────────────────────────────────────
install(TARGETS tgbot DESTINATION bin)
