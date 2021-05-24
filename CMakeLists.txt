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

include(CTest)
include(GNUInstallDirs)
include(CMakeDependentOption)
include(FeatureSummary)
include(CMakePackageConfigHelpers)

find_package(PkgConfig REQUIRED)
find_package(Threads)
set_package_properties(Threads PROPERTIES TYPE REQUIRED)
pkg_check_modules(BLOSSOM REQUIRED libblossom>=1.3.0)
pkg_check_modules(LIBZ REQUIRED zlib>=1.2.11)

if(${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
set(PKGCONFIG_DIR "${CMAKE_INSTALL_LIBDIR}/pkgconfig")
else()
set(PKGCONFIG_DIR "${CMAKE_INSTALL_PREFIX}/libdata/pkgconfig")
endif()

configure_file(tools/config.h.in config.h)

configure_file(libraptorial.pc.in
  ${CMAKE_CURRENT_BINARY_DIR}/libraptorial.pc
  @ONLY
)

configure_package_config_file(tools/RaptorialConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/RaptorialConfig.cmake
  INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/Raptorial"
)

write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/RaptorialConfigVersion.cmake
  COMPATIBILITY SameMajorVersion
)

file(GLOB LIBSRCS src/lib/*.c)

add_library(raptorial SHARED ${LIBSRCS})
target_include_directories(raptorial
  PRIVATE
    "${PROJECT_BINARY_DIR}"
    src/lib
)
target_link_libraries(raptorial
  PRIVATE
    ${BLOSSOM_LIBRARIES}
    ${LIBZ_LIBRARIES}
  PUBLIC
    Threads::Threads
)

file(GLOB RAPTPARSECHANGELOG src/bin/rapt-parsechangelog.c)
file(GLOB RAPTSHOWVERSIONSSRCS src/bin/rapt-show-versions.c)
file(GLOB RAPTORIALFILESRCS src/bin/raptorial-file.c)
file(GLOB TESTERFILESRCS src/tester/main.c)

add_executable(rapt-show-versions ${RAPTSHOWVERSIONSSRCS})
add_executable(rapt-parsechangelog ${RAPTPARSECHANGELOG})
add_executable(raptorial-file ${RAPTORIALFILESRCS})
if(${BUILD_TESTING})
add_executable(rapt-tester ${TESTERFILESRCS})
endif()

target_include_directories(raptorial-file
  PRIVATE
    "${PROJECT_BINARY_DIR}"
    src/lib
)
target_link_libraries(raptorial-file
  PRIVATE
    ${BLOSSOM_LIBRARIES}
    ${LIBZ_LIBRARIES}
    raptorial
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
    raptorial
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
    raptorial
    Threads::Threads
)

install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/libraptorial.pc
  DESTINATION ${PKGCONFIG_DIR}
)

install(FILES
  "${CMAKE_CURRENT_BINARY_DIR}/RaptorialConfig.cmake"
  "${CMAKE_CURRENT_BINARY_DIR}/RaptorialConfigVersion.cmake"
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/Raptorial"
)

file(GLOB RAPTORIAL_HEADERS
  CONFIGURE_DEPENDS
  LIST_DIRECTORIES false
  ${PROJECT_SOURCE_DIR}/src/lib/raptorial.h
)

install(FILES ${RAPTORIAL_HEADERS} DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(TARGETS raptorial
  LIBRARY
    DESTINATION ${CMAKE_INSTALL_LIBDIR}
    COMPONENT Libraries
    NAMELINK_COMPONENT Development
)

install(TARGETS rapt-show-versions DESTINATION bin)
install(TARGETS rapt-parsechangelog DESTINATION bin)
install(TARGETS raptorial-file DESTINATION bin)

if(${BUILD_TESTING})
target_include_directories(rapt-tester
  PRIVATE
    "${PROJECT_BINARY_DIR}"
    src/lib
)
target_link_libraries(rapt-tester
  PRIVATE
    ${BLOSSOM_LIBRARIES}
    ${LIBZ_LIBRARIES}
    raptorial
    Threads::Threads
)
file(GLOB DEBDISTFILES CONFIGURE_DEPENDS /var/lib/apt/lists/*Packages)
foreach(p ${DEBDISTFILES})
add_test(
  NAME rapt-tester-${p}
  COMMAND rapt-tester ${p}
)
install(TARGETS rapt-tester DESTINATION bin)
endforeach()
endif()
