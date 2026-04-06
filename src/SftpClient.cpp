#include "SftpClient.h"
#include "DebugTrace.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>

#include <algorithm>
#include <fcntl.h>

namespace {
QString normalizeRemotePath(QString path)
{
    path.replace('\\', '/');
    while (path.contains("//")) {
        path.replace("//", "/");
    }
    if (path.isEmpty()) {
        path = "/";
    }
    return path;
}

QString joinRemotePath(const QString &base, const QString &name)
{
    if (base.isEmpty() || base == "/") {
        return normalizeRemotePath("/" + name);
    }
    return normalizeRemotePath(base + "/" + name);
}

QString parentRemotePath(const QString &path)
{
    QString normalized = normalizeRemotePath(path);
    if (normalized == "/" || normalized == ".") {
        return normalized;
    }

    while (normalized.endsWith('/') && normalized.size() > 1) {
        normalized.chop(1);
    }

    const int slash = normalized.lastIndexOf('/');
    if (slash <= 0) {
        return "/";
    }
    return normalized.left(slash);
}

QString permissionString(uint32_t mode, bool isDir)
{
    QString result;
    result += isDir ? 'd' : '-';

    const int masks[] = {0400, 0200, 0100, 040, 020, 010, 04, 02, 01};
    const char chars[] = {'r', 'w', 'x', 'r', 'w', 'x', 'r', 'w', 'x'};

    for (int index = 0; index < 9; ++index) {
        result += (mode & masks[index]) ? chars[index] : '-';
    }
    return result;
}

void destroySftpSession(sftp_session &sftp)
{
    if (sftp == nullptr) {
        return;
    }
    sftp_free(sftp);
    sftp = nullptr;
}

void destroySshSession(ssh_session &session)
{
    if (session == nullptr) {
        return;
    }
    ssh_disconnect(session);
    ssh_free(session);
    session = nullptr;
}

QString sftpErrorDescription(int code)
{
    switch (code) {
    case SSH_FX_OK:
        return QStringLiteral("成功");
    case SSH_FX_EOF:
        return QStringLiteral("已到文件末尾");
    case SSH_FX_NO_SUCH_FILE:
        return QStringLiteral("路径不存在");
    case SSH_FX_PERMISSION_DENIED:
        return QStringLiteral("权限不足");
    case SSH_FX_FAILURE:
        return QStringLiteral("远端返回失败");
    case SSH_FX_BAD_MESSAGE:
        return QStringLiteral("SFTP 消息格式错误");
    case SSH_FX_NO_CONNECTION:
        return QStringLiteral("SFTP 未连接");
    case SSH_FX_CONNECTION_LOST:
        return QStringLiteral("SFTP 连接已断开");
    case SSH_FX_OP_UNSUPPORTED:
        return QStringLiteral("服务器不支持此操作");
    case SSH_FX_FILE_ALREADY_EXISTS:
        return QStringLiteral("文件已存在");
    default:
        return {};
    }
}

bool reportTransferProgress(const SftpTransferProgressHandler &progressHandler,
                            const QString &path,
                            quint64 bytesTransferred,
                            quint64 bytesTotal,
                            QString *errorMessage)
{
    if (!progressHandler) {
        return true;
    }

    const SftpTransferProgress progress{path, bytesTransferred, bytesTotal};
    if (progressHandler(progress)) {
        return true;
    }

    if (errorMessage != nullptr && errorMessage->isEmpty()) {
        *errorMessage = QStringLiteral("传输已取消。");
    }
    return false;
}

quint64 remoteFileSize(sftp_session sftp, const QString &remotePath)
{
    if (sftp == nullptr) {
        return 0;
    }

    const QByteArray utf8Path = remotePath.toUtf8();
    sftp_attributes attributes = sftp_stat(sftp, utf8Path.constData());
    if (attributes == nullptr) {
        return 0;
    }

    const quint64 size = static_cast<quint64>(attributes->size);
    sftp_attributes_free(attributes);
    return size;
}
}

SftpClient::~SftpClient()
{
    disconnectFromHost();
}

bool SftpClient::connectTo(const SessionProfile &profile,
                           const QString &secret,
                           const HostKeyPromptHandler &hostPrompt,
                           QString *errorMessage)
{
    wjsshTrace(QStringLiteral("SftpClient::connectTo start profile=%1").arg(profile.subtitle()));
    disconnectFromHost();

    m_session = openAuthenticatedSession(profile, secret, hostPrompt, errorMessage);
    if (m_session == nullptr) {
        wjsshTrace(QStringLiteral("SftpClient::connectTo auth session failed error=%1")
                       .arg(errorMessage != nullptr ? *errorMessage : QString()));
        return false;
    }

    ssh_set_blocking(m_session, 1);
    m_sftp = sftp_new(m_session);
    wjsshTrace(QStringLiteral("SftpClient::connectTo sftp_new allocated=%1").arg(m_sftp != nullptr));
    if (m_sftp == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = sessionError(QStringLiteral("初始化 SFTP 失败"));
        }
        destroySshSession(m_session);
        return false;
    }

    if (sftp_init(m_sftp) != SSH_OK) {
        wjsshTrace(QStringLiteral("SftpClient::connectTo sftp_init failed"));
        if (errorMessage != nullptr) {
            *errorMessage = sessionError(QStringLiteral("启动 SFTP 会话失败"));
        }
        destroySftpSession(m_sftp);
        destroySshSession(m_session);
        return false;
    }

    wjsshTrace(QStringLiteral("SftpClient::connectTo success"));
    return true;
}

void SftpClient::disconnectFromHost()
{
    wjsshTrace(QStringLiteral("SftpClient::disconnectFromHost begin"));
    destroySftpSession(m_sftp);
    destroySshSession(m_session);
    wjsshTrace(QStringLiteral("SftpClient::disconnectFromHost end"));
}

bool SftpClient::isConnected() const
{
    return m_session != nullptr && m_sftp != nullptr;
}

QString SftpClient::homePath(QString *errorMessage) const
{
    return canonicalPath(".", errorMessage);
}

QString SftpClient::canonicalPath(const QString &path, QString *errorMessage) const
{
    wjsshTrace(QStringLiteral("SftpClient::canonicalPath start path=%1 connected=%2").arg(path).arg(isConnected()));
    if (!isConnected()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("当前没有活动的 SFTP 连接。");
        }
        return {};
    }

    const QByteArray utf8Path = path.toUtf8();
    char *canonical = sftp_canonicalize_path(m_sftp, utf8Path.constData());
    if (canonical == nullptr) {
        wjsshTrace(QStringLiteral("SftpClient::canonicalPath failed path=%1").arg(path));
        if (errorMessage != nullptr) {
            *errorMessage = sessionError(QStringLiteral("解析远程路径失败"));
        }
        return {};
    }

    const QString result = normalizeRemotePath(QString::fromUtf8(canonical));
    SSH_STRING_FREE_CHAR(canonical);
    wjsshTrace(QStringLiteral("SftpClient::canonicalPath success path=%1 result=%2").arg(path, result));
    return result;
}

QVector<RemoteEntry> SftpClient::listDirectory(const QString &path, QString *errorMessage) const
{
    QVector<RemoteEntry> entries;
    wjsshTrace(QStringLiteral("SftpClient::listDirectory start path=%1 connected=%2").arg(path).arg(isConnected()));
    if (!isConnected()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("当前没有活动的 SFTP 连接。");
        }
        return entries;
    }

    const QByteArray utf8Path = path.toUtf8();
    sftp_dir directory = sftp_opendir(m_sftp, utf8Path.constData());
    if (directory == nullptr) {
        wjsshTrace(QStringLiteral("SftpClient::listDirectory opendir failed path=%1").arg(path));
        if (errorMessage != nullptr) {
            *errorMessage = sessionError(QStringLiteral("打开远程目录失败"));
        }
        return entries;
    }

    while (!sftp_dir_eof(directory)) {
        sftp_attributes attributes = sftp_readdir(m_sftp, directory);
        if (attributes == nullptr) {
            break;
        }

        const QString name = QString::fromUtf8(attributes->name != nullptr ? attributes->name : "");
        if (name == "." || name == "..") {
            sftp_attributes_free(attributes);
            continue;
        }

        RemoteEntry entry;
        entry.name = name;
        entry.path = joinRemotePath(path, name);
        entry.isDirectory = attributes->type == SSH_FILEXFER_TYPE_DIRECTORY;
        entry.size = static_cast<quint64>(attributes->size);
        entry.permissions = permissionString(attributes->permissions, entry.isDirectory);
        const qint64 modifiedEpoch =
            attributes->mtime64 != 0 ? static_cast<qint64>(attributes->mtime64)
                                     : static_cast<qint64>(attributes->mtime);
        entry.modifiedAt = QDateTime::fromSecsSinceEpoch(modifiedEpoch);
        entries.push_back(entry);

        sftp_attributes_free(attributes);
    }

    sftp_closedir(directory);

    std::sort(entries.begin(), entries.end(), [](const RemoteEntry &left, const RemoteEntry &right) {
        if (left.isDirectory != right.isDirectory) {
            return left.isDirectory > right.isDirectory;
        }
        return left.name.toLower() < right.name.toLower();
    });

    wjsshTrace(QStringLiteral("SftpClient::listDirectory success path=%1 entries=%2").arg(path).arg(entries.size()));
    return entries;
}

bool SftpClient::readFile(const QString &remotePath,
                          QByteArray *data,
                          QString *errorMessage,
                          const SftpTransferProgressHandler &progressHandler) const
{
    {
        if (data == nullptr) {
            if (errorMessage != nullptr) {
                *errorMessage = QStringLiteral("读取缓冲区不能为空。");
            }
            return false;
        }

        data->clear();
        const QByteArray utf8Path = remotePath.toUtf8();
        sftp_file remoteFile = sftp_open(m_sftp, utf8Path.constData(), O_RDONLY, 0);
        if (remoteFile == nullptr) {
            if (errorMessage != nullptr) {
                *errorMessage = sessionError(QStringLiteral("打开远程文件失败"));
            }
            return false;
        }

        const quint64 totalBytes = remoteFileSize(m_sftp, remotePath);
        quint64 transferredBytes = 0;
        if (!reportTransferProgress(progressHandler, remotePath, transferredBytes, totalBytes, errorMessage)) {
            sftp_close(remoteFile);
            return false;
        }

        char buffer[32768];
        while (true) {
            const ssize_t readBytes = sftp_read(remoteFile, buffer, sizeof(buffer));
            if (readBytes < 0) {
                if (errorMessage != nullptr) {
                    *errorMessage = sessionError(QStringLiteral("读取远程文件失败"));
                }
                sftp_close(remoteFile);
                data->clear();
                return false;
            }
            if (readBytes == 0) {
                break;
            }

            data->append(buffer, static_cast<qsizetype>(readBytes));
            transferredBytes += static_cast<quint64>(readBytes);
            if (!reportTransferProgress(progressHandler,
                                        remotePath,
                                        transferredBytes,
                                        totalBytes,
                                        errorMessage)) {
                sftp_close(remoteFile);
                data->clear();
                return false;
            }
        }

        sftp_close(remoteFile);
        return true;
    }

    if (data == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("读取缓冲区不能为空。");
        }
        return false;
    }

    data->clear();
    const QByteArray utf8Path = remotePath.toUtf8();
    sftp_file remoteFile = sftp_open(m_sftp, utf8Path.constData(), O_RDONLY, 0);
    if (remoteFile == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = sessionError(QStringLiteral("打开远程文件失败"));
        }
        return false;
    }

    char buffer[32768];
    while (true) {
        const ssize_t readBytes = sftp_read(remoteFile, buffer, sizeof(buffer));
        if (readBytes < 0) {
            if (errorMessage != nullptr) {
                *errorMessage = sessionError(QStringLiteral("读取远程文件失败"));
            }
            sftp_close(remoteFile);
            return false;
        }
        if (readBytes == 0) {
            break;
        }
        data->append(buffer, static_cast<qsizetype>(readBytes));
    }

    sftp_close(remoteFile);
    return true;
}

bool SftpClient::writeFile(const QString &remotePath, const QByteArray &data, QString *errorMessage) const
{
    if (!ensureDirectoryExists(parentRemotePath(remotePath), errorMessage)) {
        return false;
    }

    const QByteArray utf8Path = remotePath.toUtf8();
    sftp_file remoteFile = sftp_open(m_sftp,
                                     utf8Path.constData(),
                                     O_WRONLY | O_CREAT | O_TRUNC,
                                     0644);
    if (remoteFile == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = sessionError(QStringLiteral("写入远程文件失败"));
        }
        return false;
    }

    qsizetype offset = 0;
    while (offset < data.size()) {
        const qsizetype chunkSize = std::min<qsizetype>(32768, data.size() - offset);
        const ssize_t written = sftp_write(remoteFile, data.constData() + offset, static_cast<size_t>(chunkSize));
        if (written < 0) {
            if (errorMessage != nullptr) {
                *errorMessage = sessionError(QStringLiteral("写入远程文件失败"));
            }
            sftp_close(remoteFile);
            return false;
        }
        offset += static_cast<qsizetype>(written);
    }

    sftp_close(remoteFile);
    return true;
}

bool SftpClient::uploadFile(const QString &localPath,
                            const QString &remotePath,
                            QString *errorMessage,
                            const SftpTransferProgressHandler &progressHandler) const
{
    {
        QFile localFile(localPath);
        if (!localFile.open(QIODevice::ReadOnly)) {
            if (errorMessage != nullptr) {
                *errorMessage = QStringLiteral("无法读取本地文件：%1").arg(localFile.errorString());
            }
            return false;
        }

        if (!ensureDirectoryExists(parentRemotePath(remotePath), errorMessage)) {
            return false;
        }

        const QByteArray utf8Path = remotePath.toUtf8();
        sftp_file remoteFile = sftp_open(m_sftp,
                                         utf8Path.constData(),
                                         O_WRONLY | O_CREAT | O_TRUNC,
                                         0644);
        if (remoteFile == nullptr) {
            if (errorMessage != nullptr) {
                *errorMessage = sessionError(QStringLiteral("上传时无法创建远程文件"));
            }
            return false;
        }

        const quint64 totalBytes = static_cast<quint64>(qMax<qint64>(0, localFile.size()));
        quint64 transferredBytes = 0;
        if (!reportTransferProgress(progressHandler, localPath, transferredBytes, totalBytes, errorMessage)) {
            sftp_close(remoteFile);
            sftp_unlink(m_sftp, utf8Path.constData());
            return false;
        }

        while (!localFile.atEnd()) {
            const QByteArray chunk = localFile.read(32768);
            if (chunk.isEmpty() && localFile.error() != QFile::NoError) {
                if (errorMessage != nullptr) {
                    *errorMessage = QStringLiteral("读取本地文件失败：%1").arg(localFile.errorString());
                }
                sftp_close(remoteFile);
                sftp_unlink(m_sftp, utf8Path.constData());
                return false;
            }

            qsizetype chunkOffset = 0;
            while (chunkOffset < chunk.size()) {
                const ssize_t written = sftp_write(remoteFile,
                                                   chunk.constData() + chunkOffset,
                                                   static_cast<size_t>(chunk.size() - chunkOffset));
                if (written < 0) {
                    if (errorMessage != nullptr) {
                        *errorMessage = sessionError(QStringLiteral("写入远程文件失败"));
                    }
                    sftp_close(remoteFile);
                    sftp_unlink(m_sftp, utf8Path.constData());
                    return false;
                }

                chunkOffset += static_cast<qsizetype>(written);
                transferredBytes += static_cast<quint64>(written);
                if (!reportTransferProgress(progressHandler,
                                            localPath,
                                            transferredBytes,
                                            totalBytes,
                                            errorMessage)) {
                    sftp_close(remoteFile);
                    sftp_unlink(m_sftp, utf8Path.constData());
                    return false;
                }
            }
        }

        sftp_close(remoteFile);
        return true;
    }

    QFile localFile(localPath);
    if (!localFile.open(QIODevice::ReadOnly)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("无法读取本地文件：%1").arg(localFile.errorString());
        }
        return false;
    }

    if (!ensureDirectoryExists(parentRemotePath(remotePath), errorMessage)) {
        return false;
    }

    const QByteArray utf8Path = remotePath.toUtf8();
    sftp_file remoteFile = sftp_open(m_sftp,
                                     utf8Path.constData(),
                                     O_WRONLY | O_CREAT | O_TRUNC,
                                     0644);
    if (remoteFile == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = sessionError(QStringLiteral("上传时无法创建远程文件"));
        }
        return false;
    }

    while (!localFile.atEnd()) {
        const QByteArray chunk = localFile.read(32768);
        if (chunk.isEmpty() && localFile.error() != QFile::NoError) {
            if (errorMessage != nullptr) {
                *errorMessage = QStringLiteral("读取本地文件失败：%1").arg(localFile.errorString());
            }
            sftp_close(remoteFile);
            return false;
        }

        const ssize_t written = sftp_write(remoteFile, chunk.constData(), static_cast<size_t>(chunk.size()));
        if (written < 0 || written != chunk.size()) {
            if (errorMessage != nullptr) {
                *errorMessage = sessionError(QStringLiteral("写入远程文件失败"));
            }
            sftp_close(remoteFile);
            return false;
        }
    }

    sftp_close(remoteFile);
    return true;
}

bool SftpClient::downloadFile(const QString &remotePath,
                              const QString &localPath,
                              QString *errorMessage,
                              const SftpTransferProgressHandler &progressHandler) const
{
    {
        const QByteArray utf8Path = remotePath.toUtf8();
        sftp_file remoteFile = sftp_open(m_sftp, utf8Path.constData(), O_RDONLY, 0);
        if (remoteFile == nullptr) {
            if (errorMessage != nullptr) {
                *errorMessage = sessionError(QStringLiteral("打开远程文件失败"));
            }
            return false;
        }

        const QFileInfo info(localPath);
        if (!QDir().mkpath(info.absolutePath())) {
            if (errorMessage != nullptr) {
                *errorMessage = QStringLiteral("无法创建本地目录：%1").arg(info.absolutePath());
            }
            sftp_close(remoteFile);
            return false;
        }

        QFile localFile(localPath);
        if (!localFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            if (errorMessage != nullptr) {
                *errorMessage = QStringLiteral("无法写入本地文件：%1").arg(localFile.errorString());
            }
            sftp_close(remoteFile);
            return false;
        }

        const quint64 totalBytes = remoteFileSize(m_sftp, remotePath);
        quint64 transferredBytes = 0;
        if (!reportTransferProgress(progressHandler, remotePath, transferredBytes, totalBytes, errorMessage)) {
            localFile.close();
            localFile.remove();
            sftp_close(remoteFile);
            return false;
        }

        char buffer[32768];
        while (true) {
            const ssize_t readBytes = sftp_read(remoteFile, buffer, sizeof(buffer));
            if (readBytes < 0) {
                if (errorMessage != nullptr) {
                    *errorMessage = sessionError(QStringLiteral("读取远程文件失败"));
                }
                localFile.close();
                localFile.remove();
                sftp_close(remoteFile);
                return false;
            }
            if (readBytes == 0) {
                break;
            }
            if (localFile.write(buffer, readBytes) != readBytes) {
                if (errorMessage != nullptr) {
                    *errorMessage = QStringLiteral("写入本地文件失败：%1").arg(localFile.errorString());
                }
                localFile.close();
                localFile.remove();
                sftp_close(remoteFile);
                return false;
            }

            transferredBytes += static_cast<quint64>(readBytes);
            if (!reportTransferProgress(progressHandler,
                                        remotePath,
                                        transferredBytes,
                                        totalBytes,
                                        errorMessage)) {
                localFile.close();
                localFile.remove();
                sftp_close(remoteFile);
                return false;
            }
        }

        sftp_close(remoteFile);
        return true;
    }

    const QByteArray utf8Path = remotePath.toUtf8();
    sftp_file remoteFile = sftp_open(m_sftp, utf8Path.constData(), O_RDONLY, 0);
    if (remoteFile == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = sessionError(QStringLiteral("打开远程文件失败"));
        }
        return false;
    }

    const QFileInfo info(localPath);
    if (!QDir().mkpath(info.absolutePath())) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("无法创建本地目录：%1").arg(info.absolutePath());
        }
        sftp_close(remoteFile);
        return false;
    }

    QFile localFile(localPath);
    if (!localFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("无法写入本地文件：%1").arg(localFile.errorString());
        }
        sftp_close(remoteFile);
        return false;
    }

    char buffer[32768];
    while (true) {
        const ssize_t readBytes = sftp_read(remoteFile, buffer, sizeof(buffer));
        if (readBytes < 0) {
            if (errorMessage != nullptr) {
                *errorMessage = sessionError(QStringLiteral("读取远程文件失败"));
            }
            sftp_close(remoteFile);
            return false;
        }
        if (readBytes == 0) {
            break;
        }
        if (localFile.write(buffer, readBytes) != readBytes) {
            if (errorMessage != nullptr) {
                *errorMessage = QStringLiteral("写入本地文件失败：%1").arg(localFile.errorString());
            }
            sftp_close(remoteFile);
            return false;
        }
    }

    sftp_close(remoteFile);
    return true;
}

bool SftpClient::uploadDirectory(const QString &localDirectoryPath,
                                 const QString &remoteDirectoryPath,
                                 QString *errorMessage,
                                 const SftpTransferProgressHandler &progressHandler) const
{
    {
        QDir sourceDirectory(localDirectoryPath);
        if (!sourceDirectory.exists()) {
            if (errorMessage != nullptr) {
                *errorMessage = QStringLiteral("本地目录不存在：%1").arg(localDirectoryPath);
            }
            return false;
        }

        if (!ensureDirectoryExists(remoteDirectoryPath, errorMessage)) {
            return false;
        }

        QDirIterator iterator(localDirectoryPath,
                              QDir::NoDotAndDotDot | QDir::AllEntries,
                              QDirIterator::Subdirectories);

        while (iterator.hasNext()) {
            iterator.next();
            const QFileInfo info = iterator.fileInfo();
            const QString relative = sourceDirectory.relativeFilePath(info.absoluteFilePath());
            const QString childRemotePath = joinRemotePath(remoteDirectoryPath, relative);

            if (info.isDir()) {
                if (!ensureDirectoryExists(childRemotePath, errorMessage)) {
                    return false;
                }
                continue;
            }

            if (!uploadFile(info.absoluteFilePath(), childRemotePath, errorMessage, progressHandler)) {
                return false;
            }
        }

        return true;
    }

    QDir sourceDirectory(localDirectoryPath);
    if (!sourceDirectory.exists()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("本地目录不存在：%1").arg(localDirectoryPath);
        }
        return false;
    }

    if (!ensureDirectoryExists(remoteDirectoryPath, errorMessage)) {
        return false;
    }

    QDirIterator iterator(localDirectoryPath,
                          QDir::NoDotAndDotDot | QDir::AllEntries,
                          QDirIterator::Subdirectories);

    while (iterator.hasNext()) {
        iterator.next();
        const QFileInfo info = iterator.fileInfo();
        const QString relative = sourceDirectory.relativeFilePath(info.absoluteFilePath());
        const QString remotePath = joinRemotePath(remoteDirectoryPath, relative);

        if (info.isDir()) {
            if (!ensureDirectoryExists(remotePath, errorMessage)) {
                return false;
            }
            continue;
        }

        if (!uploadFile(info.absoluteFilePath(), remotePath, errorMessage)) {
            return false;
        }
    }

    return true;
}

bool SftpClient::downloadDirectory(const QString &remoteDirectoryPath,
                                   const QString &localDirectoryPath,
                                   QString *errorMessage,
                                   const SftpTransferProgressHandler &progressHandler) const
{
    {
        if (!QDir().mkpath(localDirectoryPath)) {
            if (errorMessage != nullptr) {
                *errorMessage = QStringLiteral("无法创建本地目录：%1").arg(localDirectoryPath);
            }
            return false;
        }

        const QVector<RemoteEntry> entries = listDirectory(remoteDirectoryPath, errorMessage);
        if (errorMessage != nullptr && !errorMessage->isEmpty()) {
            return false;
        }

        for (const RemoteEntry &entry : entries) {
            const QString localChildPath = QDir(localDirectoryPath).filePath(entry.name);
            if (entry.isDirectory) {
                if (!downloadDirectory(entry.path, localChildPath, errorMessage, progressHandler)) {
                    return false;
                }
            } else {
                if (!downloadFile(entry.path, localChildPath, errorMessage, progressHandler)) {
                    return false;
                }
            }
        }

        return true;
    }

    if (!QDir().mkpath(localDirectoryPath)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("无法创建本地目录：%1").arg(localDirectoryPath);
        }
        return false;
    }

    const QVector<RemoteEntry> entries = listDirectory(remoteDirectoryPath, errorMessage);
    if (errorMessage != nullptr && !errorMessage->isEmpty()) {
        return false;
    }

    for (const RemoteEntry &entry : entries) {
        const QString localChildPath = QDir(localDirectoryPath).filePath(entry.name);
        if (entry.isDirectory) {
            if (!downloadDirectory(entry.path, localChildPath, errorMessage)) {
                return false;
            }
        } else {
            if (!downloadFile(entry.path, localChildPath, errorMessage)) {
                return false;
            }
        }
    }

    return true;
}

bool SftpClient::createDirectory(const QString &remotePath, QString *errorMessage) const
{
    if (!ensureDirectoryExists(parentRemotePath(remotePath), errorMessage)) {
        return false;
    }

    const QByteArray utf8Path = remotePath.toUtf8();
    if (sftp_mkdir(m_sftp, utf8Path.constData(), 0755) != SSH_OK) {
        if (sftp_get_error(m_sftp) == SSH_FX_FILE_ALREADY_EXISTS) {
            return true;
        }
        if (errorMessage != nullptr) {
            *errorMessage = sessionError(QStringLiteral("创建远程目录失败"));
        }
        return false;
    }
    return true;
}

bool SftpClient::createEmptyFile(const QString &remotePath, QString *errorMessage) const
{
    return writeFile(remotePath, QByteArray(), errorMessage);
}

bool SftpClient::renamePath(const QString &oldPath, const QString &newPath, QString *errorMessage) const
{
    if (!ensureDirectoryExists(parentRemotePath(newPath), errorMessage)) {
        return false;
    }

    const QByteArray from = oldPath.toUtf8();
    const QByteArray to = newPath.toUtf8();
    if (sftp_rename(m_sftp, from.constData(), to.constData()) != SSH_OK) {
        if (errorMessage != nullptr) {
            *errorMessage = sessionError(QStringLiteral("重命名远程路径失败"));
        }
        return false;
    }
    return true;
}

bool SftpClient::removePath(const QString &remotePath, QString *errorMessage) const
{
    QString statError;
    const bool directory = existsAsDirectory(remotePath, &statError);
    if (!statError.isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = statError;
        }
        return false;
    }

    if (directory) {
        return removeDirectoryRecursive(remotePath, errorMessage);
    }

    const QByteArray utf8Path = remotePath.toUtf8();
    if (sftp_unlink(m_sftp, utf8Path.constData()) != SSH_OK) {
        if (errorMessage != nullptr) {
            *errorMessage = sessionError(QStringLiteral("删除远程文件失败"));
        }
        return false;
    }
    return true;
}

bool SftpClient::removeDirectoryRecursive(const QString &remotePath, QString *errorMessage) const
{
    const QVector<RemoteEntry> entries = listDirectory(remotePath, errorMessage);
    if (errorMessage != nullptr && !errorMessage->isEmpty()) {
        return false;
    }

    for (const RemoteEntry &entry : entries) {
        if (entry.isDirectory) {
            if (!removeDirectoryRecursive(entry.path, errorMessage)) {
                return false;
            }
        } else {
            const QByteArray childPath = entry.path.toUtf8();
            if (sftp_unlink(m_sftp, childPath.constData()) != SSH_OK) {
                if (errorMessage != nullptr) {
                    *errorMessage = sessionError(QStringLiteral("删除远程文件失败"));
                }
                return false;
            }
        }
    }

    const QByteArray utf8Path = remotePath.toUtf8();
    if (sftp_rmdir(m_sftp, utf8Path.constData()) != SSH_OK) {
        if (errorMessage != nullptr) {
            *errorMessage = sessionError(QStringLiteral("删除远程目录失败"));
        }
        return false;
    }
    return true;
}

bool SftpClient::existsAsDirectory(const QString &remotePath, QString *errorMessage) const
{
    bool isDirectory = false;
    const bool exists = pathExists(remotePath, &isDirectory, errorMessage);
    if (!exists && (errorMessage == nullptr || errorMessage->isEmpty())) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("远程路径不存在：%1").arg(remotePath);
        }
    }
    return exists && isDirectory;
}

bool SftpClient::ensureDirectoryExists(const QString &remotePath, QString *errorMessage) const
{
    const QString normalized = normalizeRemotePath(remotePath);
    if (normalized.isEmpty() || normalized == "/" || normalized == ".") {
        return true;
    }

    bool isDirectory = false;
    const bool exists = pathExists(normalized, &isDirectory, errorMessage);
    if (!exists) {
        if (errorMessage != nullptr && !errorMessage->isEmpty()) {
            return false;
        }

        if (!ensureDirectoryExists(parentRemotePath(normalized), errorMessage)) {
            return false;
        }

        const QByteArray utf8Path = normalized.toUtf8();
        if (sftp_mkdir(m_sftp, utf8Path.constData(), 0755) != SSH_OK &&
            sftp_get_error(m_sftp) != SSH_FX_FILE_ALREADY_EXISTS) {
            if (errorMessage != nullptr) {
                *errorMessage = sessionError(QStringLiteral("创建远程目录失败"));
            }
            return false;
        }
        return true;
    }

    if (!isDirectory) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("远程路径不是目录：%1").arg(normalized);
        }
        return false;
    }
    return true;
}

bool SftpClient::pathExists(const QString &remotePath, bool *isDirectory, QString *errorMessage) const
{
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }

    const QByteArray utf8Path = remotePath.toUtf8();
    sftp_attributes attributes = sftp_lstat(m_sftp, utf8Path.constData());
    if (attributes == nullptr) {
        if (sftp_get_error(m_sftp) == SSH_FX_NO_SUCH_FILE) {
            return false;
        }
        if (errorMessage != nullptr) {
            *errorMessage = sessionError(QStringLiteral("读取远程路径信息失败"));
        }
        return false;
    }

    if (isDirectory != nullptr) {
        *isDirectory = attributes->type == SSH_FILEXFER_TYPE_DIRECTORY;
    }
    sftp_attributes_free(attributes);
    return true;
}

QString SftpClient::sessionError(const QString &prefix) const
{
    return QStringLiteral("%1：%2").arg(prefix, QString::fromUtf8(ssh_get_error(m_session)));
}
