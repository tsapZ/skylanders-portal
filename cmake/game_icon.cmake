# skylanders_game_icon(<target> <rom folder>): the game's own logo as the exe's icon.
#
# tools/make_game_icon.ps1 makes it when the game is built, from the owner's disc: the NXE dashboard
# tile in its `nxeart` package (the game's logo), as an .ico in the build folder, built into the
# exe as its first icon. Windows shows that for the exe and its shortcuts (the installers' desktop
# and start menu ones point at the exe), and SDL takes the exe's first icon for the window and the
# taskbar. Without a disc, a plain icon of our own. The picture never goes into a repository.

function(skylanders_game_icon target rom_dir)
  if(NOT WIN32)
    return()
  endif()
  set(dir "${CMAKE_CURRENT_BINARY_DIR}/game_icon")
  set(ico "${dir}/game.ico")
  set(rc "${dir}/game_icon.rc")
  set(script "${_SKYLANDERS_GAME_ICON_DIR}/../tools/make_game_icon.ps1")
  set(deps "${script}")
  if(EXISTS "${rom_dir}/nxeart")
    list(APPEND deps "${rom_dir}/nxeart")
  endif()
  add_custom_command(
    OUTPUT "${ico}"
    COMMAND powershell -NoProfile -ExecutionPolicy Bypass -File "${script}" -Rom "${rom_dir}" -Out "${ico}"
    DEPENDS ${deps}
    COMMENT "Game icon from the disc's dashboard tile"
    VERBATIM)
  # The resource script names the icon by its full path (backslashes doubled for rc's strings);
  # written only when it changes, so it doesn't rebuild on every configure.
  file(TO_NATIVE_PATH "${ico}" ico_native)
  string(REPLACE "\\" "\\\\" ico_rc "${ico_native}")
  file(CONFIGURE OUTPUT "${rc}" CONTENT "1 ICON \"${ico_rc}\"\n")
  target_sources(${target} PRIVATE "${rc}")
  set_source_files_properties("${rc}" PROPERTIES OBJECT_DEPENDS "${ico}")
endfunction()

set(_SKYLANDERS_GAME_ICON_DIR "${CMAKE_CURRENT_LIST_DIR}" CACHE INTERNAL "skylanders_game_icon's folder")
