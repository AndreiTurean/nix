#----------------------------------------------------------------
# Generated CMake target import file for configuration "Release".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "nix::nix_core" for configuration "Release"
set_property(TARGET nix::nix_core APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(nix::nix_core PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libnix_core.a"
  )

list(APPEND _cmake_import_check_targets nix::nix_core )
list(APPEND _cmake_import_check_files_for_nix::nix_core "${_IMPORT_PREFIX}/lib/libnix_core.a" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
