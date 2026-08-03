#pragma once

#include <QHash>
#include <QList>
#include <QSet>
#include <QString>

#include "htmlbuilder.h"
#include "mimeparser.h"

/// The inline parts of a whole thread, keyed by their namespaced cid.
struct ThreadCidMap
{
    QHash<QString, InlinePart> parts;  ///< For the scheme handler.
    QSet<QString> allowedCids;         ///< For the interceptor. Same keys.
};

/// Flattens every message's inline parts into one namespaced map.
///
/// Two messages in a thread commonly share a Content-ID (cid:logo@example.org
/// from the same sender's newsletter template), and the thread renders as one
/// document, so the raw ids would collide and one message would show another's
/// image. Keys are "<prefix>!<content-id>".
///
/// A cidPrefix containing '!' would break the split that keeps those apart, so
/// it is sanitized here rather than trusted: Q_ASSERT fires in debug builds but
/// is compiled out in release, and this map decides which bytes a message can
/// name. Sanitizing is injective, so two distinct prefixes stay distinct.
ThreadCidMap buildThreadCidMap(const QList<ThreadRenderItem> &items);
