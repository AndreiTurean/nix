#----------------------------------------------------------------
# Generated CMake target import file for configuration "Debug".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "nix::nix_core" for configuration "Debug"
set_property(TARGET nix::nix_core APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(nix::nix_core PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_DEBUG "CXX"
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/lib/libnix_core.a"
  )

list(APPEND _cmake_import_check_targets nix::nix_core )
list(APPEND _cmake_import_check_files_for_nix::nix_core "${_IMPORT_PREFIX}/lib/libnix_core.a" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
