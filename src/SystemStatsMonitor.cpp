#include "SystemStatsMonitor.h"

#include <QLibrary>

#include <vector>

#ifdef Q_OS_WIN
#    include <iphlpapi.h>
#    include <windows.h>
#endif

namespace {

QString formatBytes(quint64 bytes)
{
    static const char *units[] = {"B", "KB", "MB", "GB", "TB", "PB"};
    double value = static_cast<double>(bytes);
    int unitIndex = 0;
    while (value >= 1024.0 && unitIndex < 5) {
        value /= 1024.0;
        ++unitIndex;
    }

    return QStringLiteral("%1 %2")
        .arg(QString::number(value, 'f', value >= 10.0 || unitIndex == 0 ? 0 : 1),
             QString::fromLatin1(units[unitIndex]));
}

QString formatRate(double bytesPerSecond)
{
    static const char *units[] = {"B/s", "KB/s", "MB/s", "GB/s"};
    double value = qMax(0.0, bytesPerSecond);
    int unitIndex = 0;
    while (value >= 1024.0 && unitIndex < 3) {
        value /= 1024.0;
        ++unitIndex;
    }

    return QStringLiteral("%1 %2")
        .arg(QString::number(value, 'f', value >= 10.0 || unitIndex == 0 ? 0 : 1),
             QString::fromLatin1(units[unitIndex]));
}

QString formatPercent(double value)
{
    return QStringLiteral("%1%").arg(QString::number(value, 'f', value >= 10.0 ? 0 : 1));
}

#ifdef Q_OS_WIN
quint64 fileTimeToTicks(const FILETIME &time)
{
    ULARGE_INTEGER value;
    value.LowPart = time.dwLowDateTime;
    value.HighPart = time.dwHighDateTime;
    return value.QuadPart;
}
#endif

} // namespace

SystemStatsMonitor::SystemStatsMonitor(QObject *parent)
    : QObject(parent)
{
    m_timer.setInterval(1000);
    m_timer.setTimerType(Qt::CoarseTimer);
    connect(&m_timer, &QTimer::timeout, this, &SystemStatsMonitor::sampleNow);
    collectSample();
}

void SystemStatsMonitor::setIntervalMs(int intervalMs)
{
    m_timer.setInterval(qBound(250, intervalMs, 60000));
}

int SystemStatsMonitor::intervalMs() const
{
    return m_timer.interval();
}

void SystemStatsMonitor::start()
{
    if (!m_timer.isActive()) {
        collectSample();
        m_timer.start();
    }
}

void SystemStatsMonitor::stop()
{
    m_timer.stop();
}

bool SystemStatsMonitor::isRunning() const
{
    return m_timer.isActive();
}

void SystemStatsMonitor::refreshNow()
{
    collectSample();
}

SystemStatsMonitor::Sample SystemStatsMonitor::latestSample() const
{
    return m_latestSample;
}

QString SystemStatsMonitor::summaryText() const
{
    return m_latestSample.summaryText;
}

void SystemStatsMonitor::sampleNow()
{
    collectSample();
}

void SystemStatsMonitor::collectSample()
{
    updateCpu();
    updateMemory();
    updateDisk();
    updateNetwork();
    m_latestSample.timestamp = QDateTime::currentDateTime();
    m_latestSample.summaryText = buildSummary();
    emit statsUpdated();
}

void SystemStatsMonitor::updateCpu()
{
#ifdef Q_OS_WIN
    FILETIME idleTime{};
    FILETIME kernelTime{};
    FILETIME userTime{};
    if (!GetSystemTimes(&idleTime, &kernelTime, &userTime)) {
        m_latestSample.cpuUsagePercent = 0.0;
        return;
    }

    const quint64 idleTicks = fileTimeToTicks(idleTime);
    const quint64 kernelTicks = fileTimeToTicks(kernelTime);
    const quint64 userTicks = fileTimeToTicks(userTime);

    if (!m_hasCpuBaseline) {
        m_prevCpuIdleTicks = idleTicks;
        m_prevCpuKernelTicks = kernelTicks;
        m_prevCpuUserTicks = userTicks;
        m_hasCpuBaseline = true;
        m_latestSample.cpuUsagePercent = 0.0;
        return;
    }

    const quint64 idleDelta = idleTicks - m_prevCpuIdleTicks;
    const quint64 kernelDelta = kernelTicks - m_prevCpuKernelTicks;
    const quint64 userDelta = userTicks - m_prevCpuUserTicks;
    const quint64 totalDelta = kernelDelta + userDelta;

    m_prevCpuIdleTicks = idleTicks;
    m_prevCpuKernelTicks = kernelTicks;
    m_prevCpuUserTicks = userTicks;

    if (totalDelta == 0) {
        m_latestSample.cpuUsagePercent = 0.0;
        return;
    }

    const double busyTicks = static_cast<double>(totalDelta - idleDelta);
    m_latestSample.cpuUsagePercent = qBound(0.0, (busyTicks * 100.0) / static_cast<double>(totalDelta), 100.0);
#else
    m_latestSample.cpuUsagePercent = 0.0;
#endif
}

void SystemStatsMonitor::updateMemory()
{
#ifdef Q_OS_WIN
    MEMORYSTATUSEX status{};
    status.dwLength = sizeof(status);
    if (!GlobalMemoryStatusEx(&status)) {
        m_latestSample.memoryUsedBytes = 0;
        m_latestSample.memoryTotalBytes = 0;
        m_latestSample.memoryUsagePercent = 0.0;
        return;
    }

    m_latestSample.memoryTotalBytes = static_cast<quint64>(status.ullTotalPhys);
    const quint64 freeBytes = static_cast<quint64>(status.ullAvailPhys);
    m_latestSample.memoryUsedBytes = m_latestSample.memoryTotalBytes > freeBytes
                                         ? m_latestSample.memoryTotalBytes - freeBytes
                                         : 0;
    m_latestSample.memoryUsagePercent = status.dwMemoryLoad;
#else
    m_latestSample.memoryUsedBytes = 0;
    m_latestSample.memoryTotalBytes = 0;
    m_latestSample.memoryUsagePercent = 0.0;
#endif
}

void SystemStatsMonitor::updateDisk()
{
#ifdef Q_OS_WIN
    DWORD requiredSize = GetLogicalDriveStringsW(0, nullptr);
    if (requiredSize == 0) {
        m_latestSample.diskUsedBytes = 0;
        m_latestSample.diskTotalBytes = 0;
        m_latestSample.diskUsagePercent = 0.0;
        return;
    }

    std::vector<wchar_t> buffer(static_cast<size_t>(requiredSize) + 1, L'\0');
    if (GetLogicalDriveStringsW(requiredSize, buffer.data()) == 0) {
        m_latestSample.diskUsedBytes = 0;
        m_latestSample.diskTotalBytes = 0;
        m_latestSample.diskUsagePercent = 0.0;
        return;
    }

    quint64 totalBytes = 0;
    quint64 freeBytes = 0;
    const wchar_t *drive = buffer.data();
    while (*drive != L'\0') {
        if (GetDriveTypeW(drive) == DRIVE_FIXED) {
            ULARGE_INTEGER freeAvailable{};
            ULARGE_INTEGER total{};
            ULARGE_INTEGER freeTotal{};
            if (GetDiskFreeSpaceExW(drive, &freeAvailable, &total, &freeTotal)) {
                totalBytes += static_cast<quint64>(total.QuadPart);
                freeBytes += static_cast<quint64>(freeTotal.QuadPart);
            }
        }

        while (*drive != L'\0') {
            ++drive;
        }
        ++drive;
    }

    m_latestSample.diskTotalBytes = totalBytes;
    m_latestSample.diskUsedBytes = totalBytes > freeBytes ? totalBytes - freeBytes : 0;
    m_latestSample.diskUsagePercent = totalBytes == 0
                                          ? 0.0
                                          : (static_cast<double>(m_latestSample.diskUsedBytes) * 100.0)
                                                / static_cast<double>(totalBytes);
#else
    m_latestSample.diskUsedBytes = 0;
    m_latestSample.diskTotalBytes = 0;
    m_latestSample.diskUsagePercent = 0.0;
#endif
}

void SystemStatsMonitor::updateNetwork()
{
#ifdef Q_OS_WIN
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    quint64 inboundBytes = 0;
    quint64 outboundBytes = 0;

    QLibrary iphlp(QStringLiteral("iphlpapi.dll"));
    if (iphlp.load()) {
        using GetIfTableFn = DWORD(WINAPI *)(PMIB_IFTABLE, PULONG, BOOL);
        const auto getIfTable = reinterpret_cast<GetIfTableFn>(iphlp.resolve("GetIfTable"));
        if (getIfTable != nullptr) {
            ULONG size = 0;
            DWORD result = getIfTable(nullptr, &size, FALSE);
            if (result == ERROR_INSUFFICIENT_BUFFER && size > 0) {
                std::vector<quint8> storage(static_cast<size_t>(size));
                auto *table = reinterpret_cast<PMIB_IFTABLE>(storage.data());
                if (getIfTable(table, &size, FALSE) == NO_ERROR && table != nullptr) {
                    for (DWORD index = 0; index < table->dwNumEntries; ++index) {
                        const MIB_IFROW &row = table->table[index];
                        if (row.dwType == IF_TYPE_SOFTWARE_LOOPBACK || row.dwOperStatus != 1) {
                            continue;
                        }
                        inboundBytes += static_cast<quint64>(row.dwInOctets);
                        outboundBytes += static_cast<quint64>(row.dwOutOctets);
                    }
                }
            }
        }
    }

    m_latestSample.networkDownloadBytesTotal = inboundBytes;
    m_latestSample.networkUploadBytesTotal = outboundBytes;

    if (!m_hasNetBaseline) {
        m_prevNetInboundBytes = inboundBytes;
        m_prevNetOutboundBytes = outboundBytes;
        m_prevNetSampleMs = nowMs;
        m_hasNetBaseline = true;
        m_latestSample.networkDownloadBytesPerSecond = 0.0;
        m_latestSample.networkUploadBytesPerSecond = 0.0;
        return;
    }

    const qint64 elapsedMs = qMax<qint64>(1, nowMs - m_prevNetSampleMs);
    const quint64 inboundDelta = inboundBytes >= m_prevNetInboundBytes ? inboundBytes - m_prevNetInboundBytes : 0;
    const quint64 outboundDelta = outboundBytes >= m_prevNetOutboundBytes ? outboundBytes - m_prevNetOutboundBytes : 0;

    m_prevNetInboundBytes = inboundBytes;
    m_prevNetOutboundBytes = outboundBytes;
    m_prevNetSampleMs = nowMs;
    m_latestSample.networkDownloadBytesPerSecond = static_cast<double>(inboundDelta) * 1000.0 / elapsedMs;
    m_latestSample.networkUploadBytesPerSecond = static_cast<double>(outboundDelta) * 1000.0 / elapsedMs;
#else
    m_latestSample.networkDownloadBytesTotal = 0;
    m_latestSample.networkUploadBytesTotal = 0;
    m_latestSample.networkDownloadBytesPerSecond = 0.0;
    m_latestSample.networkUploadBytesPerSecond = 0.0;
#endif
}

QString SystemStatsMonitor::buildSummary() const
{
    return QStringLiteral("CPU %1 | 内存 %2/%3 | 磁盘 %4/%5 | 网络 ↑%6 ↓%7")
        .arg(formatPercent(m_latestSample.cpuUsagePercent),
             formatBytes(m_latestSample.memoryUsedBytes),
             formatBytes(m_latestSample.memoryTotalBytes),
             formatBytes(m_latestSample.diskUsedBytes),
             formatBytes(m_latestSample.diskTotalBytes),
             formatRate(m_latestSample.networkUploadBytesPerSecond),
             formatRate(m_latestSample.networkDownloadBytesPerSecond));
}

QString SystemStatsMonitor::formatBytes(quint64 bytes)
{
    return ::formatBytes(bytes);
}

QString SystemStatsMonitor::formatRate(double bytesPerSecond)
{
    return ::formatRate(bytesPerSecond);
}

QString SystemStatsMonitor::formatPercent(double value)
{
    return ::formatPercent(value);
}
