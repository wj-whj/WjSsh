#pragma once

#include "ConnectionPane.h"
#include "SessionEditorDialog.h"
#include "SessionRepository.h"
#include "UiChrome.h"

#include <QHBoxLayout>
#include <QFrame>
#include <QLabel>
#include <QListWidget>
#include <QMainWindow>
#include <QPushButton>
#include <QStackedWidget>
#include <QToolButton>
#include <QTabWidget>
#include <QVector>

class QShowEvent;
class QEvent;
class QByteArray;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void showEvent(QShowEvent *event) override;
    void changeEvent(QEvent *event) override;
    bool event(QEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
#if defined(Q_OS_WIN)
    bool nativeEvent(const QByteArray &eventType, void *message, qintptr *result) override;
#endif

private slots:
    void addSession();
    void editSelectedSession();
    void deleteSelectedSession();
    void connectSelectedSession();
    void closeCurrentTab();
    void closeTab(int index);
    void handleCurrentTabChanged(int index);
    void toggleThemeMode();
    void toggleSidebar();
    void toggleCurrentTerminalFullScreen();
    void handlePaneStatusMessage(const QString &message);
    void handlePaneConnectionStateChanged(ConnectionPane *pane);
    void handlePaneRemoteStatsChanged(ConnectionPane *pane);
    void handlePaneReconnectRequested(ConnectionPane *pane);
    void handlePaneCloseRequested(ConnectionPane *pane);
    void handlePaneTerminalFullScreenRequested(ConnectionPane *pane);
    void updateSelectionDependentUi();

private:
    void loadUiPreferences();
    void saveUiPreferences() const;
    void buildUi();
    void applyTheme();
    void setThemeMode(UiChrome::ThemeMode mode);
    void setSidebarExpanded(bool expanded);
    void updateSidebarUi();
    void buildStatusBarWidgets();
    void setStatusMessage(const QString &message);
    void loadProfiles();
    void saveProfiles();
    void updateTitleBarUi();
    void toggleWindowMaximizeRestore();
    void populateSessionList();
    void updateWorkspaceState();
    void updateRemoteStatsUi();
    void setTerminalFullScreen(bool enabled);
    void syncTerminalFullScreenState();
    [[nodiscard]] int selectedProfileIndex() const;
    [[nodiscard]] SessionProfile *selectedProfile();
    [[nodiscard]] const SessionProfile *selectedProfile() const;
    [[nodiscard]] ConnectionPane *currentPane() const;
    [[nodiscard]] int indexOfPane(ConnectionPane *pane) const;
    [[nodiscard]] QString promptSecret(const SessionProfile &profile, bool *accepted) const;
    [[nodiscard]] bool confirmHostKey(const QString &title,
                                      const QString &message,
                                      const QString &fingerprint) const;
    [[nodiscard]] QString nextTabBaseTitle(const SessionProfile &profile) const;
    void addConnectedPane(ConnectionPane *pane, const QString &baseTitle);
    void updateTabLabel(ConnectionPane *pane);
    void updateTabScrollButtons();
    void refreshWindowChromeNow();
    void scheduleWindowChromeRefresh();

    SessionRepository m_repository;
    QVector<SessionProfile> m_profiles;

    QHBoxLayout *m_contentLayout = nullptr;
    QFrame *m_titleBar = nullptr;
    QLabel *m_titleBarIconLabel = nullptr;
    QLabel *m_titleBarTitleLabel = nullptr;
    QToolButton *m_minimizeWindowButton = nullptr;
    QToolButton *m_maximizeWindowButton = nullptr;
    QToolButton *m_closeWindowButton = nullptr;
    QFrame *m_sidebar = nullptr;
    QWidget *m_sidebarContent = nullptr;
    QListWidget *m_sessionList = nullptr;
    QPushButton *m_sidebarToggleButton = nullptr;
    QPushButton *m_themeToggleButton = nullptr;
    QPushButton *m_addSessionButton = nullptr;
    QPushButton *m_editSessionButton = nullptr;
    QPushButton *m_deleteSessionButton = nullptr;
    QPushButton *m_connectButton = nullptr;
    QPushButton *m_closeTabButton = nullptr;

    QFrame *m_bottomStatusBar = nullptr;
    QLabel *m_statusMessageLabel = nullptr;
    QStackedWidget *m_workspaceStack = nullptr;
    QWidget *m_emptyState = nullptr;
    QTabWidget *m_tabWidget = nullptr;
    QLabel *m_cpuStatusLabel = nullptr;
    QLabel *m_memoryStatusLabel = nullptr;
    QLabel *m_diskStatusLabel = nullptr;
    QLabel *m_networkStatusLabel = nullptr;
    UiChrome::ThemeMode m_themeMode = UiChrome::ThemeMode::Light;
    bool m_sidebarExpanded = true;
    bool m_terminalFullScreen = false;
    Qt::WindowStates m_windowStateBeforeTerminalFullScreen = Qt::WindowNoState;
    bool m_connectionInProgress = false;
    bool m_windowChromeRefreshPending = false;
    bool m_titleBarDragging = false;
    QPoint m_titleBarDragOffset;
};
