# add_qtmaildir_test(<name>) builds tests/test_<name>.cpp and registers it.
#
# resources.qrc is compiled into each test binary for the same reason the app
# compiles it rather than the static library: a qrc inside a .a registers from
# a global initialiser the linker then drops. Library code reads :/fonts/ when
# it builds the placeholder pane, so a test that never links the qrc would
# exercise only the missing-resource fallback and pass against a broken build.
function(add_qtmaildir_test name)
    add_executable(test_${name} test_${name}.cpp ${CMAKE_SOURCE_DIR}/src/resources.qrc)
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
add_qtmaildir_test(tagcolors)
add_qtmaildir_test(threadlistmodel)
add_qtmaildir_test(mailsync)
add_qtmaildir_test(syncmonitor)
add_qtmaildir_test(threadcidmap)
add_qtmaildir_test(mainwindow)
add_qtmaildir_test(messageview)
add_qtmaildir_test(querycompleter)
add_qtmaildir_test(tagdialog)
