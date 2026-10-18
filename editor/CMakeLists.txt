# qceditor-editor: the editor shared by qceditor and other programs: editor
# tabs with search and replace, Kate syntax, encodings and spelling. Each
# program keeps the editor's settings in its own place (qceditor::Settings).

add_library(qceditor-editor STATIC
    src/EditorTab.cpp
    src/FindBar.cpp
    src/EncodingChoiceDialog.cpp
    src/SpellLanguageDialog.cpp
    src/SyntaxData.cpp
    src/Settings.cpp
    src/ReplaceCommand.h
    include/qceditor/EditorTab.h
    include/qceditor/FindBar.h
    include/qceditor/EncodingChoiceDialog.h
    include/qceditor/SpellLanguageDialog.h
    include/qceditor/SyntaxData.h
    include/qceditor/Settings.h
)

target_include_directories(qceditor-editor
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(qceditor-editor
    PUBLIC
        Qt6::Widgets
        qcodeedit::qcodeedit
        qcodeedit::kate
        qcodeedit::encoding
    PRIVATE
        qcodeedit::spell
        qcodeedit::diff
)

add_library(qceditor::editor ALIAS qceditor-editor)
set_target_properties(qceditor-editor PROPERTIES EXPORT_NAME editor)

# --- Install: find_package(qceditor) gives qceditor::editor ----------------

install(TARGETS qceditor-editor
    EXPORT qceditorTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(FILES
    include/qceditor/EditorTab.h
    include/qceditor/FindBar.h
    include/qceditor/EncodingChoiceDialog.h
    include/qceditor/SpellLanguageDialog.h
    include/qceditor/SyntaxData.h
    include/qceditor/Settings.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/qceditor
)
install(EXPORT qceditorTargets
    FILE qceditorTargets.cmake
    NAMESPACE qceditor::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/qceditor
)

include(CMakePackageConfigHelpers)
configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/qceditorConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/qceditorConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/qceditor
)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/qceditorConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/qceditorConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/qceditorConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/qceditor
)
