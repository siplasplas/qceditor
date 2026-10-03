cmake_minimum_required(VERSION 3.16)
project(qceditor LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.2 REQUIRED COMPONENTS Core Gui Widgets)
find_package(qt-extra 2 REQUIRED)
find_package(qcodeedit REQUIRED)
find_package(qcodeedit-kate REQUIRED)
find_package(qcodeedit-katedata REQUIRED)

set(SOURCES
    src/main.cpp
    src/mainwindow.cpp
    src/findbar.cpp
    src/editortab.cpp
    src/syntaxdata.cpp
)

set(HEADERS
    src/mainwindow.h
    src/replacecommand.h
    src/findbar.h
    src/editortab.h
    src/syntaxdata.h
)

add_executable(qceditor ${SOURCES} ${HEADERS})

target_include_directories(qceditor PRIVATE src)

target_link_libraries(qceditor PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    qt-extra
    qcodeedit::qcodeedit
    qcodeedit::kate
    qcodeedit::katedata
)
