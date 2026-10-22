# skylanders-portal: Skylanders Portal of Power emulation for ReXGlue recompilations.
#
# skylanders_portal_core has no ReXGlue dependency and is unit-tested. It still needs the AES and
# MD5 sources the ReXGlue SDK vendors (for figure_stats), so REXSDK_DIR must point at an SDK tree.
# skylanders_portal_rex (added below when available) needs the rex::runtime target.

cmake_minimum_required(VERSION 3.25)
project(skylanders_portal LANGUAGES CXX C)

# skylanders_portal_figures: only the figure files (catalog, names, elements, stats), for programs
# without the SDK such as the Core of Light launcher. On Windows it needs no SDK tree: AES and MD5
# come from Windows (CNG). Built whenever REXSDK_DIR isn't set.
set(SKYLANDERS_PORTAL_FIGURE_SOURCES
    src/portal/figure_catalog.cpp
    src/portal/figure_crypto.cpp
    src/portal/figure_file.cpp
    src/portal/figure_info.cpp
    src/portal/figure_stats.cpp
    src/portal/trap_villain.cpp
)
if(NOT REXSDK_DIR)
    if(NOT WIN32)
        message(FATAL_ERROR "skylanders-portal: set REXSDK_DIR to a rexglue-sdk source tree")
    endif()
    add_library(skylanders_portal_figures STATIC ${SKYLANDERS_PORTAL_FIGURE_SOURCES})
    target_include_directories(skylanders_portal_figures PUBLIC src)
    target_compile_definitions(skylanders_portal_figures PRIVATE SKYLANDERS_PORTAL_CRYPTO_CNG)
    target_link_libraries(skylanders_portal_figures PRIVATE bcrypt)
    target_compile_features(skylanders_portal_figures PUBLIC cxx_std_23)
    return()
endif()

add_library(skylanders_portal_core STATIC
    src/portal/audio.cpp
    src/portal/figure_catalog.cpp
    src/portal/figure_crypto.cpp
    src/portal/figure_file.cpp
    src/portal/figure_info.cpp
    src/portal/figure_stats.cpp
    src/portal/g726.cpp
    src/portal/hybrid/real_portal_bridge.cpp
    src/portal/portal_mode.cpp
    src/portal/xbox_frame.cpp
    src/portal/software/software_portal.cpp
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

option(SKYLANDERS_PORTAL_TESTS "Build skylanders-portal unit tests" ON)
if(SKYLANDERS_PORTAL_TESTS)
    enable_testing()
    foreach(t portal_framing_test portal_mode_test software_portal_test figure_file_test
              figure_catalog_test figure_stats_test audio_test xam_bridge_test
              trap_villain_test g726_test figure_info_test real_portal_bridge_test)
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
        src/portal_rex/input_pictures.cpp
        src/portal_rex/player_menu.cpp
        src/portal_rex/settings_file.cpp
        src/portal_rex/settings_menu.cpp
        src/portal_rex/settings_schema.cpp
        src/portal_rex/png_file.cpp
        src/portal/usb/usb_portal.cpp
        src/portal_rex/sdl_audio_sink.cpp
    )
    target_include_directories(skylanders_portal_rex PUBLIC src)
    target_include_directories(skylanders_portal_rex PRIVATE "${REXSDK_DIR}/thirdparty/imgui")
    target_link_libraries(skylanders_portal_rex PUBLIC skylanders_portal_core rex::runtime hidapi::hidapi SDL3::SDL3)
    if(WIN32)
        target_link_libraries(skylanders_portal_rex PRIVATE windowscodecs ole32)
    endif()
endif()
