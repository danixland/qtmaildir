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
#include <QFile>
#include <QGuiApplication>
#include <QRegularExpression>
#include <QUrl>

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

HtmlBuilder::BrandPalette HtmlBuilder::brandPaletteFrom(const QPalette &palette)
{
    // Base, the same surface paletteFrom() reads, so the placeholder and a
    // rendered message never disagree about which way round the theme is.
    const bool dark = palette.color(QPalette::Base).lightnessF() < 0.5;

    BrandPalette brand;
    if (dark) {
        brand.background   = QColor(0x06, 0x0b, 0x10);
        brand.backgroundIn = QColor(0x0c, 0x15, 0x20);
        brand.grid         = QColor(0x18, 0x28, 0x40);
        brand.tile         = QColor(0x10, 0x1e, 0x2d);
        brand.tileBorder   = QColor(0x18, 0x28, 0x40);
        brand.accent       = QColor(0xa8, 0x55, 0xf7);
        brand.accentEdge   = QColor(0x7c, 0x3a, 0xed);
        brand.title        = QColor(0xc4, 0xd6, 0xe8);
        brand.subtitle     = QColor(0x7a, 0x9b, 0xb8);
        brand.glowAlpha    = 16;
    } else {
        // **Not the mockup's light values as written.** Rendered side by side
        // with the dark set, three of them did not survive contact with a real
        // pane, because the same nominal contrast behaves differently against
        // white than against near-black:
        //
        // - The grid at #d9dfe8 on white is about a 2% luminance step and
        //   vanished entirely, where #182840 on #060b10 reads clearly. Darkened
        //   here, and the opacity is raised for this set alone below.
        // - The glow SUBTRACTS light on white instead of adding it, so 14%
        //   washed most of the pane purple rather than hinting at a glow. The
        //   dark set keeps 16 for the opposite reason.
        // - The tile at #f0f3f7 inside a #d9dfe8 border did not separate from
        //   the background, leaving the icon floating.
        //
        // The hues are the mockup's throughout; only their strength changed.
        brand.background   = QColor(0xff, 0xff, 0xff);
        brand.backgroundIn = QColor(0xf4, 0xf6, 0xfa);
        brand.grid         = QColor(0xb9, 0xc4, 0xd4);
        brand.tile         = QColor(0xf0, 0xf3, 0xf7);
        brand.tileBorder   = QColor(0xc7, 0xd0, 0xdd);
        brand.accent       = QColor(0x93, 0x33, 0xea);
        brand.accentEdge   = QColor(0x7c, 0x3a, 0xed);
        brand.title        = QColor(0x1f, 0x29, 0x37);
        brand.subtitle     = QColor(0x37, 0x41, 0x51);
        brand.glowAlpha    = 6;
        brand.gridOpacity  = 45;
    }
    return brand;
}

namespace {

/// A bundled font as a data: URI.
///
/// The mockup reaches Google Fonts with an @import, which the interceptor
/// blocks by design and which would be a network request from a mail client
/// besides. The faces ship in the binary and are inlined here, so the document
/// fetches nothing at all.
QString fontDataUri(const char *resource)
{
    QFile file(QString::fromLatin1(resource));
    if (!file.open(QIODevice::ReadOnly)) {
        // Falls back to the generic family in the font stack rather than
        // failing to render. A missing resource is a build fault, not
        // something the user can act on mid-session.
        return QString();
    }
    return QStringLiteral("data:font/woff2;base64,")
        + QString::fromLatin1(file.readAll().toBase64());
}

/// rgba() from a colour and a percentage, for the translucent washes.
QString rgba(const QColor &c, int percent)
{
    return QStringLiteral("rgba(%1,%2,%3,%4)")
        .arg(c.red()).arg(c.green()).arg(c.blue())
        .arg(percent / 100.0);
}

/// The placeholder's stylesheet.
///
/// Sizes are clamped rather than fixed or purely fluid: this is a splitter
/// panel whose width varies from a couple of hundred pixels to most of a
/// screen. A fixed wordmark is lost in a wide pane and overflows a narrow one;
/// a purely fluid one is unreadable at one end and absurd at the other. The
/// vw term scales it, the clamp bounds stop it going either way.
///
/// Substituted by NAME, not by QString::arg positions. The stylesheet is full
/// of CSS percentages, and **arg() does NOT collapse "%%" into "%"**: every
/// percentage written that way stayed literally "50%%", which is invalid, so
/// the browser dropped each declaration containing one. That silently killed
/// the mask, the glow and both radial gradients while the pane still rendered
/// and still looked plausible, and a probe measuring only the properties
/// without percentages reported everything correct. Named tokens cannot
/// collide with a percent sign at all.
const char *kPlaceholderStyleTemplate = R"CSS(
@font-face { font-family: 'Oxanium qtmaildir'; font-weight: 800;
             font-style: normal; font-display: block;
             src: url('@FONT_OXANIUM@') format('woff2'); }
@font-face { font-family: 'Plex qtmaildir'; font-weight: 400;
             font-style: normal; font-display: block;
             src: url('@FONT_PLEX@') format('woff2'); }
* { margin: 0; padding: 0; box-sizing: border-box; }
html, body { width: 100%; height: 100%; overflow: hidden;
             background: @BG@; }
.bg { position: absolute; inset: 0;
      background: radial-gradient(circle at 30% 20%, @BG_IN@ 0%, @BG@ 55%, @BG@ 100%);
      display: flex; align-items: center; justify-content: center; }
/* The grid and the glow are sized RELATIVE TO THE PANE, which is the one place
   this departs from the mockup's numbers rather than porting them.

   The mockup draws into a fixed 1920x1080 box and scales the whole box to fit.
   Its mask is `circle at 50% 45%` fading out by 70%, which in a box that shape
   means the fade completes well inside the frame and the grid dissolves into
   darkness around the lockup. Taking those same values into a box the size of
   this pane keeps the RATIO but loses the effect: `circle` with no explicit
   extent resolves to farthest-corner, so in a pane roughly 990x650 the fade
   only completes past the corners and the grid reads as uniform to the edges,
   which is what it looked like. The same applies to the 900px glow, which is
   larger than a short pane is tall, so its falloff never appears.

   `closest-side` pins the fade to the nearer edge instead, so the vignette
   completes inside the pane at any splitter position, and the glow is a
   percentage of the pane rather than a pixel count. */
.grid { position: absolute; inset: 0;
        background-image: linear-gradient(@GRID@ 1px, transparent 1px),
                          linear-gradient(90deg, @GRID@ 1px, transparent 1px);
        background-size: 64px 64px; opacity: @GRID_OPACITY@;
        mask-image: radial-gradient(closest-side circle at 50% 45%,
                    rgba(0,0,0,0.9) 0%, transparent 75%);
        -webkit-mask-image: radial-gradient(closest-side circle at 50% 45%,
                    rgba(0,0,0,0.9) 0%, transparent 75%); }
.glow { position: absolute;
        width: min(95%, 900px); aspect-ratio: 1;
        background: radial-gradient(circle, @GLOW@ 0%, transparent 70%);
        top: 50%; left: 50%; transform: translate(-50%, -55%); }
/* Natural height, centred. The user rejected a fixed vertical split: this
   pane's height varies enormously and a ratio breaks at one extreme. */
.content { position: relative; z-index: 2; width: 100%;
           padding: 0 clamp(12px, 4vw, 48px);
           display: flex; flex-direction: column; align-items: center;
           gap: clamp(10px, 2.2vw, 22px); }
/* Header ROW, per the decision of 2026-08-07: icon left, wordmark right,
   with the glow and grid still centred behind. */
.lockup { display: flex; align-items: center;
          gap: clamp(10px, 2.4vw, 26px); }
.icon-tile { flex: none;
             width: clamp(48px, 11vw, 104px); height: clamp(48px, 11vw, 104px);
             border-radius: clamp(12px, 2.6vw, 26px);
             background: @TILE@; border: 1px solid @TILE_BORDER@;
             display: flex; align-items: center; justify-content: center;
             box-shadow: 0 0 60px @TILE_SHADOW@; }
.icon-tile svg { width: 68%; height: 68%; }
.wordmark { display: flex; flex-direction: column; gap: 0.25em; }
.title { font-family: 'Oxanium qtmaildir', sans-serif; font-weight: 800;
         font-size: clamp(26px, 6.4vw, 60px); letter-spacing: 0.01em;
         color: @TITLE@; line-height: 1; white-space: nowrap; }
.title .accent { color: @ACCENT@; }
.subtitle { font-family: 'Plex qtmaildir', sans-serif; font-weight: 400;
            font-size: clamp(9px, 1.7vw, 15px); letter-spacing: 0.04em;
            color: @SUBTITLE@; text-transform: uppercase; }
.helpers { display: flex; flex-wrap: wrap; justify-content: center;
           gap: clamp(8px, 1.8vw, 18px);
           font-family: 'Plex qtmaildir', sans-serif;
           font-size: clamp(10px, 1.8vw, 15px); }
.helpers a, .helpers span { color: @SUBTITLE@; text-decoration: none;
                            border-bottom: 1px solid transparent;
                            padding-bottom: 1px; }
.helpers a:hover { color: @ACCENT@; border-bottom-color: @ACCENT@; }
.footer { font-family: 'Plex qtmaildir', sans-serif;
          font-size: clamp(8px, 1.4vw, 12px); color: @SUBTITLE@; opacity: 0.75;
          text-align: center; line-height: 1.6; }
.footer a { color: @SUBTITLE@; text-decoration: none;
            border-bottom: 1px solid @GRID@; }
.footer a:hover { color: @ACCENT@; }
)CSS";

} // namespace

QString HtmlBuilder::buildPlaceholder(const QList<PlaceholderHelper> &helpers,
                                      const QString &version,
                                      const BrandPalette &brand)
{
    QString style = QString::fromUtf8(kPlaceholderStyleTemplate);
    const QList<QPair<QString, QString>> tokens = {
        { QStringLiteral("@BG@"),           brand.background.name() },
        { QStringLiteral("@BG_IN@"),        brand.backgroundIn.name() },
        { QStringLiteral("@GRID@"),         brand.grid.name() },
        { QStringLiteral("@TILE_BORDER@"),  brand.tileBorder.name() },
        { QStringLiteral("@GRID_OPACITY@"),
          QString::number(brand.gridOpacity / 100.0) },
        { QStringLiteral("@TILE@"),         brand.tile.name() },
        { QStringLiteral("@ACCENT@"),       brand.accent.name() },
        { QStringLiteral("@TITLE@"),        brand.title.name() },
        { QStringLiteral("@SUBTITLE@"),     brand.subtitle.name() },
        { QStringLiteral("@GLOW@"),         rgba(brand.accent, brand.glowAlpha) },
        { QStringLiteral("@TILE_SHADOW@"),  rgba(brand.accent, brand.glowAlpha - 4) },
        { QStringLiteral("@FONT_OXANIUM@"), fontDataUri(":/fonts/Oxanium-ExtraBold.woff2") },
        { QStringLiteral("@FONT_PLEX@"),    fontDataUri(":/fonts/IBMPlexSans-Regular.woff2") },
    };
    for (const auto &token : tokens)
        style.replace(token.first, token.second);

    QString helperHtml;
    for (const PlaceholderHelper &helper : helpers) {
        const QString label = helper.label.toHtmlEscaped();
        if (helper.query.isEmpty()) {
            // A state, not a search. Rendering it as a link would invite a
            // click that runs an empty query and empties the thread list.
            helperHtml += QStringLiteral("<span>%1</span>").arg(label);
            continue;
        }

        // Percent-encoded into the URL, then escaped into the attribute. A
        // saved query is user-written and reaches here verbatim, so both
        // layers are needed: one keeps it a single URL, the other keeps it
        // inside the attribute.
        const QString href = QString::fromLatin1(
            QUrl::toPercentEncoding(helper.query)).toHtmlEscaped();
        helperHtml += QStringLiteral(
            "<a href=\"qtmaildir-query:%1\">%2</a>").arg(href, label);
    }

    return QStringLiteral(
        "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
        "<style>%1</style></head><body><div class=\"bg\">"
        "<div class=\"grid\"></div><div class=\"glow\"></div>"
        "<div class=\"content\">"
        "<div class=\"lockup\">"
        "<div class=\"icon-tile\">"
        "<svg viewBox=\"0 0 256 256\" xmlns=\"http://www.w3.org/2000/svg\">"
        "<path d=\"M100,70 L176,70 A14,14 0 0 1 190,84 L190,172 "
        "A14,14 0 0 1 176,186 L100,186 L40,128 Z\" fill=\"%2\" stroke=\"%3\" "
        "stroke-width=\"6\" stroke-linejoin=\"round\"/>"
        "<circle cx=\"100\" cy=\"128\" r=\"15\" fill=\"%4\"/>"
        "</svg></div>"
        "<div class=\"wordmark\">"
        "<div class=\"title\">qt<span class=\"accent\">Mail</span>Dir</div>"
        "<div class=\"subtitle\">%5</div>"
        "</div></div>"
        "<div class=\"helpers\">%6</div>"
        "<div class=\"footer\">%7<br>"
        "<a href=\"https://danix.xyz/qtmaildir\">danix.xyz/qtmaildir</a>"
        "</div></div></div></body></html>")
        .arg(style, brand.accent.name(), brand.accentEdge.name(),
             brand.tile.name(),
             QCoreApplication::translate(
                 "HtmlBuilder", "local mail, tagged and searched"),
             helperHtml,
             QCoreApplication::translate(
                 "HtmlBuilder", "Copyright &copy; 2026 Danilo M. &middot; "
                                "version %1").arg(version.toHtmlEscaped()));
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
