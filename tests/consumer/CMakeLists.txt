cmake_minimum_required(VERSION 3.18)
project(m3_consumer LANGUAGES C CXX)
set(M3_PACKAGE_PREFIX "" CACHE STRING "Installed prefix relative to the repository root")
if(NOT M3_PACKAGE_PREFIX OR IS_ABSOLUTE "${M3_PACKAGE_PREFIX}")
    message(FATAL_ERROR "M3_PACKAGE_PREFIX must be a nonempty repository-relative installed prefix")
endif()
get_filename_component(_m3_prefix "${M3_PACKAGE_PREFIX}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_LIST_DIR}/../..")
find_package(m3 CONFIG REQUIRED PATHS "${_m3_prefix}/lib/cmake/m3" NO_DEFAULT_PATH)
add_executable(m3_consumer ../test_c_api.c)
target_link_libraries(m3_consumer PRIVATE m3::core)
set_target_properties(m3_consumer PROPERTIES
    C_STANDARD 99 C_STANDARD_REQUIRED YES C_EXTENSIONS NO LINKER_LANGUAGE CXX
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
if(MSVC)
    target_compile_options(m3_consumer PRIVATE /W4 /utf-8)
else()
    target_compile_options(m3_consumer PRIVATE -Wall -Wextra -Wpedantic)
endif()
get_target_property(_m3_type m3::core TYPE)
if(WIN32 AND _m3_type STREQUAL "SHARED_LIBRARY")
    add_custom_command(TARGET m3_consumer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:m3::core>" "$<TARGET_FILE_DIR:m3_consumer>"
        VERBATIM)
endif()
