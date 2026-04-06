#pragma once

#include <QObject>
#include <QDateTime>
#include <QMetaType>
#include <QTimer>

class SystemStatsMonitor : public QObject {
    Q_OBJECT

public:
    struct Sample {
        double cpuUsagePercent = 0.0;
        double memoryUsagePercent = 0.0;
        quint64 memoryUsedBytes = 0;
        quint64 memoryTotalBytes = 0;
        double diskUsagePercent = 0.0;
        quint64 diskUsedBytes = 0;
        quint64 diskTotalBytes = 0;
        double networkUploadBytesPerSecond = 0.0;
        double networkDownloadBytesPerSecond = 0.0;
        quint64 networkUploadBytesTotal = 0;
        quint64 networkDownloadBytesTotal = 0;
        QString summaryText;
        QDateTime timestamp;
    };

    explicit SystemStatsMonitor(QObject *parent = nullptr);

    void setIntervalMs(int intervalMs);
    [[nodiscard]] int intervalMs() const;

    void start();
    void stop();
    [[nodiscard]] bool isRunning() const;

    void refreshNow();

    [[nodiscard]] Sample latestSample() const;
    [[nodiscard]] QString summaryText() const;

signals:
    void statsUpdated();

private slots:
    void sampleNow();

private:
    void collectSample();
    void updateCpu();
    void updateMemory();
    void updateDisk();
    void updateNetwork();
    [[nodiscard]] QString buildSummary() const;

    static QString formatBytes(quint64 bytes);
    static QString formatRate(double bytesPerSecond);
    static QString formatPercent(double value);

    QTimer m_timer;
    Sample m_latestSample;

    quint64 m_prevCpuIdleTicks = 0;
    quint64 m_prevCpuKernelTicks = 0;
    quint64 m_prevCpuUserTicks = 0;
    bool m_hasCpuBaseline = false;

    quint64 m_prevNetInboundBytes = 0;
    quint64 m_prevNetOutboundBytes = 0;
    qint64 m_prevNetSampleMs = 0;
    bool m_hasNetBaseline = false;
};

Q_DECLARE_METATYPE(SystemStatsMonitor::Sample)
