cmake_minimum_required(VERSION 3.21)
project(HEXFORGE VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets Concurrent)

add_library(forge STATIC
    src/document.cpp
    src/editor.cpp
    src/formats.cpp
    src/analysis.cpp
    src/window.cpp
)
target_include_directories(forge PUBLIC src)
target_link_libraries(forge PUBLIC Qt6::Widgets Qt6::Concurrent)

add_executable(HEXFORGE MACOSX_BUNDLE src/main.cpp)
target_link_libraries(HEXFORGE PRIVATE forge)
set_target_properties(HEXFORGE PROPERTIES
    MACOSX_BUNDLE_GUI_IDENTIFIER org.hexforge.editor
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
)

include(CTest)
if(BUILD_TESTING)
    add_executable(checks tests/checks.cpp)
    target_link_libraries(checks PRIVATE forge)
    add_test(NAME core COMMAND checks)

    add_executable(analysis_checks tests/analysis.cpp)
    target_link_libraries(analysis_checks PRIVATE forge)
    add_test(NAME analysis COMMAND analysis_checks)

    add_executable(format_checks tests/formats.cpp)
    target_link_libraries(format_checks PRIVATE forge)
    add_test(NAME formats COMMAND format_checks "${CMAKE_CURRENT_SOURCE_DIR}/examples/sample.macho")

    add_executable(gui_checks tests/gui.cpp)
    target_link_libraries(gui_checks PRIVATE forge)
    add_test(NAME editor_workflow COMMAND gui_checks)
    set_tests_properties(editor_workflow PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
