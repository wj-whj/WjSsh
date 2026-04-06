#include "RemoteServerStatsMonitor.h"

#include "DebugTrace.h"
#include "SshCommon.h"

#include <QDateTime>
#include <QHash>
#include <QPointer>
#include <QThread>

extern "C" {
#include <libssh/libssh.h>
}

namespace {

struct ProbeResult {
    RemoteServerStatsMonitor::Sample sample;
    bool online = false;
    QString stateText;
};

void closeSession(ssh_session session)
{
    if (session == nullptr) {
        return;
    }
    ssh_disconnect(session);
    ssh_free(session);
}

QString shellQuote(QString text)
{
    text.replace('\'', "'\"'\"'");
    return QStringLiteral("'") + text + QStringLiteral("'");
}

QString buildRemoteProbeCommand()
{
    const QString script = QStringLiteral(
        "OS=\"$(uname -s 2>/dev/null || echo Unknown)\"\n"
        "HOST=\"$(hostname 2>/dev/null || echo unknown)\"\n"
        "printf 'os=%s\\n' \"$OS\"\n"
        "printf 'host=%s\\n' \"$HOST\"\n"
        "if [ \"$OS\" = \"Linux\" ]; then\n"
        "  CPU_A=\"$(grep '^cpu ' /proc/stat 2>/dev/null)\"\n"
        "  sleep 0.20\n"
        "  CPU_B=\"$(grep '^cpu ' /proc/stat 2>/dev/null)\"\n"
        "  if [ -n \"$CPU_A\" ] && [ -n \"$CPU_B\" ]; then\n"
        "    set -- $CPU_A\n"
        "    TOTAL1=$(( $2 + $3 + $4 + $5 + $6 + $7 + $8 + $9 + ${10:-0} + ${11:-0} ))\n"
        "    IDLE1=$(( $5 + $6 ))\n"
        "    set -- $CPU_B\n"
        "    TOTAL2=$(( $2 + $3 + $4 + $5 + $6 + $7 + $8 + $9 + ${10:-0} + ${11:-0} ))\n"
        "    IDLE2=$(( $5 + $6 ))\n"
        "    DT=$(( TOTAL2 - TOTAL1 ))\n"
        "    DI=$(( IDLE2 - IDLE1 ))\n"
        "    if [ \"$DT\" -gt 0 ]; then CPU=$(( (1000 * (DT - DI) / DT + 5) / 10 )); else CPU=0; fi\n"
        "    printf 'cpu_percent=%s\\n' \"$CPU\"\n"
        "  fi\n"
        "  awk '/^MemTotal:/{t=$2*1024} /^MemAvailable:/{a=$2*1024} END{printf \"mem_total=%.0f\\nmem_used=%.0f\\n\", t, (t>a?t-a:0)}' /proc/meminfo 2>/dev/null\n"
        "  df -kP / 2>/dev/null | awk 'NR==2{printf \"disk_total=%.0f\\ndisk_used=%.0f\\n\", $2*1024, $3*1024}'\n"
        "  awk -F'[: ]+' 'BEGIN{rx=0;tx=0} /^[[:space:]]*lo:/{next} /^[[:space:]]*[^:]+:/{rx+=$3; tx+=$11} END{printf \"net_rx=%.0f\\nnet_tx=%.0f\\n\", rx, tx}' /proc/net/dev 2>/dev/null\n"
        "elif [ \"$OS\" = \"Darwin\" ]; then\n"
        "  CPU=\"$(top -l 1 2>/dev/null | awk -F'[:,%]' '/CPU usage/ {printf \"%.0f\\n\", $2 + $4; exit}')\"\n"
        "  printf 'cpu_percent=%s\\n' \"${CPU:-0}\"\n"
        "  MEM_TOTAL=\"$(sysctl -n hw.memsize 2>/dev/null || echo 0)\"\n"
        "  PAGESIZE=\"$(pagesize 2>/dev/null || echo 4096)\"\n"
        "  VM=\"$(vm_stat 2>/dev/null)\"\n"
        "  FREE=\"$(printf '%s\\n' \"$VM\" | awk -v ps=\"$PAGESIZE\" '/Pages free/ {gsub(\"\\\\.\",\"\",$3); free=$3} /Pages speculative/ {gsub(\"\\\\.\",\"\",$3); spec=$3} END{printf \"%.0f\", (free+spec)*ps}')\"\n"
        "  [ -z \"$FREE\" ] && FREE=0\n"
        "  if [ \"$MEM_TOTAL\" -gt \"$FREE\" ] 2>/dev/null; then MEM_USED=$(( MEM_TOTAL - FREE )); else MEM_USED=0; fi\n"
        "  printf 'mem_total=%s\\nmem_used=%s\\n' \"$MEM_TOTAL\" \"$MEM_USED\"\n"
        "  df -kP / 2>/dev/null | awk 'NR==2{printf \"disk_total=%.0f\\ndisk_used=%.0f\\n\", $2*1024, $3*1024}'\n"
        "  PRIMARY_IF=\"$(route -n get default 2>/dev/null | awk '/interface:/{print $2; exit}')\"\n"
        "  if [ -n \"$PRIMARY_IF\" ]; then\n"
        "    netstat -ibn -I \"$PRIMARY_IF\" 2>/dev/null | awk 'NR>1 && $(NF-1) ~ /^[0-9]+$/ && $NF ~ /^[0-9]+$/ {inb += $(NF-1); outb += $NF} END{printf \"net_rx=%.0f\\nnet_tx=%.0f\\n\", inb+0, outb+0}'\n"
        "  else\n"
        "    printf 'net_rx=0\\nnet_tx=0\\n'\n"
        "  fi\n"
        "else\n"
        "  printf 'cpu_percent=0\\nmem_total=0\\nmem_used=0\\ndisk_total=0\\ndisk_used=0\\nnet_rx=0\\nnet_tx=0\\n'\n"
        "fi\n");

    return QStringLiteral("/bin/sh -lc %1").arg(shellQuote(script));
}

bool executeRemoteCommand(ssh_session session,
                          const QString &command,
                          QByteArray *stdoutData,
                          QByteArray *stderrData,
                          QString *errorMessage)
{
    ssh_channel channel = ssh_channel_new(session);
    if (channel == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("无法创建远端采样通道。");
        }
        return false;
    }

    auto closeChannel = [&]() {
        ssh_channel_send_eof(channel);
        ssh_channel_close(channel);
        ssh_channel_free(channel);
    };

    if (ssh_channel_open_session(channel) != SSH_OK) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("无法打开远端采样会话：%1").arg(libsshError(session));
        }
        ssh_channel_free(channel);
        return false;
    }

    const QByteArray utf8Command = command.toUtf8();
    if (ssh_channel_request_exec(channel, utf8Command.constData()) != SSH_OK) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("无法执行远端采样命令：%1").arg(libsshError(session));
        }
        closeChannel();
        return false;
    }

    QByteArray out;
    QByteArray err;
    char buffer[4096];
    int idleLoops = 0;

    while (!ssh_channel_is_eof(channel)) {
        const int stdoutRead = ssh_channel_read_timeout(channel, buffer, sizeof(buffer), 0, 400);
        if (stdoutRead == SSH_ERROR) {
            if (errorMessage != nullptr) {
                *errorMessage = QStringLiteral("读取远端采样输出失败：%1").arg(libsshError(session));
            }
            closeChannel();
            return false;
        }
        if (stdoutRead > 0) {
            out.append(buffer, stdoutRead);
            idleLoops = 0;
        }

        const int stderrRead = ssh_channel_read_timeout(channel, buffer, sizeof(buffer), 1, 20);
        if (stderrRead == SSH_ERROR) {
            if (errorMessage != nullptr) {
                *errorMessage = QStringLiteral("读取远端采样错误输出失败：%1").arg(libsshError(session));
            }
            closeChannel();
            return false;
        }
        if (stderrRead > 0) {
            err.append(buffer, stderrRead);
            idleLoops = 0;
        }

        if (stdoutRead == 0 && stderrRead == 0) {
            ++idleLoops;
            if (idleLoops > 25 && !ssh_channel_is_open(channel)) {
                break;
            }
        }
    }

    while (true) {
        const int stdoutRead = ssh_channel_read_timeout(channel, buffer, sizeof(buffer), 0, 10);
        if (stdoutRead > 0) {
            out.append(buffer, stdoutRead);
            continue;
        }

        const int stderrRead = ssh_channel_read_timeout(channel, buffer, sizeof(buffer), 1, 10);
        if (stderrRead > 0) {
            err.append(buffer, stderrRead);
            continue;
        }
        break;
    }

    const int exitStatus = ssh_channel_get_exit_status(channel);
    closeChannel();

    if (stdoutData != nullptr) {
        *stdoutData = out;
    }
    if (stderrData != nullptr) {
        *stderrData = err;
    }

    if (exitStatus != 0 && out.trimmed().isEmpty()) {
        if (errorMessage != nullptr) {
            const QString details = QString::fromUtf8(err).trimmed();
            *errorMessage = details.isEmpty()
                                ? QStringLiteral("远端采样命令执行失败，退出码 %1。").arg(exitStatus)
                                : QStringLiteral("远端采样命令执行失败：%1").arg(details);
        }
        return false;
    }

    return true;
}

QHash<QString, QString> parseKeyValues(const QByteArray &data)
{
    QHash<QString, QString> values;
    const QList<QByteArray> lines = data.split('\n');
    for (const QByteArray &lineBytes : lines) {
        const QByteArray trimmed = lineBytes.trimmed();
        if (trimmed.isEmpty()) {
            continue;
        }

        const int separator = trimmed.indexOf('=');
        if (separator <= 0) {
            continue;
        }

        const QString key = QString::fromUtf8(trimmed.left(separator)).trimmed();
        const QString value = QString::fromUtf8(trimmed.mid(separator + 1)).trimmed();
        if (!key.isEmpty()) {
            values.insert(key, value);
        }
    }
    return values;
}

quint64 parseUnsigned(const QHash<QString, QString> &values, const QString &key)
{
    bool ok = false;
    const quint64 parsed = values.value(key).toULongLong(&ok);
    return ok ? parsed : 0;
}

double parsePercent(const QHash<QString, QString> &values, const QString &key)
{
    bool ok = false;
    const double parsed = values.value(key).toDouble(&ok);
    if (!ok) {
        return 0.0;
    }
    return qBound(0.0, parsed, 100.0);
}

ProbeResult collectRemoteStats(const SessionProfile &profile,
                               const QString &secret,
                               const RemoteServerStatsMonitor::Sample &previousSample)
{
    ProbeResult result;
    wjsshTrace(QStringLiteral("RemoteServerStatsMonitor probe begin profile=%1").arg(profile.subtitle()));

    QString errorMessage;
    ssh_session session = openAuthenticatedSession(profile, secret, HostKeyPromptHandler(), &errorMessage);
    if (session == nullptr) {
        result.stateText = errorMessage.isEmpty() ? QStringLiteral("远端资源采样连接失败") : errorMessage;
        wjsshTrace(QStringLiteral("RemoteServerStatsMonitor probe connect failed profile=%1 error=%2")
                       .arg(profile.subtitle(), result.stateText));
        return result;
    }

    QByteArray stdoutData;
    QByteArray stderrData;
    const bool ok = executeRemoteCommand(session, buildRemoteProbeCommand(), &stdoutData, &stderrData, &errorMessage);
    closeSession(session);
    if (!ok) {
        if (!stderrData.trimmed().isEmpty()) {
            errorMessage = QStringLiteral("%1 (%2)")
                               .arg(errorMessage, QString::fromUtf8(stderrData).trimmed());
        }
        result.stateText = errorMessage.isEmpty() ? QStringLiteral("远端资源采样失败") : errorMessage;
        wjsshTrace(QStringLiteral("RemoteServerStatsMonitor probe exec failed profile=%1 error=%2")
                       .arg(profile.subtitle(), result.stateText));
        return result;
    }

    const QHash<QString, QString> values = parseKeyValues(stdoutData);
    result.sample.cpuUsagePercent = parsePercent(values, QStringLiteral("cpu_percent"));
    result.sample.memoryTotalBytes = parseUnsigned(values, QStringLiteral("mem_total"));
    result.sample.memoryUsedBytes = parseUnsigned(values, QStringLiteral("mem_used"));
    result.sample.diskTotalBytes = parseUnsigned(values, QStringLiteral("disk_total"));
    result.sample.diskUsedBytes = parseUnsigned(values, QStringLiteral("disk_used"));
    result.sample.networkDownloadBytesTotal = parseUnsigned(values, QStringLiteral("net_rx"));
    result.sample.networkUploadBytesTotal = parseUnsigned(values, QStringLiteral("net_tx"));
    result.sample.timestamp = QDateTime::currentDateTime();

    if (result.sample.memoryTotalBytes > 0) {
        result.sample.memoryUsagePercent =
            qBound(0.0,
                   static_cast<double>(result.sample.memoryUsedBytes) * 100.0
                       / static_cast<double>(result.sample.memoryTotalBytes),
                   100.0);
    }

    if (result.sample.diskTotalBytes > 0) {
        result.sample.diskUsagePercent =
            qBound(0.0,
                   static_cast<double>(result.sample.diskUsedBytes) * 100.0
                       / static_cast<double>(result.sample.diskTotalBytes),
                   100.0);
    }

    if (previousSample.timestamp.isValid() && result.sample.timestamp > previousSample.timestamp
        && result.sample.networkDownloadBytesTotal >= previousSample.networkDownloadBytesTotal
        && result.sample.networkUploadBytesTotal >= previousSample.networkUploadBytesTotal) {
        const qint64 elapsedMs = previousSample.timestamp.msecsTo(result.sample.timestamp);
        if (elapsedMs > 0) {
            const double seconds = static_cast<double>(elapsedMs) / 1000.0;
            result.sample.networkDownloadBytesPerSecond =
                static_cast<double>(result.sample.networkDownloadBytesTotal - previousSample.networkDownloadBytesTotal)
                / seconds;
            result.sample.networkUploadBytesPerSecond =
                static_cast<double>(result.sample.networkUploadBytesTotal - previousSample.networkUploadBytesTotal)
                / seconds;
        }
    }

    const QString host = values.value(QStringLiteral("host"), profile.host);
    const QString os = values.value(QStringLiteral("os"));
    result.sample.summaryText = os.isEmpty() ? host : QStringLiteral("%1 / %2").arg(host, os);
    result.stateText = result.sample.summaryText;
    result.online = true;
    wjsshTrace(QStringLiteral("RemoteServerStatsMonitor probe success profile=%1 cpu=%2 mem=%3/%4 disk=%5/%6 netUp=%7 netDown=%8")
                   .arg(profile.subtitle())
                   .arg(result.sample.cpuUsagePercent, 0, 'f', 1)
                   .arg(result.sample.memoryUsedBytes)
                   .arg(result.sample.memoryTotalBytes)
                   .arg(result.sample.diskUsedBytes)
                   .arg(result.sample.diskTotalBytes)
                   .arg(result.sample.networkUploadBytesPerSecond, 0, 'f', 1)
                   .arg(result.sample.networkDownloadBytesPerSecond, 0, 'f', 1));
    return result;
}

} // namespace

RemoteServerStatsMonitor::RemoteServerStatsMonitor(QObject *parent)
    : QObject(parent)
{
    qRegisterMetaType<SystemStatsMonitor::Sample>();
    m_timer.setInterval(5000);
    connect(&m_timer, &QTimer::timeout, this, &RemoteServerStatsMonitor::refreshNow);
}

RemoteServerStatsMonitor::~RemoteServerStatsMonitor()
{
    stop();
}

bool RemoteServerStatsMonitor::start(const SessionProfile &profile, const QString &secret, QString *errorMessage)
{
    Q_UNUSED(errorMessage);

    stop();

    m_profile = profile;
    m_secret = secret;
    m_latestSample = {};
    m_stateText = QStringLiteral("远端资源采集中");
    m_running = true;
    m_online = false;
    ++m_generation;
    emit availabilityChanged(false, m_stateText);

    m_timer.start();
    beginProbe(true);
    return true;
}

void RemoteServerStatsMonitor::stop()
{
    m_timer.stop();
    ++m_generation;
    m_running = false;
    m_online = false;
    m_probeInFlight = false;
    m_profile = {};
    m_secret.clear();
    m_latestSample = {};
    m_stateText = QStringLiteral("未连接");
}

bool RemoteServerStatsMonitor::isRunning() const
{
    return m_running;
}

bool RemoteServerStatsMonitor::isOnline() const
{
    return m_online;
}

bool RemoteServerStatsMonitor::hasSample() const
{
    return m_latestSample.timestamp.isValid();
}

void RemoteServerStatsMonitor::refreshNow()
{
    beginProbe(false);
}

RemoteServerStatsMonitor::Sample RemoteServerStatsMonitor::latestSample() const
{
    return m_latestSample;
}

QString RemoteServerStatsMonitor::stateText() const
{
    return m_stateText;
}

void RemoteServerStatsMonitor::beginProbe(bool immediate)
{
    if (!m_running || m_probeInFlight) {
        return;
    }

    const quint64 generation = m_generation;
    const SessionProfile profile = m_profile;
    const QString secret = m_secret;
    const Sample previousSample = m_latestSample;

    m_probeInFlight = true;
    if (immediate && !m_online) {
        m_stateText = QStringLiteral("远端资源采集中");
        emit availabilityChanged(false, m_stateText);
    }

    QPointer<RemoteServerStatsMonitor> guard(this);
    QThread *thread = QThread::create([guard, generation, profile, secret, previousSample]() {
        const ProbeResult result = collectRemoteStats(profile, secret, previousSample);
        if (guard.isNull()) {
            return;
        }

        QMetaObject::invokeMethod(
            guard,
            [guard, generation, result]() {
                if (guard.isNull()) {
                    return;
                }
                guard->finishProbe(generation, result.sample, result.online, result.stateText);
            },
            Qt::QueuedConnection);
    });

    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void RemoteServerStatsMonitor::finishProbe(quint64 generation,
                                           const Sample &sample,
                                           bool online,
                                           const QString &stateText)
{
    if (generation != m_generation) {
        return;
    }

    m_probeInFlight = false;
    m_online = online;
    m_stateText = stateText;
    if (online) {
        m_latestSample = sample;
        emit statsUpdated();
    } else {
        emit statsUpdated();
    }
    emit availabilityChanged(m_online, m_stateText);
}
