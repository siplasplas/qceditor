cmake_minimum_required(VERSION 3.16)
project(qceditor VERSION 1.4.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

include(GNUInstallDirs)

# A program that fetches qceditor for its editor library (e.g.
# gemini-commander) does not build the application.
option(QCEDITOR_BUILD_APP "Build the QCEditor application" ${PROJECT_IS_TOP_LEVEL})

find_package(Qt6 6.2 REQUIRED COMPONENTS Core Gui Widgets)
# Targets a parent project already has (e.g. fetched) are not looked for again.
# 2.7: qxMigrateToSiplasplas() moves older versions' files at start-up
if(NOT TARGET qt-extra)
    find_package(qt-extra 2.7 REQUIRED)
endif()
# 1.13: Kate data in ~/.local/share/kate-data, qce::kate::migrateDataRoot()
# 1.14: encoding detection with confidence, alternatives and preview
# 1.15.1: spell checking (qcodeedit-spell, hunspell), CodeEditArea::cursorAt()
# 1.17: insertText(), createStandardContextMenu()
# 1.19: qcodeedit-diff, a reload changes only the differing lines as an undo step
# 1.20.1: spelling suggestions over all forms of the dictionary, usually one
# 1.21: corrections of a space missing or misplaced (phraseCorrections)
foreach(package qcodeedit qcodeedit-kate qcodeedit-katedata qcodeedit-encoding qcodeedit-spell qcodeedit-diff)
    string(REPLACE "qcodeedit-" "" component ${package})
    if(package STREQUAL "qcodeedit")
        set(component qcodeedit)
    endif()
    if(NOT TARGET qcodeedit::${component})
        find_package(${package} 1.21.0 REQUIRED)
    endif()
endforeach()

# The editor itself, shared with other programs (e.g. gemini-commander).
add_subdirectory(editor)

# The application: a thin layer over the editor library (its window, plus
# moving older versions' files and keeping the recent files in its data
# directory).
if(QCEDITOR_BUILD_APP)
    add_executable(qceditor src/main.cpp)
    target_compile_definitions(qceditor PRIVATE QCEDITOR_VERSION="${PROJECT_VERSION}")
    target_link_libraries(qceditor PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        qt-extra
        qceditor::editor
        qcodeedit::katedata
    )
endif()
