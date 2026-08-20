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

// cmark-gfm's headers are C and carry no Qt interaction, so the gmime
// include-order rule does not apply here. They still go first, for consistency
// with mimeparser.cpp.
#include <cmark-gfm.h>
#include <cmark-gfm-core-extensions.h>

#include "markdownrenderer.h"

#include <QByteArray>

#include <cstdlib>

namespace {

/// The extensions this application enables, by cmark-gfm's own names.
///
/// `table` is absent deliberately, not by oversight: tables render badly
/// across mail clients regardless of who generates them. `tagfilter` is absent
/// because safe mode (see below) already suppresses raw HTML wholesale, which
/// is the stronger measure.
const char *const kExtensions[] = { "autolink", "strikethrough", "tasklist" };

}  // namespace

QString MarkdownRenderer::toHtml(const QString &markdown)
{
    if (markdown.isEmpty())
        return {};

    // Idempotent and required before cmark_find_syntax_extension() can resolve
    // any name. Calling it per render rather than once at startup keeps this
    // function free of initialisation order concerns; it is a hash lookup
    // after the first call.
    cmark_gfm_core_extensions_ensure_registered();

    // CMARK_OPT_DEFAULT is 0, and CMARK_OPT_SAFE is a NO-OP in cmark-gfm 0.29:
    // safe mode has been the default since that release, and the flag is kept
    // only for API compatibility with code written against older versions.
    // The real requirement is that CMARK_OPT_UNSAFE must never be set. Under
    // safe mode a raw <script> block is replaced with an HTML comment
    // placeholder, and a link whose scheme is not in the allowed set
    // (javascript:, vbscript:, file:, and data: except a few safe image
    // types) is replaced with an empty href. Measured against
    // cmark-gfm-0.29.0.gfm.13 on 2026-08-20: rendering the same script tag and
    // a javascript: link under OPT_DEFAULT alone, under OPT_DEFAULT|OPT_SAFE,
    // and under OPT_UNSAFE shows the first two behave identically and
    // suppress both, while OPT_UNSAFE leaks both verbatim into the output.
    // OPT_SAFE is kept anyway, both as a statement of intent and in case a
    // future cmark-gfm release makes it meaningful again; do not read its
    // presence as the mechanism actually doing the suppressing.
    const int options = CMARK_OPT_DEFAULT | CMARK_OPT_SAFE;

    cmark_parser *parser = cmark_parser_new(options);
    if (!parser)
        return {};

    for (const char *name : kExtensions) {
        // A missing extension is a broken installation rather than a
        // condition to handle: the library was found by CMake. Skipping it
        // degrades to plain CommonMark rather than crashing.
        if (cmark_syntax_extension *extension = cmark_find_syntax_extension(name))
            cmark_parser_attach_syntax_extension(parser, extension);
    }

    const QByteArray utf8 = markdown.toUtf8();
    cmark_parser_feed(parser, utf8.constData(), static_cast<size_t>(utf8.size()));

    cmark_node *document = cmark_parser_finish(parser);
    if (!document) {
        cmark_parser_free(parser);
        return {};
    }

    // The extension list must be passed to the renderer as well as to the
    // parser. Passing nullptr here parses the tasklist correctly and then
    // renders it as a plain list item, which looks like the extension never
    // worked.
    char *html = cmark_render_html(document, options,
                                   cmark_parser_get_syntax_extensions(parser));
    const QString result = html ? QString::fromUtf8(html) : QString();

    free(html);
    cmark_node_free(document);
    cmark_parser_free(parser);

    return result;
}
