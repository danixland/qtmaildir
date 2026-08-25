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

#include <notmuch.h>

#include <utility>

/// Generic owner for a notmuch handle with a destroy function.
template <typename T, void (*Destroy)(T *)>
class NmHandle
{
public:
    NmHandle() = default;
    explicit NmHandle(T *handle) : m_handle(handle) {}

    ~NmHandle() { reset(); }

    NmHandle(const NmHandle &) = delete;
    NmHandle &operator=(const NmHandle &) = delete;

    NmHandle(NmHandle &&other) noexcept
        : m_handle(std::exchange(other.m_handle, nullptr)) {}

    NmHandle &operator=(NmHandle &&other) noexcept
    {
        if (this != &other) {
            reset();
            m_handle = std::exchange(other.m_handle, nullptr);
        }
        return *this;
    }

    void reset(T *handle = nullptr)
    {
        if (m_handle)
            Destroy(m_handle);
        m_handle = handle;
    }

    T *get() const { return m_handle; }
    explicit operator bool() const { return m_handle != nullptr; }

private:
    T *m_handle = nullptr;
};

using NmQuery    = NmHandle<notmuch_query_t, notmuch_query_destroy>;
using NmThreads  = NmHandle<notmuch_threads_t, notmuch_threads_destroy>;
using NmMessages = NmHandle<notmuch_messages_t, notmuch_messages_destroy>;
using NmThread   = NmHandle<notmuch_thread_t, notmuch_thread_destroy>;
using NmMessage  = NmHandle<notmuch_message_t, notmuch_message_destroy>;
using NmTags     = NmHandle<notmuch_tags_t, notmuch_tags_destroy>;
using NmFilenames =
    NmHandle<notmuch_filenames_t, notmuch_filenames_destroy>;
