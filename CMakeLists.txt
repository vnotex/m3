cmake_minimum_required(VERSION 3.18)
project(m3 VERSION 0.1.0 LANGUAGES C CXX)

set(_m3_top_level OFF)
if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
    set(_m3_top_level ON)
endif()
option(M3_BUILD_SHARED "Build m3 as a shared library" ON)
option(M3_BUILD_TESTS "Build m3 tests" ${_m3_top_level})
option(M3_BUILD_QT "Build the Qt editor library" OFF)
option(M3_BUILD_QT_DEMO "Build the Qt editor demo" ${M3_BUILD_QT})
if(M3_BUILD_QT_DEMO AND NOT M3_BUILD_QT)
    message(FATAL_ERROR "M3_BUILD_QT_DEMO requires M3_BUILD_QT")
endif()
if(M3_BUILD_SHARED)
    add_library(m3core SHARED src/api.cpp)
    target_compile_definitions(m3core PRIVATE M3_BUILD_DLL PUBLIC M3_SHARED)
else()
    add_library(m3core STATIC src/api.cpp)
endif()
target_sources(m3core PRIVATE src/model.cpp src/json_codec.cpp src/layout.cpp)
add_library(m3::core ALIAS m3core)
set_target_properties(m3core PROPERTIES EXPORT_NAME core
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO
    POSITION_INDEPENDENT_CODE ON C_VISIBILITY_PRESET hidden CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES)
function(m3_target_defaults target)
    set_target_properties(${target} PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
        RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /utf-8 $<$<COMPILE_LANGUAGE:CXX>:/EHsc>)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()
m3_target_defaults(m3core)
target_include_directories(m3core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
# Private, unmodified nlohmann/json 3.11.3 at 9cca280a4d0ccf0c08f47a99aa71d1b0e52f8d03.
target_include_directories(m3core SYSTEM PRIVATE third_party)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
install(TARGETS m3core EXPORT m3Targets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/m3 DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    PATTERN "qt" EXCLUDE)
install(FILES LICENSE third_party/nlohmann/LICENSE.MIT DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/m3)
if(M3_BUILD_QT)
    install(FILES third_party/lucide/LICENSE
        DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/m3 RENAME lucide-LICENSE)
    find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets)
    add_subdirectory(src/qt)
endif()
configure_package_config_file(cmake/m3Config.cmake.in "${CMAKE_CURRENT_BINARY_DIR}/m3Config.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/m3)
write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/m3ConfigVersion.cmake"
    VERSION ${PROJECT_VERSION} COMPATIBILITY SameMajorVersion)
install(EXPORT m3Targets NAMESPACE m3:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/m3)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/m3Config.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/m3ConfigVersion.cmake" DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/m3)
if(M3_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
