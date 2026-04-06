#include "SessionRepository.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QStandardPaths>

#ifdef Q_OS_WIN
#include <windows.h>
#include <wincrypt.h>
#endif

namespace {
QString appDataDir()
{
    QString location = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (location.isEmpty()) {
        location = QDir::homePath() + "/.wjssh";
    }
    return location;
}

QString encryptSecretForStorage(const QString &secret)
{
    if (secret.isEmpty()) {
        return {};
    }

    const QByteArray utf8 = secret.toUtf8();

#ifdef Q_OS_WIN
    DATA_BLOB inputBlob;
    inputBlob.cbData = static_cast<DWORD>(utf8.size());
    inputBlob.pbData = reinterpret_cast<BYTE *>(const_cast<char *>(utf8.constData()));

    DATA_BLOB outputBlob {};
    if (CryptProtectData(&inputBlob,
                         L"WjSsh Session Secret",
                         nullptr,
                         nullptr,
                         nullptr,
                         CRYPTPROTECT_UI_FORBIDDEN,
                         &outputBlob) != FALSE) {
        const QByteArray encrypted(reinterpret_cast<const char *>(outputBlob.pbData),
                                   static_cast<int>(outputBlob.cbData));
        LocalFree(outputBlob.pbData);
        return QString::fromLatin1(encrypted.toBase64());
    }
#endif

    return QString::fromLatin1(utf8.toBase64());
}

QString decryptSecretFromStorage(const QString &encoded)
{
    if (encoded.isEmpty()) {
        return {};
    }

    const QByteArray payload = QByteArray::fromBase64(encoded.toLatin1());
    if (payload.isEmpty()) {
        return {};
    }

#ifdef Q_OS_WIN
    DATA_BLOB inputBlob;
    inputBlob.cbData = static_cast<DWORD>(payload.size());
    inputBlob.pbData = reinterpret_cast<BYTE *>(const_cast<char *>(payload.constData()));

    DATA_BLOB outputBlob {};
    if (CryptUnprotectData(&inputBlob,
                           nullptr,
                           nullptr,
                           nullptr,
                           nullptr,
                           CRYPTPROTECT_UI_FORBIDDEN,
                           &outputBlob) != FALSE) {
        const QByteArray decrypted(reinterpret_cast<const char *>(outputBlob.pbData),
                                   static_cast<int>(outputBlob.cbData));
        LocalFree(outputBlob.pbData);
        return QString::fromUtf8(decrypted);
    }
#endif

    return QString::fromUtf8(payload);
}
}

QJsonObject SessionProfile::toJson() const
{
    QJsonObject object = {
        {"id", id},
        {"name", name},
        {"host", host},
        {"port", port},
        {"username", username},
        {"authMode", authModeKey()},
        {"rememberPassword", authMode == AuthMode::Password && rememberPassword},
        {"privateKeyPath", privateKeyPath},
        {"initialPath", initialPath}
    };

    if (authMode == AuthMode::Password && rememberPassword && !password.isEmpty()) {
        object.insert("savedPassword", encryptSecretForStorage(password));
    }

    return object;
}

SessionProfile SessionProfile::fromJson(const QJsonObject &object)
{
    SessionProfile profile;
    profile.id = object.value("id").toString();
    profile.name = object.value("name").toString();
    profile.host = object.value("host").toString();
    profile.port = object.value("port").toInt(22);
    profile.username = object.value("username").toString();
    profile.authMode = authModeFromString(object.value("authMode").toString());
    profile.rememberPassword = object.value("rememberPassword").toBool(false);
    profile.password = decryptSecretFromStorage(object.value("savedPassword").toString());
    if (profile.password.isEmpty()) {
        profile.rememberPassword = false;
    }
    profile.privateKeyPath = object.value("privateKeyPath").toString();
    profile.initialPath = object.value("initialPath").toString();
    return profile;
}

QString SessionProfile::authModeKey() const
{
    return authModeToString(authMode);
}

QString SessionProfile::displayName() const
{
    return name.trimmed().isEmpty() ? host.trimmed() : name.trimmed();
}

QString SessionProfile::subtitle() const
{
    return QString("%1@%2:%3").arg(username, host).arg(port);
}

bool SessionProfile::isValid() const
{
    return !host.trimmed().isEmpty() && !username.trimmed().isEmpty() && port > 0;
}

QString authModeToString(AuthMode mode)
{
    return mode == AuthMode::PrivateKey ? "privateKey" : "password";
}

AuthMode authModeFromString(const QString &value)
{
    return value == "privateKey" ? AuthMode::PrivateKey : AuthMode::Password;
}

QVector<SessionProfile> SessionRepository::load() const
{
    QFile file(storagePath());
    if (!file.exists() || !file.open(QIODevice::ReadOnly)) {
        return {};
    }

    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    const QJsonArray array = document.array();

    QVector<SessionProfile> profiles;
    profiles.reserve(array.size());
    for (const QJsonValue &value : array) {
        if (!value.isObject()) {
            continue;
        }
        SessionProfile profile = SessionProfile::fromJson(value.toObject());
        if (profile.isValid()) {
            profiles.push_back(profile);
        }
    }
    return profiles;
}

bool SessionRepository::save(const QVector<SessionProfile> &profiles, QString *errorMessage) const
{
    QDir directory(appDataDir());
    if (!directory.exists() && !directory.mkpath(".")) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("无法创建会话配置目录：%1").arg(directory.path());
        }
        return false;
    }

    QJsonArray array;
    for (const SessionProfile &profile : profiles) {
        array.push_back(profile.toJson());
    }

    QFile file(storagePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("无法写入会话配置文件：%1").arg(file.errorString());
        }
        return false;
    }

    file.write(QJsonDocument(array).toJson(QJsonDocument::Indented));
    return true;
}

QString SessionRepository::storagePath() const
{
    return appDataDir() + "/sessions.json";
}
