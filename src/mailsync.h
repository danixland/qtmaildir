#pragma once

#include <QObject>
#include <QProcess>
#include <QString>

/// Runs the configured external sync command.
///
/// qtmaildir deliberately does not implement sync itself. The existing script
/// holds a flock that is the shared mutex between the user's cron sync, which
/// runs every 10 minutes, and any manual sync; running the script joins that
/// mutex, whereas a built-in implementation would sit outside it and could run
/// mbsync concurrently with cron, corrupting Maildir UID state.
class MailSync : public QObject
{
    Q_OBJECT
public:
    explicit MailSync(const QString &command, QObject *parent = nullptr);

    /// False when no command is configured; the UI disables its Sync button.
    bool isAvailable() const { return !m_command.isEmpty(); }
    bool isRunning() const;

    /// Returns false if unavailable or already running. A true return means the
    /// process was handed to the event loop, not that it launched successfully:
    /// a missing binary surfaces asynchronously through finished(false, ...).
    bool start();

    QString log() const { return m_log; }

signals:
    void started();
    void outputReceived(const QString &chunk);
    void finished(bool success, int exitCode);

private:
    void handleReadyRead();
    void handleFinished(int exitCode, QProcess::ExitStatus status);
    void handleError(QProcess::ProcessError error);

    QString m_command;
    QProcess m_process;
    QString m_log;
};
