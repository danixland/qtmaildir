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

#include "tagrules.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QSaveFile>
#include <algorithm>

namespace {

constexpr int kFormatVersion = 1;
constexpr int kDefaultStage = 50;

/// Fields this version understands. Anything else is preserved.
bool isKnownKey(const QString &key)
{
    static const QStringList known{
        QStringLiteral("id"),      QStringLiteral("stage"),
        QStringLiteral("enabled"), QStringLiteral("add"),
        QStringLiteral("remove"),  QStringLiteral("query"),
        QStringLiteral("note"),
    };
    return known.contains(key);
}

QStringList stringsOf(const QJsonValue &value)
{
    QStringList out;
    const QJsonArray array = value.toArray();
    for (const QJsonValue &entry : array) {
        const QString text = entry.toString().trimmed();
        if (!text.isEmpty())
            out.append(text);
    }
    return out;
}

} // namespace

QString TagRules::defaultPath()
{
    // Not QStandardPaths::ConfigLocation: that appends the organization and
    // application names, which would put the file under qtmaildir's own
    // directory. mailctl reads this path too, so it must be tool-neutral.
    QString base = qEnvironmentVariable("XDG_CONFIG_HOME");
    if (base.isEmpty())
        base = QDir::homePath() + QStringLiteral("/.config");
    return base + QStringLiteral("/mailrules/rules.json");
}

void TagRules::load(const QString &path)
{
    const QString target = path.isEmpty() ? defaultPath() : path;

    m_rules.clear();
    m_warnings.clear();
    m_unknown = QJsonObject();
    m_missing = false;

    QFile file(target);
    if (!file.exists()) {
        m_missing = true;
        return;
    }

    if (!file.open(QIODevice::ReadOnly)) {
        m_warnings.append(
            QObject::tr("Cannot read %1: %2").arg(target, file.errorString()));
        return;
    }

    QJsonParseError error{};
    const QJsonDocument document =
        QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        m_warnings.append(QObject::tr("Cannot read %1: %2")
                              .arg(target, error.errorString()));
        return;
    }

    const QJsonObject root = document.object();

    const int version = root.value(QStringLiteral("version"))
                            .toInt(kFormatVersion);
    if (version != kFormatVersion) {
        m_warnings.append(
            QObject::tr("%1 uses format version %2, newer than this version "
                        "understands (%3); refusing to guess")
                .arg(target).arg(version).arg(kFormatVersion));
        return;
    }

    for (auto it = root.begin(); it != root.end(); ++it) {
        if (it.key() != QStringLiteral("version")
            && it.key() != QStringLiteral("rules")) {
            m_unknown.insert(it.key(), it.value());
        }
    }

    // An id is a handle: a UI selects on it and a diff tracks it.
    static const QRegularExpression idPattern(
        QStringLiteral("^[a-z0-9][a-z0-9-]*$"));

    QStringList seen;
    const QJsonArray array = root.value(QStringLiteral("rules")).toArray();
    for (int index = 0; index < array.size(); ++index) {
        const QJsonObject object = array.at(index).toObject();
        const QString where = QObject::tr("rule #%1").arg(index + 1);

        TagRule rule;
        rule.id = object.value(QStringLiteral("id")).toString();
        if (!idPattern.match(rule.id).hasMatch()) {
            m_warnings.append(
                QObject::tr("%1: id '%2' is missing or not lowercase letters, "
                            "digits and dashes; dropped")
                    .arg(where, rule.id));
            continue;
        }

        if (seen.contains(rule.id)) {
            m_warnings.append(QObject::tr("Rule '%1': duplicate id; keeping "
                                          "the first").arg(rule.id));
            continue;
        }

        rule.query = object.value(QStringLiteral("query")).toString().trimmed();
        if (rule.query.isEmpty()) {
            m_warnings.append(
                QObject::tr("Rule '%1': no query; dropped").arg(rule.id));
            continue;
        }

        rule.add = stringsOf(object.value(QStringLiteral("add")));
        rule.remove = stringsOf(object.value(QStringLiteral("remove")));
        if (rule.add.isEmpty() && rule.remove.isEmpty()) {
            m_warnings.append(QObject::tr("Rule '%1': adds and removes "
                                          "nothing; dropped").arg(rule.id));
            continue;
        }

        rule.stage = object.value(QStringLiteral("stage")).toInt(kDefaultStage);
        rule.enabled = object.value(QStringLiteral("enabled")).toBool(true);
        rule.note = object.value(QStringLiteral("note")).toString();

        for (auto it = object.begin(); it != object.end(); ++it) {
            if (!isKnownKey(it.key()))
                rule.unknown.insert(it.key(), it.value());
        }

        seen.append(rule.id);
        m_rules.append(rule);
    }
}

bool TagRules::save(const QString &path) const
{
    const QString target = path.isEmpty() ? defaultPath() : path;

    QDir().mkpath(QFileInfo(target).absolutePath());

    QJsonArray array;
    for (const TagRule &rule : m_rules) {
        QJsonObject object;
        object.insert(QStringLiteral("id"), rule.id);
        object.insert(QStringLiteral("stage"), rule.stage);
        object.insert(QStringLiteral("enabled"), rule.enabled);
        object.insert(QStringLiteral("add"),
                      QJsonArray::fromStringList(rule.add));
        object.insert(QStringLiteral("remove"),
                      QJsonArray::fromStringList(rule.remove));
        object.insert(QStringLiteral("query"), rule.query);
        object.insert(QStringLiteral("note"), rule.note);
        for (auto it = rule.unknown.begin(); it != rule.unknown.end(); ++it)
            object.insert(it.key(), it.value());
        array.append(object);
    }

    QJsonObject root = m_unknown;
    root.insert(QStringLiteral("version"), kFormatVersion);
    root.insert(QStringLiteral("rules"), array);

    // QSaveFile writes a temporary and renames on commit, which is the same
    // atomicity mailrules.py gets from os.replace. The hook must never read a
    // half-written file.
    QSaveFile file(target);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return file.commit();
}

QList<TagRule> TagRules::ordered() const
{
    QList<TagRule> out;
    for (const TagRule &rule : m_rules) {
        if (rule.enabled)
            out.append(rule);
    }
    // stable_sort, not sort: ties must keep file order, which is the tie-break
    // the format promises and what lets a user sequence rules within a stage.
    std::stable_sort(out.begin(), out.end(),
                     [](const TagRule &a, const TagRule &b) {
                         return a.stage < b.stage;
                     });
    return out;
}
