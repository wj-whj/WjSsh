#pragma once

#include "SessionProfile.h"
#include "SshCommon.h"

#include <QDateTime>
#include <QString>
#include <QVector>
#include <QByteArray>
#include <functional>

extern "C" {
#include <libssh/sftp.h>
}

struct RemoteEntry {
    QString name;
    QString path;
    bool isDirectory = false;
    quint64 size = 0;
    QString permissions;
    QDateTime modifiedAt;
};

struct SftpTransferProgress {
    QString path;
    quint64 bytesTransferred = 0;
    quint64 bytesTotal = 0;
};

using SftpTransferProgressHandler = std::function<bool(const SftpTransferProgress &)>;

class SftpClient {
public:
    ~SftpClient();

    bool connectTo(const SessionProfile &profile,
                   const QString &secret,
                   const HostKeyPromptHandler &hostPrompt,
                   QString *errorMessage);
    void disconnectFromHost();

    [[nodiscard]] bool isConnected() const;
    [[nodiscard]] QString homePath(QString *errorMessage) const;
    [[nodiscard]] QString canonicalPath(const QString &path, QString *errorMessage) const;
    [[nodiscard]] QVector<RemoteEntry> listDirectory(const QString &path, QString *errorMessage) const;
    bool readFile(const QString &remotePath,
                  QByteArray *data,
                  QString *errorMessage,
                  const SftpTransferProgressHandler &progressHandler = {}) const;
    bool writeFile(const QString &remotePath, const QByteArray &data, QString *errorMessage) const;

    bool uploadFile(const QString &localPath,
                    const QString &remotePath,
                    QString *errorMessage,
                    const SftpTransferProgressHandler &progressHandler = {}) const;
    bool downloadFile(const QString &remotePath,
                      const QString &localPath,
                      QString *errorMessage,
                      const SftpTransferProgressHandler &progressHandler = {}) const;
    bool uploadDirectory(const QString &localDirectoryPath,
                         const QString &remoteDirectoryPath,
                         QString *errorMessage,
                         const SftpTransferProgressHandler &progressHandler = {}) const;
    bool downloadDirectory(const QString &remoteDirectoryPath,
                           const QString &localDirectoryPath,
                           QString *errorMessage,
                           const SftpTransferProgressHandler &progressHandler = {}) const;
    bool createDirectory(const QString &remotePath, QString *errorMessage) const;
    bool createEmptyFile(const QString &remotePath, QString *errorMessage) const;
    bool renamePath(const QString &oldPath, const QString &newPath, QString *errorMessage) const;
    bool removePath(const QString &remotePath, QString *errorMessage) const;

private:
    bool removeDirectoryRecursive(const QString &remotePath, QString *errorMessage) const;
    [[nodiscard]] bool existsAsDirectory(const QString &remotePath, QString *errorMessage) const;
    bool ensureDirectoryExists(const QString &remotePath, QString *errorMessage) const;
    bool pathExists(const QString &remotePath, bool *isDirectory, QString *errorMessage) const;
    [[nodiscard]] QString sessionError(const QString &prefix) const;

    ssh_session m_session = nullptr;
    sftp_session m_sftp = nullptr;
};
