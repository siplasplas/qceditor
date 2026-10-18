cmake_minimum_required(VERSION 3.16)
project(qceditor VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

include(GNUInstallDirs)

find_package(Qt6 6.2 REQUIRED COMPONENTS Core Gui Widgets)
# 2.7: qxMigrateToSiplasplas() moves older versions' files at start-up
find_package(qt-extra 2.7 REQUIRED)
# 1.13: Kate data in ~/.local/share/kate-data, qce::kate::migrateDataRoot()
# 1.14: encoding detection with confidence, alternatives and preview
# 1.15.1: spell checking (qcodeedit-spell, hunspell), CodeEditArea::cursorAt()
find_package(qcodeedit 1.15.1 REQUIRED)
find_package(qcodeedit-kate 1.15.1 REQUIRED)
find_package(qcodeedit-katedata 1.15.1 REQUIRED)
find_package(qcodeedit-encoding 1.15.1 REQUIRED)
find_package(qcodeedit-spell 1.15.1 REQUIRED)

# The editor itself, shared with other programs (e.g. gemini-commander).
add_subdirectory(editor)

# The application: a thin layer over the editor library.
add_executable(qceditor
    src/main.cpp
    src/mainwindow.cpp
    src/mainwindow.h
)

target_include_directories(qceditor PRIVATE src)

target_link_libraries(qceditor PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    qt-extra
    qceditor::editor
    qcodeedit::katedata
)
