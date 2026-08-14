# add_qtmaildir_test(<name>) builds tests/test_<name>.cpp and registers it.
#
# resources.qrc is compiled into each test binary for the same reason the app
# compiles it rather than the static library: a qrc inside a .a registers from
# a global initialiser the linker then drops. Library code reads :/fonts/ when
# it builds the placeholder pane, so a test that never links the qrc would
# exercise only the missing-resource fallback and pass against a broken build.
#
# Every test runs under the OFFSCREEN platform, set here rather than left to
# whatever the invoking session happens to use. This is not tidiness: a test
# that renders needs deterministic geometry, and a real windowing system does
# not provide it.
#
# The case that forced it (item 52). theDescriptionSurvivesAModestPopupWidth
# sizes a line edit to 550px and asserts a description survives the popup that
# results. Under Wayland the compositor gave the popup a viewport of 1278x0: the
# zero height made the grab return a null pixmap, which is what failed, but the
# 1278 width is the worse half. Had the grab succeeded, the test would have
# measured a popup more than twice the width it claims to be testing and passed
# while proving nothing about the 550px case.
#
# So the platform is pinned for correctness, not to dodge a red result. `ctest`
# sets no QT_QPA_PLATFORM of its own, so without this the suite's verdict
# depends on how it was invoked: it passed for a developer exporting `offscreen`
# by hand and failed under `ctest` in the same tree, which cost one wrong
# diagnosis before the cause was found.
function(add_qtmaildir_test name)
    add_executable(test_${name} test_${name}.cpp ${CMAKE_SOURCE_DIR}/src/resources.qrc)
    target_link_libraries(test_${name} PRIVATE qtmaildir_lib Qt6::Test)
    add_test(NAME ${name} COMMAND test_${name})
    set_tests_properties(${name} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
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
add_qtmaildir_test(cardlayout)
add_qtmaildir_test(marks)
add_qtmaildir_test(carddelegate)
add_qtmaildir_test(threadlistmodel)
add_qtmaildir_test(mailsync)
add_qtmaildir_test(syncmonitor)
add_qtmaildir_test(threadcidmap)
add_qtmaildir_test(mainwindow)
add_qtmaildir_test(messageview)
add_qtmaildir_test(querycompleter)
add_qtmaildir_test(tagdialog)
add_qtmaildir_test(tagrules)
add_qtmaildir_test(rulequery)
add_qtmaildir_test(searchterm)
add_qtmaildir_test(tagstrip)
