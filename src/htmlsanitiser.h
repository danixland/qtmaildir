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

/// Makes a stranger's HTML safe to put INSIDE A MESSAGE WE SEND.
///
/// Item 171. This is not the message pane's problem restated: the pane renders
/// hostile markup TO THE USER, behind protections that live in `MessageView`
/// (an off-the-record profile, JavaScript disabled, and `RequestInterceptor`
/// blocking every request by default and failing closed). **None of those
/// apply to a forward.** The markup leaves this process and is rendered by
/// somebody else's client, under their policy, on their machine, and the
/// interceptor cannot help because it intercepts requests WE would make.
///
/// So the sanitising happens to the bytes, before `MessageSender` sees them,
/// and there is no second line of defence behind it. Forwarding a tracking
/// pixel forwards the tracking: the original sender learns that the forwarded
/// copy was opened, by whom and how often, and the recipient never agreed to
/// that.
///
/// **This is an ALLOW-LIST, and the distinction from `namespaceCids()` is the
/// whole design.** That function is a block-list: it names the attributes that
/// can carry a `cid:` and rewrites those, and it documents scoping `srcset=`
/// out because its quoting grammar differs. That trade is correct for
/// rewriting and wrong here, because the two failures are not comparable:
///
/// | | a missed reference means |
/// |---|---|
/// | `namespaceCids` (rewrite) | one broken image |
/// | this (strip) | a beacon reaching the recipient |
///
/// Anything not recognised is therefore REMOVED rather than kept. A new
/// attribute, an unanticipated quoting form, a `srcset`, a CSS `image-set()`,
/// an `@import`: each is handled by the default, not by having been enumerated
/// in advance.
///
/// The invariant every test asserts, and the one to preserve under any edit:
/// **after sanitising, no attribute value and no CSS construct contains a URL
/// whose scheme is anything but `cid:`.**
namespace HtmlSanitiser {

/// \p html with every remote-fetching construct removed.
///
/// `cid:` references are KEPT: they travel inside the message, fetch nothing,
/// and are what lets an inline logo survive a forward. Structural and
/// presentational markup is kept too, `style=""` included, with its remote
/// constructs removed.
///
/// Removed: any attribute value carrying a non-`cid:` URL (`http:`, `https:`,
/// protocol-relative `//host/path`, `data:`, `file:`); the elements that exist
/// to fetch or redirect (`link`, `script`, `iframe`, `object`, `embed`,
/// `base`, and `meta http-equiv="refresh"`); CSS `url()`, `@import` and
/// `image-set()` naming anything but a `cid:`; and event-handler attributes.
///
/// A stripped `<img>` leaves a gap. That is correct, and must not be papered
/// over with a placeholder that itself fetches.
QString stripRemoteContent(const QString &html);

/// True when \p html carries anything `stripRemoteContent()` would remove.
///
/// Drives the composer's control: the checkbox is only worth showing for a
/// message that actually has remote content. Never used to DECIDE whether to
/// strip, only whether to offer the choice.
bool hasRemoteContent(const QString &html);

} // namespace HtmlSanitiser
