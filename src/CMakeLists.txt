add_library(qtmaildir_lib STATIC
    keymap.cpp
    config.cpp
    mimeparser.cpp
    markdownrenderer.cpp
    messagebuilder.cpp
    requestinterceptor.cpp
    htmlbuilder.cpp
    cidschemehandler.cpp
    cardlayout.cpp
    busyindicator.cpp
    marks.cpp
    carddelegate.cpp
    notmuchworker.cpp
    maildirname.cpp
    draftstore.cpp
    tagchip.cpp
    tagcolors.cpp
    savequerydialog.cpp
    tagdialog.cpp
    tagrules.cpp
    tagrulesdialog.cpp
    tagstrip.cpp
    threadlistmodel.cpp
    threadlistview.cpp
    mailsync.cpp
    syncmonitor.cpp
    threadcidmap.cpp
    messageview.cpp
    messagedetailsdialog.cpp
    mainwindow.cpp
    querycompleter.cpp
    rulequery.cpp
    searchterm.cpp
)

target_include_directories(qtmaildir_lib
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${NOTMUCH_INCLUDE_DIR}
           ${CMAKE_BINARY_DIR}/generated/qtmaildir)

target_link_libraries(qtmaildir_lib
    PUBLIC Qt6::Widgets Qt6::Svg Qt6::WebEngineWidgets PkgConfig::GMIME
           ${NOTMUCH_LIBRARY} PkgConfig::CMARK_GFM
           ${CMARK_GFM_EXTENSIONS_LIBRARY})

# resources.qrc belongs to the executable, not to the static library. A qrc
# compiled into a .a registers itself from a global initialiser, and the linker
# drops that object because nothing references it, so the resource silently
# fails to exist at runtime.
add_executable(qtmaildir main.cpp resources.qrc)
target_link_libraries(qtmaildir PRIVATE qtmaildir_lib)

install(TARGETS qtmaildir RUNTIME DESTINATION bin)

# The .ts is tracked; the .qm is generated and is not. Built beside the binary
# so a build tree runs translated without installing, and installed to the
# AppDataLocation main.cpp searches. Building the .qm without installing it
# reproduces exactly the state item 22 exists to fix, so the install rule is
# part of this and not a follow-up.
if(Qt6LinguistTools_FOUND)
    set(QTMAILDIR_TS ${CMAKE_SOURCE_DIR}/translations/qtmaildir_it_IT.ts)
    set(QTMAILDIR_QM ${CMAKE_CURRENT_BINARY_DIR}/translations/qtmaildir_it_IT.qm)
    add_custom_command(
        OUTPUT ${QTMAILDIR_QM}
        COMMAND ${CMAKE_COMMAND} -E make_directory
                ${CMAKE_CURRENT_BINARY_DIR}/translations
        COMMAND Qt6::lrelease ${QTMAILDIR_TS} -qm ${QTMAILDIR_QM}
        DEPENDS ${QTMAILDIR_TS}
        COMMENT "Compiling Italian translation"
        VERBATIM)
    add_custom_target(qtmaildir_translations ALL DEPENDS ${QTMAILDIR_QM})
    add_dependencies(qtmaildir qtmaildir_translations)
    install(FILES ${QTMAILDIR_QM}
            DESTINATION share/qtmaildir/translations)
else()
    message(STATUS
        "Qt6 LinguistTools not found; building without translations")
endif()

# The icon goes into the hicolor theme under its scalable directory, which is
# where a desktop environment looks for the Icon= name in the .desktop entry.
install(FILES ${CMAKE_SOURCE_DIR}/assets/icons/qtmaildir.svg
        DESTINATION share/icons/hicolor/scalable/apps)
install(FILES ${CMAKE_SOURCE_DIR}/assets/qtmaildir.desktop
        DESTINATION share/applications)
