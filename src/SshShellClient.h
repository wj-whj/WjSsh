#pragma once

#include "SessionProfile.h"
#include "SshCommon.h"

#include <atomic>
#include <QMutex>
#include <QObject>
#include <QStringConverter>
#include <QThread>

extern "C" {
#include <libssh/libssh.h>
}

class SshShellClient : public QObject {
    Q_OBJECT

public:
    explicit SshShellClient(QObject *parent = nullptr);
    ~SshShellClient() override;

    bool connectTo(const SessionProfile &profile,
                   const QString &secret,
                   const HostKeyPromptHandler &hostPrompt,
                   QString *errorMessage);
    void disconnectFromHost();

    [[nodiscard]] bool isConnected() const;
    bool resizeTerminal(int columns, int rows, QString *errorMessage = nullptr);
    bool sendRawData(const QByteArray &data, QString *errorMessage = nullptr);
    bool sendCommand(const QString &command, QString *errorMessage = nullptr);
    bool sendControlCharacter(char value, QString *errorMessage = nullptr);

signals:
    void outputReceived(const QString &text, bool isErrorStream);
    void disconnectedUnexpectedly(const QString &reason);

private:
    QString decodeChunk(QByteArrayView data, bool isErrorStream);

    ssh_session m_session = nullptr;
    ssh_channel m_channel = nullptr;
    QThread *m_reader = nullptr;
    std::atomic_bool m_readerRunning = false;
    mutable QMutex m_mutex;
    QStringDecoder m_stdoutDecoder {QStringConverter::Utf8};
    QStringDecoder m_stderrDecoder {QStringConverter::Utf8};
};
