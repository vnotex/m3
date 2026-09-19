cmake_minimum_required(VERSION 3.18)
project(m3_qt_consumer LANGUAGES CXX)
set(M3_PACKAGE_PREFIX "" CACHE STRING "Installed prefix relative to the repository root")
if(NOT M3_PACKAGE_PREFIX OR IS_ABSOLUTE "${M3_PACKAGE_PREFIX}")
    message(FATAL_ERROR "M3_PACKAGE_PREFIX must be a nonempty repository-relative installed prefix")
endif()
get_filename_component(_m3_prefix "${M3_PACKAGE_PREFIX}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_LIST_DIR}/../..")
find_package(m3 CONFIG REQUIRED COMPONENTS qt_editor PATHS "${_m3_prefix}/lib/cmake/m3" NO_DEFAULT_PATH)
add_executable(m3_qt_consumer main.cpp)
target_link_libraries(m3_qt_consumer PRIVATE m3::qt_editor)
set_target_properties(m3_qt_consumer PROPERTIES
    CXX_EXTENSIONS NO AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
if(MSVC)
    target_compile_options(m3_qt_consumer PRIVATE /W4 /utf-8 /EHsc)
else()
    target_compile_options(m3_qt_consumer PRIVATE -Wall -Wextra -Wpedantic)
endif()
if(WIN32)
    add_custom_command(TARGET m3_qt_consumer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:m3::qt_editor>" "$<TARGET_FILE_DIR:m3_qt_consumer>"
        VERBATIM)
    get_target_property(_m3_type m3::core TYPE)
    if(_m3_type STREQUAL "SHARED_LIBRARY")
        add_custom_command(TARGET m3_qt_consumer POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:m3::core>" "$<TARGET_FILE_DIR:m3_qt_consumer>"
            VERBATIM)
    endif()
endif()
