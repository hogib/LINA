# LINA feasibility spike: Qt 6 Quick on the two heaviest DAW views (arrangement, piano roll).
# Throwaway code; counterpart of spike/ui-egui on the same synthetic workload.
cmake_minimum_required(VERSION 3.25)
project(lina_ui_qtquick_spike VERSION 0.0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Qt6 6.7 REQUIRED COMPONENTS Core Gui Qml Quick OpenGL QuickControls2)
qt_standard_project_setup(REQUIRES 6.7)

# The document model: plain C++23, no Qt. Kept in its own target so that a Qt include in it
# is a build error rather than a review comment.
add_library(lina_model STATIC src/model.cpp src/model.hpp)
target_include_directories(lina_model PUBLIC src)
target_compile_options(lina_model PRIVATE -Wall -Wextra)

qt_add_executable(lina_ui_spike
    src/main.cpp
    src/bench.cpp src/bench.hpp
    src/sgutil.hpp)

# The .qml files live in qml/ but belong to the module root.
foreach(name Main ArrangementPane PianoRollPane NaiveArrangement)
    set_source_files_properties(qml/${name}.qml PROPERTIES QT_RESOURCE_ALIAS ${name}.qml)
endforeach()

qt_add_qml_module(lina_ui_spike
    URI Lina
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/ArrangementPane.qml
        qml/PianoRollPane.qml
        qml/NaiveArrangement.qml
    SOURCES
        src/controller.cpp src/controller.hpp
        src/arrangement_item.cpp src/arrangement_item.hpp
        src/piano_roll_item.cpp src/piano_roll_item.hpp
        src/clip_list_model.cpp src/clip_list_model.hpp)

target_include_directories(lina_ui_spike PRIVATE src)
target_compile_options(lina_ui_spike PRIVATE -Wall -Wextra)
target_link_libraries(lina_ui_spike PRIVATE
    lina_model Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::OpenGL Qt6::QuickControls2)
