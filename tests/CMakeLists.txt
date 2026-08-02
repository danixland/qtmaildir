# add_qtmaildir_test(<name>) builds tests/test_<name>.cpp and registers it.
function(add_qtmaildir_test name)
    add_executable(test_${name} test_${name}.cpp)
    target_link_libraries(test_${name} PRIVATE qtmaildir_lib Qt6::Test)
    add_test(NAME ${name} COMMAND test_${name})
endfunction()

add_qtmaildir_test(keymap)
