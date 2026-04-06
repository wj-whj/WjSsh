#pragma once

#include "RemoteServerStatsMonitor.h"
#include "RemoteFileEditorDialog.h"
#include "SessionProfile.h"
#include "SftpClient.h"
#include "SshShellClient.h"
#include "TerminalView.h"

#include <QLabel>
#include <QLineEdit>
#include <QPoint>
#include <QPushButton>
#include <QSplitter>
#include <QTableWidget>
#include <QWidget>
#include <QHash>
#include <functional>

class QEvent;
class QObject;

class ConnectionPane : public QWidget {
    Q_OBJECT

public:
    explicit ConnectionPane(const SessionProfile &profile, QWidget *parent = nullptr);
    ~ConnectionPane() override;

    [[nodiscard]] const SessionProfile &profile() const;
    [[nodiscard]] bool isConnected() const;
    [[nodiscard]] bool sftpVisible() const;
    [[nodiscard]] bool hasRemoteStats() const;
    [[nodiscard]] bool remoteStatsOnline() const;
    [[nodiscard]] QString remoteStatsStateText() const;
    [[nodiscard]] SystemStatsMonitor::Sample latestRemoteStats() const;
    [[nodiscard]] bool terminalFocusMode() const;

    bool connectToHost(const QString &secret,
                       const HostKeyPromptHandler &hostPrompt,
                       QString *errorMessage);
    void disconnectFromHost(bool quiet = false);
    void focusCommandInput();
    void setSftpVisible(bool visible);
    void setTerminalFocusMode(bool enabled);
    void refreshThemeState();
    void refreshTranslations();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

signals:
    void statusMessage(const QString &message);
    void connectionStateChanged(ConnectionPane *pane);
    void remoteStatsChanged(ConnectionPane *pane);
    void requestReconnect(ConnectionPane *pane);
    void requestClose(ConnectionPane *pane);
    void requestToggleTerminalFullScreen(ConnectionPane *pane);

private slots:
    void sendRawInput(const QByteArray &data);
    void interruptCommand();
    void clearConsole();
    void refreshCurrentDirectory();
    void navigateToParent();
    void changeDirectoryFromPathBar();
    void handleFileActivated(int row, int column);
    void uploadFile();
    void uploadDirectory();
    void downloadSelected();
    void copySelectedToClipboard();
    void pasteFromClipboard();
    void previewOrEditSelected();
    void createRemoteDirectory();
    void createRemoteFile();
    void renameSelected();
    void deleteSelectedRemotePath();
    void toggleSftpPanel();
    void handleTerminalSizeChanged(int columns, int rows);
    void handleShellOutput(const QString &text, bool isErrorStream);
    void handleShellDisconnect(const QString &reason);
    void updateSelectionDependentUi();
    void showRemoteContextMenu(const QPoint &position);

private:
    enum RemoteRoles {
        PathRole = Qt::UserRole + 1,
        DirectoryRole,
        SizeRole
    };

    void buildUi();
    void setConnectedState(bool connected, const QString &message = QString());
    void appendConsoleText(const QString &text);
    void refreshDirectory(const QString &path, bool syncShell);
    void populateRemoteTable(const QVector<RemoteEntry> &entries);
    [[nodiscard]] QString selectedRemotePath(bool *isDirectory = nullptr) const;
    void syncShellDirectory();
    void updateSftpToggleButton();
    void updateHeaderPathLabel();
    void updateTerminalPathFromOutput(const QString &text);
    [[nodiscard]] QString normalizePromptPath(QString promptPath, const QString &promptUser) const;
    void applySessionShellBootstrap();
    void scheduleAutoScriptFromEnvironment();
    void uploadLocalPaths(const QStringList &paths);
    void startRemoteDragExport();
    bool runTransferOperation(const QString &title,
                              const QString &initialLabel,
                              const std::function<bool(const SftpTransferProgressHandler &, QString *)> &operation,
                              QString *errorMessage = nullptr);
    [[nodiscard]] QString prepareLocalExportPath(const QString &remotePath,
                                                 bool isDirectory,
                                                 QString *errorMessage,
                                                 const SftpTransferProgressHandler &progressHandler = {});
    [[nodiscard]] QString cachedLocalExportPath(const QString &remotePath) const;

    SessionProfile m_profile;
    QString m_currentRemotePath;
    QString m_terminalRemotePath;
    QString m_remoteHomePath;
    QString m_connectionSecret;
    bool m_connected = false;
    bool m_sftpExpanded = false;
    bool m_terminalFocusMode = false;
    bool m_restoreSftpAfterFocusMode = false;
    bool m_transferInProgress = false;
    int m_lastSftpWidth = 440;

    SshShellClient m_shellClient;
    SftpClient m_sftpClient;
    RemoteServerStatsMonitor *m_remoteStatsMonitor = nullptr;

    QLabel *m_titleLabel = nullptr;
    QLabel *m_remotePathLabel = nullptr;
    QLabel *m_statusBadge = nullptr;
    QPushButton *m_reconnectButton = nullptr;
    QPushButton *m_disconnectButton = nullptr;
    QPushButton *m_toggleSftpButton = nullptr;
    QPushButton *m_fullScreenButton = nullptr;
    QPushButton *m_closeButton = nullptr;

    QSplitter *m_splitter = nullptr;
    QWidget *m_sftpPanel = nullptr;

    TerminalView *m_consoleOutput = nullptr;
    QPushButton *m_interruptButton = nullptr;

    QLineEdit *m_pathEdit = nullptr;
    QPushButton *m_goButton = nullptr;
    QPushButton *m_upButton = nullptr;
    QPushButton *m_refreshButton = nullptr;
    QPushButton *m_uploadButton = nullptr;
    QPushButton *m_uploadDirectoryButton = nullptr;
    QPushButton *m_downloadButton = nullptr;
    QPushButton *m_previewButton = nullptr;
    QPushButton *m_newFolderButton = nullptr;
    QPushButton *m_newFileButton = nullptr;
    QPushButton *m_renameButton = nullptr;
    QPushButton *m_deleteRemoteButton = nullptr;
    QTableWidget *m_remoteTable = nullptr;
    QPoint m_remoteDragStartPos;
    QHash<QString, QString> m_dragExportCache;
};
