# skylanders-portal: Skylanders Portal of Power emulation for ReXGlue recompilations.
#
# skylanders_portal_core has no ReXGlue dependency and is unit-tested. It still needs the AES and
# MD5 sources the ReXGlue SDK vendors (for figure_stats), so REXSDK_DIR must point at an SDK tree.
# skylanders_portal_rex (added below when available) needs the rex::runtime target.

cmake_minimum_required(VERSION 3.25)
project(skylanders_portal LANGUAGES CXX C)

# skylanders_game_icon(): the game's logo from its disc as the exe's icon (cmake/game_icon.cmake).
include(cmake/game_icon.cmake)
# skylanders_game_xex(): a game's disc check, its installer CLI and tests (cmake/game_xex.cmake).
include(cmake/game_xex.cmake)

# skylanders_alchemy: readers for the games' Alchemy files (IGA archives in their versions, IGZ
# objects, Swap Force's IGX, the LZMA and deflate blocks inside), no SDK needed, for the games and
# the launcher. inflate.cpp takes stb_image's zlib decoder: the SDK's copy, or without an SDK the
# folder SKYLANDERS_STB_DIR names (the one holding stb_image.h).
if(REXSDK_DIR)
    set(SKYLANDERS_ALCHEMY_STB "${REXSDK_DIR}/thirdparty/stb")
elseif(SKYLANDERS_STB_DIR)
    set(SKYLANDERS_ALCHEMY_STB "${SKYLANDERS_STB_DIR}")
endif()
if(SKYLANDERS_ALCHEMY_STB)
    add_library(skylanders_alchemy STATIC
        src/alchemy/iga_archive.cpp
        src/alchemy/iga8_archive.cpp
        src/alchemy/iga11_archive.cpp
        src/alchemy/igx_reader.cpp
        src/alchemy/igz_file.cpp
        src/alchemy/igz6_file.cpp
        src/alchemy/igz8_file.cpp
        src/alchemy/inflate.cpp
        src/alchemy/lzma_decoder.cpp
        src/alchemy/xenos_dxt.cpp
    )
    target_include_directories(skylanders_alchemy PUBLIC src)
    target_include_directories(skylanders_alchemy PRIVATE "${SKYLANDERS_ALCHEMY_STB}")
    target_compile_features(skylanders_alchemy PUBLIC cxx_std_23)
endif()

# skylanders_portal_figures: only the figure files (catalog, names, elements, stats), for programs
# without the SDK such as the Core of Light launcher. On Windows it needs no SDK tree: AES and MD5
# come from Windows (CNG). Built whenever REXSDK_DIR isn't set.
set(SKYLANDERS_PORTAL_FIGURE_SOURCES
    src/portal/data_folder.cpp
    src/portal/figure_catalog.cpp
    src/portal/figure_crypto.cpp
    src/portal/figure_file.cpp
    src/portal/figure_info.cpp
    src/portal/figure_stats.cpp
    src/portal/trap_villain.cpp
)
if(NOT REXSDK_DIR)
    add_library(skylanders_portal_figures STATIC ${SKYLANDERS_PORTAL_FIGURE_SOURCES})
    target_include_directories(skylanders_portal_figures PUBLIC src)
    if(WIN32)
        target_compile_definitions(skylanders_portal_figures PRIVATE SKYLANDERS_PORTAL_CRYPTO_CNG)
        target_link_libraries(skylanders_portal_figures PRIVATE bcrypt shell32 ole32)
    else()
        find_package(OpenSSL REQUIRED)
        target_compile_definitions(skylanders_portal_figures PRIVATE SKYLANDERS_PORTAL_CRYPTO_OPENSSL)
        target_link_libraries(skylanders_portal_figures PRIVATE OpenSSL::Crypto)
    endif()
    target_compile_features(skylanders_portal_figures PUBLIC cxx_std_23)
    if(SKYLANDERS_PORTAL_TESTS AND CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
        enable_testing()
        add_executable(skylanders_figure_crypto_test tests/figure_crypto_test.cpp)
        target_include_directories(skylanders_figure_crypto_test PRIVATE tests)
        target_link_libraries(skylanders_figure_crypto_test PRIVATE skylanders_portal_figures)
        add_test(NAME skylanders_figure_crypto_test COMMAND skylanders_figure_crypto_test)
    endif()
    return()
endif()

add_library(skylanders_portal_core STATIC
    src/portal/audio.cpp
    src/portal/data_folder.cpp
    src/portal/figure_catalog.cpp
    src/portal/figure_crypto.cpp
    src/portal/figure_file.cpp
    src/portal/figure_info.cpp
    src/portal/figure_stats.cpp
    src/portal/g726.cpp
    src/portal/hybrid/real_portal_bridge.cpp
    src/portal/menu_favourites.cpp
    src/portal/mod_list.cpp
    src/portal/old_portal_light.cpp
    src/portal/portal_mode.cpp
    src/portal/problem_report_text.cpp
    src/portal/prompt_icons.cpp
    src/portal/prompt_tables.cpp
    src/portal/run_logic.cpp
    src/portal/save_medals.cpp
    src/portal/soul_gem_names.cpp
    src/portal/vv_save.cpp
    src/portal/xbox_frame.cpp
    src/portal/software/software_portal.cpp
    src/portal/traffic_log.cpp
    src/portal/trap_villain.cpp
    src/portal/xam_bridge.cpp
    "${REXSDK_DIR}/thirdparty/aes_128/aes.c"
    "${REXSDK_DIR}/thirdparty/libmspack/cabextract/md5.c"
)
target_include_directories(skylanders_portal_core PUBLIC src)
target_include_directories(skylanders_portal_core PRIVATE
    "${REXSDK_DIR}/thirdparty/aes_128"
    "${REXSDK_DIR}/thirdparty/libmspack/cabextract")
target_compile_features(skylanders_portal_core PUBLIC cxx_std_23)
if(WIN32)
    # data_folder.cpp: the Documents and AppData known folders.
    target_link_libraries(skylanders_portal_core PUBLIC shell32 ole32)
endif()

option(SKYLANDERS_PORTAL_TESTS "Build skylanders-portal unit tests" ON)
if(SKYLANDERS_PORTAL_TESTS)
    enable_testing()
    foreach(t portal_framing_test portal_mode_test software_portal_test figure_file_test
              figure_catalog_test figure_crypto_test figure_stats_test audio_test xam_bridge_test
              trap_villain_test g726_test figure_info_test real_portal_bridge_test traffic_log_test
              old_portal_light_test prompt_icons_test run_logic_test save_medals_test soul_gem_names_test mod_list_test vv_save_test vv_tracker_bits_test
              menu_favourites_test problem_report_test case_path_test)
        add_executable(skylanders_${t} tests/${t}.cpp)
        target_include_directories(skylanders_${t} PRIVATE tests)
        target_link_libraries(skylanders_${t} PRIVATE skylanders_portal_core)
        add_test(NAME skylanders_${t} COMMAND skylanders_${t})
    endforeach()
endif()

# skylanders_portal_rex: ReXGlue glue (settings, XAM handler, F6 overlay, USB portal, audio).
# Only built when the including project provides the rex::runtime target.
if(TARGET rex::runtime)
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/hidapi/CMakeLists.txt")
        message(FATAL_ERROR "skylanders-portal: thirdparty/hidapi missing; run git submodule update --init --recursive")
    endif()
    set(HIDAPI_WITH_TESTS OFF CACHE BOOL "" FORCE)
    set(BUILD_SHARED_LIBS OFF)
    add_subdirectory(thirdparty/hidapi)

    add_library(skylanders_portal_rex STATIC
        src/portal_rex/portal_rex.cpp
        src/portal_rex/portal_overlay_dialog.cpp
        src/portal_rex/portal_menu.cpp
        src/portal_rex/portal_menu_input.cpp
        src/portal_rex/menu_draw.cpp
        src/portal_rex/achievements_ui.cpp
        src/portal_rex/discord_presence.cpp
        src/portal_rex/input_pictures.cpp
        src/portal_rex/player_menu.cpp
        src/portal_rex/settings_file.cpp
        src/portal_rex/settings_menu.cpp
        src/portal_rex/settings_schema.cpp
        src/portal_rex/button_prompts.cpp
        src/portal_rex/title_music.cpp
        src/portal_rex/png_file.cpp
        src/portal_rex/problem_note.cpp
        src/portal_rex/problem_report.cpp
        src/portal_rex/movie_skip.cpp
        src/portal_rex/file_watch.cpp
        src/portal_rex/soul_gems.cpp
        src/portal_rex/run_rules.cpp
        src/portal_rex/mods.cpp
        src/portal_rex/remapper_check.cpp
        src/portal/usb/usb_portal.cpp
        src/portal/usb/xbox360_portal.cpp
        src/portal/usb/winusb_portal_io.cpp
        src/portal_rex/sdl_audio_sink.cpp
        src/portal_rex/speaker_overlay.cpp
        src/portal_rex/dev_overlay.cpp
        src/portal/system_locale.cpp
        # Test tools: scripted input, memory watches, the crash report, GPU crash diagnostics,
        # where the player is (for problem reports).
        src/dev/gpu_diagnostics.cpp
        src/dev/input_script.cpp
        src/dev/where.cpp
        src/dev/write_watch.cpp
    )
    target_include_directories(skylanders_portal_rex PUBLIC src)
    target_include_directories(skylanders_portal_rex PRIVATE "${REXSDK_DIR}/thirdparty/imgui")
    # The in-game menus' stone frame (morpheus284's art from Core of Light, CC BY 4.0; assets/README.md) built into
    # the program as bytes (menu_frame_png.inc, included by portal_menu.cpp); rewritten only when the picture changes.
    set(SKYLANDERS_FRAME_PNG "${CMAKE_CURRENT_SOURCE_DIR}/assets/menu_frame.png")
    file(READ "${SKYLANDERS_FRAME_PNG}" _skylanders_frame_hex HEX)
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," _skylanders_frame_bytes "${_skylanders_frame_hex}")
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/generated/menu_frame_png.inc.new" "${_skylanders_frame_bytes}\n")
    configure_file("${CMAKE_CURRENT_BINARY_DIR}/generated/menu_frame_png.inc.new"
                   "${CMAKE_CURRENT_BINARY_DIR}/generated/menu_frame_png.inc" COPYONLY)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${SKYLANDERS_FRAME_PNG}")
    # The PlayStation button prompts by Laytonator2 (Core of Light #112, CC BY 4.0; assets/README.md), built in the
    # same way: one array per picture, named after its file (prompt_art_png.inc, included by button_prompts.cpp).
    set(_skylanders_art "")
    foreach(_name cross circle square triangle start select l1 r1 l2 r2 l3 r3)
        set(_png "${CMAKE_CURRENT_SOURCE_DIR}/assets/prompts_playstation/${_name}.png")
        file(READ "${_png}" _hex HEX)
        string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," _bytes "${_hex}")
        string(APPEND _skylanders_art "const uint8_t kArt_${_name}[] = {${_bytes}};\n")
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_png}")
    endforeach()
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/generated/prompt_art_png.inc.new" "${_skylanders_art}")
    configure_file("${CMAKE_CURRENT_BINARY_DIR}/generated/prompt_art_png.inc.new"
                   "${CMAKE_CURRENT_BINARY_DIR}/generated/prompt_art_png.inc" COPYONLY)
    target_include_directories(skylanders_portal_rex PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
    target_link_libraries(skylanders_portal_rex PUBLIC skylanders_portal_core rex::runtime hidapi::hidapi SDL3::SDL3)
    # The SDK's FFmpeg decodes the title music from the disc (title_music.cpp).
    target_link_libraries(skylanders_portal_rex PRIVATE libavcodec libavutil)
    if(WIN32)
        target_link_libraries(skylanders_portal_rex PRIVATE windowscodecs ole32)
    else()
        find_package(ZLIB REQUIRED)
        target_include_directories(skylanders_portal_rex PRIVATE "${REXSDK_DIR}/thirdparty/stb")
        target_link_libraries(skylanders_portal_rex PRIVATE ZLIB::ZLIB)
        if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
            find_package(PkgConfig REQUIRED)
            pkg_check_modules(SKYLANDERS_LIBUSB REQUIRED IMPORTED_TARGET libusb-1.0)
            target_link_libraries(skylanders_portal_rex PRIVATE PkgConfig::SKYLANDERS_LIBUSB)
        endif()
    endif()
    if(SKYLANDERS_PORTAL_TESTS)
        enable_testing()
        add_executable(skylanders_png_file_test tests/png_file_test.cpp)
        target_include_directories(skylanders_png_file_test PRIVATE tests)
        target_link_libraries(skylanders_png_file_test PRIVATE skylanders_portal_rex)
        add_test(NAME skylanders_png_file_test COMMAND skylanders_png_file_test)
        add_executable(skylanders_system_locale_test tests/system_locale_test.cpp src/portal/system_locale.cpp)
        target_include_directories(skylanders_system_locale_test PRIVATE src tests)
        target_compile_features(skylanders_system_locale_test PRIVATE cxx_std_23)
        add_test(NAME skylanders_system_locale_test COMMAND skylanders_system_locale_test)
    endif()
endif()
