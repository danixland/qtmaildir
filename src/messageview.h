#pragma once

#include <QList>
#include <QUrl>
#include <QWidget>

#include "htmlbuilder.h"
#include "mimeparser.h"

class QLabel;
class QPushButton;
class QWebEngineView;
class QWebEngineProfile;
class CidSchemeHandler;
class RequestInterceptor;

/// The message pane: thread header, body, attachment bar.
///
/// A whole thread renders into one web view. A newsletter thread can hold
/// dozens of messages, and one view per message would spawn one Chromium
/// render process per message.
class MessageView : public QWidget
{
    Q_OBJECT
public:
    explicit MessageView(QWidget *parent = nullptr);
    ~MessageView() override;

    /// The base URL every document in this pane is loaded with, and the only
    /// qtmaildir: URL the interceptor trusts. Defined once so setHtml() and
    /// setDocumentUrl() cannot drift apart: if they ever disagree, the
    /// interceptor fails closed and the pane renders nothing at all.
    static QUrl documentUrl() { return QUrl(QStringLiteral("qtmaildir://message")); }

    /// Renders a whole thread, oldest first. Items whose expanded flag is
    /// false collapse to a one-line stub.
    void showThread(const QList<ThreadRenderItem> &items);

    void showError(const QString &text, const QString &filePath);
    void clear();

public slots:
    void toggleHtml();
    void loadRemoteContent();

signals:
    void statusMessage(const QString &text);

private:
    void render();
    void updateHeader();
    void setDocument(const QString &html);

    QList<ThreadRenderItem> m_items;
    bool m_preferHtml = true;

    QWebEngineProfile *m_profile = nullptr;
    QWebEngineView *m_view = nullptr;
    RequestInterceptor *m_interceptor = nullptr;
    CidSchemeHandler *m_cidHandler = nullptr;

    QLabel *m_headerLabel = nullptr;
    QLabel *m_blockedLabel = nullptr;
    QPushButton *m_loadRemoteButton = nullptr;
    QWidget *m_attachmentBar = nullptr;
};
