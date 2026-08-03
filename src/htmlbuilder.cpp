/*
 * qtmaildir - a Qt6 mail client for notmuch-indexed Maildirs
 * Copyright (C) 2026 Danilo M. <danix@danix.xyz>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "htmlbuilder.h"

#include <QRegularExpression>

namespace {

const char *kStyle = R"CSS(
body { font-family: sans-serif; font-size: 10pt; margin: 12px; }
pre.plain { white-space: pre-wrap; word-wrap: break-word;
            font-family: monospace; margin: 0; }
span.quote { color: #4a6f8a; }
.message { border-top: 1px solid #bbb; padding: 10px 0; }
.message:first-child { border-top: none; }
.msg-header { font-size: 9pt; color: #555; margin-bottom: 8px; }
.msg-header .who { font-weight: bold; color: #000; }
.stub { font-size: 9pt; color: #666; padding: 4px 0;
        border-top: 1px solid #ddd; }
)CSS";

} // namespace

QString HtmlBuilder::renderPlain(const QString &text)
{
    QString out;
    out += QStringLiteral("<pre class=\"plain\">");

    const QStringList lines = text.split(QLatin1Char('\n'));
    for (int i = 0; i < lines.size(); ++i) {
        const QString &line = lines.at(i);
        const bool quoted = line.startsWith(QLatin1Char('>'));

        if (quoted)
            out += QStringLiteral("<span class=\"quote\">");
        out += line.toHtmlEscaped();
        if (quoted)
            out += QStringLiteral("</span>");

        if (i + 1 < lines.size())
            out += QLatin1Char('\n');
    }

    out += QStringLiteral("</pre>");
    return out;
}

QString HtmlBuilder::document(const QString &bodyHtml)
{
    return QStringLiteral(
        "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
        "<style>%1</style></head><body>%2</body></html>")
        .arg(QString::fromUtf8(kStyle), bodyHtml);
}

QString HtmlBuilder::namespaceCids(const QString &html, const QString &prefix)
{
    if (prefix.isEmpty())
        return html;

    // The '!' separator is only unambiguous if prefix itself never contains
    // one: the FIRST '!' in "cid:<prefix>!<id>" is always taken as the
    // separator, so a hostile Content-ID containing '!' only extends the id
    // half, never collides with a different prefix. This concatenation is
    // performed independently in two places (here and
    // CidSchemeHandler::namespacedKey); neither trusts the other to have
    // checked it, so both assert it directly. Q_ASSERT is compiled out in
    // release builds — the property that matters there is pinned by
    // TestHtmlBuilder::namespacedKeyRejectsPrefixContainingSeparator instead.
    Q_ASSERT_X(!prefix.contains(QLatin1Char('!')), "HtmlBuilder::namespaceCids",
               "cidPrefix must never contain '!': it is the separator, and a "
               "prefix containing one would make the split ambiguous");

    // This runs on the sender's raw, unescaped HTML markup (not on text that
    // has been through toHtmlEscaped()), so no double-escaping happens here;
    // it is purely a URL rewrite over the existing markup.
    //
    // Two independent patterns are needed:
    //
    // 1. Attribute values: src=, href=, background=, poster= (the common
    //    real-world attributes that can carry a cid: reference in HTML
    //    email), quoted with " or ', quoted the other way, or entirely
    //    unquoted (<img src=cid:x> is valid HTML and unquoted references are
    //    seen in the wild). Attribute name is matched case-insensitively and
    //    whitespace/newlines are tolerated around '='.
    //
    // 2. CSS url(cid:...): appears both inside a style="" attribute value
    //    and inside a <style> block, quoted or unquoted. This is handled as
    //    a separate pass since it has different quoting/terminator rules
    //    (a bare url(...) form terminates on ')', not on a matching quote).
    //
    // srcset= is not handled: it is a list of URL/descriptor pairs with a
    // different quoting grammar, and cid: URLs in srcset are not something
    // real-world mail has been observed to use; treating it is out of scope
    // for this task.
    QString out = html;

    {
        // (?:"([^"]*)"|'([^']*)'|([^\s"'<>]+)) picks the correctly-bounded
        // value for whichever quoting style is used, so a quoted value can
        // never be captured past its own closing quote (fixing the greedy
        // [^"']+ that the naive version used, which is provably safe here
        // since each alternative's character class already excludes its own
        // terminator). capturedStart(N) (rather than testing the captured
        // text for emptiness) is what disambiguates which alternative
        // matched, so an empty-but-present cid ("cid:\"\"") is handled
        // correctly too.
        static const QRegularExpression re(
            QStringLiteral(
                "\\b(src|href|background|poster)"
                "\\s*=\\s*"
                "(?:\"cid:([^\"]*)\"|'cid:([^']*)'|cid:([^\\s\"'<>]+))"),
            QRegularExpression::CaseInsensitiveOption);

        QString rewritten;
        qsizetype last = 0;
        auto it = re.globalMatch(out);
        while (it.hasNext()) {
            const QRegularExpressionMatch m = it.next();
            QString id;
            QChar quote;
            if (m.capturedStart(2) != -1) {
                id = m.captured(2);
                quote = QLatin1Char('"');
            } else if (m.capturedStart(3) != -1) {
                id = m.captured(3);
                quote = QLatin1Char('\'');
            } else {
                id = m.captured(4);
            }

            rewritten += out.mid(last, m.capturedStart() - last);
            rewritten += m.captured(1);
            rewritten += QStringLiteral("=");
            if (!quote.isNull())
                rewritten += quote;
            rewritten += QStringLiteral("cid:%1!%2").arg(prefix, id);
            if (!quote.isNull())
                rewritten += quote;
            last = m.capturedEnd();
        }
        rewritten += out.mid(last);
        out = rewritten;
    }

    {
        // CSS url(cid:...), quoted (' or ") or bare, inside a style="" value
        // or a <style> block. Bare form terminates at ')'.
        static const QRegularExpression re(
            QStringLiteral(
                "url\\(\\s*"
                "(?:\"cid:([^\"]*)\"|'cid:([^']*)'|cid:([^)\\s]+))"
                "\\s*\\)"),
            QRegularExpression::CaseInsensitiveOption);

        QString rewritten;
        qsizetype last = 0;
        auto it = re.globalMatch(out);
        while (it.hasNext()) {
            const QRegularExpressionMatch m = it.next();
            QString id;
            QChar quote;
            if (m.capturedStart(1) != -1) {
                id = m.captured(1);
                quote = QLatin1Char('"');
            } else if (m.capturedStart(2) != -1) {
                id = m.captured(2);
                quote = QLatin1Char('\'');
            } else {
                id = m.captured(3);
            }

            rewritten += out.mid(last, m.capturedStart() - last);
            rewritten += QStringLiteral("url(");
            if (!quote.isNull())
                rewritten += quote;
            rewritten += QStringLiteral("cid:%1!%2").arg(prefix, id);
            if (!quote.isNull())
                rewritten += quote;
            rewritten += QStringLiteral(")");
            last = m.capturedEnd();
        }
        rewritten += out.mid(last);
        out = rewritten;
    }

    return out;
}

QString HtmlBuilder::renderBody(const ThreadRenderItem &item, Mode mode)
{
    if (mode == PreferHtml && item.message.hasHtml())
        return namespaceCids(item.message.htmlBody, item.cidPrefix);
    return renderPlain(item.message.plainBody);
}

QString HtmlBuilder::renderStub(const ParsedMessage &message)
{
    return QStringLiteral("<div class=\"stub\">%1 &mdash; %2</div>")
        .arg(message.from.toHtmlEscaped(), message.subject.toHtmlEscaped());
}

QString HtmlBuilder::build(const ParsedMessage &message, Mode mode)
{
    ThreadRenderItem item;
    item.message = message;
    item.expanded = true;
    return document(renderBody(item, mode));
}

QString HtmlBuilder::buildThread(const QList<ThreadRenderItem> &items, Mode mode)
{
    QString body;

    for (int i = 0; i < items.size(); ++i) {
        const ThreadRenderItem &item = items.at(i);

        if (!item.expanded) {
            body += renderStub(item.message);
            continue;
        }

        body += QStringLiteral(
            "<div class=\"message\" id=\"msg-%1\">"
            "<div class=\"msg-header\"><span class=\"who\">%2</span><br>%3</div>"
            "%4</div>")
            .arg(QString::number(i),
                 item.message.from.toHtmlEscaped(),
                 item.message.date.toHtmlEscaped(),
                 renderBody(item, mode));
    }

    return document(body);
}
