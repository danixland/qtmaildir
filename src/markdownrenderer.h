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

#include <QString>

/// Renders the composer's markdown body into the HTML part's fragment.
///
/// A namespace of free functions rather than a class: there is no state, and
/// keeping it painter-free and widget-free is what lets the extension
/// configuration be tested on its own. `MessageBuilder` calls this; nothing
/// else does.
namespace MarkdownRenderer {

/// The markdown source as an HTML fragment: no <html>, <head> or <body>.
///
/// Three extensions are enabled (autolink, strikethrough, tasklist) and
/// tables are deliberately not. Raw HTML in the input is suppressed by
/// cmark-gfm's safe mode, which is the DEFAULT in 0.29 and is not the
/// CMARK_OPT_SAFE flag (a no-op); see markdownrenderer.cpp for the
/// measurement. The requirement is that CMARK_OPT_UNSAFE is never set.
QString toHtml(const QString &markdown);

}  // namespace MarkdownRenderer
