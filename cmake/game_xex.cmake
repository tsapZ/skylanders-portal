# skylanders_game_xex(TARGET <game exe> PREFIX <name> TITLE <disc name> HASH_FILE <file>)
#
# The disc check of one game port (src/xex): reads the supported discs' SHA-256 from HASH_FILE (one
# per line, as docs/game/default.xex.sha256 holds them) and their code pins from default.xex.code
# beside it if there is one, links the check into TARGET with SKYLANDERS_XEX_SHA256 (the hashes and
# pins, comma separated) and SKYLANDERS_GAME_TITLE (TITLE, e.g.
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
    # The code pins (src/xex/xex_header.h) beside it, default.xex.code: "<address>:<SHA-256>" per
    # line. Another pressing with a supported disc's pin runs the same code; the pins go into the
    # same list.
    cmake_path(GET ARG_HASH_FILE PARENT_PATH hash_dir)
    set(code_file "${hash_dir}/default.xex.code")
    if(EXISTS "${code_file}")
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${code_file}")
        file(STRINGS "${code_file}" lines REGEX "^[0-9a-fA-F]")
        foreach(line IN LISTS lines)
            string(REGEX MATCH "^[0-9a-fA-F]+:[0-9a-fA-F]+" pin "${line}")
            if(NOT pin MATCHES "^[0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F]:")
                message(FATAL_ERROR "${code_file}: every line must start with <8 hex digits>:<SHA-256>")
            endif()
            string(LENGTH "${pin}" pin_len)
            if(NOT pin_len EQUAL 73)
                message(FATAL_ERROR "${code_file}: every line must start with <8 hex digits>:<SHA-256>")
            endif()
            string(APPEND hashes ",${pin}")
        endforeach()
    endif()
    # The code this build is recompiled from: the pin of the game's own rom/default.xex (cmake/xex_pin.py), as
    # "code=<pin>" in the list. A disc with other code is refused at start even when its hash or pin is listed (Core
    # of Light #118, #135, 2026-11-22: the SuperChargers downloads are built from the En/De disc's code; the French /
    # Dutch and Scandinavian discs, listed for source builds from them, have their own and crashed in the CRT start-up).
    # Without Python or a rom/default.xex (a check-only build), nothing is added and the list alone decides.
    set(own_xex "${CMAKE_CURRENT_SOURCE_DIR}/rom/default.xex")
    if(EXISTS "${own_xex}")
        find_package(Python3 COMPONENTS Interpreter QUIET)
        if(Python3_Interpreter_FOUND)
            set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${own_xex}")
            execute_process(COMMAND "${Python3_EXECUTABLE}" -I "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/xex_pin.py" "${own_xex}"
                            OUTPUT_VARIABLE own_pin OUTPUT_STRIP_TRAILING_WHITESPACE
                            ERROR_VARIABLE own_pin_error RESULT_VARIABLE own_pin_result)
            if(own_pin MATCHES "^[0-9A-F]+:[0-9a-f]+$")
                message(STATUS "${ARG_PREFIX}: runs discs with the code of rom/default.xex, ${own_pin}")
                string(APPEND hashes ",code=${own_pin}")
            elseif(NOT own_pin_result EQUAL 0)
                # (said out loud: without it a listed disc with other code passes the check again)
                message(WARNING "${ARG_PREFIX}: cmake/xex_pin.py failed (${own_pin_result}) on rom/default.xex: "
                                "${own_pin_error}; the disc check takes every listed disc")
            else()
                message(WARNING "${ARG_PREFIX}: rom/default.xex gives no code pin (not an XEX2 with code pages?); "
                                "the disc check takes every listed disc")
            endif()
        endif()
    endif()

    # This file's folder: the function runs in the game's scope, where the library's variables
    # aren't set.
    cmake_path(GET CMAKE_CURRENT_FUNCTION_LIST_DIR PARENT_PATH lib)
    set(src "${lib}/src")
    # The build for the log's first line (BuildAndSystemLine): the release version from the game's
    # installer script (#define MyAppVersion) and its commit, in a generated source of this library
    # only (as a define of the game's target every commit would compile the whole game again),
    # written again when the script or the commit changes.
    set(version "unknown")
    file(GLOB iss "${CMAKE_CURRENT_SOURCE_DIR}/installer/*.iss")
    if(iss)
        list(GET iss 0 iss)
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${iss}")
        file(STRINGS "${iss}" version_line REGEX "^#define MyAppVersion \"")
        string(REGEX REPLACE "^#define MyAppVersion \"([^\"]+)\".*$" "\\1" version "${version_line}")
    endif()
    set(commit "unknown")
    find_package(Git QUIET)
    if(GIT_FOUND AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.git/HEAD")
        execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${CMAKE_CURRENT_SOURCE_DIR}" rev-parse --short HEAD
                        OUTPUT_VARIABLE commit OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
        execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${CMAKE_CURRENT_SOURCE_DIR}" symbolic-ref -q HEAD
                        OUTPUT_VARIABLE head_ref OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/.git/HEAD")
        if(head_ref AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.git/${head_ref}")
            set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/.git/${head_ref}")
        endif()
        if(NOT commit)
            set(commit "unknown")
        endif()
    endif()
    set(build_info "${CMAKE_CURRENT_BINARY_DIR}/${ARG_PREFIX}_build_version.cpp")
    file(CONFIGURE OUTPUT "${build_info}" CONTENT
         "// Generated by skylanders_game_xex (cmake/game_xex.cmake).\nnamespace skylanders::xex {\nconst char* GameVersion() { return \"${version}\"; }\nconst char* GameCommit() { return \"${commit}\"; }\n}  // namespace skylanders::xex\n")

    add_library(${ARG_PREFIX}_verify STATIC "${src}/xex/xex_verify.cpp" "${src}/xex/build_info.cpp"
                "${build_info}" "${REXSDK_DIR}/thirdparty/crypto/sha256.cpp")
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
