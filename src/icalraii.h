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

#include <libical/ical.h>

#include <cstdlib>
#include <memory>

/// Owners for libical handles, the calendar's counterpart to nmraii.h.
///
/// The same rule as notmuch's: no libical pointer leaves calendarstore.cpp,
/// the one file that includes this header. unique_ptr rather than NmHandle's
/// template because every libical handle here has exactly one destroy
/// function and no aliasing, which is what unique_ptr already expresses.
struct IcalComponentFree {
    void operator()(icalcomponent *c) const { icalcomponent_free(c); }
};
using IcalComponent = std::unique_ptr<icalcomponent, IcalComponentFree>;

struct IcalRecurFree {
    void operator()(icalrecur_iterator *i) const { icalrecur_iterator_free(i); }
};
using IcalRecurIterator = std::unique_ptr<icalrecur_iterator, IcalRecurFree>;

/// For strings returned by the `*_r` functions, which the caller must free().
/// The plain variants return a ring buffer libical reuses, so a pointer kept
/// from one is overwritten by a later call.
struct IcalStringFree {
    void operator()(char *s) const { std::free(s); }
};
using IcalString = std::unique_ptr<char, IcalStringFree>;
