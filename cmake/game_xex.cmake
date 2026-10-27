# skylanders_game_xex(TARGET <game exe> PREFIX <name> TITLE <disc name> HASH_FILE <file>)
#
# The disc check of one game port (src/xex): reads the supported discs' SHA-256 from HASH_FILE (one
# per line, as docs/game/default.xex.sha256 holds them), links the check into TARGET with
# SKYLANDERS_XEX_SHA256 (the hashes, comma separated) and SKYLANDERS_GAME_TITLE (TITLE, e.g.
# "Skylanders: Giants", for the "put your disc in" message), and adds <PREFIX>_xexcheck (the CLI
# the installer runs, exit codes in xex_check_exit_code.h) and the tests <PREFIX>_tests,
# xex_check_cli_argv_test and xex_check_exit_code_test. Needs REXSDK_DIR for the SDK's vendored
# SHA-256.
function(skylanders_game_xex)
    cmake_parse_arguments(ARG "" "TARGET;PREFIX;TITLE;HASH_FILE" "" ${ARGN})
    if(NOT ARG_TARGET OR NOT ARG_PREFIX OR NOT ARG_TITLE OR NOT ARG_HASH_FILE)
        message(FATAL_ERROR "skylanders_game_xex: TARGET, PREFIX, TITLE and HASH_FILE are needed")
    endif()
    if(NOT REXSDK_DIR)
        message(FATAL_ERROR "skylanders_game_xex: REXSDK_DIR must be set (the check uses its vendored SHA-256)")
    endif()

    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${ARG_HASH_FILE}")
    file(STRINGS "${ARG_HASH_FILE}" lines REGEX "^[0-9a-fA-F]+")
    set(hashes "")
    foreach(line IN LISTS lines)
        string(REGEX MATCH "^[0-9a-fA-F]+" hash "${line}")
        string(LENGTH "${hash}" hash_len)
        if(NOT hash_len EQUAL 64)
            message(FATAL_ERROR "${ARG_HASH_FILE}: every line must start with a 64-character hex SHA-256 (found ${hash_len} hex characters)")
        endif()
        if(hashes)
            string(APPEND hashes ",")
        endif()
        string(APPEND hashes "${hash}")
    endforeach()
    if(NOT hashes)
        message(FATAL_ERROR "${ARG_HASH_FILE} holds no SHA-256")
    endif()

    # This file's folder: the function runs in the game's scope, where the library's variables
    # aren't set.
    cmake_path(GET CMAKE_CURRENT_FUNCTION_LIST_DIR PARENT_PATH lib)
    set(src "${lib}/src")
    add_library(${ARG_PREFIX}_verify STATIC "${src}/xex/xex_verify.cpp" "${REXSDK_DIR}/thirdparty/crypto/sha256.cpp")
    target_include_directories(${ARG_PREFIX}_verify PUBLIC "${src}")
    target_include_directories(${ARG_PREFIX}_verify PRIVATE "${REXSDK_DIR}/thirdparty")
    target_compile_features(${ARG_PREFIX}_verify PUBLIC cxx_std_23)

    set(defs SKYLANDERS_XEX_SHA256="${hashes}" SKYLANDERS_GAME_TITLE="${ARG_TITLE}"
             SKYLANDERS_XEXCHECK_NAME="${ARG_PREFIX}_xexcheck")
    target_link_libraries(${ARG_TARGET} PRIVATE ${ARG_PREFIX}_verify)
    target_compile_definitions(${ARG_TARGET} PRIVATE ${defs})

    add_executable(${ARG_PREFIX}_xexcheck "${src}/xex/xex_check_cli.cpp")
    target_link_libraries(${ARG_PREFIX}_xexcheck PRIVATE ${ARG_PREFIX}_verify)
    target_compile_definitions(${ARG_PREFIX}_xexcheck PRIVATE ${defs})

    enable_testing()
    set(tests "${lib}/tests")
    add_executable(${ARG_PREFIX}_tests "${tests}/xex_verify_test.cpp")
    target_link_libraries(${ARG_PREFIX}_tests PRIVATE ${ARG_PREFIX}_verify)
    add_test(NAME xex_verify COMMAND ${ARG_PREFIX}_tests)
    # Runs the built <PREFIX>_xexcheck against a non-ASCII path (argv narrowing on Windows); it has to
    # be in the same output directory at test time.
    add_executable(xex_check_cli_argv_test "${tests}/xex_check_cli_argv_test.cpp")
    target_compile_definitions(xex_check_cli_argv_test PRIVATE SKYLANDERS_XEXCHECK_NAME="${ARG_PREFIX}_xexcheck")
    add_dependencies(xex_check_cli_argv_test ${ARG_PREFIX}_xexcheck)
    add_test(NAME xex_check_cli_argv COMMAND xex_check_cli_argv_test)
    add_executable(xex_check_exit_code_test "${tests}/xex_check_exit_code_test.cpp")
    target_link_libraries(xex_check_exit_code_test PRIVATE ${ARG_PREFIX}_verify)
    add_test(NAME xex_check_exit_code COMMAND xex_check_exit_code_test)
endfunction()
