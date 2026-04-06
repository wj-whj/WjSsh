#pragma once

#include "SessionProfile.h"

#include <QString>
#include <functional>

extern "C" {
#include <libssh/libssh.h>
}

using HostKeyPromptHandler = std::function<bool(const QString &title,
                                                const QString &message,
                                                const QString &fingerprint)>;

ssh_session openAuthenticatedSession(const SessionProfile &profile,
                                     const QString &secret,
                                     const HostKeyPromptHandler &hostPrompt,
                                     QString *errorMessage);

QString libsshError(void *handle);

