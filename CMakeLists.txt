cmake_minimum_required(VERSION 3.14.0)
project(notcurses VERSION 2.0.9
  DESCRIPTION "Fast APT tools"
  HOMEPAGE_URL "https://www.sprezzatech.com/wiki/index.php/Raptorial"
  LANGUAGES C CXX)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_VISIBILITY_PRESET hidden)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)

include(GNUInstallDirs)
include(CMakeDependentOption)
include(FeatureSummary)
include(CMakePackageConfigHelpers)

find_package(PkgConfig REQUIRED)
find_package(Threads)
set_package_properties(Threads PROPERTIES TYPE REQUIRED)
pkg_check_modules(BLOSSOM REQUIRED libblossom>=1.3.0)
pkg_check_modules(LIBZ REQUIRED zlib>=1.2.11)

configure_file(tools/config.h.in config.h)

file(GLOB LIBSRCS src/lib/*.c)

add_library(libraptorial SHARED ${LIBSRCS})
target_include_directories(libraptorial
  PRIVATE
    "${PROJECT_BINARY_DIR}"
    src/lib
)
target_link_libraries(libraptorial
  PRIVATE
    ${BLOSSOM_LIBRARIES}
    ${LIBZ_LIBRARIES}
  PUBLIC
    Threads::Threads
)

file(GLOB RAPTPARSECHANGELOG src/bin/rapt-parsechangelog.c)
file(GLOB RAPTSHOWVERSIONSSRCS src/bin/rapt-show-versions.c)
file(GLOB RAPTORIALFILESRCS src/bin/raptorial-file.c)

add_executable(rapt-show-versions ${RAPTSHOWVERSIONSSRCS})
add_executable(rapt-parsechangelog ${RAPTPARSECHANGELOG})
add_executable(raptorial-file ${RAPTORIALFILESRCS})

target_include_directories(raptorial-file
  PRIVATE
    "${PROJECT_BINARY_DIR}"
    src/lib
)
target_link_libraries(raptorial-file
  PRIVATE
    ${BLOSSOM_LIBRARIES}
    ${LIBZ_LIBRARIES}
    libraptorial
    Threads::Threads
)

target_include_directories(rapt-parsechangelog
  PRIVATE
    "${PROJECT_BINARY_DIR}"
    src/lib
)
target_link_libraries(rapt-parsechangelog
  PRIVATE
    ${BLOSSOM_LIBRARIES}
    ${LIBZ_LIBRARIES}
    libraptorial
    Threads::Threads
)

target_include_directories(rapt-show-versions
  PRIVATE
    "${PROJECT_BINARY_DIR}"
    src/lib
)
target_link_libraries(rapt-show-versions
  PRIVATE
    ${BLOSSOM_LIBRARIES}
    ${LIBZ_LIBRARIES}
    libraptorial
    Threads::Threads
)
