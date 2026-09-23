add_library(m3qt SHARED
    ../../include/m3/qt/editor.h
    presentation.h
    mindmap_controller.h
    mindmap_controller.cpp
    mindmap_view.h
    mindmap_view.cpp
    editor.cpp
    html_export.h
    html_export.cpp
    emoji_line_edit.h
    emoji_line_edit.cpp)
qt_add_resources(m3qt m3_property_styles
    PREFIX "/m3/qt"
    FILES node_properties.qss color_swatch.qss)
qt_add_resources(m3qt m3_emoji_catalog
    PREFIX "/m3/emoji"
    BASE "${PROJECT_SOURCE_DIR}/third_party/unicode"
    FILES ../../third_party/unicode/emoji-test.txt ../../third_party/unicode/LICENSE)
add_library(m3::qt_editor ALIAS m3qt)
target_link_libraries(m3qt PUBLIC Qt6::Widgets PRIVATE m3::core)
target_include_directories(m3qt PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_include_directories(m3qt SYSTEM PRIVATE ../../third_party)
target_compile_definitions(m3qt PRIVATE M3_QT_BUILD_DLL)
target_compile_features(m3qt PUBLIC cxx_std_17)
m3_target_defaults(m3qt)
set_target_properties(m3qt PROPERTIES EXPORT_NAME qt_editor
    AUTOMOC ON CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO
    POSITION_INDEPENDENT_CODE ON C_VISIBILITY_PRESET hidden CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES)

if(APPLE)
    set_target_properties(m3qt PROPERTIES INSTALL_RPATH "@loader_path")
elseif(UNIX)
    set_target_properties(m3qt PROPERTIES INSTALL_RPATH "$ORIGIN")
endif()
install(TARGETS m3qt EXPORT m3QtTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(FILES ../../include/m3/qt/editor.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/m3/qt)
install(EXPORT m3QtTargets NAMESPACE m3:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/m3)

if(M3_BUILD_QT_DEMO)
    find_package(Qt6 6.5 REQUIRED COMPONENTS Network)
    add_executable(m3_qt_demo
        ../../examples/qt_demo.cpp
        ../../examples/qt_demo_window.h
        ../../examples/qt_demo_window.cpp)
    target_link_libraries(m3_qt_demo PRIVATE m3::qt_editor Qt6::Network)
    m3_target_defaults(m3_qt_demo)
    set_target_properties(m3_qt_demo PROPERTIES
        CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO)
    if(APPLE)
        set_target_properties(m3_qt_demo PROPERTIES INSTALL_RPATH "@loader_path/../${CMAKE_INSTALL_LIBDIR}")
    elseif(UNIX)
        set_target_properties(m3_qt_demo PROPERTIES INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
    endif()
    install(TARGETS m3_qt_demo RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    if(WIN32)
        qt_generate_deploy_app_script(TARGET m3_qt_demo OUTPUT_SCRIPT _m3_qt_deploy_script)
        # Qt's deploy helper requires an absolute prefix, unlike cmake --install.
        install(CODE [[get_filename_component(CMAKE_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}" ABSOLUTE)]])
        install(SCRIPT "${_m3_qt_deploy_script}")
    endif()
endif()
