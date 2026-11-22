# SPDX-License-Identifier: GPL-3.0-only
# Standalone self-test for the Blamdom mod catalog logic:
#   - BlamdomAPI::matchModFile / modFileVersion / isNewerVersion (pure, header-only)
#   - BlamdomAPI::buildModMetadata + the Packwiz .pw.toml write/read round-trip
#     with the new BLAMDOM provider (real Packwiz.cpp + ModIndex.cpp, no GUI)
#
#   cmake -S tests/blamdom-catalog -B /tmp/blamdom-catalog-test -DCMAKE_PREFIX_PATH=/opt/qt/6.9.3/gcc_64 \
#     && cmake --build /tmp/blamdom-catalog-test && QT_QPA_PLATFORM=offscreen /tmp/blamdom-catalog-test/selftest
cmake_minimum_required(VERSION 3.16)
project(blamdom_catalog_selftest CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Core Network)
find_package(tomlplusplus 3.2.0)
# fallback to pkgconfig, important especially as many distros package toml++ built with meson
if(NOT tomlplusplus_FOUND)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(tomlplusplus REQUIRED IMPORTED_TARGET tomlplusplus>=3.2.0)
endif()

set(LAUNCHER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../launcher)

add_executable(selftest
    selftest.cpp
    ${LAUNCHER_DIR}/Json.cpp
    ${LAUNCHER_DIR}/modplatform/packwiz/Packwiz.cpp
    ${LAUNCHER_DIR}/modplatform/ModIndex.cpp
    ${LAUNCHER_DIR}/Version.cpp
)
target_include_directories(selftest PRIVATE ${LAUNCHER_DIR})
target_link_libraries(selftest PRIVATE Qt6::Core Qt6::Network)
if(TARGET PkgConfig::tomlplusplus)
    target_link_libraries(selftest PRIVATE PkgConfig::tomlplusplus)
else()
    target_link_libraries(selftest PRIVATE tomlplusplus::tomlplusplus)
endif()
