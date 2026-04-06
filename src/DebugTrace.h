#pragma once

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QMutex>
#include <QMutexLocker>
#include <QStandardPaths>
#include <QTextStream>

inline bool wjsshTraceEnabled()
{
    static const bool enabled = qEnvironmentVariableIsSet("WJSSH_TRACE");
    return enabled;
}

inline QString wjsshTracePath()
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (dir.isEmpty()) {
        dir = QDir::tempPath() + "/WjSsh";
    }
    QDir().mkpath(dir);
    return dir + "/trace.log";
}

inline void wjsshTrace(const QString &message)
{
    if (!wjsshTraceEnabled()) {
        return;
    }

    static QMutex mutex;
    QMutexLocker locker(&mutex);

    QFile file(wjsshTracePath());
    if (!file.open(QIODevice::Append | QIODevice::Text)) {
        return;
    }

    QTextStream stream(&file);
    stream << QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz")
           << " [pid=" << QCoreApplication::applicationPid() << "] "
           << message << '\n';
}
