add_library(qtmaildir_lib STATIC
    keymap.cpp
    config.cpp
    mimeparser.cpp
    requestinterceptor.cpp
    htmlbuilder.cpp
    cidschemehandler.cpp
    notmuchworker.cpp
    threadlistmodel.cpp
    mailsync.cpp
    threadcidmap.cpp
    messageview.cpp
    mainwindow.cpp
)

target_include_directories(qtmaildir_lib
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${NOTMUCH_INCLUDE_DIR})

target_link_libraries(qtmaildir_lib
    PUBLIC Qt6::Widgets Qt6::WebEngineWidgets PkgConfig::GMIME ${NOTMUCH_LIBRARY})

add_executable(qtmaildir main.cpp)
target_link_libraries(qtmaildir PRIVATE qtmaildir_lib)

install(TARGETS qtmaildir RUNTIME DESTINATION bin)
