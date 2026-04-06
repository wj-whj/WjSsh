#include "SshCommon.h"

#include <QByteArray>

extern "C" {
#include <libssh/libssh.h>
}

namespace {
void closeSession(ssh_session session)
{
    if (session == nullptr) {
        return;
    }
    ssh_disconnect(session);
    ssh_free(session);
}

bool verifyServer(ssh_session session,
                  const SessionProfile &profile,
                  const HostKeyPromptHandler &hostPrompt,
                  QString *errorMessage)
{
    ssh_key key = nullptr;
    unsigned char *hash = nullptr;
    size_t hashLength = 0;

    if (ssh_get_server_publickey(session, &key) != SSH_OK) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("读取服务器公钥失败：%1").arg(libsshError(session));
        }
        return false;
    }

    if (ssh_get_publickey_hash(key, SSH_PUBLICKEY_HASH_SHA256, &hash, &hashLength) != SSH_OK) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("计算服务器指纹失败：%1").arg(libsshError(session));
        }
        ssh_key_free(key);
        return false;
    }

    char *hexFingerprint = ssh_get_hexa(hash, hashLength);
    const QString fingerprint = QString::fromUtf8(hexFingerprint != nullptr ? hexFingerprint : "");

    SSH_STRING_FREE_CHAR(hexFingerprint);
    ssh_clean_pubkey_hash(&hash);
    ssh_key_free(key);

    const enum ssh_known_hosts_e state = ssh_session_is_known_server(session);
    switch (state) {
    case SSH_KNOWN_HOSTS_OK:
        return true;
    case SSH_KNOWN_HOSTS_UNKNOWN:
    case SSH_KNOWN_HOSTS_NOT_FOUND: {
        if (!hostPrompt) {
            if (errorMessage != nullptr) {
                *errorMessage = QStringLiteral("检测到新的服务器主机密钥，但当前没有确认回调。");
            }
            return false;
        }

        const QString title = QStringLiteral("首次连接 %1").arg(profile.host);
        const QString message =
            QStringLiteral("服务器尚未在 known_hosts 中登记。\n\n"
                           "主机：%1\n"
                           "用户：%2\n"
                           "指纹：%3\n\n"
                           "确认无误后可继续并写入 known_hosts。")
                .arg(profile.host, profile.username, fingerprint);

        if (!hostPrompt(title, message, fingerprint)) {
            if (errorMessage != nullptr) {
                *errorMessage = QStringLiteral("已取消连接：服务器指纹未被信任。");
            }
            return false;
        }

        if (ssh_session_update_known_hosts(session) != SSH_OK) {
            if (errorMessage != nullptr) {
                *errorMessage = QStringLiteral("写入 known_hosts 失败：%1").arg(libsshError(session));
            }
            return false;
        }
        return true;
    }
    case SSH_KNOWN_HOSTS_CHANGED:
    case SSH_KNOWN_HOSTS_OTHER:
        if (errorMessage != nullptr) {
            *errorMessage =
                QStringLiteral("服务器主机密钥与本地记录不一致，已中止连接。\n当前指纹：%1").arg(fingerprint);
        }
        return false;
    case SSH_KNOWN_HOSTS_ERROR:
    default:
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("校验服务器主机密钥失败：%1").arg(libsshError(session));
        }
        return false;
    }
}
}

ssh_session openAuthenticatedSession(const SessionProfile &profile,
                                     const QString &secret,
                                     const HostKeyPromptHandler &hostPrompt,
                                     QString *errorMessage)
{
    ssh_session session = ssh_new();
    if (session == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("无法初始化 libssh 会话。");
        }
        return nullptr;
    }

    const QByteArray host = profile.host.toUtf8();
    const QByteArray user = profile.username.toUtf8();
    const QByteArray identity = profile.privateKeyPath.toUtf8();
    const QByteArray secretUtf8 = secret.toUtf8();
    const int port = profile.port;
    const int logLevel = SSH_LOG_NOLOG;
    const long timeoutSeconds = 10;

    ssh_options_set(session, SSH_OPTIONS_HOST, host.constData());
    ssh_options_set(session, SSH_OPTIONS_USER, user.constData());
    ssh_options_set(session, SSH_OPTIONS_PORT, &port);
    ssh_options_set(session, SSH_OPTIONS_LOG_VERBOSITY, &logLevel);
    ssh_options_set(session, SSH_OPTIONS_TIMEOUT, &timeoutSeconds);

    if (profile.authMode == AuthMode::PrivateKey && !profile.privateKeyPath.trimmed().isEmpty()) {
        ssh_options_set(session, SSH_OPTIONS_IDENTITY, identity.constData());
    }

    if (ssh_connect(session) != SSH_OK) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("SSH 连接失败：%1").arg(libsshError(session));
        }
        closeSession(session);
        return nullptr;
    }

    if (!verifyServer(session, profile, hostPrompt, errorMessage)) {
        closeSession(session);
        return nullptr;
    }

    int authResult = SSH_AUTH_ERROR;
    if (profile.authMode == AuthMode::Password) {
        authResult = ssh_userauth_password(
            session,
            nullptr,
            secretUtf8.isEmpty() ? "" : secretUtf8.constData());
    } else {
        authResult = ssh_userauth_publickey_auto(
            session,
            nullptr,
            secretUtf8.isEmpty() ? nullptr : secretUtf8.constData());
    }

    if (authResult != SSH_AUTH_SUCCESS) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("认证失败：%1").arg(libsshError(session));
        }
        closeSession(session);
        return nullptr;
    }

    return session;
}

QString libsshError(void *handle)
{
    const char *message = ssh_get_error(handle);
    if (message == nullptr) {
        return QStringLiteral("未知错误");
    }
    return QString::fromUtf8(message);
}
