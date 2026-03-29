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

# Helper: all include dirs for the project
set(PROJECT_INCLUDE_DIRS
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/db
    ${CMAKE_SOURCE_DIR}/src/storage
    ${CMAKE_SOURCE_DIR}/src/ocr
    ${CMAKE_SOURCE_DIR}/src/infra
    ${CMAKE_SOURCE_DIR}/src/telegram
    ${CJSON_DIR}
)

set(SOURCES
    ${CMAKE_SOURCE_DIR}/src/main.c
    ${CMAKE_SOURCE_DIR}/src/config.c
    ${CMAKE_SOURCE_DIR}/src/processor.c
    ${CMAKE_SOURCE_DIR}/src/utils.c
        ${CMAKE_SOURCE_DIR}/src/telegram/bot.c
        ${CMAKE_SOURCE_DIR}/src/telegram/bot_commands.c
        ${CMAKE_SOURCE_DIR}/src/telegram/bot_telegram.c
        ${CMAKE_SOURCE_DIR}/src/telegram/reply_builder.c
    ${CMAKE_SOURCE_DIR}/src/db/db_sqlite.c
    ${CMAKE_SOURCE_DIR}/src/storage/storage_local.c
    ${CMAKE_SOURCE_DIR}/src/ocr/gemini.c
    ${CMAKE_SOURCE_DIR}/src/ocr/ocr_service.c
    ${CMAKE_SOURCE_DIR}/src/ocr/prompt_fetcher.c
    ${CMAKE_SOURCE_DIR}/src/infra/http_client.c
    ${CMAKE_SOURCE_DIR}/src/infra/file_repository.c
    ${CMAKE_SOURCE_DIR}/src/infra/file_repository_memory.c
    ${CMAKE_SOURCE_DIR}/src/infra/document_store.c
    ${CMAKE_SOURCE_DIR}/src/infra/event_bus.c
    ${CJSON_DIR}/cJSON.c
)

if(PostgreSQL_FOUND)
    list(APPEND SOURCES
        ${CMAKE_SOURCE_DIR}/src/db/db_postgres.c
        ${CMAKE_SOURCE_DIR}/src/storage/storage_supabase.c
    )
endif()

add_executable(tgbot ${SOURCES})

target_include_directories(tgbot PRIVATE ${PROJECT_INCLUDE_DIRS})

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
        ${PROJECT_INCLUDE_DIRS}
        ${CMAKE_SOURCE_DIR}/tests
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
        ${CMAKE_SOURCE_DIR}/src/storage/storage_local.c
        ${CMAKE_SOURCE_DIR}/src/utils.c
    )

    add_test_module(test_db
        ${CMAKE_SOURCE_DIR}/tests/test_db.c
        ${CMAKE_SOURCE_DIR}/src/db/db_sqlite.c
        ${CMAKE_SOURCE_DIR}/src/utils.c
    )

    add_test_module(test_json
        ${CMAKE_SOURCE_DIR}/tests/test_json.c
        ${CMAKE_SOURCE_DIR}/src/ocr/gemini.c
        ${CMAKE_SOURCE_DIR}/src/utils.c
    )

    add_test_module(test_processor
        ${CMAKE_SOURCE_DIR}/tests/test_processor.c
        ${CMAKE_SOURCE_DIR}/src/processor.c
        ${CMAKE_SOURCE_DIR}/src/ocr/gemini.c
        ${CMAKE_SOURCE_DIR}/src/storage/storage_local.c
        ${CMAKE_SOURCE_DIR}/src/db/db_sqlite.c
        ${CMAKE_SOURCE_DIR}/src/utils.c
        ${CMAKE_SOURCE_DIR}/src/infra/file_repository.c
        ${CMAKE_SOURCE_DIR}/src/infra/file_repository_memory.c
        ${CMAKE_SOURCE_DIR}/src/ocr/ocr_service.c
        ${CMAKE_SOURCE_DIR}/src/telegram/reply_builder.c
    )
    target_compile_definitions(test_processor PRIVATE TEST_BUILD)

    add_test_module(test_config
        ${CMAKE_SOURCE_DIR}/tests/test_config.c
        ${CMAKE_SOURCE_DIR}/src/config.c
    )

    add_test_module(test_edge_cases
        ${CMAKE_SOURCE_DIR}/tests/test_edge_cases.c
        ${CMAKE_SOURCE_DIR}/src/storage/storage_local.c
        ${CMAKE_SOURCE_DIR}/src/db/db_sqlite.c
        ${CMAKE_SOURCE_DIR}/src/utils.c
    )

    add_test_module(test_utils
        ${CMAKE_SOURCE_DIR}/tests/test_utils.c
        ${CMAKE_SOURCE_DIR}/src/utils.c
        ${CMAKE_SOURCE_DIR}/src/storage/storage_local.c
    )

    add_test_module(test_http_client
        ${CMAKE_SOURCE_DIR}/tests/test_http_client.c
        ${CMAKE_SOURCE_DIR}/src/infra/http_client.c
        ${CMAKE_SOURCE_DIR}/src/utils.c
    )

    add_test_module(test_bot
        ${CMAKE_SOURCE_DIR}/tests/test_bot.c
        ${CMAKE_SOURCE_DIR}/src/telegram/bot.c
        ${CMAKE_SOURCE_DIR}/src/telegram/bot_commands.c
        ${CMAKE_SOURCE_DIR}/src/telegram/bot_telegram.c
        ${CMAKE_SOURCE_DIR}/src/telegram/reply_builder.c
        ${CMAKE_SOURCE_DIR}/src/processor.c
        ${CMAKE_SOURCE_DIR}/src/ocr/gemini.c
        ${CMAKE_SOURCE_DIR}/src/config.c
        ${CMAKE_SOURCE_DIR}/src/db/db_sqlite.c
        ${CMAKE_SOURCE_DIR}/src/storage/storage_local.c
        ${CMAKE_SOURCE_DIR}/src/utils.c
        ${CMAKE_SOURCE_DIR}/src/ocr/prompt_fetcher.c
        ${CMAKE_SOURCE_DIR}/src/infra/http_client.c
        ${CMAKE_SOURCE_DIR}/src/infra/file_repository.c
        ${CMAKE_SOURCE_DIR}/src/infra/file_repository_memory.c
        ${CMAKE_SOURCE_DIR}/src/ocr/ocr_service.c
    )

    add_test_module(test_prompts
        ${CMAKE_SOURCE_DIR}/tests/test_prompts.c
        ${CMAKE_SOURCE_DIR}/src/db/db_sqlite.c
        ${CMAKE_SOURCE_DIR}/src/ocr/prompt_fetcher.c
        ${CMAKE_SOURCE_DIR}/src/utils.c
    )

    add_test_module(test_file_repository
        ${CMAKE_SOURCE_DIR}/tests/test_file_repository.c
        ${CMAKE_SOURCE_DIR}/src/infra/file_repository.c
        ${CMAKE_SOURCE_DIR}/src/infra/file_repository_memory.c
        ${CMAKE_SOURCE_DIR}/src/db/db_sqlite.c
        ${CMAKE_SOURCE_DIR}/src/storage/storage_local.c
        ${CMAKE_SOURCE_DIR}/src/utils.c
    )

    add_test_module(test_document_store
        ${CMAKE_SOURCE_DIR}/tests/test_document_store.c
        ${CMAKE_SOURCE_DIR}/src/infra/document_store.c
        ${CMAKE_SOURCE_DIR}/src/storage/storage_local.c
        ${CMAKE_SOURCE_DIR}/src/utils.c
    )

    add_test_module(test_event_bus
        ${CMAKE_SOURCE_DIR}/tests/test_event_bus.c
        ${CMAKE_SOURCE_DIR}/src/infra/event_bus.c
    )
endif()

# ── Lint ──────────────────────────────────────────────────────────────────────
find_program(CLANG_TIDY_EXE clang-tidy)
if(CLANG_TIDY_EXE)
    set(LINT_SOURCES
        ${CMAKE_SOURCE_DIR}/src/main.c
        ${CMAKE_SOURCE_DIR}/src/config.c
        ${CMAKE_SOURCE_DIR}/src/ocr/gemini.c
        ${CMAKE_SOURCE_DIR}/src/processor.c
        ${CMAKE_SOURCE_DIR}/src/telegram/bot.c
        ${CMAKE_SOURCE_DIR}/src/telegram/bot_commands.c
        ${CMAKE_SOURCE_DIR}/src/telegram/bot_telegram.c
        ${CMAKE_SOURCE_DIR}/src/utils.c
        ${CMAKE_SOURCE_DIR}/src/db/db_sqlite.c
        ${CMAKE_SOURCE_DIR}/src/storage/storage_local.c
        ${CMAKE_SOURCE_DIR}/src/db/db_postgres.c
        ${CMAKE_SOURCE_DIR}/src/storage/storage_supabase.c
        ${CMAKE_SOURCE_DIR}/src/ocr/prompt_fetcher.c
        ${CMAKE_SOURCE_DIR}/src/infra/http_client.c
        ${CMAKE_SOURCE_DIR}/src/infra/file_repository.c
        ${CMAKE_SOURCE_DIR}/src/infra/file_repository_memory.c
        ${CMAKE_SOURCE_DIR}/src/ocr/ocr_service.c
        ${CMAKE_SOURCE_DIR}/src/infra/document_store.c
        ${CMAKE_SOURCE_DIR}/src/infra/event_bus.c
    )

    add_custom_target(lint
        COMMAND ${CLANG_TIDY_EXE}
            -p ${CMAKE_BINARY_DIR}
            ${LINT_SOURCES}
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
    file(GLOB TEST_FORMAT_FILES ${CMAKE_SOURCE_DIR}/tests/test_*.c)

    set(FORMAT_HEADERS
        ${CMAKE_SOURCE_DIR}/src/telegram/bot.h
        ${CMAKE_SOURCE_DIR}/src/telegram/bot_commands.h
        ${CMAKE_SOURCE_DIR}/src/telegram/bot_telegram.h
        ${CMAKE_SOURCE_DIR}/src/config.h
        ${CMAKE_SOURCE_DIR}/src/processor.h
        ${CMAKE_SOURCE_DIR}/src/utils.h
        ${CMAKE_SOURCE_DIR}/src/db/db.h
        ${CMAKE_SOURCE_DIR}/src/db/db_backend.h
        ${CMAKE_SOURCE_DIR}/src/storage/storage.h
        ${CMAKE_SOURCE_DIR}/src/storage/storage_backend.h
        ${CMAKE_SOURCE_DIR}/src/ocr/gemini.h
        ${CMAKE_SOURCE_DIR}/src/ocr/ocr_service.h
        ${CMAKE_SOURCE_DIR}/src/ocr/prompt_fetcher.h
        ${CMAKE_SOURCE_DIR}/src/infra/http_client.h
        ${CMAKE_SOURCE_DIR}/src/infra/file_repository.h
        ${CMAKE_SOURCE_DIR}/src/infra/document_store.h
        ${CMAKE_SOURCE_DIR}/src/infra/event_bus.h
    )

    set(FORMAT_MISC
        ${CMAKE_SOURCE_DIR}/tests/main.c
        ${CMAKE_SOURCE_DIR}/tests/test_helpers.h
        ${CMAKE_SOURCE_DIR}/tests/test_runner.h
    )

    add_custom_target(format
        COMMAND ${CLANG_FORMAT_EXE} -i
            ${SOURCES}
            ${FORMAT_HEADERS}
            ${FORMAT_MISC}
            ${TEST_FORMAT_FILES}
        COMMENT "Formatting all source files with clang-format"
        VERBATIM
    )

    add_custom_target(format-check
        COMMAND ${CLANG_FORMAT_EXE} --dry-run --Werror
            ${SOURCES}
            ${FORMAT_HEADERS}
            ${FORMAT_MISC}
            ${TEST_FORMAT_FILES}
        COMMENT "Checking formatting with clang-format"
        VERBATIM
    )

    message(STATUS "clang-format found - 'make format' and 'make format-check' targets available")
endif()

# ── Test Coverage ─────────────────────────────────────────────────────────────
option(ENABLE_COVERAGE "Enable code coverage instrumentation" OFF)

if(ENABLE_COVERAGE AND BUILD_TESTS)
    set(COVERAGE_FLAGS --coverage -O0 -g -fno-inline)
    set(TEST_TARGETS test_storage test_db test_json test_config test_edge_cases
                     test_utils test_bot test_prompts test_processor test_http_client
                     test_file_repository test_document_store test_event_bus)
    foreach(T ${TEST_TARGETS})
        target_compile_options(${T} PRIVATE ${COVERAGE_FLAGS})
        target_link_options(${T} PRIVATE --coverage)
    endforeach()

    find_program(LCOV_EXE lcov)
    find_program(GENHTML_EXE genhtml)

    if(LCOV_EXE AND GENHTML_EXE)
        add_custom_target(coverage
            COMMAND ${LCOV_EXE} --capture --directory ${CMAKE_BINARY_DIR} --output-file coverage.info --ignore-errors mismatch
            COMMAND ${LCOV_EXE} --remove coverage.info '*/tests/*' '*/third_party/*' --output-file coverage_filtered.info --ignore-errors unused
            COMMAND ${GENHTML_EXE} coverage_filtered.info --output-directory coverage_report
            COMMENT "Generating code coverage report"
            VERBATIM
        )

        add_custom_target(test-coverage
            COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
            COMMAND ${LCOV_EXE} --capture --directory ${CMAKE_BINARY_DIR} --output-file coverage.info --ignore-errors mismatch
            COMMAND ${LCOV_EXE} --remove coverage.info '*/tests/*' '*/third_party/*' --output-file coverage_filtered.info --ignore-errors unused
            COMMAND ${GENHTML_EXE} coverage_filtered.info --output-directory coverage_report
            COMMENT "Running tests and generating coverage report"
            VERBATIM
        )

        add_custom_target(coverage-check
            COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
            COMMAND ${LCOV_EXE} --capture --directory ${CMAKE_BINARY_DIR} --output-file coverage.info --ignore-errors mismatch
            COMMAND ${LCOV_EXE} --remove coverage.info '*/tests/*' '*/third_party/*' --output-file coverage_filtered.info --ignore-errors unused
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

set(COVERAGE_MIN_PERCENT 60 CACHE STRING "Minimum line coverage percentage threshold")

# ── Install ───────────────────────────────────────────────────────────────────
install(TARGETS tgbot DESTINATION bin)
