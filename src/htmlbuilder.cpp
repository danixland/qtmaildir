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

#include <QCoreApplication>
#include <QGuiApplication>
#include <QRegularExpression>

namespace {

/// The stylesheet, with every colour supplied by the caller.
///
/// %1 background, %2 text, %3 quote, %4 border, %5 dim.
const char *kStyleTemplate = R"CSS(
body { font-family: sans-serif; font-size: 10pt; margin: 12px;
       background: %1; color: %2; }
pre.plain { white-space: pre-wrap; word-wrap: break-word;
            font-family: monospace; margin: 0; }
span.quote { color: %3; }
.message { border-top: 1px solid %4; padding: 10px 0; }
.message:first-child { border-top: none; }
.msg-header { font-size: 9pt; color: %5; margin-bottom: 8px; }
.msg-header .who { font-weight: bold; color: %2; }
.stub { font-size: 9pt; color: %5; padding: 4px 0;
        border-top: 1px solid %4; }
)CSS";

/// Mixes two colours, `weight` being how much of `a` survives.
///
/// Blending is what makes the derived colours theme-correct. A fixed grey is
/// only "subtle" against the background it was chosen for: #555 reads as a
/// quiet label on white and nearly vanishes on near-black.
QColor blend(const QColor &a, const QColor &b, qreal weight)
{
    const qreal inverse = 1.0 - weight;
    return QColor::fromRgbF(a.redF() * weight + b.redF() * inverse,
                            a.greenF() * weight + b.greenF() * inverse,
                            a.blueF() * weight + b.blueF() * inverse);
}

} // namespace

HtmlBuilder::Palette HtmlBuilder::paletteFrom(const QPalette &palette)
{
    // Base and Text, not Window and WindowText: the pane is a content surface
    // like a text edit, and on many themes Base differs from Window.
    const QColor background = palette.color(QPalette::Base);
    const QColor text = palette.color(QPalette::Text);

    Palette result;
    result.background = background;
    result.text = text;

    // Both derived from the pair, so they land at the right contrast whichever
    // way round the theme is.
    result.dim = blend(text, background, 0.6);
    result.border = blend(text, background, 0.25);

    // The quote colour keeps its hue, since "this is quoted" is carried by the
    // colour being different rather than by it being dimmer, but it is pulled
    // toward the background so it stays readable on a dark theme instead of
    // glowing.
    const QColor quoteHue(0x4a, 0x6f, 0x8a);
    result.quote = background.lightnessF() < 0.5
        ? blend(quoteHue.lighter(160), background, 0.75)
        : blend(quoteHue, background, 0.85);

    return result;
}

HtmlBuilder::Palette HtmlBuilder::defaultPalette()
{
    if (const QGuiApplication *app =
            qobject_cast<QGuiApplication *>(QCoreApplication::instance()))
        return paletteFrom(app->palette());

    // No GUI application: only reachable from a test that did not pass a
    // palette. A default-constructed QPalette is light, which matches what
    // this code did before it was themed at all.
    return paletteFrom(QPalette());
}

QString HtmlBuilder::styleSheet(const Palette &palette)
{
    return QString::fromUtf8(kStyleTemplate)
        .arg(palette.background.name(), palette.text.name(),
             palette.quote.name(), palette.border.name(),
             palette.dim.name());
}

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

QString HtmlBuilder::document(const QString &bodyHtml, const Palette &palette)
{
    return QStringLiteral(
        "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
        "<style>%1</style></head><body>%2</body></html>")
        .arg(styleSheet(palette), bodyHtml);
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
    return build(message, mode, defaultPalette());
}

QString HtmlBuilder::build(const ParsedMessage &message, Mode mode,
                           const Palette &palette)
{
    ThreadRenderItem item;
    item.message = message;
    item.expanded = true;
    return document(renderBody(item, mode), palette);
}

QString HtmlBuilder::buildThread(const QList<ThreadRenderItem> &items, Mode mode)
{
    return buildThread(items, mode, defaultPalette());
}

QString HtmlBuilder::buildThread(const QList<ThreadRenderItem> &items, Mode mode,
                                 const Palette &palette)
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

    return document(body, palette);
}
