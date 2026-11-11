# Fast offscreen tests: the editor tabs with the find bar module built along
# with the application.
find_package(Qt6 6.2 REQUIRED COMPONENTS Test)

add_executable(test_find_plugin test_find_plugin.cpp)
target_link_libraries(test_find_plugin PRIVATE
    Qt6::Test
    Qt6::Widgets
    qceditor::editor
    siplasplas::plugin-api
)
target_compile_definitions(test_find_plugin PRIVATE
    SPP_FINDBAR_PLUGIN_FILE="$<TARGET_FILE:spp-findbar>")
add_dependencies(test_find_plugin spp-findbar)
add_test(NAME test_find_plugin COMMAND test_find_plugin)
