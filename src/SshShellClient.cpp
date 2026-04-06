#include "SshShellClient.h"
#include "DebugTrace.h"

#include <QMetaObject>
#include <QMutexLocker>
#include <QThread>

namespace {
void destroyChannel(ssh_channel &channel)
{
    if (channel == nullptr) {
        return;
    }

    ssh_channel_send_eof(channel);
    ssh_channel_close(channel);
    ssh_channel_free(channel);
    channel = nullptr;
}

void destroySession(ssh_session &session)
{
    if (session == nullptr) {
        return;
    }

    ssh_disconnect(session);
    ssh_free(session);
    session = nullptr;
}

QString summarizeChunk(QString text)
{
    text.replace("\r", "\\r");
    text.replace("\n", "\\n");
    text.replace("\t", "\\t");
    text.replace(QString(QChar(0x1b)), "\\e");
    return text.left(200);
}
}

SshShellClient::SshShellClient(QObject *parent)
    : QObject(parent)
{
}

SshShellClient::~SshShellClient()
{
    disconnectFromHost();
}

bool SshShellClient::connectTo(const SessionProfile &profile,
                               const QString &secret,
                               const HostKeyPromptHandler &hostPrompt,
                               QString *errorMessage)
{
    wjsshTrace(QStringLiteral("SshShellClient::connectTo start profile=%1").arg(profile.subtitle()));
    disconnectFromHost();

    m_stdoutDecoder = QStringDecoder(QStringConverter::Utf8);
    m_stderrDecoder = QStringDecoder(QStringConverter::Utf8);

    m_session = openAuthenticatedSession(profile, secret, hostPrompt, errorMessage);
    if (m_session == nullptr) {
        wjsshTrace(QStringLiteral("SshShellClient::connectTo auth session failed error=%1")
                       .arg(errorMessage != nullptr ? *errorMessage : QString()));
        return false;
    }
    wjsshTrace(QStringLiteral("SshShellClient::connectTo auth session ready"));

    m_channel = ssh_channel_new(m_session);
    wjsshTrace(QStringLiteral("SshShellClient::connectTo channel allocated=%1").arg(m_channel != nullptr));
    if (m_channel == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("无法创建远程终端通道。");
        }
        destroySession(m_session);
        return false;
    }

    if (ssh_channel_open_session(m_channel) != SSH_OK) {
        wjsshTrace(QStringLiteral("SshShellClient::connectTo open session failed"));
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("打开远程终端失败：%1").arg(libsshError(m_session));
        }
        destroyChannel(m_channel);
        destroySession(m_session);
        return false;
    }

    auto requestEnv = [this](const char *name, const char *value) {
        const int rc = ssh_channel_request_env(m_channel, name, value);
        wjsshTrace(QStringLiteral("SshShellClient::connectTo request env %1=%2 rc=%3")
                       .arg(QString::fromLatin1(name))
                       .arg(QString::fromLatin1(value))
                       .arg(rc));
    };

    requestEnv("TERM", "xterm-256color");
    requestEnv("LANG", "C.UTF-8");
    requestEnv("LC_ALL", "C.UTF-8");
    requestEnv("COLORTERM", "truecolor");
    requestEnv("TERM_PROGRAM", "WjSsh");
    requestEnv("TERM_PROGRAM_VERSION", "0.1.0");
    requestEnv("CLICOLOR", "1");
    requestEnv("CLICOLOR_FORCE", "1");
    requestEnv("LC_TERMINAL", "WjSsh");
    requestEnv("LC_TERMINAL_VERSION", "0.1.0");
    ssh_channel_request_pty_size(m_channel, "xterm-256color", 200, 52);

    if (ssh_channel_request_shell(m_channel) != SSH_OK) {
        wjsshTrace(QStringLiteral("SshShellClient::connectTo request shell failed"));
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("启动远程终端失败：%1").arg(libsshError(m_session));
        }
        destroyChannel(m_channel);
        destroySession(m_session);
        return false;
    }

    m_readerRunning = true;
    wjsshTrace(QStringLiteral("SshShellClient::connectTo shell ready, starting reader"));
    m_reader = QThread::create([this]() {
        char buffer[4096];

        while (m_readerRunning) {
            bool anyData = false;

            for (int stderrStream = 0; stderrStream <= 1; ++stderrStream) {
                int polled = 0;
                {
                    QMutexLocker locker(&m_mutex);
                    if (m_channel == nullptr || !ssh_channel_is_open(m_channel)) {
                        QMetaObject::invokeMethod(this,
                                                  [this]() {
                                                      emit disconnectedUnexpectedly(QStringLiteral("远程终端已关闭。"));
                                                  },
                                                  Qt::QueuedConnection);
                        return;
                    }
                    polled = ssh_channel_poll_timeout(m_channel, 120, stderrStream);
                }

                if (polled < 0) {
                    QMetaObject::invokeMethod(this,
                                              [this]() {
                                                  emit disconnectedUnexpectedly(QStringLiteral("远程终端读取失败。"));
                                              },
                                              Qt::QueuedConnection);
                    return;
                }

                while (polled > 0 && m_readerRunning) {
                    int bytesRead = 0;
                    {
                        QMutexLocker locker(&m_mutex);
                        bytesRead = ssh_channel_read_nonblocking(m_channel,
                                                                 buffer,
                                                                 sizeof(buffer),
                                                                 stderrStream);
                    }

                    if (bytesRead < 0) {
                        QMetaObject::invokeMethod(this,
                                                  [this]() {
                                                      emit disconnectedUnexpectedly(QStringLiteral("远程终端输出流异常中断。"));
                                                  },
                                                  Qt::QueuedConnection);
                        return;
                    }
                    if (bytesRead == 0) {
                        break;
                    }

                    anyData = true;
                    const bool isErrorStream = stderrStream == 1;
                    const QString chunk = decodeChunk(QByteArrayView(buffer, bytesRead), isErrorStream);
                    wjsshTrace(QStringLiteral("SshShellClient reader chunk bytes=%1 textLen=%2 stderr=%3")
                                   .arg(bytesRead)
                                   .arg(chunk.size())
                                   .arg(isErrorStream));
                    if (!chunk.isEmpty()) {
                        wjsshTrace(QStringLiteral("SshShellClient reader chunk sample=%1").arg(summarizeChunk(chunk)));
                    }

                    if (!chunk.isEmpty()) {
                        QMetaObject::invokeMethod(this,
                                                  [this, chunk, isErrorStream]() {
                                                      emit outputReceived(chunk, isErrorStream);
                                                  },
                                                  Qt::QueuedConnection);
                    }

                    {
                        QMutexLocker locker(&m_mutex);
                        polled = ssh_channel_poll_timeout(m_channel, 0, stderrStream);
                    }
                }
            }

            if (!anyData) {
                QThread::msleep(50);
            }
        }
    });
    m_reader->start();

    return true;
}

void SshShellClient::disconnectFromHost()
{
    wjsshTrace(QStringLiteral("SshShellClient::disconnectFromHost begin"));
    QThread *reader = nullptr;
    {
        QMutexLocker locker(&m_mutex);
        m_readerRunning = false;
        reader = m_reader;
        m_reader = nullptr;
        destroyChannel(m_channel);
        destroySession(m_session);
    }

    if (reader != nullptr) {
        reader->wait();
        delete reader;
    }
    wjsshTrace(QStringLiteral("SshShellClient::disconnectFromHost end"));
}

bool SshShellClient::isConnected() const
{
    QMutexLocker locker(&m_mutex);
    return m_session != nullptr && m_channel != nullptr && ssh_channel_is_open(m_channel);
}

bool SshShellClient::resizeTerminal(int columns, int rows, QString *errorMessage)
{
    QMutexLocker locker(&m_mutex);
    if (m_channel == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("当前终端未连接。");
        }
        return false;
    }

    if (ssh_channel_change_pty_size(m_channel, columns, rows) != SSH_OK) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("更新终端尺寸失败：%1").arg(libsshError(m_session));
        }
        return false;
    }

    return true;
}

bool SshShellClient::sendRawData(const QByteArray &data, QString *errorMessage)
{
    QMutexLocker locker(&m_mutex);
    if (m_channel == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("当前终端未连接。");
        }
        return false;
    }

    const int written = ssh_channel_write(m_channel, data.constData(), data.size());
    if (written < 0) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("发送数据失败：%1").arg(libsshError(m_session));
        }
        return false;
    }

    return written == data.size();
}

bool SshShellClient::sendCommand(const QString &command, QString *errorMessage)
{
    return sendRawData((command + "\n").toUtf8(), errorMessage);
}

bool SshShellClient::sendControlCharacter(char value, QString *errorMessage)
{
    QMutexLocker locker(&m_mutex);
    if (m_channel == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("当前终端未连接。");
        }
        return false;
    }

    const int written = ssh_channel_write(m_channel, &value, 1);
    if (written < 0) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("发送控制字符失败：%1").arg(libsshError(m_session));
        }
        return false;
    }

    return true;
}

QString SshShellClient::decodeChunk(QByteArrayView data, bool isErrorStream)
{
    QStringDecoder &decoder = isErrorStream ? m_stderrDecoder : m_stdoutDecoder;
    const QString decoded = decoder.decode(data);
    if (!decoder.hasError()) {
        return decoded;
    }

    decoder = QStringDecoder(QStringConverter::Utf8);

    QString fallback = QString::fromLocal8Bit(data);
    if (const auto gbEncoding = QStringConverter::encodingForName("GB18030")) {
        QStringDecoder gbDecoder(*gbEncoding);
        const QString gbDecoded = gbDecoder.decode(data);
        if (!gbDecoder.hasError()
            && gbDecoded.count(QChar::ReplacementCharacter)
                   <= fallback.count(QChar::ReplacementCharacter)) {
            fallback = gbDecoded;
        }
    }

    return fallback;
}
