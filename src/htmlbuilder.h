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

#pragma once

#include <QColor>
#include <QList>
#include <QPalette>

#include "mimeparser.h"

/// One message's place in a rendered thread.
struct ThreadRenderItem
{
    ParsedMessage message;

    /// Matched messages render in full; unmatched collapse to a one-line stub.
    bool expanded = true;

    /// Whether the message is flagged, for the mark beside the subject in the
    /// message pane's header (item 70).
    ///
    /// Carried here rather than derived from `message`, because it comes from
    /// the notmuch TAGS and ParsedMessage holds only what the MIME parser found
    /// in the file. MessageRef already answers both, so this costs no query.
    /// Unused by the generated HTML itself: the header is a QLabel above the
    /// web view, not part of the sandboxed document.
    bool flagged = false;

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

    /// The colours the document's own stylesheet uses.
    ///
    /// Passed in rather than read from qApp inside the builder, so the CSS can
    /// be tested against a known palette without a running application, and so
    /// nothing here depends on widget state.
    ///
    /// **Scope.** These style the chrome around messages and the plain-text
    /// render. A message that brings its own HTML brings its own colours, and
    /// those are deliberately left alone: rewriting a sender's styling would
    /// break layouts that depend on it, and a newsletter that sets a white
    /// background is entitled to stay white.
    struct Palette {
        QColor background;  ///< The pane itself.
        QColor text;        ///< Body text.
        QColor dim;         ///< Headers and stubs: present but secondary.
        QColor border;      ///< Rules between messages.
        QColor quote;       ///< Quoted lines in plain text.
    };

    /// The brand colours of the placeholder pane.
    ///
    /// **A deliberate exception to the Palette above**, which derives from the
    /// desktop theme. A logo is brand rather than chrome, so these are the
    /// values from the user's mockup and are not blended toward anything. The
    /// desktop theme still decides WHICH set is used, so the pane never renders
    /// a light lockup on a dark desktop.
    struct BrandPalette {
        QColor background;   ///< The pane, behind the radial wash.
        QColor backgroundIn; ///< The lighter centre of that wash.
        QColor grid;         ///< Grid rules, and the icon tile's border.
        QColor tile;         ///< The icon tile's fill.
        QColor tileBorder;   ///< The icon tile's edge. Separate from `grid`,
                             ///< which needs a different strength on light.
        QColor accent;       ///< "Mail" in the wordmark, and the envelope.
        QColor accentEdge;   ///< The envelope's stroke.
        QColor title;        ///< The wordmark, apart from the accent span.
        QColor subtitle;     ///< The tagline, the helpers and the footer.

        /// Percent alpha of the accent glow. Deliberately different between the
        /// two sets: on dark the glow adds light and can be generous, on light
        /// it subtracts and the same value washes the whole pane.
        int glowAlpha = 0;

        /// Percent opacity of the grid, for the same reason.
        int gridOpacity = 35;
    };

    /// Picks the dark or the light brand set from the desktop palette.
    ///
    /// Decided on the window's Base lightness, the same surface the document
    /// Palette reads, so the two agree about which way round the theme is.
    static BrandPalette brandPaletteFrom(const QPalette &palette);

    /// One helper line under the wordmark: a count, and the query it runs.
    ///
    /// An empty query renders as text rather than as a link, which is what the
    /// sync line uses: it reports a state rather than naming a search.
    struct PlaceholderHelper {
        QString label;  ///< Already-translated, e.g. "12 unread".
        QString query;  ///< notmuch query, or empty for a non-link line.
    };

    /// The pane shown when no thread is displayed.
    ///
    /// Rendered into the same web view as a message rather than into a second
    /// widget stacked behind it, so there is one document path and one set of
    /// security rules.
    static QString buildPlaceholder(const QList<PlaceholderHelper> &helpers,
                                    const QString &version,
                                    const BrandPalette &brand);

    /// Derives the document palette from a widget palette.
    ///
    /// The dim and border colours are blends rather than fixed greys, which is
    /// what makes this work on a dark theme: a hardcoded #555 that reads as
    /// "subtle" on white is nearly invisible on near-black.
    static Palette paletteFrom(const QPalette &palette);

    /// The palette used when a caller supplies none: the running application's.
    /// Falls back to a light default with no QApplication, which only happens
    /// in a test that did not ask for a palette.
    static Palette defaultPalette();

    /// Single message, used for the error card and for tests.
    static QString build(const ParsedMessage &message, Mode mode);
    static QString build(const ParsedMessage &message, Mode mode,
                         const Palette &palette);

    /// The whole thread, oldest first.
    static QString buildThread(const QList<ThreadRenderItem> &items, Mode mode);
    static QString buildThread(const QList<ThreadRenderItem> &items, Mode mode,
                               const Palette &palette);

    /// Rewrites cid: URLs in an HTML body to their namespaced form.
    static QString namespaceCids(const QString &html, const QString &prefix);

private:
    static QString renderPlain(const QString &text);
    static QString renderBody(const ThreadRenderItem &item, Mode mode);
    static QString renderStub(const ParsedMessage &message);
    static QString document(const QString &bodyHtml, const Palette &palette);
    static QString styleSheet(const Palette &palette);
};
