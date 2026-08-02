#pragma once

#include <QList>

#include "mimeparser.h"

/// One message's place in a rendered thread.
struct ThreadRenderItem
{
    ParsedMessage message;

    /// Matched messages render in full; unmatched collapse to a one-line stub.
    bool expanded = true;

    /// Disambiguates cid: references. Two newsletters in one thread commonly
    /// use the same Content-ID (cid:logo@example.org), which would collide in
    /// a single document, so every reference is rewritten to
    /// cid:<prefix>!<id>.
    ///
    /// Requirement on whatever generates this value: it must never contain
    /// '!'. The separator that makes cid:<prefix>!<id> unambiguous is the
    /// FIRST '!' in the namespaced string; that only holds if the prefix
    /// half is guaranteed free of the character, since the id half is
    /// attacker-controlled and may legitimately contain '!' itself. The
    /// documented "m<index>" form (e.g. "m0", "m1") satisfies this. Enforced
    /// with Q_ASSERT at both places that perform this concatenation
    /// (HtmlBuilder::namespaceCids and CidSchemeHandler::namespacedKey).
    QString cidPrefix;
};

/// Turns parsed messages into the HTML string handed to the web view.
///
/// Plain text goes through the same path as HTML so the view has one render
/// path rather than two. A whole thread renders as ONE document rather than one
/// view per message: a thread of newsletters can hold dozens of messages, and a
/// QWebEngineView each would spawn a Chromium render process each.
class HtmlBuilder
{
public:
    enum Mode {
        PreferHtml,  ///< Use the HTML part when the message has one.
        ForcePlain,  ///< Always render the plain part, escaped.
    };

    /// Single message, used for the error card and for tests.
    static QString build(const ParsedMessage &message, Mode mode);

    /// The whole thread, oldest first.
    static QString buildThread(const QList<ThreadRenderItem> &items, Mode mode);

    /// Rewrites cid: URLs in an HTML body to their namespaced form.
    static QString namespaceCids(const QString &html, const QString &prefix);

private:
    static QString renderPlain(const QString &text);
    static QString renderBody(const ThreadRenderItem &item, Mode mode);
    static QString renderStub(const ParsedMessage &message);
    static QString document(const QString &bodyHtml);
};
