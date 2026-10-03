cmake_minimum_required(VERSION 3.16)
project(qceditor LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.2 REQUIRED COMPONENTS Core Gui Widgets)
find_package(qcodeedit REQUIRED)
find_package(qcodeedit-kate REQUIRED)
find_package(qcodeedit-katedata REQUIRED)

set(SOURCES
    src/main.cpp
    src/mainwindow.cpp
    src/editortab.cpp
    src/syntaxdata.cpp
    src/widgets/mrutabwidget.cpp
    src/widgets/Ev.cpp
)

set(HEADERS
    src/mainwindow.h
    src/editortab.h
    src/syntaxdata.h
    src/widgets/mrutabwidget.h
    src/widgets/Ev.h
)

add_executable(qceditor ${SOURCES} ${HEADERS})

target_include_directories(qceditor PRIVATE src)

target_link_libraries(qceditor PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    qcodeedit::qcodeedit
    qcodeedit::kate
    qcodeedit::katedata
)
