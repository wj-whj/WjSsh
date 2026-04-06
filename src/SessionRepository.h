#pragma once

#include "SessionProfile.h"

#include <QVector>

class SessionRepository {
public:
    [[nodiscard]] QVector<SessionProfile> load() const;
    bool save(const QVector<SessionProfile> &profiles, QString *errorMessage) const;
    [[nodiscard]] QString storagePath() const;
};

