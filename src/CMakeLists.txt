add_library(qtmaildir_lib STATIC
    keymap.cpp
    config.cpp
    mimeparser.cpp
    requestinterceptor.cpp
    htmlbuilder.cpp
    cidschemehandler.cpp
    cardlayout.cpp
    marks.cpp
    carddelegate.cpp
    notmuchworker.cpp
    tagchip.cpp
    tagcolors.cpp
    tagdialog.cpp
    tagstrip.cpp
    threadlistmodel.cpp
    threadlistview.cpp
    mailsync.cpp
    syncmonitor.cpp
    threadcidmap.cpp
    messageview.cpp
    mainwindow.cpp
    querycompleter.cpp
)

target_include_directories(qtmaildir_lib
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${NOTMUCH_INCLUDE_DIR}
           ${CMAKE_BINARY_DIR}/generated/qtmaildir)

target_link_libraries(qtmaildir_lib
    PUBLIC Qt6::Widgets Qt6::Svg Qt6::WebEngineWidgets PkgConfig::GMIME
           ${NOTMUCH_LIBRARY})

# resources.qrc belongs to the executable, not to the static library. A qrc
# compiled into a .a registers itself from a global initialiser, and the linker
# drops that object because nothing references it, so the resource silently
# fails to exist at runtime.
add_executable(qtmaildir main.cpp resources.qrc)
target_link_libraries(qtmaildir PRIVATE qtmaildir_lib)

install(TARGETS qtmaildir RUNTIME DESTINATION bin)

# The icon goes into the hicolor theme under its scalable directory, which is
# where a desktop environment looks for the Icon= name in the .desktop entry.
install(FILES ${CMAKE_SOURCE_DIR}/assets/icons/qtmaildir.svg
        DESTINATION share/icons/hicolor/scalable/apps)
install(FILES ${CMAKE_SOURCE_DIR}/assets/qtmaildir.desktop
        DESTINATION share/applications)
