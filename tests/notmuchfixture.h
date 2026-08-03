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

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QString>
#include <QTemporaryDir>
#include <QTextStream>

/// A throwaway notmuch database in a temporary directory.
///
/// Builds a Maildir tree, writes a notmuch config pointing at it, and runs
/// `notmuch new`. Nothing touches the developer's own ~/Mail or
/// ~/.notmuch-config: the config path is handed to the worker explicitly.
///
/// Maildir flags are not decoration. notmuch synchronizes them with tags at
/// index time, so a file named `...:2,S` ("seen") comes out WITHOUT the unread
/// tag no matter what [new] tags says. addMessage() takes the unread state and
/// picks the filename accordingly.
class NotmuchFixture
{
public:
    /// True when the temporary tree was created. Check before use.
    bool isValid() const { return m_dir.isValid(); }

    QString configPath() const { return m_dir.filePath(QStringLiteral("config")); }
    QString maildirPath() const { return m_dir.filePath(QStringLiteral("mail")); }

    /// Writes one message into <folder>/cur (or new/ when unread).
    ///
    /// Returns false if the file could not be written. Call index() afterwards.
    bool addMessage(const QString &folder, const QString &messageId,
                    const QString &subject, const QString &from,
                    const QString &date, const QString &body,
                    bool unread = true, const QString &inReplyTo = QString())
    {
        // Unread messages must not carry the maildir "S" flag, so they go to
        // new/ where no flags exist at all.
        const QString sub = unread ? QStringLiteral("new") : QStringLiteral("cur");
        const QString dirPath = maildirPath() + QLatin1Char('/') + folder;
        QDir dir;
        if (!dir.mkpath(dirPath + QStringLiteral("/cur"))
            || !dir.mkpath(dirPath + QStringLiteral("/new"))
            || !dir.mkpath(dirPath + QStringLiteral("/tmp"))) {
            return false;
        }

        // The local part of the id makes a safe, unique, flag-free filename.
        QString base = messageId;
        base.remove(QLatin1Char('<')).remove(QLatin1Char('>'));
        base.replace(QLatin1Char('@'), QLatin1Char('.'));
        base.replace(QLatin1Char('/'), QLatin1Char('.'));
        if (!unread)
            base += QStringLiteral(":2,S");

        QFile file(dirPath + QLatin1Char('/') + sub + QLatin1Char('/') + base);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
            return false;

        QTextStream out(&file);
        out << "From: " << from << "\n"
            << "To: danix@danix.xyz\n"
            << "Subject: " << subject << "\n"
            << "Message-ID: <" << messageId << ">\n"
            << "Date: " << date << "\n";
        if (!inReplyTo.isEmpty())
            out << "In-Reply-To: <" << inReplyTo << ">\n"
                << "References: <" << inReplyTo << ">\n";
        out << "\n" << body << "\n";
        out.flush();
        file.close();
        return true;
    }

    /// Writes the config and runs `notmuch new`. Safe to call repeatedly.
    /// Returns false (with error() set) if notmuch is missing or fails.
    bool index()
    {
        QFile config(configPath());
        if (!config.open(QIODevice::WriteOnly | QIODevice::Text)) {
            m_error = QStringLiteral("cannot write fixture config");
            return false;
        }
        QTextStream out(&config);
        out << "[database]\n"
            << "path=" << maildirPath() << "\n"
            << "[new]\n"
            << "tags=unread;inbox;\n";
        out.flush();
        config.close();

        QProcess proc;
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("NOTMUCH_CONFIG"), configPath());
        proc.setProcessEnvironment(env);
        proc.start(QStringLiteral("notmuch"), { QStringLiteral("new") });
        if (!proc.waitForStarted(5000)) {
            m_error = QStringLiteral("notmuch not found on PATH");
            return false;
        }
        if (!proc.waitForFinished(30000) || proc.exitCode() != 0) {
            m_error = QStringLiteral("notmuch new failed: %1")
                          .arg(QString::fromLocal8Bit(proc.readAllStandardError()));
            return false;
        }
        return true;
    }

    QString error() const { return m_error; }

private:
    QTemporaryDir m_dir;
    QString m_error;
};
