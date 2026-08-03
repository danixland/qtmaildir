# add_qtmaildir_test(<name>) builds tests/test_<name>.cpp and registers it.
function(add_qtmaildir_test name)
    add_executable(test_${name} test_${name}.cpp)
    target_link_libraries(test_${name} PRIVATE qtmaildir_lib Qt6::Test)
    add_test(NAME ${name} COMMAND test_${name})
endfunction()

add_qtmaildir_test(keymap)
add_qtmaildir_test(config)
add_qtmaildir_test(mimeparser)
target_compile_definitions(test_mimeparser PRIVATE
    FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
add_qtmaildir_test(interceptor)
add_qtmaildir_test(htmlbuilder)
add_qtmaildir_test(notmuchworker)
add_qtmaildir_test(threadlistmodel)
add_qtmaildir_test(mailsync)
add_qtmaildir_test(threadcidmap)
add_qtmaildir_test(mainwindow)
