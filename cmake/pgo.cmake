# Profile-guided optimization for a game port, included by its CMakeLists.txt before the SDK
# (generated/rexglue.cmake), so the options reach the SDK's targets too: its command processor
# thread is where Spyro's Adventure spends its frame on the CPU (2026-10-30: Level_036 52 -> 59 FPS
# uncapped, Level_006 72 -> 77; the spots the graphics card limits stay the same).
#
#   SKYLANDERS_PGO=generate     an instrumented build: it counts what runs and writes the counts
#                               (LLVM_PROFILE_FILE, "%m" per module) when it exits; for scripted
#                               runs only, it is slower
#   SKYLANDERS_PGO_PROFILE=<f>  a build that uses the counts, merged with llvm-profdata
#
# Neither is set by default: an ordinary build. The counts aren't in the repository; they come
# from scripted runs of the game on the maintainer's machine (levels other than the ones the
# benchmarks measure), and a profile older than the code only misses the functions that changed.
set(SKYLANDERS_PGO "" CACHE STRING "generate: an instrumented build for profile-guided optimization")
set(SKYLANDERS_PGO_PROFILE "" CACHE FILEPATH "a merged profile (llvm-profdata) to optimize with")

if(SKYLANDERS_PGO STREQUAL "generate")
    add_compile_options("$<$<COMPILE_LANGUAGE:C,CXX>:-fprofile-generate>")
    add_compile_definitions(SKYLANDERS_PGO_GEN=1)
    add_link_options(-fprofile-generate)
    # The SDK's DLLs export everything; their export lists then name counters of functions the
    # linker folded away.
    string(APPEND CMAKE_SHARED_LINKER_FLAGS " -Wl,/force:unresolved")
    message(STATUS "Profile-guided optimization: instrumented build")
elseif(SKYLANDERS_PGO_PROFILE)
    if(NOT EXISTS "${SKYLANDERS_PGO_PROFILE}")
        message(FATAL_ERROR "SKYLANDERS_PGO_PROFILE: ${SKYLANDERS_PGO_PROFILE} doesn't exist")
    endif()
    add_compile_options("$<$<COMPILE_LANGUAGE:C,CXX>:-fprofile-use=${SKYLANDERS_PGO_PROFILE}>"
                        "$<$<COMPILE_LANGUAGE:C,CXX>:-Wno-profile-instr-unprofiled>"
                        "$<$<COMPILE_LANGUAGE:C,CXX>:-Wno-profile-instr-out-of-date>")
    # The compiler doesn't name the profile among a file's inputs: a new profile at the same path
    # changes this define (configure runs again when the file changes), so everything is compiled
    # with it.
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${SKYLANDERS_PGO_PROFILE}")
    file(MD5 "${SKYLANDERS_PGO_PROFILE}" _skylanders_pgo_md5)
    string(SUBSTRING "${_skylanders_pgo_md5}" 0 12 _skylanders_pgo_md5)
    add_compile_definitions(SKYLANDERS_PGO_PROFILE_ID=0x${_skylanders_pgo_md5})
    message(STATUS "Profile-guided optimization: ${SKYLANDERS_PGO_PROFILE}")
endif()
