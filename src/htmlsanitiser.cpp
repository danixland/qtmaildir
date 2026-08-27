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

#include "htmlsanitiser.h"

#include <QRegularExpression>
#include <QSet>
#include <QStringList>

namespace {

/// Elements that exist to fetch or redirect, removed with their content.
///
/// `script` and `style` carry their payload as TEXT, so emptying an attribute
/// would leave the fetch intact; the element goes whole. `style` is here even
/// though CSS is otherwise kept, because a block can carry `@import` and
/// `url()` and rewriting inside it is the same problem as the attribute case
/// with different terminators. Presentational styling survives through
/// `style=""` attributes, which are handled per attribute below.
const QSet<QString> &elementsRemovedWhole()
{
    static const QSet<QString> set = {
        QStringLiteral("script"), QStringLiteral("style"),
        QStringLiteral("iframe"), QStringLiteral("object"),
        QStringLiteral("embed"),  QStringLiteral("applet"),
        QStringLiteral("frame"),  QStringLiteral("frameset"),
    };
    return set;
}

/// Void elements that fetch or redirect and have no closing tag.
const QSet<QString> &voidElementsRemoved()
{
    static const QSet<QString> set = {
        QStringLiteral("link"), QStringLiteral("base"),
        QStringLiteral("meta"),
    };
    return set;
}

/// True when \p value names something that would be fetched from off-message.
///
/// The test is on the SCHEME, never on a substring: a `cid:` id may legitimately
/// contain "https" (`cid:https-logo@example.org`), and stripping that would
/// destroy a valid inline reference.
///
/// Everything that is not plainly a `cid:` or a same-document fragment counts
/// as remote. That is the allow-list rule: an unanticipated scheme is remote by
/// default rather than by enumeration.
bool valueIsRemote(const QString &value)
{
    const QString trimmed = value.trimmed();
    if (trimmed.isEmpty())
        return false;

    // Protocol-relative: no scheme, still fetches, and a check for "http"
    // misses it entirely.
    if (trimmed.startsWith(QLatin1String("//")))
        return true;

    // A same-document fragment or a bare relative path fetches nothing new in
    // a mail context and carries no scheme to judge.
    const qsizetype colon = trimmed.indexOf(QLatin1Char(':'));
    if (colon < 0)
        return !trimmed.startsWith(QLatin1Char('#')) ? false : false;

    const QString scheme = trimmed.left(colon).toLower();

    // A colon can appear in a relative path with no scheme before it; a scheme
    // is letters, digits, '+', '-', '.' only.
    static const QRegularExpression schemeShape(
        QStringLiteral("^[a-z][a-z0-9+.-]*$"));
    if (!schemeShape.match(scheme).hasMatch())
        return false;

    return scheme != QLatin1String("cid");
}

/// True when a CSS fragment reaches the network.
///
/// `url()`, `@import` and `image-set()` all fetch, in a `style=""` attribute
/// and in a block alike. The bare `url(...)` form terminates on ')' rather
/// than on a quote, which is why this is a pass of its own rather than the
/// attribute logic reused.
bool cssIsRemote(const QString &css)
{
    static const QRegularExpression url(
        QStringLiteral("url\\s*\\(\\s*['\"]?([^'\")]*)"),
        QRegularExpression::CaseInsensitiveOption);

    auto it = url.globalMatch(css);
    while (it.hasNext()) {
        if (valueIsRemote(it.next().captured(1)))
            return true;
    }

    static const QRegularExpression atImport(
        QStringLiteral("@import\\s+['\"]?([^'\";]*)"),
        QRegularExpression::CaseInsensitiveOption);
    auto imports = atImport.globalMatch(css);
    while (imports.hasNext()) {
        const QString target = imports.next().captured(1).trimmed();
        // `@import url(...)` is already covered by the url() pass; a bare
        // `@import "x.css"` is not.
        if (!target.startsWith(QLatin1String("url"), Qt::CaseInsensitive)
            && valueIsRemote(target)) {
            return true;
        }
    }

    // image-set() wraps url() in every real spelling, so the url() pass above
    // covers it; a bare image-set("x.png") is caught here.
    static const QRegularExpression imageSet(
        QStringLiteral("image-set\\s*\\(\\s*['\"]([^'\"]*)"),
        QRegularExpression::CaseInsensitiveOption);
    auto sets = imageSet.globalMatch(css);
    while (sets.hasNext()) {
        if (valueIsRemote(sets.next().captured(1)))
            return true;
    }

    return false;
}

/// One attribute, as parsed out of a tag.
struct Attribute
{
    QString name;   ///< Lowercased.
    QString raw;    ///< The whole `name="value"` source, to re-emit verbatim.
    QString value;  ///< Unquoted.
};

/// Splits the inside of a tag into its attributes.
///
/// Tolerates the three quoting forms and whitespace or newlines around '=',
/// all of which are real in mail and each of which alone defeats a naive
/// pattern.
QList<Attribute> parseAttributes(const QString &inner)
{
    QList<Attribute> out;

    static const QRegularExpression attr(
        QStringLiteral("([a-zA-Z_:][-a-zA-Z0-9_:.]*)"      // name
                       "(?:\\s*=\\s*"                       // = with space
                       "(?:\"([^\"]*)\"|'([^']*)'|([^\\s>]+))"  // 3 quotings
                       ")?"));

    auto it = attr.globalMatch(inner);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        Attribute a;
        a.name = m.captured(1).toLower();
        a.raw = m.captured(0);
        for (int group : { 2, 3, 4 }) {
            if (m.hasCaptured(group)) {
                a.value = m.captured(group);
                break;
            }
        }
        out.append(a);
    }

    return out;
}

/// True when this attribute must not survive, whatever element carries it.
bool attributeIsUnsafe(const Attribute &attr)
{
    // Event handlers. The recipient's client most likely disables scripting,
    // but that is their policy and not ours to assume for them.
    if (attr.name.startsWith(QLatin1String("on")))
        return true;

    // CSS reaches the network from inside a style attribute.
    if (attr.name == QLatin1String("style"))
        return cssIsRemote(attr.value);

    // **The allow-list.** Every other attribute is judged by its VALUE rather
    // than by whether the name was enumerated. This is the difference from
    // HtmlBuilder::namespaceCids(), which names the attributes it rewrites and
    // scopes srcset out: a missed rewrite is a broken image, a missed strip is
    // a beacon. srcset, poster, data-*, and whatever HTML adds next are all
    // handled here by default.
    //
    // srcset carries a LIST of "url descriptor" pairs, so each entry is
    // judged; a single remote entry condemns the attribute.
    for (const QString &piece : attr.value.split(QLatin1Char(','))) {
        const QString candidate = piece.trimmed().section(QLatin1Char(' '), 0, 0);
        if (valueIsRemote(candidate))
            return true;
    }

    return false;
}

/// The shared walk. \p report is called for anything that would be removed;
/// when \p rewrite is false the walk only reports, which is what
/// hasRemoteContent() needs.
QString walk(const QString &html, bool *foundOut)
{
    QString out;
    out.reserve(html.size());
    bool found = false;

    static const QRegularExpression tag(QStringLiteral("<(/?)([a-zA-Z][^\\s/>]*)([^>]*)>"));

    // Matched by hand from `pos` rather than with globalMatch(): skipping a
    // removed element's CONTENT moves pos forward, and globalMatch iterates
    // over matches found against the original string, so it would hand back
    // tags from inside the region just skipped. That produced duplicated
    // output and a surviving iframe, caught by
    // aFetchingElementIsRemovedWhole().
    qsizetype pos = 0;
    while (true) {
        const QRegularExpressionMatch m = tag.match(html, pos);
        if (!m.hasMatch())
            break;

        out += html.mid(pos, m.capturedStart() - pos);
        pos = m.capturedEnd();

        const bool closing = !m.captured(1).isEmpty();
        const QString name = m.captured(2).toLower();
        const QString inner = m.captured(3);

        if (elementsRemovedWhole().contains(name)) {
            found = true;
            if (!closing) {
                // Drop the content too: for script and style it IS the payload.
                const QRegularExpression until(
                    QStringLiteral("</\\s*%1\\s*>").arg(name),
                    QRegularExpression::CaseInsensitiveOption);
                const QRegularExpressionMatch end = until.match(html, pos);
                pos = end.hasMatch() ? end.capturedEnd() : html.size();
            }
            continue;
        }

        if (voidElementsRemoved().contains(name)) {
            // meta is only dangerous as a refresh; the charset declaration is
            // ordinary and harmless.
            if (name == QLatin1String("meta")) {
                bool refresh = false;
                for (const Attribute &a : parseAttributes(inner)) {
                    if (a.name == QLatin1String("http-equiv")
                        && a.value.trimmed().compare(QLatin1String("refresh"),
                                                     Qt::CaseInsensitive) == 0) {
                        refresh = true;
                    }
                }
                if (!refresh) {
                    out += m.captured(0);
                    continue;
                }
            }
            found = true;
            continue;
        }

        if (closing) {
            out += m.captured(0);
            continue;
        }

        // Rebuild the tag from the attributes that survive.
        QStringList kept;
        bool dropped = false;
        for (const Attribute &a : parseAttributes(inner)) {
            if (attributeIsUnsafe(a)) {
                dropped = true;
                continue;
            }
            kept.append(a.raw);
        }

        if (dropped)
            found = true;

        // An <img> whose src was the thing removed would render as a broken
        // image icon in the recipient's client, which is noisier than the gap
        // the design asks for. Drop the element instead, and only when it has
        // nothing left to show.
        if (dropped && name == QLatin1String("img")) {
            bool hasSrc = false;
            for (const QString &k : kept) {
                if (k.startsWith(QLatin1String("src"), Qt::CaseInsensitive))
                    hasSrc = true;
            }
            if (!hasSrc)
                continue;
        }

        QString rebuilt = QStringLiteral("<") + m.captured(2);
        if (!kept.isEmpty())
            rebuilt += QLatin1Char(' ') + kept.join(QLatin1Char(' '));
        if (inner.trimmed().endsWith(QLatin1Char('/')))
            rebuilt += QLatin1Char('/');
        rebuilt += QLatin1Char('>');
        out += rebuilt;
    }

    out += html.mid(pos);

    if (foundOut)
        *foundOut = found;
    return out;
}

} // namespace

QString HtmlSanitiser::stripRemoteContent(const QString &html)
{
    return walk(html, nullptr);
}

bool HtmlSanitiser::hasRemoteContent(const QString &html)
{
    bool found = false;
    walk(html, &found);
    return found;
}
