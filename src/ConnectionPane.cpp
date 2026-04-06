#include "ConnectionPane.h"

#include "DebugTrace.h"
#include "Localization.h"
#include "UiChrome.h"

#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileIconProvider>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QFrame>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QItemSelectionModel>
#include <QMenu>
#include <QMimeData>
#include <QMessageBox>
#include <QMouseEvent>
#include <QProgressDialog>
#include <QRegularExpression>
#include <QElapsedTimer>
#include <QScopedValueRollback>
#include <QShortcut>
#include <QStandardPaths>
#include <QStyle>
#include <QTimer>
#include <QUuid>
#include <QUrl>
#include <QVBoxLayout>
#include <string>
#include <utility>

#if defined(Q_OS_WIN)
#include <Windows.h>
#include <shlobj.h>
#endif

namespace {
QString joinRemotePath(const QString &base, const QString &name)
{
    QString normalizedBase = base;
    normalizedBase.replace('\\', '/');
    if (normalizedBase.endsWith('/')) {
        return normalizedBase + name;
    }
    if (normalizedBase.isEmpty() || normalizedBase == "/") {
        return "/" + name;
    }
    return normalizedBase + "/" + name;
}

QString parentRemotePath(const QString &path)
{
    QString normalized = path;
    normalized.replace('\\', '/');
    if (normalized.isEmpty() || normalized == "/") {
        return "/";
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

QString shellQuote(QString path)
{
    path.replace('\'', "'\"'\"'");
    return "'" + path + "'";
}

QString humanReadableSize(quint64 size)
{
    static const QStringList units = {QStringLiteral("B"),
                                      QStringLiteral("KB"),
                                      QStringLiteral("MB"),
                                      QStringLiteral("GB"),
                                      QStringLiteral("TB")};

    double value = static_cast<double>(size);
    int unit = 0;
    while (value >= 1024.0 && unit < units.size() - 1) {
        value /= 1024.0;
        ++unit;
    }

    return unit == 0 ? QString::number(size) + " " + units[unit]
                     : QString::number(value, 'f', 1) + " " + units[unit];
}

QString normalizedRemoteSuffix(const QString &name)
{
    const QString lower = name.toLower();
    if (lower.endsWith(QStringLiteral(".tar.gz")) || lower.endsWith(QStringLiteral(".tgz"))) {
        return QStringLiteral("tgz");
    }
    if (lower.endsWith(QStringLiteral(".tar.bz2")) || lower.endsWith(QStringLiteral(".tbz2"))) {
        return QStringLiteral("tbz");
    }
    if (lower.endsWith(QStringLiteral(".tar.xz")) || lower.endsWith(QStringLiteral(".txz"))) {
        return QStringLiteral("txz");
    }
    return QFileInfo(name).suffix().toLower();
}

QString remoteEntryTypeLabel(const RemoteEntry &entry)
{
    if (entry.isDirectory) {
        return Localization::translateText(QStringLiteral("目录"));
    }

    const QString suffix = normalizedRemoteSuffix(entry.name);
    return suffix.isEmpty() ? Localization::translateText(QStringLiteral("文件"))
                            : Localization::translateText(QStringLiteral("%1 文件").arg(suffix.toUpper()));
}

QIcon remoteEntryIcon(const RemoteEntry &entry, QWidget *widget)
{
    if (entry.isDirectory) {
        return widget != nullptr ? widget->style()->standardIcon(QStyle::SP_DirIcon) : QIcon();
    }

    static QFileIconProvider provider;
    const QString suffix = normalizedRemoteSuffix(entry.name);
    if (!suffix.isEmpty()) {
        const QString probeDir =
            QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation)).filePath(QStringLiteral("WjSshIconProbe"));
        QDir().mkpath(probeDir);
        const QString probePath = QDir(probeDir).filePath(QStringLiteral("probe.%1").arg(suffix));
        bool created = false;
        if (!QFileInfo::exists(probePath)) {
            QFile probe(probePath);
            if (probe.open(QIODevice::WriteOnly)) {
                probe.close();
                created = true;
            }
        }

        const QIcon icon = provider.icon(QFileInfo(probePath));
        if (created) {
            QFile::remove(probePath);
        }
        if (!icon.isNull()) {
            return icon;
        }
    }

    return widget != nullptr ? widget->style()->standardIcon(QStyle::SP_FileIcon) : provider.icon(QFileIconProvider::File);
}

QString remoteEntryToolTip(const RemoteEntry &entry, const QString &typeLabel)
{
    return Localization::translateText(QStringLiteral("名称：%1\n完整路径：%2\n类型：%3\n大小：%4\n修改时间：%5\n权限：%6")
                                           .arg(entry.name,
                                                entry.path,
                                                typeLabel,
                                                entry.isDirectory ? QStringLiteral("-") : humanReadableSize(entry.size),
                                                entry.modifiedAt.isValid()
                                                    ? entry.modifiedAt.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))
                                                    : QStringLiteral("-"),
                                                entry.permissions));
}

#if defined(Q_OS_WIN)
QString windowsFileGroupDescriptorMime()
{
    return QStringLiteral("application/x-qt-windows-mime;value=\"FileGroupDescriptorW\"");
}

QString windowsFileGroupDescriptorAnsiMime()
{
    return QStringLiteral("application/x-qt-windows-mime;value=\"FileGroupDescriptor\"");
}

QString windowsFileContentsMime()
{
    return QStringLiteral("application/x-qt-windows-mime;value=\"FileContents\";index=0");
}

FILETIME fileTimeFromDateTime(const QDateTime &dateTime)
{
    FILETIME result{};
    if (!dateTime.isValid()) {
        return result;
    }

    const qint64 unixMs = dateTime.toUTC().toMSecsSinceEpoch();
    const quint64 fileTimeTicks = (static_cast<quint64>(unixMs) + 11644473600000ULL) * 10000ULL;
    result.dwLowDateTime = static_cast<DWORD>(fileTimeTicks & 0xFFFFFFFFULL);
    result.dwHighDateTime = static_cast<DWORD>(fileTimeTicks >> 32);
    return result;
}

QByteArray buildWindowsFileDescriptor(const QString &fileName, quint64 size, const QDateTime &modifiedAt)
{
    QByteArray bytes(sizeof(FILEGROUPDESCRIPTORW), Qt::Uninitialized);
    bytes.fill('\0');

    auto *descriptor = reinterpret_cast<FILEGROUPDESCRIPTORW *>(bytes.data());
    descriptor->cItems = 1;
    descriptor->fgd[0].dwFlags = FD_ATTRIBUTES | FD_FILESIZE;
#ifdef FD_UNICODE
    descriptor->fgd[0].dwFlags |= FD_UNICODE;
#endif
    descriptor->fgd[0].dwFileAttributes = FILE_ATTRIBUTE_NORMAL;
    descriptor->fgd[0].nFileSizeHigh = static_cast<DWORD>(size >> 32);
    descriptor->fgd[0].nFileSizeLow = static_cast<DWORD>(size & 0xFFFFFFFFULL);
    if (modifiedAt.isValid()) {
        descriptor->fgd[0].dwFlags |= FD_WRITESTIME;
        descriptor->fgd[0].ftLastWriteTime = fileTimeFromDateTime(modifiedAt);
    }

    const std::wstring wideName = fileName.left(MAX_PATH - 1).toStdWString();
    wcsncpy_s(descriptor->fgd[0].cFileName, MAX_PATH, wideName.c_str(), _TRUNCATE);
    return bytes;
}

QByteArray buildWindowsAnsiFileDescriptor(const QString &fileName, quint64 size, const QDateTime &modifiedAt)
{
    QByteArray bytes(sizeof(FILEGROUPDESCRIPTORA), Qt::Uninitialized);
    bytes.fill('\0');

    auto *descriptor = reinterpret_cast<FILEGROUPDESCRIPTORA *>(bytes.data());
    descriptor->cItems = 1;
    descriptor->fgd[0].dwFlags = FD_ATTRIBUTES | FD_FILESIZE;
    descriptor->fgd[0].dwFileAttributes = FILE_ATTRIBUTE_NORMAL;
    descriptor->fgd[0].nFileSizeHigh = static_cast<DWORD>(size >> 32);
    descriptor->fgd[0].nFileSizeLow = static_cast<DWORD>(size & 0xFFFFFFFFULL);
    if (modifiedAt.isValid()) {
        descriptor->fgd[0].dwFlags |= FD_WRITESTIME;
        descriptor->fgd[0].ftLastWriteTime = fileTimeFromDateTime(modifiedAt);
    }

    const QByteArray localName = fileName.left(MAX_PATH - 1).toLocal8Bit();
    strncpy_s(descriptor->fgd[0].cFileName, MAX_PATH, localName.constData(), _TRUNCATE);
    return bytes;
}

class VirtualRemoteFileMimeData final : public QMimeData {
public:
    using ContentProvider = std::function<QByteArray(QString *)>;

    VirtualRemoteFileMimeData(QString displayName,
                              quint64 fileSize,
                              QDateTime modifiedAt,
                              ContentProvider provider)
        : m_displayName(std::move(displayName))
        , m_fileSize(fileSize)
        , m_modifiedAt(std::move(modifiedAt))
        , m_provider(std::move(provider))
    {
    }

    [[nodiscard]] bool contentRequested() const
    {
        return m_contentRequested;
    }

    [[nodiscard]] bool contentReady() const
    {
        return m_contentLoaded;
    }

    [[nodiscard]] QString lastError() const
    {
        return m_lastError;
    }

protected:
    QStringList formats() const override
    {
        return {windowsFileGroupDescriptorMime(), windowsFileGroupDescriptorAnsiMime(), windowsFileContentsMime()};
    }

    bool hasFormat(const QString &mimeType) const override
    {
        return mimeType == windowsFileGroupDescriptorMime()
               || mimeType == windowsFileGroupDescriptorAnsiMime()
               || mimeType == windowsFileContentsMime();
    }

    QVariant retrieveData(const QString &mimeType, QMetaType type) const override
    {
        Q_UNUSED(type);

        if (mimeType == windowsFileGroupDescriptorMime()) {
            return buildWindowsFileDescriptor(m_displayName, m_fileSize, m_modifiedAt);
        }

        if (mimeType == windowsFileGroupDescriptorAnsiMime()) {
            return buildWindowsAnsiFileDescriptor(m_displayName, m_fileSize, m_modifiedAt);
        }

        if (mimeType == windowsFileContentsMime()) {
            m_contentRequested = true;
            if (!m_contentLoaded && m_lastError.isEmpty() && m_provider) {
                m_content = m_provider(&m_lastError);
                m_contentLoaded = !m_content.isEmpty() || m_fileSize == 0;
            }
            return m_content;
        }

        return {};
    }

private:
    QString m_displayName;
    quint64 m_fileSize = 0;
    QDateTime m_modifiedAt;
    ContentProvider m_provider;
    mutable bool m_contentRequested = false;
    mutable bool m_contentLoaded = false;
    mutable QByteArray m_content;
    mutable QString m_lastError;
};
#endif

void localizeMessageBoxButtons(QMessageBox &box)
{
    if (auto *button = box.button(QMessageBox::Ok)) {
        button->setText(Localization::translateText(QStringLiteral("确定")));
    }
    if (auto *button = box.button(QMessageBox::Cancel)) {
        button->setText(Localization::translateText(QStringLiteral("取消")));
    }
    if (auto *button = box.button(QMessageBox::Yes)) {
        button->setText(Localization::translateText(QStringLiteral("是")));
    }
    if (auto *button = box.button(QMessageBox::No)) {
        button->setText(Localization::translateText(QStringLiteral("否")));
    }
}

QMessageBox::StandardButton execThemedMessageBox(QWidget *parent,
                                                 QMessageBox::Icon icon,
                                                 const QString &title,
                                                 const QString &text,
                                                 QMessageBox::StandardButtons buttons = QMessageBox::Ok,
                                                 QMessageBox::StandardButton defaultButton = QMessageBox::Ok)
{
    QMessageBox box(icon,
                    Localization::translateText(title),
                    Localization::translateText(text),
                    buttons,
                    parent);
    box.setDefaultButton(defaultButton);
    localizeMessageBoxButtons(box);
    UiChrome::applyMessageBoxTheme(&box);
    return static_cast<QMessageBox::StandardButton>(box.exec());
}

QString execThemedTextInput(QWidget *parent,
                            const QString &title,
                            const QString &label,
                            const QString &value,
                            bool *accepted)
{
    QInputDialog dialog(parent);
    dialog.setInputMode(QInputDialog::TextInput);
    dialog.setWindowTitle(Localization::translateText(title));
    dialog.setLabelText(Localization::translateText(label));
    dialog.setTextValue(value);
    dialog.setOkButtonText(Localization::translateText(QStringLiteral("确定")));
    dialog.setCancelButtonText(Localization::translateText(QStringLiteral("取消")));
    UiChrome::applyInputDialogTheme(&dialog);

    const bool ok = dialog.exec() == QDialog::Accepted;
    if (accepted != nullptr) {
        *accepted = ok;
    }
    return ok ? dialog.textValue() : QString();
}

QStringList execThemedOpenFilesDialog(QWidget *parent, const QString &title)
{
    QFileDialog dialog(parent, Localization::translateText(title));
    dialog.setFileMode(QFileDialog::ExistingFiles);
    dialog.setOption(QFileDialog::DontUseNativeDialog, true);
    UiChrome::applyDialogTheme(&dialog);
    return dialog.exec() == QDialog::Accepted ? dialog.selectedFiles() : QStringList();
}

QString execThemedExistingDirectoryDialog(QWidget *parent, const QString &title)
{
    QFileDialog dialog(parent, Localization::translateText(title));
    dialog.setFileMode(QFileDialog::Directory);
    dialog.setOption(QFileDialog::ShowDirsOnly, true);
    dialog.setOption(QFileDialog::DontUseNativeDialog, true);
    UiChrome::applyDialogTheme(&dialog);
    return dialog.exec() == QDialog::Accepted && !dialog.selectedFiles().isEmpty()
               ? dialog.selectedFiles().constFirst()
               : QString();
}

QString execThemedSaveFileDialog(QWidget *parent, const QString &title, const QString &initialName)
{
    QFileDialog dialog(parent, Localization::translateText(title), initialName);
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setFileMode(QFileDialog::AnyFile);
    dialog.setOption(QFileDialog::DontUseNativeDialog, true);
    UiChrome::applyDialogTheme(&dialog);
    return dialog.exec() == QDialog::Accepted && !dialog.selectedFiles().isEmpty()
               ? dialog.selectedFiles().constFirst()
               : QString();
}

QString sessionBootstrapScript()
{
    return QStringLiteral(
        "case \"$(uname -s 2>/dev/null)\" in "
        "Darwin) "
        "export CLICOLOR=1 CLICOLOR_FORCE=1 COLORTERM=truecolor TERM_PROGRAM=WjSsh TERM_PROGRAM_VERSION=0.1.0 LC_TERMINAL=WjSsh LC_TERMINAL_VERSION=0.1.0; "
        "[ -n \"$LSCOLORS\" ] || export LSCOLORS='ExFxCxDxBxegedabagacad'; "
        "if [ -n \"$ZSH_VERSION\" ]; then "
        "PROMPT_EOL_MARK=''; "
        "unsetopt PROMPT_SP 2>/dev/null || true; "
        "autoload -Uz colors >/dev/null 2>&1 && colors >/dev/null 2>&1; "
        "case \"$PROMPT\" in '%n@%m %1~ %# '|'%m%# '|'%~ %# ') PROMPT='%F{81}%n@%m%f %F{110}%1~%f $ ' ;; esac; "
        "fi; "
        "alias ls='ls -G'; "
        "clear 2>/dev/null || printf '\\033[2J\\033[H'; "
        ";; "
        "esac\n");
}

QString stripTerminalSequences(const QString &text)
{
    QString plain;
    plain.reserve(text.size());

    for (int index = 0; index < text.size();) {
        const QChar ch = text.at(index);
        if (ch == QChar('\x1b')) {
            if (index + 1 >= text.size()) {
                break;
            }
            if (text.at(index + 1) == QChar('[')) {
                int end = index + 2;
                while (end < text.size()) {
                    const ushort code = text.at(end).unicode();
                    if (code >= 0x40 && code <= 0x7E) {
                        break;
                    }
                    ++end;
                }
                index = end < text.size() ? end + 1 : text.size();
                continue;
            }
            if (text.at(index + 1) == QChar(']')) {
                const int bell = text.indexOf(QChar('\x07'), index + 2);
                const int st = text.indexOf(QStringLiteral("\x1b\\"), index + 2);
                const int end = (bell >= 0 && (st < 0 || bell < st)) ? bell + 1 : (st >= 0 ? st + 2 : -1);
                index = end >= 0 ? end : text.size();
                continue;
            }
            index += 2;
            continue;
        }
        if (ch == QChar('\b')) {
            if (!plain.isEmpty()) {
                plain.chop(1);
            }
            ++index;
            continue;
        }
        if (ch.unicode() < 0x20 && ch != QChar('\r') && ch != QChar('\n') && ch != QChar('\t')) {
            ++index;
            continue;
        }
        plain.append(ch);
        ++index;
    }

    return plain;
}

bool isTransferCancelledMessage(const QString &message)
{
    return message.contains(QStringLiteral("传输已取消")) || message.contains(QStringLiteral("取消"))
           || message.contains(QStringLiteral("cancel"), Qt::CaseInsensitive);
}

struct PromptInfo {
    QString user;
    QString path;
};

PromptInfo extractPromptInfo(const QString &line)
{
    static const QRegularExpression bashStyle(
        QStringLiteral(R"(^(?<user>[^@\s]+)@[^:\s]+:(?<path>.+?)(?:\s*)[$#%]\s*$)"));
    static const QRegularExpression spacedStyle(
        QStringLiteral(R"(^(?<user>[^@\s]+)@[^:\s]+\s+(?<path>.+?)\s+[$#%]\s*$)"));

    const QString trimmed = line.trimmed();
    if (trimmed.isEmpty()) {
        return {};
    }

    if (const auto match = bashStyle.match(trimmed); match.hasMatch()) {
        return {match.captured(QStringLiteral("user")).trimmed(),
                match.captured(QStringLiteral("path")).trimmed()};
    }
    if (const auto match = spacedStyle.match(trimmed); match.hasMatch()) {
        return {match.captured(QStringLiteral("user")).trimmed(),
                match.captured(QStringLiteral("path")).trimmed()};
    }
    return {};
}
}

ConnectionPane::ConnectionPane(const SessionProfile &profile, QWidget *parent)
    : QWidget(parent)
    , m_profile(profile)
    , m_remoteStatsMonitor(new RemoteServerStatsMonitor(this))
{
    buildUi();
    Localization::applyWidgetTexts(this);

    connect(&m_shellClient, &SshShellClient::outputReceived, this, &ConnectionPane::handleShellOutput);
    connect(&m_shellClient,
            &SshShellClient::disconnectedUnexpectedly,
            this,
            &ConnectionPane::handleShellDisconnect);
    connect(m_remoteStatsMonitor,
            &RemoteServerStatsMonitor::statsUpdated,
            this,
            [this]() { emit remoteStatsChanged(this); });
    connect(m_remoteStatsMonitor,
            &RemoteServerStatsMonitor::availabilityChanged,
            this,
            [this](bool, const QString &) { emit remoteStatsChanged(this); });

    auto *clearShortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+L")), this);
    connect(clearShortcut, &QShortcut::activated, this, &ConnectionPane::clearConsole);

    auto *toggleSftpShortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+S")), this);
    connect(toggleSftpShortcut, &QShortcut::activated, this, &ConnectionPane::toggleSftpPanel);

    auto *copyShortcut = new QShortcut(QKeySequence::Copy, this);
    copyShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(copyShortcut, &QShortcut::activated, this, [this]() {
        if (m_remoteTable != nullptr && (m_remoteTable->hasFocus() || m_remoteTable->viewport()->hasFocus())) {
            copySelectedToClipboard();
        }
    });

    auto *pasteShortcut = new QShortcut(QKeySequence::Paste, this);
    pasteShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(pasteShortcut, &QShortcut::activated, this, [this]() {
        if (m_sftpPanel != nullptr && m_sftpPanel->isVisible()
            && m_remoteTable != nullptr
            && (m_remoteTable->hasFocus() || m_remoteTable->viewport()->hasFocus())) {
            pasteFromClipboard();
        }
    });

    setConnectedState(false);
    updateSftpToggleButton();
    setSftpVisible(false);
}

ConnectionPane::~ConnectionPane()
{
    disconnectFromHost(true);
}

const SessionProfile &ConnectionPane::profile() const
{
    return m_profile;
}

bool ConnectionPane::isConnected() const
{
    return m_connected;
}

bool ConnectionPane::sftpVisible() const
{
    return m_sftpExpanded;
}

bool ConnectionPane::terminalFocusMode() const
{
    return m_terminalFocusMode;
}

bool ConnectionPane::hasRemoteStats() const
{
    return m_remoteStatsMonitor != nullptr && m_remoteStatsMonitor->hasSample();
}

bool ConnectionPane::remoteStatsOnline() const
{
    return m_remoteStatsMonitor != nullptr && m_remoteStatsMonitor->isOnline();
}

QString ConnectionPane::remoteStatsStateText() const
{
    return m_remoteStatsMonitor != nullptr ? m_remoteStatsMonitor->stateText() : QString();
}

SystemStatsMonitor::Sample ConnectionPane::latestRemoteStats() const
{
    return m_remoteStatsMonitor != nullptr ? m_remoteStatsMonitor->latestSample() : SystemStatsMonitor::Sample();
}

void ConnectionPane::setTerminalFocusMode(bool enabled)
{
    if (enabled == m_terminalFocusMode) {
        if (m_fullScreenButton != nullptr) {
            m_fullScreenButton->setText(
                Localization::translateText(enabled ? QStringLiteral("退出全屏 Esc") : QStringLiteral("全屏 F11")));
            m_fullScreenButton->setToolTip(Localization::translateText(
                enabled ? QStringLiteral("退出终端全屏") : QStringLiteral("进入终端全屏")));
        }
        return;
    }

    m_terminalFocusMode = enabled;
    if (enabled) {
        m_restoreSftpAfterFocusMode = m_sftpExpanded;
        if (m_sftpExpanded) {
            setSftpVisible(false);
        }
    } else if (m_restoreSftpAfterFocusMode) {
        setSftpVisible(true);
        m_restoreSftpAfterFocusMode = false;
    }

    if (m_fullScreenButton != nullptr) {
        m_fullScreenButton->setText(
            Localization::translateText(enabled ? QStringLiteral("退出全屏 Esc") : QStringLiteral("全屏 F11")));
        m_fullScreenButton->setToolTip(
            Localization::translateText(enabled ? QStringLiteral("退出终端全屏") : QStringLiteral("进入终端全屏")));
    }
}

void ConnectionPane::refreshThemeState()
{
    m_statusBadge->setText(Localization::translateText(m_connected ? QStringLiteral("在线")
                                                                   : QStringLiteral("离线")));
    m_statusBadge->setStyleSheet(
        UiChrome::themeMode() == UiChrome::ThemeMode::Dark
            ? (m_connected ? QStringLiteral("background:#1A232B;color:#F7FAFC;border:1px solid #4B5C6A;border-radius:15px;padding:0 12px;")
                           : QStringLiteral("background:#15181C;color:#C2C9CF;border:1px solid #2D343C;border-radius:15px;padding:0 12px;"))
            : (m_connected ? QStringLiteral("background:#EAF7EE;color:#1D6B39;border:1px solid #A9D5B6;border-radius:15px;padding:0 12px;")
                           : QStringLiteral("background:#F3F5F7;color:#67727E;border:1px solid #D4DCE3;border-radius:15px;padding:0 12px;")));
}

void ConnectionPane::refreshTranslations()
{
    Localization::applyWidgetTexts(this);
    refreshThemeState();
    updateSftpToggleButton();
    updateHeaderPathLabel();

    if (m_connected && !m_currentRemotePath.isEmpty()) {
        refreshDirectory(m_currentRemotePath, false);
    } else {
        updateSelectionDependentUi();
    }
}

bool ConnectionPane::connectToHost(const QString &secret,
                                   const HostKeyPromptHandler &hostPrompt,
                                   QString *errorMessage)
{
    wjsshTrace(QStringLiteral("ConnectionPane::connectToHost start profile=%1").arg(m_profile.subtitle()));
    if (m_connected) {
        disconnectFromHost(true);
    }

    if (!m_shellClient.connectTo(m_profile, secret, hostPrompt, errorMessage)) {
        wjsshTrace(QStringLiteral("ConnectionPane::connectToHost shell connect failed error=%1")
                       .arg(errorMessage != nullptr ? *errorMessage : QString()));
        return false;
    }
    wjsshTrace(QStringLiteral("ConnectionPane::connectToHost shell connected"));

    applySessionShellBootstrap();

    if (!m_sftpClient.connectTo(m_profile, secret, hostPrompt, errorMessage)) {
        wjsshTrace(QStringLiteral("ConnectionPane::connectToHost sftp connect failed error=%1")
                       .arg(errorMessage != nullptr ? *errorMessage : QString()));
        m_shellClient.disconnectFromHost();
        return false;
    }
    wjsshTrace(QStringLiteral("ConnectionPane::connectToHost sftp connected"));

    m_connectionSecret = secret;
    appendConsoleText(QStringLiteral("[本地] 已连接到 %1\n").arg(m_profile.subtitle()));

    const QString startPath =
        m_currentRemotePath.isEmpty()
            ? (m_profile.initialPath.isEmpty() ? QStringLiteral(".") : m_profile.initialPath)
            : m_currentRemotePath;

    setConnectedState(true, QStringLiteral("%1 已连接").arg(m_profile.displayName()));
    if (m_remoteStatsMonitor != nullptr) {
        m_remoteStatsMonitor->start(m_profile, m_connectionSecret);
        emit remoteStatsChanged(this);
    }
    handleTerminalSizeChanged(m_consoleOutput->terminalColumns(), m_consoleOutput->terminalRows());
    wjsshTrace(QStringLiteral("ConnectionPane::connectToHost before refreshDirectory path=%1").arg(startPath));
    refreshDirectory(startPath, true);
    wjsshTrace(QStringLiteral("ConnectionPane::connectToHost after refreshDirectory"));
    focusCommandInput();
    scheduleAutoScriptFromEnvironment();
    wjsshTrace(QStringLiteral("ConnectionPane::connectToHost success"));
    return true;
}

void ConnectionPane::disconnectFromHost(bool quiet)
{
    wjsshTrace(QStringLiteral("ConnectionPane::disconnectFromHost quiet=%1").arg(quiet));
    const bool hadConnection = m_connected || m_shellClient.isConnected() || m_sftpClient.isConnected();

    if (m_remoteStatsMonitor != nullptr) {
        m_remoteStatsMonitor->stop();
    }
    m_shellClient.disconnectFromHost();
    m_sftpClient.disconnectFromHost();
    m_connectionSecret.clear();

    if (hadConnection && !quiet) {
        appendConsoleText(QStringLiteral("\n[本地] 连接已断开\n"));
    }
    setConnectedState(false, quiet ? QString() : QStringLiteral("%1 已断开").arg(m_profile.displayName()));
    emit remoteStatsChanged(this);
}

void ConnectionPane::focusCommandInput()
{
    if (m_consoleOutput != nullptr) {
        m_consoleOutput->setFocus(Qt::OtherFocusReason);
        QTimer::singleShot(0, m_consoleOutput, [output = m_consoleOutput]() {
            if (output != nullptr) {
                output->setFocus(Qt::OtherFocusReason);
            }
        });
    }
}

void ConnectionPane::setSftpVisible(bool visible)
{
    const bool panelVisible = m_sftpPanel != nullptr && m_sftpPanel->isVisible();
    if (visible == m_sftpExpanded && panelVisible == visible) {
        updateSftpToggleButton();
        return;
    }

    if (visible) {
        m_sftpPanel->show();
        const int minTerminalWidth = 320;
        const int minSftpWidth = 260;
        const int totalWidth = qMax(m_splitter != nullptr ? m_splitter->width() : width(), minTerminalWidth + minSftpWidth);
        const int sftpWidth = qBound(minSftpWidth,
                                     m_lastSftpWidth,
                                     qMax(minSftpWidth, totalWidth - minTerminalWidth));
        const int terminalWidth = qMax(minTerminalWidth, totalWidth - sftpWidth);
        m_splitter->setSizes({terminalWidth, sftpWidth});
    } else {
        const QList<int> sizes = m_splitter->sizes();
        if (sizes.size() > 1 && sizes[1] > 0) {
            m_lastSftpWidth = sizes[1];
        }
        m_sftpPanel->hide();
        m_splitter->setSizes({1, 0});
    }

    m_sftpExpanded = visible;
    updateSftpToggleButton();
}

bool ConnectionPane::eventFilter(QObject *watched, QEvent *event)
{
    const bool isRemoteTableObject =
        watched == m_remoteTable || (m_remoteTable != nullptr && watched == m_remoteTable->viewport());
    if (!isRemoteTableObject || event == nullptr) {
        return QWidget::eventFilter(watched, event);
    }

    switch (event->type()) {
    case QEvent::DragEnter: {
        auto *dragEvent = static_cast<QDragEnterEvent *>(event);
        if (m_connected && dragEvent->mimeData()->hasUrls()) {
            dragEvent->acceptProposedAction();
            return true;
        }
        break;
    }
    case QEvent::DragMove: {
        auto *dragEvent = static_cast<QDragMoveEvent *>(event);
        if (m_connected && dragEvent->mimeData()->hasUrls()) {
            dragEvent->acceptProposedAction();
            return true;
        }
        break;
    }
    case QEvent::Drop: {
        auto *dropEvent = static_cast<QDropEvent *>(event);
        if (!m_connected || !dropEvent->mimeData()->hasUrls()) {
            break;
        }

        QStringList localPaths;
        for (const QUrl &url : dropEvent->mimeData()->urls()) {
            if (!url.isLocalFile()) {
                continue;
            }
            const QString localPath = url.toLocalFile();
            if (!localPath.isEmpty()) {
                localPaths.push_back(localPath);
            }
        }

        if (!localPaths.isEmpty()) {
            uploadLocalPaths(localPaths);
            dropEvent->acceptProposedAction();
            return true;
        }
        break;
    }
    case QEvent::MouseButtonPress: {
        auto *mouseEvent = static_cast<QMouseEvent *>(event);
        if (mouseEvent->button() == Qt::LeftButton) {
            m_remoteDragStartPos = mouseEvent->pos();
        }
        break;
    }
    case QEvent::MouseMove: {
        auto *mouseEvent = static_cast<QMouseEvent *>(event);
        if (!(mouseEvent->buttons() & Qt::LeftButton) || !m_connected) {
            break;
        }
        if ((mouseEvent->pos() - m_remoteDragStartPos).manhattanLength() >= QApplication::startDragDistance()) {
            startRemoteDragExport();
            return true;
        }
        break;
    }
    default:
        break;
    }

    return QWidget::eventFilter(watched, event);
}

void ConnectionPane::buildUi()
{
    auto *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(10);

    auto *toolbar = new QWidget(this);
    auto *toolbarLayout = new QHBoxLayout(toolbar);
    toolbarLayout->setContentsMargins(2, 0, 2, 0);
    toolbarLayout->setSpacing(10);

    m_titleLabel = new QLabel(m_profile.displayName(), toolbar);
    m_titleLabel->setObjectName("sectionTitle");
    m_remotePathLabel = new QLabel(QStringLiteral("未连接"), toolbar);
    m_remotePathLabel->setObjectName("mutedText");
    m_remotePathLabel->setWordWrap(false);

    m_statusBadge = new QLabel(QStringLiteral("离线"), toolbar);
    m_statusBadge->setObjectName("statusBadge");
    m_statusBadge->setAlignment(Qt::AlignCenter);
    m_statusBadge->setMinimumWidth(76);
    m_statusBadge->setFixedHeight(30);

    m_reconnectButton = new QPushButton(QStringLiteral("重新连接"), toolbar);
    m_disconnectButton = new QPushButton(QStringLiteral("断开"), toolbar);
    m_toggleSftpButton = new QPushButton(toolbar);
    m_closeButton = new QPushButton(QStringLiteral("关闭标签"), toolbar);

    toolbarLayout->addWidget(m_titleLabel);
    toolbarLayout->addWidget(m_remotePathLabel, 1);
    toolbarLayout->addWidget(m_statusBadge);
    toolbarLayout->addWidget(m_reconnectButton);
    toolbarLayout->addWidget(m_disconnectButton);
    toolbarLayout->addWidget(m_toggleSftpButton);
    toolbarLayout->addWidget(m_closeButton);

    m_splitter = new QSplitter(Qt::Horizontal, this);
    m_splitter->setChildrenCollapsible(false);
    m_splitter->setHandleWidth(8);

    auto *terminalCard = new QFrame(m_splitter);
    terminalCard->setObjectName("terminalCard");
    auto *terminalLayout = new QVBoxLayout(terminalCard);
    terminalLayout->setContentsMargins(14, 14, 14, 14);
    terminalLayout->setSpacing(10);

    auto *terminalHeader = new QHBoxLayout();
    terminalHeader->setSpacing(8);
    auto *terminalTitle = new QLabel(QStringLiteral("终端"), terminalCard);
    terminalTitle->setObjectName("sectionTitle");
    auto *terminalHint =
        new QLabel(QStringLiteral("直接在终端内输入。Tab 补全，Ctrl+C 中断，Ctrl+L 远端清屏，F11 终端全屏"), terminalCard);
    terminalHint->setObjectName("mutedText");
    m_fullScreenButton = new QPushButton(QStringLiteral("全屏 F11"), terminalCard);
    m_fullScreenButton->setToolTip(QStringLiteral("进入终端全屏"));
    auto *clearButton = new QPushButton(QStringLiteral("清屏"), terminalCard);
    m_interruptButton = new QPushButton(QStringLiteral("Ctrl+C"), terminalCard);
    terminalHeader->addWidget(terminalTitle);
    terminalHeader->addWidget(terminalHint, 1);
    terminalHeader->addWidget(m_fullScreenButton);
    terminalHeader->addWidget(clearButton);
    terminalHeader->addWidget(m_interruptButton);

    m_consoleOutput = new TerminalView(terminalCard);
    m_consoleOutput->setObjectName("consoleOutput");
    m_consoleOutput->setPlaceholderText(QStringLiteral("连接后，直接在这里输入命令。"));
    setFocusProxy(m_consoleOutput);

    terminalLayout->addLayout(terminalHeader);
    terminalLayout->addWidget(m_consoleOutput, 1);

    m_sftpPanel = new QFrame(m_splitter);
    m_sftpPanel->setObjectName("panelCard");
    auto *filesLayout = new QVBoxLayout(m_sftpPanel);
    filesLayout->setContentsMargins(14, 14, 14, 14);
    filesLayout->setSpacing(10);

    auto *filesTitle = new QLabel(QStringLiteral("SFTP"), m_sftpPanel);
    filesTitle->setObjectName("sectionTitle");
    filesLayout->addWidget(filesTitle);

    auto *columnHint = new QLabel(QStringLiteral("可拖动表头分隔线调整列宽"), m_sftpPanel);
    columnHint->setObjectName("mutedText");
    filesLayout->addWidget(columnHint);

    auto *pathRow = new QHBoxLayout();
    pathRow->setSpacing(8);
    m_pathEdit = new QLineEdit(m_sftpPanel);
    m_pathEdit->setPlaceholderText(QStringLiteral("输入远程目录路径"));
    m_goButton = new QPushButton(QStringLiteral("打开"), m_sftpPanel);
    m_upButton = new QPushButton(QStringLiteral("上一级"), m_sftpPanel);
    m_refreshButton = new QPushButton(QStringLiteral("刷新"), m_sftpPanel);
    pathRow->addWidget(m_pathEdit, 1);
    pathRow->addWidget(m_goButton);
    pathRow->addWidget(m_upButton);
    pathRow->addWidget(m_refreshButton);
    filesLayout->addLayout(pathRow);

    m_uploadButton = new QPushButton(QStringLiteral("上传文件"), m_sftpPanel);
    m_uploadDirectoryButton = new QPushButton(QStringLiteral("上传目录"), m_sftpPanel);
    m_downloadButton = new QPushButton(QStringLiteral("下载"), m_sftpPanel);
    m_previewButton = new QPushButton(QStringLiteral("预览/编辑"), m_sftpPanel);
    m_newFolderButton = new QPushButton(QStringLiteral("新建目录"), m_sftpPanel);
    m_newFileButton = new QPushButton(QStringLiteral("新建文件"), m_sftpPanel);
    m_renameButton = new QPushButton(QStringLiteral("重命名"), m_sftpPanel);
    m_deleteRemoteButton = new QPushButton(QStringLiteral("删除"), m_sftpPanel);
    m_deleteRemoteButton->setObjectName("dangerButton");
    m_uploadButton->hide();
    m_uploadDirectoryButton->hide();
    m_downloadButton->hide();
    m_previewButton->hide();
    m_newFolderButton->hide();
    m_newFileButton->hide();
    m_renameButton->hide();
    m_deleteRemoteButton->hide();

    m_remoteTable = new QTableWidget(m_sftpPanel);
    m_remoteTable->setColumnCount(5);
    m_remoteTable->setHorizontalHeaderLabels(
        {QStringLiteral("名称"),
         QStringLiteral("类型"),
         QStringLiteral("大小"),
         QStringLiteral("修改时间"),
         QStringLiteral("权限")});
    m_remoteTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_remoteTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_remoteTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_remoteTable->setAlternatingRowColors(true);
    m_remoteTable->setContextMenuPolicy(Qt::CustomContextMenu);
    m_remoteTable->setShowGrid(false);
    m_remoteTable->setWordWrap(false);
    m_remoteTable->setTextElideMode(Qt::ElideMiddle);
    m_remoteTable->setDragEnabled(false);
    m_remoteTable->setAcceptDrops(true);
    m_remoteTable->viewport()->setAcceptDrops(true);
    m_remoteTable->setDropIndicatorShown(true);
    m_remoteTable->setMouseTracking(true);
    m_remoteTable->setIconSize(QSize(20, 20));
    m_remoteTable->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_remoteTable->verticalHeader()->setVisible(false);
    auto *remoteHeader = m_remoteTable->horizontalHeader();
    remoteHeader->setStretchLastSection(false);
    remoteHeader->setSectionResizeMode(0, QHeaderView::Interactive);
    remoteHeader->setSectionResizeMode(1, QHeaderView::Interactive);
    remoteHeader->setSectionResizeMode(2, QHeaderView::Interactive);
    remoteHeader->setSectionResizeMode(3, QHeaderView::Interactive);
    remoteHeader->setSectionResizeMode(4, QHeaderView::Interactive);
    remoteHeader->setMinimumSectionSize(72);
    remoteHeader->setSectionsClickable(true);
    remoteHeader->setSectionsMovable(false);
    remoteHeader->setCascadingSectionResizes(false);
    remoteHeader->setHighlightSections(false);
    remoteHeader->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    remoteHeader->setToolTip(QStringLiteral("拖动列标题之间的分隔线可调整宽度"));
    m_remoteTable->setColumnWidth(0, 320);
    m_remoteTable->setColumnWidth(1, 120);
    m_remoteTable->setColumnWidth(2, 110);
    m_remoteTable->setColumnWidth(3, 170);
    m_remoteTable->setColumnWidth(4, 110);
    m_remoteTable->installEventFilter(this);
    m_remoteTable->viewport()->installEventFilter(this);
    filesLayout->addWidget(m_remoteTable, 1);

    rootLayout->addWidget(toolbar);
    rootLayout->addWidget(m_splitter, 1);
    m_sftpPanel->hide();

    connect(m_reconnectButton, &QPushButton::clicked, this, [this]() { emit requestReconnect(this); });
    connect(m_disconnectButton, &QPushButton::clicked, this, [this]() { disconnectFromHost(); });
    connect(m_toggleSftpButton, &QPushButton::clicked, this, &ConnectionPane::toggleSftpPanel);
    connect(m_fullScreenButton,
            &QPushButton::clicked,
            this,
            [this]() { emit requestToggleTerminalFullScreen(this); });
    connect(m_closeButton, &QPushButton::clicked, this, [this]() { emit requestClose(this); });
    connect(clearButton, &QPushButton::clicked, this, &ConnectionPane::clearConsole);
    connect(m_interruptButton, &QPushButton::clicked, this, &ConnectionPane::interruptCommand);
    connect(m_consoleOutput, &TerminalView::rawInput, this, &ConnectionPane::sendRawInput);
    connect(m_consoleOutput, &TerminalView::interruptRequested, this, &ConnectionPane::interruptCommand);
    connect(m_consoleOutput, &TerminalView::terminalSizeChanged, this, &ConnectionPane::handleTerminalSizeChanged);
    connect(m_goButton, &QPushButton::clicked, this, &ConnectionPane::changeDirectoryFromPathBar);
    connect(m_pathEdit, &QLineEdit::returnPressed, this, &ConnectionPane::changeDirectoryFromPathBar);
    connect(m_upButton, &QPushButton::clicked, this, &ConnectionPane::navigateToParent);
    connect(m_refreshButton, &QPushButton::clicked, this, &ConnectionPane::refreshCurrentDirectory);
    connect(m_uploadButton, &QPushButton::clicked, this, &ConnectionPane::uploadFile);
    connect(m_uploadDirectoryButton, &QPushButton::clicked, this, &ConnectionPane::uploadDirectory);
    connect(m_downloadButton, &QPushButton::clicked, this, &ConnectionPane::downloadSelected);
    connect(m_previewButton, &QPushButton::clicked, this, &ConnectionPane::previewOrEditSelected);
    connect(m_newFolderButton, &QPushButton::clicked, this, &ConnectionPane::createRemoteDirectory);
    connect(m_newFileButton, &QPushButton::clicked, this, &ConnectionPane::createRemoteFile);
    connect(m_renameButton, &QPushButton::clicked, this, &ConnectionPane::renameSelected);
    connect(m_deleteRemoteButton, &QPushButton::clicked, this, &ConnectionPane::deleteSelectedRemotePath);
    connect(m_remoteTable, &QTableWidget::cellDoubleClicked, this, &ConnectionPane::handleFileActivated);
    connect(m_remoteTable, &QWidget::customContextMenuRequested, this, &ConnectionPane::showRemoteContextMenu);
    connect(m_remoteTable->selectionModel(),
            &QItemSelectionModel::selectionChanged,
            this,
            &ConnectionPane::updateSelectionDependentUi);
    connect(m_splitter, &QSplitter::splitterMoved, this, [this](int, int) {
        if (!m_sftpExpanded || m_splitter == nullptr) {
            return;
        }
        const QList<int> sizes = m_splitter->sizes();
        if (sizes.size() > 1 && sizes[1] > 0) {
            m_lastSftpWidth = sizes[1];
        }
    });

    terminalCard->setMinimumWidth(320);
    m_sftpPanel->setMinimumWidth(260);
    m_splitter->setStretchFactor(0, 1);
    m_splitter->setStretchFactor(1, 0);
}

void ConnectionPane::setConnectedState(bool connected, const QString &message)
{
    m_connected = connected;
    refreshThemeState();

    m_reconnectButton->setEnabled(!connected);
    m_disconnectButton->setEnabled(connected);
    m_interruptButton->setEnabled(connected);
    m_pathEdit->setEnabled(connected);
    m_goButton->setEnabled(connected);
    m_upButton->setEnabled(connected);
    m_refreshButton->setEnabled(connected);
    m_uploadButton->setEnabled(connected);
    m_uploadDirectoryButton->setEnabled(connected);
    m_newFolderButton->setEnabled(connected);
    m_newFileButton->setEnabled(connected);
    if (!connected) {
        m_terminalRemotePath.clear();
    }
    updateHeaderPathLabel();

    updateSelectionDependentUi();
    emit connectionStateChanged(this);
    emit remoteStatsChanged(this);

    if (!message.isEmpty()) {
        emit statusMessage(message);
    }
}

void ConnectionPane::appendConsoleText(const QString &text)
{
    if (text.isEmpty()) {
        return;
    }

    m_consoleOutput->appendLocalMessage(Localization::translateText(text));
}

void ConnectionPane::refreshDirectory(const QString &path, bool syncShell)
{
    wjsshTrace(QStringLiteral("ConnectionPane::refreshDirectory path=%1 syncShell=%2").arg(path).arg(syncShell));
    if (!m_sftpClient.isConnected()) {
        wjsshTrace(QStringLiteral("ConnectionPane::refreshDirectory skipped because sftp disconnected"));
        return;
    }

    QString errorMessage;
    const QString canonical = m_sftpClient.canonicalPath(path, &errorMessage);
    if (canonical.isEmpty()) {
        wjsshTrace(QStringLiteral("ConnectionPane::refreshDirectory canonical failed error=%1").arg(errorMessage));
        execThemedMessageBox(this, QMessageBox::Warning, QStringLiteral("目录打开失败"), errorMessage);
        return;
    }

    const QVector<RemoteEntry> entries = m_sftpClient.listDirectory(canonical, &errorMessage);
    if (!errorMessage.isEmpty()) {
        if (errorMessage.contains(QStringLiteral("权限不足"))
            || errorMessage.contains(QStringLiteral("Permission denied"), Qt::CaseInsensitive)) {
            errorMessage += QStringLiteral("\n\n当前 SFTP 会话仍以 %1 登录；如果你是在终端里 sudo/su 到 root，SFTP 不会跟着提权。若要访问 /root，请直接使用有权限的账号建立连接。")
                                .arg(m_profile.username);
        }
        wjsshTrace(QStringLiteral("ConnectionPane::refreshDirectory list failed error=%1").arg(errorMessage));
        execThemedMessageBox(this, QMessageBox::Warning, QStringLiteral("读取目录失败"), errorMessage);
        return;
    }

    if (m_remoteHomePath.isEmpty() && (path == QStringLiteral(".") || path.isEmpty())) {
        m_remoteHomePath = canonical;
    }
    m_currentRemotePath = canonical;
    if (m_terminalRemotePath.isEmpty()) {
        m_terminalRemotePath = canonical;
    }
    m_pathEdit->setText(canonical);
    updateHeaderPathLabel();
    populateRemoteTable(entries);
    emit statusMessage(QStringLiteral("%1 已载入").arg(canonical));

    if (syncShell) {
        syncShellDirectory();
    }
    wjsshTrace(QStringLiteral("ConnectionPane::refreshDirectory success canonical=%1 entries=%2")
                   .arg(canonical)
                   .arg(entries.size()));
}

void ConnectionPane::populateRemoteTable(const QVector<RemoteEntry> &entries)
{
    m_remoteTable->setRowCount(entries.size());

    for (int row = 0; row < entries.size(); ++row) {
        const RemoteEntry &entry = entries[row];
        const QString typeLabel = remoteEntryTypeLabel(entry);
        const QString toolTip = remoteEntryToolTip(entry, typeLabel);

        auto *nameItem = new QTableWidgetItem(entry.name);
        nameItem->setData(PathRole, entry.path);
        nameItem->setData(DirectoryRole, entry.isDirectory);
        nameItem->setData(SizeRole, QVariant::fromValue<qulonglong>(entry.size));
        nameItem->setIcon(remoteEntryIcon(entry, this));
        nameItem->setToolTip(toolTip);

        auto *typeItem = new QTableWidgetItem(typeLabel);
        auto *sizeItem = new QTableWidgetItem(entry.isDirectory ? QStringLiteral("-") : humanReadableSize(entry.size));
        auto *timeItem =
            new QTableWidgetItem(entry.modifiedAt.isValid() ? entry.modifiedAt.toString("yyyy-MM-dd HH:mm:ss")
                                                            : QStringLiteral("-"));
        auto *permItem = new QTableWidgetItem(entry.permissions);
        typeItem->setToolTip(toolTip);
        sizeItem->setToolTip(toolTip);
        timeItem->setToolTip(toolTip);
        permItem->setToolTip(toolTip);

        m_remoteTable->setItem(row, 0, nameItem);
        m_remoteTable->setItem(row, 1, typeItem);
        m_remoteTable->setItem(row, 2, sizeItem);
        m_remoteTable->setItem(row, 3, timeItem);
        m_remoteTable->setItem(row, 4, permItem);
    }
}

QString ConnectionPane::selectedRemotePath(bool *isDirectory) const
{
    const int row = m_remoteTable->currentRow();
    if (row < 0) {
        return {};
    }

    auto *item = m_remoteTable->item(row, 0);
    if (item == nullptr) {
        return {};
    }

    if (isDirectory != nullptr) {
        *isDirectory = item->data(DirectoryRole).toBool();
    }
    return item->data(PathRole).toString();
}

void ConnectionPane::syncShellDirectory()
{
    if (!m_connected || m_currentRemotePath.isEmpty()) {
        return;
    }

    m_terminalRemotePath = m_currentRemotePath;
    updateHeaderPathLabel();
    QString errorMessage;
    m_shellClient.sendCommand(QStringLiteral("cd %1").arg(shellQuote(m_currentRemotePath)), &errorMessage);
}

void ConnectionPane::updateSftpToggleButton()
{
    const bool visible = m_sftpPanel != nullptr && m_sftpPanel->isVisible();
    m_sftpExpanded = visible;
    m_toggleSftpButton->setText(Localization::translateText(
        visible ? QStringLiteral("隐藏 SFTP") : QStringLiteral("显示 SFTP")));
}

void ConnectionPane::updateHeaderPathLabel()
{
    QString label = QStringLiteral("未连接");
    if (m_connected) {
        const QString shellPath = m_terminalRemotePath.trimmed();
        const QString sftpPath = m_currentRemotePath.trimmed();
        if (!shellPath.isEmpty() && !sftpPath.isEmpty() && shellPath != sftpPath) {
            label = QStringLiteral("终端：%1    SFTP：%2").arg(shellPath, sftpPath);
        } else if (!shellPath.isEmpty()) {
            label = shellPath;
        } else if (!sftpPath.isEmpty()) {
            label = sftpPath;
        } else {
            label = QStringLiteral("已连接");
        }
    }
    const QString translated = Localization::translateText(label);
    m_remotePathLabel->setText(translated);
    m_remotePathLabel->setToolTip(translated);
}

QString ConnectionPane::normalizePromptPath(QString promptPath, const QString &promptUser) const
{
    promptPath = promptPath.trimmed();
    promptPath.replace('\\', '/');
    while (promptPath.size() > 1 && promptPath.endsWith('/')) {
        promptPath.chop(1);
    }
    const bool rootPrompt = promptUser.compare(QStringLiteral("root"), Qt::CaseInsensitive) == 0;
    if (promptPath == QStringLiteral("~")) {
        if (rootPrompt) {
            return QStringLiteral("/root");
        }
        return !m_remoteHomePath.isEmpty() ? m_remoteHomePath : m_currentRemotePath;
    }
    if (promptPath.startsWith(QStringLiteral("~/"))) {
        if (rootPrompt) {
            return QDir::cleanPath(QStringLiteral("/root/") + promptPath.mid(2));
        }
        const QString home = !m_remoteHomePath.isEmpty() ? m_remoteHomePath : m_currentRemotePath;
        if (!home.isEmpty()) {
            return QDir::cleanPath(home + QLatin1Char('/') + promptPath.mid(2));
        }
    }
    return promptPath;
}

void ConnectionPane::updateTerminalPathFromOutput(const QString &text)
{
    const QString plain = stripTerminalSequences(text);
    const QStringList lines = plain.split(QRegularExpression(QStringLiteral("[\r\n]+")),
                                          Qt::SkipEmptyParts);
    for (int index = lines.size() - 1; index >= 0; --index) {
        const PromptInfo prompt = extractPromptInfo(lines.at(index));
        if (prompt.path.isEmpty()) {
            continue;
        }
        const QString normalized = normalizePromptPath(prompt.path, prompt.user);
        if (normalized.isEmpty() || normalized == m_terminalRemotePath) {
            return;
        }
        m_terminalRemotePath = normalized;
        wjsshTrace(QStringLiteral("ConnectionPane::updateTerminalPathFromOutput path=%1").arg(normalized));
        updateHeaderPathLabel();
        return;
    }
}

void ConnectionPane::applySessionShellBootstrap()
{
    QString errorMessage;
    const QByteArray script = sessionBootstrapScript().toUtf8();
    if (!m_shellClient.sendRawData(script, &errorMessage)) {
        wjsshTrace(QStringLiteral("ConnectionPane::applySessionShellBootstrap failed error=%1").arg(errorMessage));
        return;
    }
    wjsshTrace(QStringLiteral("ConnectionPane::applySessionShellBootstrap sent bytes=%1").arg(script.size()));
}

void ConnectionPane::scheduleAutoScriptFromEnvironment()
{
    const QByteArray encoded = qEnvironmentVariable("WJSSH_AUTO_SCRIPT_B64").toLatin1();
    if (encoded.isEmpty()) {
        return;
    }

    const QByteArray script = QByteArray::fromBase64(encoded);
    if (script.isEmpty()) {
        wjsshTrace(QStringLiteral("ConnectionPane::scheduleAutoScriptFromEnvironment ignored invalid base64"));
        return;
    }

    bool ok = false;
    const int delay = qEnvironmentVariableIntValue("WJSSH_AUTO_SCRIPT_DELAY_MS", &ok);
    const int delayMs = ok ? qMax(0, delay) : 1200;
    wjsshTrace(QStringLiteral("ConnectionPane::scheduleAutoScriptFromEnvironment queued bytes=%1 delayMs=%2")
                   .arg(script.size())
                   .arg(delayMs));

    QTimer::singleShot(delayMs, this, [this, script]() {
        if (!m_connected) {
            wjsshTrace(QStringLiteral("ConnectionPane::scheduleAutoScriptFromEnvironment skipped because connection closed"));
            return;
        }
        QString errorMessage;
        if (!m_shellClient.sendRawData(script, &errorMessage)) {
            wjsshTrace(QStringLiteral("ConnectionPane::scheduleAutoScriptFromEnvironment send failed error=%1")
                           .arg(errorMessage));
            return;
        }
        QByteArray sample = script;
        sample.replace("\r", "\\r");
        sample.replace("\n", "\\n");
        sample.replace("\t", "\\t");
        wjsshTrace(QStringLiteral("ConnectionPane::scheduleAutoScriptFromEnvironment sent sample=%1")
                       .arg(QString::fromLatin1(sample.left(120))));
    });
}

void ConnectionPane::sendRawInput(const QByteArray &data)
{
    if (!m_connected || data.isEmpty()) {
        return;
    }

    QByteArray sample = data;
    sample.replace("\r", "\\r");
    sample.replace("\n", "\\n");
    sample.replace("\t", "\\t");
    wjsshTrace(QStringLiteral("ConnectionPane::sendRawInput bytes=%1 sample=%2")
                   .arg(data.size())
                   .arg(QString::fromLatin1(sample.left(40))));

    QString errorMessage;
    if (!m_shellClient.sendRawData(data, &errorMessage)) {
        execThemedMessageBox(this, QMessageBox::Warning, QStringLiteral("发送失败"), errorMessage);
    }
}

void ConnectionPane::interruptCommand()
{
    QString errorMessage;
    if (!m_shellClient.sendControlCharacter('\x03', &errorMessage)) {
        execThemedMessageBox(this, QMessageBox::Warning, QStringLiteral("发送失败"), errorMessage);
    }
}

void ConnectionPane::clearConsole()
{
    m_consoleOutput->resetTerminal();
    emit statusMessage(QStringLiteral("%1 终端输出已清空").arg(m_profile.displayName()));
}

void ConnectionPane::refreshCurrentDirectory()
{
    if (m_currentRemotePath.isEmpty()) {
        refreshDirectory(QStringLiteral("."), false);
        return;
    }
    refreshDirectory(m_currentRemotePath, false);
}

void ConnectionPane::navigateToParent()
{
    if (m_currentRemotePath.isEmpty()) {
        return;
    }
    refreshDirectory(parentRemotePath(m_currentRemotePath), true);
}

void ConnectionPane::changeDirectoryFromPathBar()
{
    const QString path = m_pathEdit->text().trimmed();
    if (path.isEmpty()) {
        return;
    }
    refreshDirectory(path, true);
}

bool ConnectionPane::runTransferOperation(
    const QString &title,
    const QString &initialLabel,
    const std::function<bool(const SftpTransferProgressHandler &, QString *)> &operation,
    QString *errorMessage)
{
    if (m_transferInProgress) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("当前已有传输任务正在进行，请稍候。");
        }
        return false;
    }

    QScopedValueRollback<bool> busyGuard(m_transferInProgress, true);

    QProgressDialog progressDialog(Localization::translateText(initialLabel),
                                   Localization::translateText(QStringLiteral("取消")),
                                   0,
                                   0,
                                   this);
    progressDialog.setWindowTitle(Localization::translateText(title));
    progressDialog.setWindowModality(Qt::WindowModal);
    progressDialog.setMinimumDuration(0);
    progressDialog.setAutoClose(false);
    progressDialog.setAutoReset(false);
    progressDialog.setValue(0);
    UiChrome::applyDialogTheme(&progressDialog);
    progressDialog.show();
    QApplication::processEvents();

    QElapsedTimer updateTimer;
    updateTimer.start();

    const auto progressHandler = [&progressDialog, &initialLabel, &updateTimer](const SftpTransferProgress &progress) {
        if (progressDialog.wasCanceled()) {
            return false;
        }

        const bool shouldRefresh = updateTimer.elapsed() >= 40
                                   || progress.bytesTransferred == 0
                                   || (progress.bytesTotal > 0 && progress.bytesTransferred >= progress.bytesTotal);
        if (!shouldRefresh) {
            return !progressDialog.wasCanceled();
        }

        QString targetName = QFileInfo(progress.path).fileName();
        if (targetName.isEmpty()) {
            targetName = progress.path;
        }

        QString label = initialLabel;
        if (!targetName.isEmpty()) {
            label += QStringLiteral("\n") + targetName;
        }

        if (progress.bytesTotal > 0) {
            label += QStringLiteral("\n%1 / %2")
                         .arg(humanReadableSize(progress.bytesTransferred), humanReadableSize(progress.bytesTotal));
            progressDialog.setRange(0, 1000);
            const int value =
                static_cast<int>(qBound<quint64>(0,
                                                 (progress.bytesTransferred * 1000ULL) / progress.bytesTotal,
                                                 1000));
            progressDialog.setValue(value);
        } else {
            progressDialog.setRange(0, 0);
        }

        progressDialog.setLabelText(Localization::translateText(label));
        QApplication::processEvents();
        updateTimer.restart();
        return !progressDialog.wasCanceled();
    };

    QString localError;
    const bool ok = operation(progressHandler, &localError);
    progressDialog.hide();
    QApplication::processEvents();

    if (!ok) {
        if (errorMessage != nullptr) {
            *errorMessage = localError.isEmpty()
                                ? (progressDialog.wasCanceled() ? QStringLiteral("传输已取消。") : QStringLiteral("传输失败。"))
                                : localError;
        }
        return false;
    }

    return true;
}

void ConnectionPane::handleFileActivated(int row, int column)
{
    Q_UNUSED(row);
    Q_UNUSED(column);

    bool isDirectory = false;
    const QString path = selectedRemotePath(&isDirectory);
    if (path.isEmpty()) {
        return;
    }

    if (isDirectory) {
        refreshDirectory(path, true);
        return;
    }

    previewOrEditSelected();
}

void ConnectionPane::uploadLocalPaths(const QStringList &paths)
{
    if (!m_connected || paths.isEmpty()) {
        return;
    }

    QString errorMessage;
    if (!runTransferOperation(
            QStringLiteral("上传中"),
            QStringLiteral("正在上传到 %1").arg(m_currentRemotePath),
            [this, &paths](const SftpTransferProgressHandler &progressHandler, QString *taskError) {
                for (const QString &path : paths) {
                    const QFileInfo info(path);
                    const QString remotePath = joinRemotePath(m_currentRemotePath, info.fileName());
                    const bool ok = info.isDir()
                                        ? m_sftpClient.uploadDirectory(path, remotePath, taskError, progressHandler)
                                        : m_sftpClient.uploadFile(path, remotePath, taskError, progressHandler);
                    if (!ok) {
                        return false;
                    }
                }
                return true;
            },
            &errorMessage)) {
        if (isTransferCancelledMessage(errorMessage)) {
            emit statusMessage(QStringLiteral("已取消上传"));
            return;
        }
        execThemedMessageBox(this, QMessageBox::Warning, QStringLiteral("上传失败"), errorMessage);
        return;
    }

    emit statusMessage(QStringLiteral("已上传 %1 个项目到 %2").arg(paths.size()).arg(m_currentRemotePath));
    refreshCurrentDirectory();
}

QString ConnectionPane::prepareLocalExportPath(const QString &remotePath,
                                               bool isDirectory,
                                               QString *errorMessage,
                                               const SftpTransferProgressHandler &progressHandler)
{
    const QString cachedPath = cachedLocalExportPath(remotePath);
    if (!cachedPath.isEmpty()) {
        return cachedPath;
    }

    const QString baseName = QFileInfo(remotePath).fileName();
    const QString exportRoot = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
                                   .filePath(QStringLiteral("WjSshDragExport"));
    if (!QDir().mkpath(exportRoot)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("无法创建本地临时导出目录。");
        }
        return {};
    }

    const QString sessionKeySource = !m_profile.id.isEmpty() ? m_profile.id : (m_profile.host + QStringLiteral("|") + m_profile.username);
    const QString sessionKey =
        QString::fromLatin1(QCryptographicHash::hash(sessionKeySource.toUtf8(), QCryptographicHash::Sha1).toHex().left(12));
    const QString remoteKey =
        QString::fromLatin1(QCryptographicHash::hash(remotePath.toUtf8(), QCryptographicHash::Sha1).toHex().left(12));

    const QString sessionRoot = QDir(exportRoot).filePath(sessionKey);
    const QString stableContainer = QDir(sessionRoot).filePath(remoteKey);
    if (!QDir().mkpath(stableContainer)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("无法创建本地临时导出目录。");
        }
        return {};
    }

    const QString localPath = QDir(stableContainer).filePath(baseName);
    QFileInfo localInfo(localPath);
    if (localInfo.exists()) {
        if (isDirectory) {
            QDir(localPath).removeRecursively();
        } else {
            QFile::remove(localPath);
        }
    }

    const bool ok = isDirectory ? m_sftpClient.downloadDirectory(remotePath, localPath, errorMessage, progressHandler)
                                : m_sftpClient.downloadFile(remotePath, localPath, errorMessage, progressHandler);
    if (!ok) {
        if (isDirectory) {
            QDir(localPath).removeRecursively();
        } else {
            QFile::remove(localPath);
        }
        return {};
    }

    m_dragExportCache.insert(remotePath, localPath);
    return localPath;
}

QString ConnectionPane::cachedLocalExportPath(const QString &remotePath) const
{
    const QString localPath = m_dragExportCache.value(remotePath);
    if (localPath.isEmpty()) {
        return {};
    }

    const QFileInfo info(localPath);
    return info.exists() ? localPath : QString();
}

void ConnectionPane::startRemoteDragExport()
{
    bool isDirectory = false;
    const QString remotePath = selectedRemotePath(&isDirectory);
    if (remotePath.isEmpty()) {
        return;
    }

    auto *currentItem = m_remoteTable->item(m_remoteTable->currentRow(), 0);
    const qulonglong fileSize = currentItem != nullptr ? currentItem->data(SizeRole).toULongLong() : 0;

#if defined(Q_OS_WIN)
    if (!isDirectory) {
        const int row = m_remoteTable->currentRow();
        const QString displayName = QFileInfo(remotePath).fileName();
        const QDateTime modifiedAt =
            row >= 0 && m_remoteTable->item(row, 3) != nullptr
                ? QDateTime::fromString(m_remoteTable->item(row, 3)->text(), QStringLiteral("yyyy-MM-dd HH:mm:ss"))
                : QDateTime();

        auto *mimeData = new VirtualRemoteFileMimeData(
            displayName,
            fileSize,
            modifiedAt,
            [this, remotePath](QString *taskError) {
                QByteArray data;
                const bool ok = runTransferOperation(
                    QStringLiteral("导出中"),
                    QStringLiteral("正在导出到拖拽目标"),
                    [this, &remotePath, &data](const SftpTransferProgressHandler &progressHandler, QString *innerError) {
                        return m_sftpClient.readFile(remotePath, &data, innerError, progressHandler);
                    },
                    taskError);
                return ok ? data : QByteArray();
            });

        auto *drag = new QDrag(m_remoteTable);
        drag->setMimeData(mimeData);
        drag->setPixmap(style()->standardIcon(QStyle::SP_FileIcon).pixmap(32, 32));
        const Qt::DropAction dropAction = drag->exec(Qt::CopyAction);

        if (mimeData->contentRequested()) {
            if (mimeData->contentReady()) {
                emit statusMessage(QStringLiteral("已导出到目标：%1").arg(displayName));
            } else if (isTransferCancelledMessage(mimeData->lastError())) {
                emit statusMessage(QStringLiteral("已取消导出"));
            } else if (!mimeData->lastError().isEmpty()) {
                execThemedMessageBox(this, QMessageBox::Warning, QStringLiteral("导出失败"), mimeData->lastError());
            }
            return;
        }

        if (dropAction == Qt::IgnoreAction) {
            emit statusMessage(QStringLiteral("已取消拖拽"));
        }
        return;
    }
#endif

    const QString cachedPath = cachedLocalExportPath(remotePath);
    const bool needsPrepare = cachedPath.isEmpty();
    QString errorMessage;
    QString localPath = cachedPath;
    if (needsPrepare) {
        const bool prepared = runTransferOperation(
            QStringLiteral("准备导出"),
            QStringLiteral("正在准备拖拽导出"),
            [this, &remotePath, isDirectory, &localPath](const SftpTransferProgressHandler &progressHandler,
                                                         QString *taskError) {
                localPath = prepareLocalExportPath(remotePath, isDirectory, taskError, progressHandler);
                return !localPath.isEmpty();
            },
            &errorMessage);
        if (!prepared) {
            if (isTransferCancelledMessage(errorMessage)) {
                emit statusMessage(QStringLiteral("已取消导出"));
                return;
            }
            execThemedMessageBox(this, QMessageBox::Warning, QStringLiteral("导出失败"), errorMessage);
            return;
        }
    }

    if (localPath.isEmpty()) {
        execThemedMessageBox(this, QMessageBox::Warning, QStringLiteral("导出失败"), QStringLiteral("未能生成本地导出文件。"));
        return;
    }

    auto *mimeData = new QMimeData();
    mimeData->setUrls({QUrl::fromLocalFile(localPath)});

    auto *drag = new QDrag(m_remoteTable);
    drag->setMimeData(mimeData);
    drag->setPixmap(style()->standardIcon(isDirectory ? QStyle::SP_DirIcon : QStyle::SP_FileIcon).pixmap(32, 32));
    const Qt::DropAction dropAction = drag->exec(Qt::CopyAction);

    if (dropAction == Qt::IgnoreAction) {
        emit statusMessage(QStringLiteral("未放置到目标，已缓存到：%1").arg(localPath));
        execThemedMessageBox(this,
                             QMessageBox::Information,
                             QStringLiteral("导出已准备"),
                             QStringLiteral("文件已经临时导出到本地：\n%1\n\n如果这次目标没有接住拖拽，现在再次拖拽会直接使用这个本地缓存，不会重复下载。")
                                 .arg(localPath));
        return;
    }

    emit statusMessage(QStringLiteral("已导出到目标：%1").arg(localPath));
}

void ConnectionPane::uploadFile()
{
    const QStringList files = execThemedOpenFilesDialog(this, QStringLiteral("选择要上传的本地文件"));
    if (files.isEmpty()) {
        return;
    }

    QString errorMessage;
    const bool ok = runTransferOperation(
        QStringLiteral("上传中"),
        QStringLiteral("正在上传到 %1").arg(m_currentRemotePath),
        [this, &files](const SftpTransferProgressHandler &progressHandler, QString *taskError) {
            for (const QString &file : files) {
                const QFileInfo info(file);
                if (!m_sftpClient.uploadFile(file,
                                             joinRemotePath(m_currentRemotePath, info.fileName()),
                                             taskError,
                                             progressHandler)) {
                    return false;
                }
            }
            return true;
        },
        &errorMessage);
    if (!ok) {
        if (isTransferCancelledMessage(errorMessage)) {
            emit statusMessage(QStringLiteral("已取消上传"));
            return;
        }
        execThemedMessageBox(this, QMessageBox::Warning, QStringLiteral("上传失败"), errorMessage);
        return;
    }

    refreshCurrentDirectory();
}

void ConnectionPane::uploadDirectory()
{
    const QString localDirectory = execThemedExistingDirectoryDialog(this, QStringLiteral("选择要上传的本地目录"));
    if (localDirectory.isEmpty()) {
        return;
    }

    const QString folderName = QFileInfo(localDirectory).fileName();
    QString errorMessage;
    if (!runTransferOperation(
            QStringLiteral("上传中"),
            QStringLiteral("正在上传目录到 %1").arg(m_currentRemotePath),
            [this, &localDirectory, &folderName](const SftpTransferProgressHandler &progressHandler,
                                                 QString *taskError) {
                return m_sftpClient.uploadDirectory(localDirectory,
                                                    joinRemotePath(m_currentRemotePath, folderName),
                                                    taskError,
                                                    progressHandler);
            },
            &errorMessage)) {
        if (isTransferCancelledMessage(errorMessage)) {
            emit statusMessage(QStringLiteral("已取消上传"));
            return;
        }
        execThemedMessageBox(this, QMessageBox::Warning, QStringLiteral("上传失败"), errorMessage);
        return;
    }

    refreshCurrentDirectory();
}

void ConnectionPane::downloadSelected()
{
    bool isDirectory = false;
    const QString remotePath = selectedRemotePath(&isDirectory);
    if (remotePath.isEmpty()) {
        return;
    }

    QString errorMessage;
    if (isDirectory) {
        const QString localBase = execThemedExistingDirectoryDialog(this, QStringLiteral("选择本地保存目录"));
        if (localBase.isEmpty()) {
            return;
        }

        const QString targetDirectory = QDir(localBase).filePath(QFileInfo(remotePath).fileName());
        if (!runTransferOperation(
                QStringLiteral("下载中"),
                QStringLiteral("正在下载目录到 %1").arg(targetDirectory),
                [this, &remotePath, &targetDirectory](const SftpTransferProgressHandler &progressHandler,
                                                      QString *taskError) {
                    return m_sftpClient.downloadDirectory(remotePath, targetDirectory, taskError, progressHandler);
                },
                &errorMessage)) {
            if (isTransferCancelledMessage(errorMessage)) {
                emit statusMessage(QStringLiteral("已取消下载"));
                return;
            }
            execThemedMessageBox(this, QMessageBox::Warning, QStringLiteral("下载失败"), errorMessage);
            return;
        }

        emit statusMessage(QStringLiteral("目录已下载到 %1").arg(targetDirectory));
        return;
    }

    const QString localPath =
        execThemedSaveFileDialog(this, QStringLiteral("保存到本地"), QFileInfo(remotePath).fileName());
    if (localPath.isEmpty()) {
        return;
    }

    if (!runTransferOperation(
            QStringLiteral("下载中"),
            QStringLiteral("正在下载到 %1").arg(localPath),
            [this, &remotePath, &localPath](const SftpTransferProgressHandler &progressHandler, QString *taskError) {
                return m_sftpClient.downloadFile(remotePath, localPath, taskError, progressHandler);
            },
            &errorMessage)) {
        if (isTransferCancelledMessage(errorMessage)) {
            emit statusMessage(QStringLiteral("已取消下载"));
            return;
        }
        execThemedMessageBox(this, QMessageBox::Warning, QStringLiteral("下载失败"), errorMessage);
        return;
    }

    emit statusMessage(QStringLiteral("已下载到 %1").arg(localPath));
}

void ConnectionPane::copySelectedToClipboard()
{
    bool isDirectory = false;
    const QString remotePath = selectedRemotePath(&isDirectory);
    if (!m_connected || remotePath.isEmpty()) {
        return;
    }

    QString errorMessage;
    QString localPath = cachedLocalExportPath(remotePath);
    if (localPath.isEmpty()) {
        const bool prepared = runTransferOperation(
            QStringLiteral("复制导出中"),
            QStringLiteral("正在准备复制到系统剪贴板"),
            [this, &remotePath, isDirectory, &localPath](const SftpTransferProgressHandler &progressHandler,
                                                         QString *taskError) {
                localPath = prepareLocalExportPath(remotePath, isDirectory, taskError, progressHandler);
                return !localPath.isEmpty();
            },
            &errorMessage);
        if (!prepared) {
            if (isTransferCancelledMessage(errorMessage)) {
                emit statusMessage(QStringLiteral("已取消复制导出"));
                return;
            }
            execThemedMessageBox(this, QMessageBox::Warning, QStringLiteral("复制导出失败"), errorMessage);
            return;
        }
    }

    if (localPath.isEmpty()) {
        execThemedMessageBox(this, QMessageBox::Warning, QStringLiteral("复制导出失败"), QStringLiteral("未能生成本地导出文件。"));
        return;
    }

    auto *mimeData = new QMimeData();
    mimeData->setUrls({QUrl::fromLocalFile(localPath)});
    mimeData->setText(localPath);
    QApplication::clipboard()->setMimeData(mimeData);

    emit statusMessage(QStringLiteral("已复制到系统剪贴板：%1").arg(QFileInfo(localPath).fileName()));
}

void ConnectionPane::pasteFromClipboard()
{
    if (!m_connected) {
        return;
    }

    const QMimeData *mimeData = QApplication::clipboard()->mimeData();
    if (mimeData == nullptr) {
        execThemedMessageBox(this,
                             QMessageBox::Information,
                             QStringLiteral("无法粘贴"),
                             QStringLiteral("系统剪贴板中没有可导入的本地文件或目录。"));
        return;
    }

    QStringList localPaths;
    if (mimeData->hasUrls()) {
        for (const QUrl &url : mimeData->urls()) {
            if (!url.isLocalFile()) {
                continue;
            }
            const QString localPath = url.toLocalFile();
            if (!localPath.isEmpty() && QFileInfo::exists(localPath)) {
                localPaths.push_back(localPath);
            }
        }
    } else if (mimeData->hasText()) {
        const QStringList candidates = mimeData->text().split(QRegularExpression(QStringLiteral("[\r\n]+")),
                                                              Qt::SkipEmptyParts);
        for (QString path : candidates) {
            path = path.trimmed();
            if (path.startsWith('"') && path.endsWith('"') && path.size() >= 2) {
                path = path.mid(1, path.size() - 2);
            }
            if (!path.isEmpty() && QFileInfo::exists(path)) {
                localPaths.push_back(path);
            }
        }
    }

    localPaths.removeDuplicates();
    if (localPaths.isEmpty()) {
        execThemedMessageBox(this,
                             QMessageBox::Information,
                             QStringLiteral("无法粘贴"),
                             QStringLiteral("系统剪贴板中没有可导入的本地文件或目录。"));
        return;
    }

    uploadLocalPaths(localPaths);
}

void ConnectionPane::previewOrEditSelected()
{
    bool isDirectory = false;
    const QString remotePath = selectedRemotePath(&isDirectory);
    if (remotePath.isEmpty() || isDirectory) {
        return;
    }

    auto *item = m_remoteTable->item(m_remoteTable->currentRow(), 0);
    const qulonglong fileSize = item != nullptr ? item->data(SizeRole).toULongLong() : 0;
    if (fileSize > 2ULL * 1024ULL * 1024ULL) {
        const auto answer = execThemedMessageBox(
            this,
            QMessageBox::Question,
            QStringLiteral("打开大文件"),
            QStringLiteral("该文件约 %1，继续载入到编辑器中吗？").arg(humanReadableSize(fileSize)),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No);
        if (answer != QMessageBox::Yes) {
            return;
        }
    }

    QByteArray data;
    QString errorMessage;
    if (!m_sftpClient.readFile(remotePath, &data, &errorMessage)) {
        execThemedMessageBox(this, QMessageBox::Warning, QStringLiteral("打开失败"), errorMessage);
        return;
    }

    RemoteFileEditorDialog dialog(this);
    dialog.setFilePath(remotePath);
    dialog.setContent(data);
    if (dialog.exec() != QDialog::Accepted || dialog.isProbablyBinary()) {
        return;
    }

    if (!m_sftpClient.writeFile(remotePath, dialog.content(), &errorMessage)) {
        execThemedMessageBox(this, QMessageBox::Warning, QStringLiteral("保存失败"), errorMessage);
        return;
    }

    emit statusMessage(QStringLiteral("已保存 %1").arg(remotePath));
    refreshCurrentDirectory();
}

void ConnectionPane::createRemoteDirectory()
{
    bool ok = false;
    const QString name =
        execThemedTextInput(this, QStringLiteral("新建目录"), QStringLiteral("目录名称"), QString(), &ok);
    if (!ok || name.trimmed().isEmpty()) {
        return;
    }

    QString errorMessage;
    if (!m_sftpClient.createDirectory(joinRemotePath(m_currentRemotePath, name.trimmed()), &errorMessage)) {
        execThemedMessageBox(this, QMessageBox::Warning, QStringLiteral("创建失败"), errorMessage);
        return;
    }

    refreshCurrentDirectory();
}

void ConnectionPane::createRemoteFile()
{
    bool ok = false;
    const QString name =
        execThemedTextInput(this, QStringLiteral("新建文件"), QStringLiteral("文件名称"), QString(), &ok);
    if (!ok || name.trimmed().isEmpty()) {
        return;
    }

    QString errorMessage;
    if (!m_sftpClient.createEmptyFile(joinRemotePath(m_currentRemotePath, name.trimmed()), &errorMessage)) {
        execThemedMessageBox(this, QMessageBox::Warning, QStringLiteral("创建失败"), errorMessage);
        return;
    }

    refreshCurrentDirectory();
}

void ConnectionPane::renameSelected()
{
    bool isDirectory = false;
    const QString remotePath = selectedRemotePath(&isDirectory);
    if (remotePath.isEmpty()) {
        return;
    }

    const QString currentName = QFileInfo(remotePath).fileName();
    bool ok = false;
    const QString newName =
        execThemedTextInput(this, QStringLiteral("重命名"), QStringLiteral("新的名称"), currentName, &ok);
    if (!ok || newName.trimmed().isEmpty() || newName == currentName) {
        return;
    }

    QString errorMessage;
    if (!m_sftpClient.renamePath(remotePath,
                                 joinRemotePath(parentRemotePath(remotePath), newName.trimmed()),
                                 &errorMessage)) {
        execThemedMessageBox(this, QMessageBox::Warning, QStringLiteral("重命名失败"), errorMessage);
        return;
    }

    refreshCurrentDirectory();
}

void ConnectionPane::deleteSelectedRemotePath()
{
    bool isDirectory = false;
    const QString remotePath = selectedRemotePath(&isDirectory);
    if (remotePath.isEmpty()) {
        return;
    }

    const auto answer = execThemedMessageBox(
        this,
        QMessageBox::Warning,
        QStringLiteral("确认删除"),
        isDirectory ? QStringLiteral("确定递归删除目录“%1”及其内容吗？").arg(remotePath)
                    : QStringLiteral("确定删除文件“%1”吗？").arg(remotePath),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);
    if (answer != QMessageBox::Yes) {
        return;
    }

    QString errorMessage;
    if (!m_sftpClient.removePath(remotePath, &errorMessage)) {
        execThemedMessageBox(this, QMessageBox::Warning, QStringLiteral("删除失败"), errorMessage);
        return;
    }

    refreshCurrentDirectory();
}

void ConnectionPane::toggleSftpPanel()
{
    setSftpVisible(!m_sftpExpanded);
}

void ConnectionPane::handleTerminalSizeChanged(int columns, int rows)
{
    if (!m_connected || columns <= 0 || rows <= 0) {
        return;
    }

    m_shellClient.resizeTerminal(columns, rows);
}


void ConnectionPane::showRemoteContextMenu(const QPoint &position)
{
    if (m_remoteTable == nullptr) {
        return;
    }

    if (QTableWidgetItem *item = m_remoteTable->itemAt(position)) {
        m_remoteTable->setCurrentItem(item);
    }

    bool isDirectory = false;
    const QString remotePath = selectedRemotePath(&isDirectory);
    const bool hasSelection = !remotePath.isEmpty();

    QMenu menu(this);
    QAction *refreshAction = menu.addAction(Localization::translateText(QStringLiteral("刷新")));
    QAction *uploadFileAction = menu.addAction(Localization::translateText(QStringLiteral("上传文件")));
    QAction *uploadDirectoryAction = menu.addAction(Localization::translateText(QStringLiteral("上传目录")));
    QAction *pasteAction = menu.addAction(Localization::translateText(QStringLiteral("从剪贴板导入")));
    menu.addSeparator();

    refreshAction->setEnabled(m_connected);
    uploadFileAction->setEnabled(m_connected);
    uploadDirectoryAction->setEnabled(m_connected);
    const QMimeData *clipboardData = QApplication::clipboard()->mimeData();
    bool hasClipboardFiles = clipboardData != nullptr && clipboardData->hasUrls();
    if (!hasClipboardFiles && clipboardData != nullptr && clipboardData->hasText()) {
        const QStringList candidates = clipboardData->text().split(QRegularExpression(QStringLiteral("[\r\n]+")),
                                                                   Qt::SkipEmptyParts);
        for (QString path : candidates) {
            path = path.trimmed();
            if (path.startsWith('"') && path.endsWith('"') && path.size() >= 2) {
                path = path.mid(1, path.size() - 2);
            }
            if (!path.isEmpty() && QFileInfo::exists(path)) {
                hasClipboardFiles = true;
                break;
            }
        }
    }
    pasteAction->setEnabled(m_connected && hasClipboardFiles);

    QAction *openAction = nullptr;
    QAction *downloadAction = nullptr;
    QAction *copyAction = nullptr;
    QAction *previewAction = nullptr;
    QAction *renameAction = nullptr;
    QAction *deleteAction = nullptr;

    if (hasSelection && isDirectory) {
        openAction = menu.addAction(Localization::translateText(QStringLiteral("打开")));
    }
    if (hasSelection) {
        copyAction = menu.addAction(Localization::translateText(QStringLiteral("复制导出")));
        downloadAction = menu.addAction(Localization::translateText(QStringLiteral("下载")));
        if (!isDirectory) {
            previewAction = menu.addAction(Localization::translateText(QStringLiteral("预览/编辑")));
        }
        renameAction = menu.addAction(Localization::translateText(QStringLiteral("重命名")));
        deleteAction = menu.addAction(Localization::translateText(QStringLiteral("删除")));
    }

    menu.addSeparator();
    QAction *newFolderAction = menu.addAction(Localization::translateText(QStringLiteral("新建目录")));
    QAction *newFileAction = menu.addAction(Localization::translateText(QStringLiteral("新建文件")));
    newFolderAction->setEnabled(m_connected);
    newFileAction->setEnabled(m_connected);

    QAction *chosen = menu.exec(m_remoteTable->viewport()->mapToGlobal(position));
    if (chosen == nullptr) {
        return;
    }

    if (chosen == refreshAction) refreshCurrentDirectory();
    else if (chosen == uploadFileAction) uploadFile();
    else if (chosen == uploadDirectoryAction) uploadDirectory();
    else if (chosen == pasteAction) pasteFromClipboard();
    else if (chosen == openAction) handleFileActivated(m_remoteTable->currentRow(), 0);
    else if (chosen == copyAction) copySelectedToClipboard();
    else if (chosen == downloadAction) downloadSelected();
    else if (chosen == previewAction) previewOrEditSelected();
    else if (chosen == renameAction) renameSelected();
    else if (chosen == deleteAction) deleteSelectedRemotePath();
    else if (chosen == newFolderAction) createRemoteDirectory();
    else if (chosen == newFileAction) createRemoteFile();
}

void ConnectionPane::handleShellOutput(const QString &text, bool isErrorStream)
{
    wjsshTrace(QStringLiteral("ConnectionPane::handleShellOutput len=%1 stderr=%2")
                   .arg(text.size())
                   .arg(isErrorStream));
    m_consoleOutput->appendRemoteText(text, isErrorStream);
    if (!isErrorStream) {
        updateTerminalPathFromOutput(text);
    }
    wjsshTrace(QStringLiteral("ConnectionPane::handleShellOutput rendered"));
}

void ConnectionPane::handleShellDisconnect(const QString &reason)
{
    if (!m_connected) {
        return;
    }

    appendConsoleText(QStringLiteral("\n[本地] %1\n").arg(reason));
    disconnectFromHost(true);
    emit statusMessage(QStringLiteral("%1 连接中断：%2").arg(m_profile.displayName(), reason));
}

void ConnectionPane::updateSelectionDependentUi()
{
    bool isDirectory = false;
    const bool hasRemoteSelection = !selectedRemotePath(&isDirectory).isEmpty();

    m_downloadButton->setEnabled(m_connected && hasRemoteSelection);
    m_previewButton->setEnabled(m_connected && hasRemoteSelection && !isDirectory);
    m_renameButton->setEnabled(m_connected && hasRemoteSelection);
    m_deleteRemoteButton->setEnabled(m_connected && hasRemoteSelection);
}
