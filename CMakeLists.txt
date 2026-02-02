# ---------------------------------------------------------------------------
# Author: Dirk 
# ---------------------------------------------------------------------------

set(CMLOC "CMakeLists: ")

cmake_minimum_required(VERSION 3.15)
if(COMMAND cmake_policy)
  if (POLICY CMP0043)
    cmake_policy(SET CMP0043 NEW)
  endif ()
  cmake_policy(SET CMP0048 NEW)
  if (POLICY CMP0068)
    cmake_policy(SET CMP0068 NEW)
  endif ()
  if (POLICY CMP0076)
    cmake_policy(SET CMP0076 NEW)
  endif ()
  if (POLICY CMP0077)
    cmake_policy(SET CMP0077 NEW)
  endif ()
  if (POLICY CMP0079)
    cmake_policy(SET CMP0079 NEW)
  endif ()
  if (POLICY CMP0175)
    cmake_policy(SET CMP0175 NEW)
  endif ()
  if (POLICY CMP0177)
    cmake_policy(SET CMP0177 NEW)
  endif ()
endif ()

set(CMAKE_BUILD_WITH_INSTALL_RPATH ON)

# Plugin metadata
set(VERBOSE_NAME "Signalk_notes_opencpn")
set(COMMON_NAME "Signalk_notes_opencpn")
set(TITLE_NAME "signalk_notes_opencpn")
set(PACKAGE_CONTACT "Jon Gough")
set(PACKAGE "signalk_notes_opencpn")

set(SHORT_DESCRIPTION
    "Plugin to show notes from SignalK created by SignalK plugins like 'Garmin Active Captain Resource' or 'EuRIS (European River Information Services)* on the map"
)
set(LONG_DESCRIPTION
    "signalk_notes_opencpn Plugin is used to show notes from SignalK created by SignalK plugins like 'Garmin Active Captain Resource' or 'EuRIS (European River Information Services)' on the map"
)

set(VERSION_MAJOR "1")
set(VERSION_MINOR "0")
set(VERSION_PATCH "0")
set(VERSION_TWEAK "1")
set(VERSION_DATE "15/01/2026")
set(OCPN_MIN_VERSION "ov58")

set(OCPN_API_VERSION_MAJOR "1")
set(OCPN_API_VERSION_MINOR "18")

set(WX_VER "32")
set(WX_VER_PATCH "8")

set(TP_COMMENT "  * Release using CI")
set(PARENT "opencpn")

set(PROD "prod")
set(BETA "beta")
set(ALPHA "alpha")

set(CLOUDSMITH_BASE_REPOSITORY "signalk_notes_opencpn")
set(CLOUDSMITH_USER "opencpn")

# ---------------------------------------------------------
# Cloudsmith Repo Selection based on Git Tag
# ---------------------------------------------------------

# Default: prod
set(CLOUDSMITH_TARGET_REPO "${CLOUDSMITH_BASE_REPOSITORY}-${PROD}")

# Detect tag from environment (GitHub Actions sets GITHUB_REF)
if(DEFINED ENV{GITHUB_REF})
    set(GIT_REF $ENV{GITHUB_REF})

    message(STATUS "Detected Git ref: ${GIT_REF}")

    if(GIT_REF MATCHES "refs/tags/.*alpha.*")
        message(STATUS "Cloudsmith: alpha release detected")
        set(CLOUDSMITH_TARGET_REPO "${CLOUDSMITH_BASE_REPOSITORY}-${ALPHA}")

    elseif(GIT_REF MATCHES "refs/tags/.*beta.*")
        message(STATUS "Cloudsmith: beta release detected")
        set(CLOUDSMITH_TARGET_REPO "${CLOUDSMITH_BASE_REPOSITORY}-${BETA}")

    elseif(GIT_REF MATCHES "refs/tags/v[0-9]+\\.[0-9]+\\.[0-9]+")
        message(STATUS "Cloudsmith: production release detected")
        set(CLOUDSMITH_TARGET_REPO "${CLOUDSMITH_BASE_REPOSITORY}-${PROD}")

    else()
        message(STATUS "Cloudsmith: no matching tag, using default (prod)")
    endif()
endif()

# Final repo string used by PluginPackage.cmake
set(PLUGIN_CLOUDSMITH_REPO "${CLOUDSMITH_USER}/${CLOUDSMITH_TARGET_REPO}")

message(STATUS "Cloudsmith target repo resolved to: ${PLUGIN_CLOUDSMITH_REPO}")

# ---------------------------------------------------------

set(XML_INFO_URL
    "https://github.com/formifan2002/signalk-notes-opencpn"
)
set(XML_SUMMARY ${SHORT_DESCRIPTION})
set(XML_DESCRIPTION ${LONG_DESCRIPTION})

option(PLUGIN_USE_SVG "Use SVG graphics" ON)
set(CMAKE_CXX_STANDARD 11)

set(USE_LOCAL_GLU FALSE)
set(OpenGL_GL_PREFERENCE "LEGACY")

option(USE_GL "Enable OpenGL support" ON)
message(STATUS "${CMLOC}USE_GL: ${USE_GL}")

if ("${CMAKE_BUILD_TYPE}" STREQUAL "")
  set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose the type of build" FORCE)
endif ()
set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "" "Debug" "Release" "RelWithDebInfo" "MinSizeRel")
message(STATUS "${CMLOC}Build type: ${CMAKE_BUILD_TYPE}")

project(${PACKAGE})
set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include("PluginSetup")

set(CMAKE_VERBOSE_MAKEFILE ON)
option(Plugin_CXX11 "Use c++11" OFF)

include("PluginConfigure")

add_definitions(-DUSE_S57)

set(SRCS
    src/tpicons.cpp
    src/signalk_notes_opencpn_pi.cpp
    src/tpSignalKNotes.cpp
    src/tpConfigDialog.cpp
)

set(HDRS
    include/signalk_notes_opencpn_pi.h
    include/tpicons.h
    include/tpSignalKNotes.h
    include/tpConfigDialog.h
)

add_definitions(-DPLUGIN_USE_SVG)

# ---------------------------------------------------------------------------
# JSON Schema Validator
# ---------------------------------------------------------------------------

if (APPLE)
  option(OD_JSON_SCHEMA_VALIDATOR "Use JSON Schema validator" OFF)
else ()
  option(OD_JSON_SCHEMA_VALIDATOR "Use JSON Schema validator" ON)
endif ()

set(OD_JSON_SCHEMA_VALIDATOR ON)

if (OD_JSON_SCHEMA_VALIDATOR)
  message(STATUS "${CMLOC}Using JSON Schema validation")
  add_definitions(-DOD_JSON_SCHEMA_VALIDATOR)

  add_subdirectory(opencpn-libs/json-schema-validator)
  target_link_libraries(${PACKAGE_NAME} ocpn::json-schema-validator)
else ()
  message(STATUS "${CMLOC}Not using JSON Schema validation")
endif ()

# ---------------------------------------------------------------------------

set(SRC ${SRCS} ${HDRS})

include_directories(BEFORE ${PROJECT_SOURCE_DIR}/include)
include_directories(BEFORE ${PROJECT_SOURCE_DIR}/ocpninclude)

target_sources(${PACKAGE_NAME} PUBLIC ${SRC})

# ---------------------------------------------------------------------------
# Link libraries
# ---------------------------------------------------------------------------

if (NOT OCPN_FLATPAK_CONFIG)

  message(STATUS "${CMLOC}Adding target link libraries to ${PACKAGE_NAME}")

  if (WIN32)
    add_subdirectory(opencpn-libs/WindowsHeaders)
    target_link_libraries(${PACKAGE_NAME} windows::headers)
  endif ()

  add_subdirectory(opencpn-libs/api-${OCPN_API_VERSION_MINOR})
  target_link_libraries(${PACKAGE_NAME} ocpn::api)

  add_subdirectory(opencpn-libs/tinyxml)
  target_link_libraries(${PACKAGE_NAME} ocpn::tinyxml)

  add_subdirectory(opencpn-libs/odapi)
  target_link_libraries(${PACKAGE_NAME} ocpn::odapi)

  add_subdirectory(opencpn-libs/jsonlib)
  target_link_libraries(${PACKAGE_NAME} ocpn::jsonlib)

  add_subdirectory(opencpn-libs/wxJSON)
  target_link_libraries(${PACKAGE_NAME} ocpn::wxjson)

  add_subdirectory(opencpn-libs/plugin_dc)
  target_link_libraries(${PACKAGE_NAME} ocpn::plugin-dc)

endif ()

add_definitions(-DTIXML_USE_STL)

# ---------------------------------------------------------------------------

if (UNIX AND NOT APPLE AND NOT QT_ANDRIOD AND NOT OCPN_FLATPAK_CONFIG AND NOT OCPN_FLATPAK_BUILD)
  if (OD_JSON_SCHEMA_VALIDATOR)
    set(lib_name lib${PACKAGE_NAME}.so)
    find_file(elfedit elfedit)
    if (NOT (${elfedit} STREQUAL "elfedit-NOTFOUND"))
      add_custom_command(
        TARGET ${PACKAGE_NAME}
        POST_BUILD
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        COMMENT "Running post build action on ${lib_name}."
        COMMAND sh -c 'elfedit --input-osabi=Linux --output-osabi=none ${lib_name} || (echo 0)'
      )
    endif ()
  endif ()
endif ()

if (QT_ANDROID)
  include_directories(BEFORE ${qt_android_include})
endif ()

include("PluginInstall")
include("PluginLocalization")
include("PluginPackage")

message(STATUS "${CMLOC}include directories: ")
get_property(dirs DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY INCLUDE_DIRECTORIES)
foreach (dir ${dirs})
  message(STATUS "${CMLOC}dir='${dir}'")
endforeach ()
