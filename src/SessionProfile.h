#pragma once

#include <QJsonObject>
#include <QString>

enum class AuthMode {
    Password,
    PrivateKey
};

struct SessionProfile {
    QString id;
    QString name;
    QString host;
    int port = 22;
    QString username;
    AuthMode authMode = AuthMode::Password;
    QString password;
    bool rememberPassword = false;
    QString privateKeyPath;
    QString initialPath;

    [[nodiscard]] QJsonObject toJson() const;
    static SessionProfile fromJson(const QJsonObject &object);

    [[nodiscard]] QString authModeKey() const;
    [[nodiscard]] QString displayName() const;
    [[nodiscard]] QString subtitle() const;
    [[nodiscard]] bool isValid() const;
};

QString authModeToString(AuthMode mode);
AuthMode authModeFromString(const QString &value);
