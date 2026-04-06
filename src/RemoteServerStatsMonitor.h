#pragma once

#include "SessionProfile.h"
#include "SystemStatsMonitor.h"

#include <QObject>
#include <QTimer>

class RemoteServerStatsMonitor : public QObject {
    Q_OBJECT

public:
    using Sample = SystemStatsMonitor::Sample;

    explicit RemoteServerStatsMonitor(QObject *parent = nullptr);
    ~RemoteServerStatsMonitor() override;

    bool start(const SessionProfile &profile, const QString &secret, QString *errorMessage = nullptr);
    void stop();

    [[nodiscard]] bool isRunning() const;
    [[nodiscard]] bool isOnline() const;
    [[nodiscard]] bool hasSample() const;
    void refreshNow();

    [[nodiscard]] Sample latestSample() const;
    [[nodiscard]] QString stateText() const;

signals:
    void statsUpdated();
    void availabilityChanged(bool online, const QString &reason);

private:
    void beginProbe(bool immediate);
    void finishProbe(quint64 generation, const Sample &sample, bool online, const QString &stateText);

    QTimer m_timer;
    SessionProfile m_profile;
    QString m_secret;
    Sample m_latestSample;
    QString m_stateText;
    quint64 m_generation = 0;
    bool m_running = false;
    bool m_online = false;
    bool m_probeInFlight = false;
};
