# rules for finding the exSID library

find_package(PkgConfig REQUIRED)
pkg_check_modules(EXSID libexsid>=2.0)

find_path(EXSID_INCLUDE_DIR exSID.h HINTS ${EXSID_INCLUDEDIR} ${EXSID_INCLUDE_DIRS})
find_library(EXSID_LIBRARY NAMES exsid HINTS ${EXSID_LIBDIR} ${EXSID_LIBRARY_DIRS})

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(exsid DEFAULT_MSG EXSID_LIBRARY EXSID_INCLUDE_DIR)

mark_as_advanced(EXSID_INCLUDE_DIR EXSID_LIBRARY)

set(EXSID_INCLUDE_DIRS ${EXSID_INCLUDE_DIR})
set(EXSID_LIBRARIES ${EXSID_LIBRARY})
