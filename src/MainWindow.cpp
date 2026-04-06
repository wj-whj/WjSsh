#include "MainWindow.h"

#include "DebugTrace.h"
#include "UiChrome.h"

#include <QApplication>
#include <QEvent>
#include <QDialog>
#include <QFrame>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QMouseEvent>
#include <QScreen>
#include <QSettings>
#include <QShortcut>
#include <QShowEvent>
#include <QStatusBar>
#include <QTabBar>
#include <QTimer>
#include <QVBoxLayout>
#include <QWindow>

#if defined(Q_OS_WIN)
#include <Windows.h>
#endif

namespace {

QString formatBytesCompact(quint64 bytes)
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

QString formatRateCompact(double bytesPerSecond)
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

QString formatPercentCompact(double value)
{
    return QStringLiteral("%1%").arg(QString::number(value, 'f', value >= 10.0 ? 0 : 1));
}

QString themeModeKey(UiChrome::ThemeMode mode)
{
    return mode == UiChrome::ThemeMode::Dark ? QStringLiteral("dark") : QStringLiteral("light");
}

UiChrome::ThemeMode themeModeFromKey(const QString &value)
{
    return value.compare(QStringLiteral("dark"), Qt::CaseInsensitive) == 0 ? UiChrome::ThemeMode::Dark
                                                                            : UiChrome::ThemeMode::Light;
}

void localizeMessageBoxButtons(QMessageBox &box)
{
    if (auto *button = box.button(QMessageBox::Ok)) {
        button->setText(QStringLiteral("确定"));
    }
    if (auto *button = box.button(QMessageBox::Cancel)) {
        button->setText(QStringLiteral("取消"));
    }
    if (auto *button = box.button(QMessageBox::Yes)) {
        button->setText(QStringLiteral("是"));
    }
    if (auto *button = box.button(QMessageBox::No)) {
        button->setText(QStringLiteral("否"));
    }
}

QMessageBox::StandardButton execThemedMessageBox(QWidget *parent,
                                                 QMessageBox::Icon icon,
                                                 const QString &title,
                                                 const QString &text,
                                                 QMessageBox::StandardButtons buttons = QMessageBox::Ok,
                                                 QMessageBox::StandardButton defaultButton = QMessageBox::Ok,
                                                 const QString &informativeText = QString())
{
    QMessageBox box(icon, title, text, buttons, parent);
    box.setDefaultButton(defaultButton);
    if (!informativeText.isEmpty()) {
        box.setInformativeText(informativeText);
    }
    localizeMessageBoxButtons(box);
    UiChrome::applyMessageBoxTheme(&box);
    return static_cast<QMessageBox::StandardButton>(box.exec());
}

QString execThemedTextInput(QWidget *parent,
                            const QString &title,
                            const QString &label,
                            QLineEdit::EchoMode echoMode,
                            const QString &textValue,
                            bool *accepted)
{
    QInputDialog dialog(parent);
    dialog.setInputMode(QInputDialog::TextInput);
    dialog.setWindowTitle(title);
    dialog.setLabelText(label);
    dialog.setTextEchoMode(echoMode);
    dialog.setTextValue(textValue);
    dialog.setOkButtonText(QStringLiteral("确定"));
    dialog.setCancelButtonText(QStringLiteral("取消"));
    UiChrome::applyInputDialogTheme(&dialog);

    const bool ok = dialog.exec() == QDialog::Accepted;
    if (accepted != nullptr) {
        *accepted = ok;
    }
    return ok ? dialog.textValue() : QString();
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint | Qt::WindowMinMaxButtonsHint | Qt::WindowCloseButtonHint);
    loadUiPreferences();
    buildUi();
    applyTheme();
    updateRemoteStatsUi();
    loadProfiles();

    const QString autoConnectTarget = qEnvironmentVariable("WJSSH_AUTO_CONNECT").trimmed();
    if (!autoConnectTarget.isEmpty()) {
        QTimer::singleShot(0, this, [this, autoConnectTarget]() {
            for (int row = 0; row < m_profiles.size(); ++row) {
                const SessionProfile &profile = m_profiles[row];
                if (profile.displayName() == autoConnectTarget
                    || profile.subtitle() == autoConnectTarget
                    || profile.host == autoConnectTarget
                    || profile.id == autoConnectTarget) {
                    wjsshTrace(QStringLiteral("MainWindow auto-connect target matched row=%1 profile=%2")
                                   .arg(row)
                                   .arg(profile.subtitle()));
                    m_sessionList->setCurrentRow(row);
                    connectSelectedSession();
                    return;
                }
            }
            wjsshTrace(QStringLiteral("MainWindow auto-connect target not found target=%1").arg(autoConnectTarget));
        });
    }
}

MainWindow::~MainWindow() = default;

void MainWindow::showEvent(QShowEvent *event)
{
    QMainWindow::showEvent(event);
    scheduleWindowChromeRefresh();
}

void MainWindow::changeEvent(QEvent *event)
{
    QMainWindow::changeEvent(event);
    if (event == nullptr) {
        return;
    }

    switch (event->type()) {
    case QEvent::ActivationChange:
    case QEvent::WindowStateChange:
    case QEvent::PaletteChange:
    case QEvent::ApplicationPaletteChange:
    case QEvent::StyleChange:
    case QEvent::ThemeChange:
    case QEvent::WinIdChange:
        scheduleWindowChromeRefresh();
        break;
    default:
        break;
    }
}

bool MainWindow::event(QEvent *event)
{
    if (event != nullptr) {
        switch (event->type()) {
        case QEvent::WindowActivate:
        case QEvent::WindowStateChange:
        case QEvent::Show:
        case QEvent::Polish:
        case QEvent::PolishRequest:
            scheduleWindowChromeRefresh();
            break;
        default:
            break;
        }
    }

    return QMainWindow::event(event);
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    const bool titleBarTarget
        = watched == m_titleBar || watched == m_titleBarIconLabel || watched == m_titleBarTitleLabel;
    if (titleBarTarget && event != nullptr && !m_terminalFullScreen) {
        if (event->type() == QEvent::MouseButtonDblClick) {
            auto *mouseEvent = static_cast<QMouseEvent *>(event);
            if (mouseEvent->button() == Qt::LeftButton) {
                toggleWindowMaximizeRestore();
                return true;
            }
        }
        if (event->type() == QEvent::MouseButtonPress) {
            auto *mouseEvent = static_cast<QMouseEvent *>(event);
            if (mouseEvent->button() == Qt::LeftButton && !isMaximized() && !isFullScreen()) {
                m_titleBarDragging = true;
                m_titleBarDragOffset = mouseEvent->globalPosition().toPoint() - frameGeometry().topLeft();
                return true;
            }
        }
        if (event->type() == QEvent::MouseMove) {
            auto *mouseEvent = static_cast<QMouseEvent *>(event);
            if (m_titleBarDragging && (mouseEvent->buttons() & Qt::LeftButton) && !isMaximized() && !isFullScreen()) {
                move(mouseEvent->globalPosition().toPoint() - m_titleBarDragOffset);
                return true;
            }
        }
        if (event->type() == QEvent::MouseButtonRelease) {
            auto *mouseEvent = static_cast<QMouseEvent *>(event);
            if (mouseEvent->button() == Qt::LeftButton) {
                m_titleBarDragging = false;
            }
        }
        if (event->type() == QEvent::Leave) {
            m_titleBarDragging = false;
        }
    }

    return QMainWindow::eventFilter(watched, event);
}

#if defined(Q_OS_WIN)
bool MainWindow::nativeEvent(const QByteArray &eventType, void *message, qintptr *result)
{
    if (message != nullptr) {
        auto *msg = static_cast<MSG *>(message);
        switch (msg->message) {
        case WM_NCHITTEST: {
            if (isFullScreen()) {
                break;
            }

            const HWND hwnd = reinterpret_cast<HWND>(winId());
            RECT windowRect{};
            if (!GetWindowRect(hwnd, &windowRect)) {
                break;
            }

            const QPoint globalPos(static_cast<short>(LOWORD(msg->lParam)),
                                   static_cast<short>(HIWORD(msg->lParam)));
            const int frame = isMaximized() ? 0 : 8;

            const bool onLeft = globalPos.x() >= windowRect.left && globalPos.x() < windowRect.left + frame;
            const bool onRight = globalPos.x() < windowRect.right && globalPos.x() >= windowRect.right - frame;
            const bool onTop = globalPos.y() >= windowRect.top && globalPos.y() < windowRect.top + frame;
            const bool onBottom = globalPos.y() < windowRect.bottom && globalPos.y() >= windowRect.bottom - frame;

            if (!isMaximized()) {
                if (onTop && onLeft) {
                    *result = HTTOPLEFT;
                    return true;
                }
                if (onTop && onRight) {
                    *result = HTTOPRIGHT;
                    return true;
                }
                if (onBottom && onLeft) {
                    *result = HTBOTTOMLEFT;
                    return true;
                }
                if (onBottom && onRight) {
                    *result = HTBOTTOMRIGHT;
                    return true;
                }
                if (onTop) {
                    *result = HTTOP;
                    return true;
                }
                if (onBottom) {
                    *result = HTBOTTOM;
                    return true;
                }
                if (onLeft) {
                    *result = HTLEFT;
                    return true;
                }
                if (onRight) {
                    *result = HTRIGHT;
                    return true;
                }
            }

            if (m_titleBar != nullptr && m_titleBar->isVisible()) {
                const QPoint localPos = mapFromGlobal(globalPos);
                const QPoint titleBarPos = m_titleBar->mapFrom(this, localPos);
                if (m_titleBar->rect().contains(titleBarPos)) {
                    QWidget *child = childAt(localPos);
                    const bool overControlButton = child == m_minimizeWindowButton || child == m_maximizeWindowButton
                                                   || child == m_closeWindowButton
                                                   || (child != nullptr
                                                       && (child->parentWidget() == m_minimizeWindowButton
                                                           || child->parentWidget() == m_maximizeWindowButton
                                                           || child->parentWidget() == m_closeWindowButton));
                    if (!overControlButton) {
                        *result = HTCAPTION;
                        return true;
                    }
                }
            }
            break;
        }
        case WM_GETMINMAXINFO: {
            if (QScreen *currentScreen = screen()) {
                auto *mmi = reinterpret_cast<MINMAXINFO *>(msg->lParam);
                const QRect available = currentScreen->availableGeometry();
                const QRect screenRect = currentScreen->geometry();
                mmi->ptMaxPosition.x = available.x() - screenRect.x();
                mmi->ptMaxPosition.y = available.y() - screenRect.y();
                mmi->ptMaxSize.x = available.width();
                mmi->ptMaxSize.y = available.height();
                *result = 0;
                return true;
            }
            break;
        }
        case WM_ACTIVATE:
        case WM_NCACTIVATE:
        case WM_WINDOWPOSCHANGED:
        case WM_SIZE:
            scheduleWindowChromeRefresh();
            break;
        default:
            break;
        }
    }

    return QMainWindow::nativeEvent(eventType, message, result);
}
#endif

void MainWindow::refreshWindowChromeNow()
{
    updateTitleBarUi();
}

void MainWindow::scheduleWindowChromeRefresh()
{
    if (m_windowChromeRefreshPending) {
        return;
    }

    m_windowChromeRefreshPending = true;
    QTimer::singleShot(0, this, [this]() {
        m_windowChromeRefreshPending = false;
        refreshWindowChromeNow();
        QTimer::singleShot(80, this, [this]() { refreshWindowChromeNow(); });
        QTimer::singleShot(220, this, [this]() { refreshWindowChromeNow(); });
    });
}

void MainWindow::loadUiPreferences()
{
    QSettings settings;
    m_themeMode = themeModeFromKey(settings.value(QStringLiteral("ui/theme"), QStringLiteral("light")).toString());
    m_sidebarExpanded = settings.value(QStringLiteral("ui/sidebarExpanded"), true).toBool();
    UiChrome::setThemeMode(m_themeMode);
}

void MainWindow::saveUiPreferences() const
{
    QSettings settings;
    settings.setValue(QStringLiteral("ui/theme"), themeModeKey(m_themeMode));
    settings.setValue(QStringLiteral("ui/sidebarExpanded"), m_sidebarExpanded);
}

void MainWindow::buildUi()
{
    const QRect available = screen() != nullptr ? screen()->availableGeometry()
                                                : QApplication::primaryScreen()->availableGeometry();
    const int safeWidth = qMax(1, available.width() - 24);
    const int safeHeight = qMax(1, available.height() - 24);
    resize(qMin(1560, safeWidth), qMin(940, safeHeight));
    setMinimumSize(qMin(960, safeWidth), qMin(680, safeHeight));
    setWindowTitle(QStringLiteral("WjSsh"));
    move(available.center() - QPoint(width() / 2, height() / 2));

    auto *central = new QWidget(this);
    setCentralWidget(central);

    auto *outerLayout = new QVBoxLayout(central);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->setSpacing(0);

    m_titleBar = new QFrame(central);
    m_titleBar->setObjectName("windowTitleBar");
    m_titleBar->setFixedHeight(42);

    auto *titleBarLayout = new QHBoxLayout(m_titleBar);
    titleBarLayout->setContentsMargins(14, 0, 6, 0);
    titleBarLayout->setSpacing(8);

    m_titleBarIconLabel = new QLabel(m_titleBar);
    m_titleBarIconLabel->setObjectName("windowTitleIcon");
    m_titleBarIconLabel->setFixedSize(18, 18);
    m_titleBarIconLabel->setPixmap(windowIcon().pixmap(18, 18));

    m_titleBarTitleLabel = new QLabel(windowTitle(), m_titleBar);
    m_titleBarTitleLabel->setObjectName("windowTitleLabel");

    m_minimizeWindowButton = new QToolButton(m_titleBar);
    m_minimizeWindowButton->setObjectName("windowControlButton");
    m_minimizeWindowButton->setText(QStringLiteral("-"));
    m_minimizeWindowButton->setToolTip(QStringLiteral("最小化"));

    m_maximizeWindowButton = new QToolButton(m_titleBar);
    m_maximizeWindowButton->setObjectName("windowControlButton");
    m_maximizeWindowButton->setToolTip(QStringLiteral("最大化"));

    m_closeWindowButton = new QToolButton(m_titleBar);
    m_closeWindowButton->setObjectName("windowCloseButton");
    m_closeWindowButton->setText(QStringLiteral("x"));
    m_closeWindowButton->setToolTip(QStringLiteral("关闭"));

    titleBarLayout->addWidget(m_titleBarIconLabel, 0, Qt::AlignVCenter);
    titleBarLayout->addWidget(m_titleBarTitleLabel, 0, Qt::AlignVCenter);
    titleBarLayout->addStretch();
    titleBarLayout->addWidget(m_minimizeWindowButton, 0, Qt::AlignVCenter);
    titleBarLayout->addWidget(m_maximizeWindowButton, 0, Qt::AlignVCenter);
    titleBarLayout->addWidget(m_closeWindowButton, 0, Qt::AlignVCenter);

    m_contentLayout = new QHBoxLayout();
    m_contentLayout->setContentsMargins(18, 18, 18, 0);
    m_contentLayout->setSpacing(18);

    m_sidebar = new QFrame(central);
    m_sidebar->setObjectName("sidebar");
    m_sidebar->setMinimumWidth(290);
    m_sidebar->setMaximumWidth(340);

    auto *sidebarLayout = new QVBoxLayout(m_sidebar);
    sidebarLayout->setContentsMargins(18, 18, 18, 18);
    sidebarLayout->setSpacing(14);

    auto *sidebarHeader = new QHBoxLayout();
    sidebarHeader->setSpacing(8);
    m_sidebarToggleButton = new QPushButton(m_sidebar);
    m_sidebarToggleButton->setObjectName("sidebarUtilityButton");
    m_themeToggleButton = new QPushButton(m_sidebar);
    m_themeToggleButton->setObjectName("sidebarUtilityButton");
    sidebarHeader->addWidget(m_sidebarToggleButton);
    sidebarHeader->addStretch();
    sidebarHeader->addWidget(m_themeToggleButton);
    sidebarLayout->addLayout(sidebarHeader);

    m_sidebarContent = new QWidget(m_sidebar);
    auto *sidebarContentLayout = new QVBoxLayout(m_sidebarContent);
    sidebarContentLayout->setContentsMargins(0, 0, 0, 0);
    sidebarContentLayout->setSpacing(14);

    auto *heroCard = new QFrame(m_sidebarContent);
    heroCard->setObjectName("heroCard");

    auto *heroLayout = new QVBoxLayout(heroCard);
    heroLayout->setContentsMargins(18, 18, 18, 18);
    heroLayout->setSpacing(8);

    auto *appTitle = new QLabel(QStringLiteral("WjSsh"), heroCard);
    appTitle->setObjectName("appTitle");
    auto *appSubtitle = new QLabel(QStringLiteral("多会话 SSH 远程工作台"), heroCard);
    appSubtitle->setObjectName("appSubtitle");
    appSubtitle->setWordWrap(true);

    heroLayout->addWidget(appTitle);
    heroLayout->addWidget(appSubtitle);
    auto *sessionsTitle = new QLabel(QStringLiteral("连接会话"), m_sidebarContent);
    sessionsTitle->setObjectName("sectionTitle");

    m_sessionList = new QListWidget(m_sidebarContent);
    m_sessionList->setSpacing(8);
    m_sessionList->setSelectionMode(QAbstractItemView::SingleSelection);

    auto *sessionActionRow = new QHBoxLayout();
    sessionActionRow->setSpacing(8);
    m_addSessionButton = new QPushButton(QStringLiteral("新建"), m_sidebarContent);
    m_editSessionButton = new QPushButton(QStringLiteral("编辑"), m_sidebarContent);
    m_deleteSessionButton = new QPushButton(QStringLiteral("删除"), m_sidebarContent);

    sessionActionRow->addWidget(m_addSessionButton);
    sessionActionRow->addWidget(m_editSessionButton);
    sessionActionRow->addWidget(m_deleteSessionButton);
    m_connectButton = new QPushButton(QStringLiteral("打开连接"), m_sidebarContent);
    m_connectButton->setObjectName("primaryButton");
    m_closeTabButton = new QPushButton(QStringLiteral("关闭当前标签"), m_sidebarContent);

    sidebarContentLayout->addWidget(heroCard);
    sidebarContentLayout->addWidget(sessionsTitle);
    sidebarContentLayout->addWidget(m_sessionList, 1);
    sidebarContentLayout->addLayout(sessionActionRow);
    sidebarContentLayout->addWidget(m_connectButton);
    sidebarContentLayout->addWidget(m_closeTabButton);
    sidebarLayout->addWidget(m_sidebarContent, 1);

    auto *workspace = new QWidget(central);
    auto *workspaceLayout = new QVBoxLayout(workspace);
    workspaceLayout->setContentsMargins(0, 0, 0, 0);
    workspaceLayout->setSpacing(0);

    m_workspaceStack = new QStackedWidget(workspace);

    m_emptyState = new QFrame(m_workspaceStack);
    m_emptyState->setObjectName("panelCard");

    auto *emptyLayout = new QVBoxLayout(m_emptyState);
    emptyLayout->setContentsMargins(28, 28, 28, 28);
    emptyLayout->setSpacing(0);

    auto *emptyContent = new QWidget(m_emptyState);
    emptyContent->setMaximumWidth(620);
    auto *emptyContentLayout = new QVBoxLayout(emptyContent);
    emptyContentLayout->setContentsMargins(0, 0, 0, 0);
    emptyContentLayout->setSpacing(12);

    auto *emptyTitle = new QLabel(QStringLiteral("暂无打开的连接"), emptyContent);
    emptyTitle->setObjectName("sectionTitle");
    emptyTitle->setAlignment(Qt::AlignCenter);
    emptyTitle->setWordWrap(true);

    auto *emptyHint = new QLabel(QStringLiteral("从左侧选择一个会话，然后点击“打开连接”来新建 SSH 标签页。"),
                                 emptyContent);
    emptyHint->setObjectName("mutedText");
    emptyHint->setAlignment(Qt::AlignCenter);
    emptyHint->setWordWrap(true);
    emptyHint->setMinimumWidth(420);

    emptyContentLayout->addWidget(emptyTitle);
    emptyContentLayout->addWidget(emptyHint);
    emptyLayout->addStretch();
    emptyLayout->addWidget(emptyContent, 0, Qt::AlignCenter);
    emptyLayout->addStretch();

    m_tabWidget = new QTabWidget(m_workspaceStack);
    m_tabWidget->setTabsClosable(true);
    m_tabWidget->setMovable(true);
    m_tabWidget->setDocumentMode(false);
    m_tabWidget->tabBar()->setExpanding(false);
    m_tabWidget->tabBar()->setUsesScrollButtons(true);
    QTimer::singleShot(0, this, &MainWindow::updateTabScrollButtons);

    m_workspaceStack->addWidget(m_emptyState);
    m_workspaceStack->addWidget(m_tabWidget);

    workspaceLayout->addWidget(m_workspaceStack, 1);

    m_contentLayout->addWidget(m_sidebar);
    m_contentLayout->addWidget(workspace, 1);

    connect(m_addSessionButton, &QPushButton::clicked, this, &MainWindow::addSession);
    connect(m_editSessionButton, &QPushButton::clicked, this, &MainWindow::editSelectedSession);
    connect(m_deleteSessionButton, &QPushButton::clicked, this, &MainWindow::deleteSelectedSession);
    connect(m_connectButton, &QPushButton::clicked, this, &MainWindow::connectSelectedSession);
    connect(m_closeTabButton, &QPushButton::clicked, this, &MainWindow::closeCurrentTab);
    connect(m_sidebarToggleButton, &QPushButton::clicked, this, &MainWindow::toggleSidebar);
    connect(m_themeToggleButton, &QPushButton::clicked, this, &MainWindow::toggleThemeMode);
    connect(m_sessionList, &QListWidget::itemDoubleClicked, this, [this]() { connectSelectedSession(); });
    connect(m_sessionList, &QListWidget::currentRowChanged, this, &MainWindow::updateSelectionDependentUi);
    connect(m_tabWidget, &QTabWidget::tabCloseRequested, this, &MainWindow::closeTab);
    connect(m_tabWidget, &QTabWidget::currentChanged, this, &MainWindow::handleCurrentTabChanged);

    auto *closeShortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+W")), this);
    connect(closeShortcut, &QShortcut::activated, this, &MainWindow::closeCurrentTab);
    auto *themeShortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+T")), this);
    connect(themeShortcut, &QShortcut::activated, this, &MainWindow::toggleThemeMode);
    auto *sidebarShortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+B")), this);
    connect(sidebarShortcut, &QShortcut::activated, this, &MainWindow::toggleSidebar);
    auto *fullScreenShortcut = new QShortcut(QKeySequence(Qt::Key_F11), this);
    connect(fullScreenShortcut, &QShortcut::activated, this, &MainWindow::toggleCurrentTerminalFullScreen);
    auto *escapeShortcut = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    connect(escapeShortcut,
            &QShortcut::activated,
            this,
            [this]() {
                if (m_terminalFullScreen) {
                    setTerminalFullScreen(false);
                }
            });
    buildStatusBarWidgets();
    outerLayout->addWidget(m_titleBar);
    outerLayout->addLayout(m_contentLayout, 1);
    outerLayout->addWidget(m_bottomStatusBar);
    statusBar()->hide();
    setStatusMessage(QStringLiteral("会话配置保存位置：%1").arg(m_repository.storagePath()));
    updateWorkspaceState();
    updateTitleBarUi();
    updateSidebarUi();
    updateSelectionDependentUi();

    connect(m_minimizeWindowButton, &QToolButton::clicked, this, &QWidget::showMinimized);
    connect(m_maximizeWindowButton, &QToolButton::clicked, this, &MainWindow::toggleWindowMaximizeRestore);
    connect(m_closeWindowButton, &QToolButton::clicked, this, &QWidget::close);
    connect(this, &QWidget::windowTitleChanged, this, [this](const QString &) { updateTitleBarUi(); });
    m_titleBar->installEventFilter(this);
    m_titleBarIconLabel->installEventFilter(this);
    m_titleBarTitleLabel->installEventFilter(this);
}

void MainWindow::applyTheme()
{
    UiChrome::setThemeMode(m_themeMode);
    if (qApp != nullptr) {
        UiChrome::applyAppTheme(qApp);
    }

    const QString styleSheet = m_themeMode == UiChrome::ThemeMode::Dark
                                   ? QStringLiteral(
                                         "QWidget {"
                                         "  color: #EEF2F5;"
                                         "  font-family: 'Microsoft YaHei UI', 'Segoe UI Variable Text', 'Segoe UI', sans-serif;"
                                         "  font-size: 13px;"
                                         "}"
                                         "QMainWindow { background: #1F1F1F; }"
                                         "QFrame#windowTitleBar {"
                                         "  background: #1F1F1F; border-bottom: 1px solid #2E2E2E;"
                                         "}"
                                         "QLabel#windowTitleLabel { color: #F3F3F3; font-size: 13px; font-weight: 600; }"
                                         "QLabel#windowTitleIcon { background: transparent; }"
                                         "QToolButton#windowControlButton, QToolButton#windowCloseButton {"
                                         "  background: transparent; border: 1px solid transparent; border-radius: 8px;"
                                         "  min-width: 40px; max-width: 40px; min-height: 28px; max-height: 28px;"
                                         "  padding: 0px; font-size: 14px; color: #E6E6E6;"
                                         "}"
                                         "QToolButton#windowControlButton:hover { background: #2C2C2C; border-color: #3A3A3A; }"
                                         "QToolButton#windowControlButton:pressed { background: #262626; }"
                                         "QToolButton#windowCloseButton:hover { background: #C42B1C; border-color: #C42B1C; color: white; }"
                                         "QToolButton#windowCloseButton:pressed { background: #A52619; border-color: #A52619; color: white; }"
                                         "QFrame#bottomStatusBar {"
                                         "  background: #232323; border-top: 1px solid #323232;"
                                         "}"
                                         "QFrame#sidebar, QFrame#panelCard {"
                                         "  background: #242424;"
                                         "  border: 1px solid #343434;"
                                         "  border-radius: 22px;"
                                         "}"
                                         "QFrame#terminalCard {"
                                         "  background: #000000;"
                                         "  border: 1px solid #141414;"
                                         "  border-radius: 22px;"
                                         "}"
                                         "QFrame#terminalCard QLabel#sectionTitle { color: #F7F8FA; }"
                                         "QFrame#terminalCard QLabel#mutedText { color: rgba(235, 239, 243, 0.76); }"
                                         "QFrame#terminalCard QPushButton {"
                                         "  background: #111111; color: #F5F7FA; border: 1px solid #2A2A2A;"
                                         "}"
                                         "QFrame#terminalCard QPushButton:hover { background: #1B1B1B; }"
                                         "QFrame#heroCard {"
                                         "  background: qlineargradient(x1:0, y1:0, x2:1, y2:1,"
                                         "              stop:0 #2A2A2A, stop:0.56 #252525, stop:1 #212121);"
                                         "  border-radius: 22px;"
                                         "}"
                                         "QLabel#appTitle { font-size: 28px; font-weight: 700; color: #FFFFFF; }"
                                         "QLabel#appSubtitle, QLabel#mutedText { color: rgba(229, 234, 239, 0.78); }"
                                         "QLabel#sectionTitle, QLabel#dialogTitle { font-size: 16px; font-weight: 700; color: #FBFCFD; }"
                                         "QLabel#statusMetric {"
                                         "  background: #2C2C2C; border: 1px solid #3D3D3D; border-radius: 11px;"
                                         "  padding: 4px 10px; color: #EFEFEF; margin-left: 6px;"
                                         "}"
                                         "QLabel#statusMessageText { color: #D0D0D0; padding: 0 6px; }"
                                         "QLabel#statusBadge {"
                                         "  border-radius: 18px; font-weight: 700; background: #2C2C2C; color: #F3F3F3; padding: 0 14px;"
                                         "}"
                                         "QPushButton {"
                                         "  background: #2E2E2E; border: 1px solid #3F3F3F; border-radius: 12px; padding: 8px 14px;"
                                         "}"
                                         "QPushButton#sidebarUtilityButton { min-width: 84px; }"
                                         "QPushButton:hover { background: #373737; }"
                                         "QPushButton#primaryButton { background: #3A3A3A; border-color: #525252; color: white; font-weight: 700; }"
                                         "QPushButton#primaryButton:hover { background: #454545; }"
                                         "QPushButton#dangerButton { background: #5A4348; border-color: #785C62; }"
                                         "QPushButton#dangerButton:hover { background: #684F55; }"
                                         "QLineEdit, QSpinBox, QComboBox, QPlainTextEdit, QTextEdit, QTableWidget, QListWidget {"
                                         "  background: #262626; border: 1px solid #393939; border-radius: 14px;"
                                         "  selection-background-color: #454545; selection-color: white;"
                                         "}"
                                         "QLineEdit, QSpinBox, QComboBox { padding: 8px 10px; }"
                                         "QLineEdit#terminalInput, QPlainTextEdit#consoleOutput, QTextEdit#consoleOutput {"
                                         "  font-family: 'Cascadia Mono', 'Consolas', monospace;"
                                         "}"
                                         "QPlainTextEdit#consoleOutput, QTextEdit#consoleOutput { font-size: 12px; padding: 12px; background: #000000; border-color: #1B1B1B; color:#F3F6FA; }"
                                         "QTableWidget { alternate-background-color: #272727; gridline-color: transparent; }"
                                         "QTableWidget::item { border: none; padding: 6px 8px; }"
                                         "QTableWidget::item:selected { background: #404040; color: white; }"
                                         "QTableCornerButton::section { background: #282828; border: none; }"
                                         "QListWidget::item {"
                                         "  margin: 4px; padding: 12px; border-radius: 14px; border: 1px solid transparent;"
                                         "}"
                                         "QListWidget::item:selected { border-color: #4B4B4B; background: #303030; }"
                                         "QHeaderView::section { background: #242424; padding: 8px; border: none; border-right: 1px solid #363636; border-bottom: 1px solid #363636; }"
                                         "QHeaderView::section:hover { background: #2A2A2A; }"
                                         "QMenu { background: #242424; border: 1px solid #363636; padding: 8px; }"
                                         "QMenu::item { padding: 8px 16px; border-radius: 8px; }"
                                         "QMenu::item:selected { background: #343434; }"
                                         "QMenu::separator { height: 1px; background: #3A3A3A; margin: 6px 10px; }"
                                         "QTabWidget::pane { background: transparent; border: none; margin-top: 6px; }"
                                         "QTabBar::tab {"
                                         "  background: #262626; border: 1px solid #383838;"
                                         "  padding: 5px 14px; min-width: 112px; min-height: 28px; margin-right: 6px; border-radius: 10px;"
                                         "}"
                                         "QTabBar::tab:selected { background: #303030; border-color: #4A4A4A; color: #FFFFFF; }"
                                         "QTabBar::tab:hover { background: #2C2C2C; }"
                                         "QTabBar QToolButton {"
                                         "  background: #2A2A2A; border: 1px solid #3A3A3A; border-radius: 9px;"
                                         "  color: #D8D8D8; padding: 0px; width: 26px; height: 26px; font-size: 16px; font-weight: 700;"
                                         "}"
                                         "QTabBar QToolButton:hover { background: #353535; border-color: #484848; }"
                                         "QTabBar QToolButton:disabled { background: #242424; color: #7B7B7B; border-color: #303030; }"
                                         "QSplitter::handle { background: transparent; }"
                                         "QSplitter::handle:hover { background: rgba(160, 160, 160, 0.14); }"
                                         "QScrollBar:vertical { background: transparent; width: 12px; margin: 2px; }"
                                         "QScrollBar::handle:vertical { background: #505050; border-radius: 6px; min-height: 24px; }"
                                         "QScrollBar:horizontal { background: transparent; height: 12px; margin: 2px; }"
                                         "QScrollBar::handle:horizontal { background: #505050; border-radius: 6px; min-width: 24px; }"
                                         "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical,"
                                         "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal {"
                                         "  background: transparent; border: none; width: 0px; height: 0px;"
                                         "}"
                                         "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical,"
                                         "QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal {"
                                         "  background: transparent;"
                                         "}")
                                   : QStringLiteral(
                                         "QWidget {"
                                         "  color: #1F2B36;"
                                         "  font-family: 'Microsoft YaHei UI', 'Segoe UI Variable Text', 'Segoe UI', sans-serif;"
                                         "  font-size: 13px;"
                                         "}"
                                         "QMainWindow { background: #F3F6FA; }"
                                         "QFrame#windowTitleBar {"
                                         "  background: #FFFFFF; border-bottom: 1px solid #E5E5E5;"
                                         "}"
                                         "QLabel#windowTitleLabel { color: #1F2B36; font-size: 13px; font-weight: 600; }"
                                         "QLabel#windowTitleIcon { background: transparent; }"
                                         "QToolButton#windowControlButton, QToolButton#windowCloseButton {"
                                         "  background: transparent; border: 1px solid transparent; border-radius: 8px;"
                                         "  min-width: 40px; max-width: 40px; min-height: 28px; max-height: 28px;"
                                         "  padding: 0px; font-size: 14px; color: #33414D;"
                                         "}"
                                         "QToolButton#windowControlButton:hover { background: #EEF2F6; border-color: #D9E1E9; }"
                                         "QToolButton#windowControlButton:pressed { background: #E3E8EE; }"
                                         "QToolButton#windowCloseButton:hover { background: #C42B1C; border-color: #C42B1C; color: white; }"
                                         "QToolButton#windowCloseButton:pressed { background: #A52619; border-color: #A52619; color: white; }"
                                         "QFrame#bottomStatusBar {"
                                         "  background: #F7F9FC; border-top: 1px solid #D9E1E8;"
                                         "}"
                                         "QFrame#sidebar, QFrame#panelCard {"
                                         "  background: #FFFFFF;"
                                         "  border: 1px solid #D6DEE6;"
                                         "  border-radius: 22px;"
                                         "}"
                                         "QFrame#terminalCard {"
                                         "  background: #1F1F1F;"
                                         "  border: 1px solid #303030;"
                                         "  border-radius: 22px;"
                                         "}"
                                         "QFrame#terminalCard QLabel#sectionTitle { color: #F7F8FA; }"
                                         "QFrame#terminalCard QLabel#mutedText { color: rgba(235, 239, 243, 0.76); }"
                                         "QFrame#terminalCard QPushButton {"
                                         "  background: #2A2A2A; color: #F5F7FA; border: 1px solid #3A3A3A;"
                                         "}"
                                         "QFrame#terminalCard QPushButton:hover { background: #353535; }"
                                         "QFrame#heroCard {"
                                         "  background: qlineargradient(x1:0, y1:0, x2:1, y2:1,"
                                         "              stop:0 #FFFFFF, stop:0.55 #F6F9FC, stop:1 #EDF3F8);"
                                         "  border: 1px solid #D7E1EA;"
                                         "  border-radius: 22px;"
                                         "}"
                                         "QLabel#appTitle { font-size: 28px; font-weight: 700; color: #182531; }"
                                         "QLabel#appSubtitle, QLabel#mutedText { color: #667685; }"
                                         "QLabel#sectionTitle, QLabel#dialogTitle { font-size: 16px; font-weight: 700; color: #16222D; }"
                                         "QLabel#statusMetric {"
                                         "  background: #FFFFFF; border: 1px solid #D7E0E8; border-radius: 11px;"
                                         "  padding: 4px 10px; color: #24313C; margin-left: 6px;"
                                         "}"
                                         "QLabel#statusMessageText { color: #5F6F7E; padding: 0 6px; }"
                                         "QLabel#statusBadge {"
                                         "  border-radius: 18px; font-weight: 700; background: #F3F5F7; color: #43515D; padding: 0 14px;"
                                         "}"
                                         "QPushButton {"
                                         "  background: #FFFFFF; border: 1px solid #CDD6DE; border-radius: 12px; padding: 8px 14px;"
                                         "}"
                                         "QPushButton#sidebarUtilityButton { min-width: 84px; }"
                                         "QPushButton:hover { background: #EFF4F8; }"
                                         "QPushButton#primaryButton { background: #1F6FEB; border-color: #1559BE; color: white; font-weight: 700; }"
                                         "QPushButton#primaryButton:hover { background: #2B78F0; }"
                                         "QPushButton#dangerButton { background: #FFF4F4; border-color: #E7B7B7; color: #A34545; }"
                                         "QPushButton#dangerButton:hover { background: #FFEAEA; }"
                                         "QLineEdit, QSpinBox, QComboBox, QPlainTextEdit, QTextEdit, QTableWidget, QListWidget {"
                                         "  background: #FFFFFF; border: 1px solid #D0D8E1; border-radius: 14px;"
                                         "  selection-background-color: #D6E7FF; selection-color: #123150;"
                                         "}"
                                         "QLineEdit, QSpinBox, QComboBox { padding: 8px 10px; }"
                                         "QLineEdit#terminalInput, QPlainTextEdit#consoleOutput, QTextEdit#consoleOutput {"
                                         "  font-family: 'Cascadia Mono', 'Consolas', monospace;"
                                         "}"
                                         "QPlainTextEdit#consoleOutput, QTextEdit#consoleOutput {"
                                         "  font-size: 12px; padding: 12px; background: #1F1F1F; color: #F3F6FA; border-color: #303030;"
                                         "}"
                                         "QTableWidget { alternate-background-color: #F7FAFD; gridline-color: transparent; }"
                                         "QTableWidget::item { border: none; padding: 6px 8px; }"
                                         "QTableWidget::item:selected { background: #E3EDF8; color: #123150; }"
                                         "QTableCornerButton::section { background: #F0F4F8; border: none; }"
                                         "QListWidget::item {"
                                         "  margin: 4px; padding: 12px; border-radius: 14px; border: 1px solid transparent;"
                                         "}"
                                         "QListWidget::item:selected { border-color: #BFD0E2; background: #ECF3FA; }"
                                         "QHeaderView::section { background: #F4F7FA; padding: 8px; border: none; border-right: 1px solid #D5DDE6; border-bottom: 1px solid #D5DDE6; }"
                                         "QHeaderView::section:hover { background: #EDF3F8; }"
                                         "QMenu { background: #FFFFFF; border: 1px solid #D0D9E2; padding: 8px; }"
                                         "QMenu::item { padding: 8px 16px; border-radius: 8px; }"
                                         "QMenu::item:selected { background: #EAF1F8; }"
                                         "QMenu::separator { height: 1px; background: #DDE5EC; margin: 6px 10px; }"
                                         "QTabWidget::pane { background: transparent; border: none; margin-top: 6px; }"
                                         "QTabBar::tab {"
                                         "  background: #FFFFFF; border: 1px solid #D2DAE3;"
                                         "  padding: 5px 14px; min-width: 112px; min-height: 28px; margin-right: 6px; border-radius: 10px;"
                                         "}"
                                         "QTabBar::tab:selected { background: #EEF4FA; border-color: #AFC3D6; color: #16222D; }"
                                         "QTabBar::tab:hover { background: #F4F8FC; }"
                                         "QTabBar QToolButton {"
                                         "  background: #FFFFFF; border: 1px solid #D2DAE3; border-radius: 9px;"
                                         "  color: #576677; padding: 0px; width: 26px; height: 26px; font-size: 16px; font-weight: 700;"
                                         "}"
                                         "QTabBar QToolButton:hover { background: #EEF4FA; border-color: #B6C6D6; }"
                                         "QTabBar QToolButton:disabled { background: #F6F8FB; color: #B0BAC4; border-color: #E0E6EC; }"
                                         "QSplitter::handle { background: transparent; }"
                                         "QSplitter::handle:hover { background: rgba(135, 149, 165, 0.18); }"
                                         "QScrollBar:vertical { background: transparent; width: 12px; margin: 2px; }"
                                         "QScrollBar::handle:vertical { background: #C4CED8; border-radius: 6px; min-height: 24px; }"
                                         "QScrollBar:horizontal { background: transparent; height: 12px; margin: 2px; }"
                                         "QScrollBar::handle:horizontal { background: #C4CED8; border-radius: 6px; min-width: 24px; }"
                                         "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical,"
                                         "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal {"
                                         "  background: transparent; border: none; width: 0px; height: 0px;"
                                         "}"
                                         "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical,"
                                         "QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal {"
                                         "  background: transparent;"
                                         "}");

    setStyleSheet(styleSheet);
    updateTitleBarUi();
    updateSidebarUi();
    for (int index = 0; index < m_tabWidget->count(); ++index) {
        if (auto *pane = qobject_cast<ConnectionPane *>(m_tabWidget->widget(index))) {
            pane->refreshThemeState();
        }
    }
}

void MainWindow::buildStatusBarWidgets()
{
    m_bottomStatusBar = new QFrame(centralWidget());
    m_bottomStatusBar->setObjectName("bottomStatusBar");
    m_bottomStatusBar->setFixedHeight(44);

    auto *layout = new QHBoxLayout(m_bottomStatusBar);
    layout->setContentsMargins(14, 6, 14, 6);
    layout->setSpacing(6);

    m_statusMessageLabel = new QLabel(QStringLiteral("就绪"), m_bottomStatusBar);
    m_statusMessageLabel->setObjectName("statusMessageText");
    m_statusMessageLabel->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);

    m_cpuStatusLabel = new QLabel(QStringLiteral("CPU --"), m_bottomStatusBar);
    m_memoryStatusLabel = new QLabel(QStringLiteral("内存 --"), m_bottomStatusBar);
    m_diskStatusLabel = new QLabel(QStringLiteral("磁盘 --"), m_bottomStatusBar);
    m_networkStatusLabel = new QLabel(QStringLiteral("网络 --"), m_bottomStatusBar);

    layout->addWidget(m_statusMessageLabel, 1);
    for (QLabel *label : {m_cpuStatusLabel, m_memoryStatusLabel, m_diskStatusLabel, m_networkStatusLabel}) {
        label->setObjectName("statusMetric");
        label->setAlignment(Qt::AlignCenter);
        layout->addWidget(label);
    }
}

void MainWindow::updateTitleBarUi()
{
    if (m_titleBar == nullptr || m_titleBarTitleLabel == nullptr || m_maximizeWindowButton == nullptr
        || m_minimizeWindowButton == nullptr || m_closeWindowButton == nullptr) {
        return;
    }

    m_titleBarTitleLabel->setText(windowTitle());
    const bool maximized = isMaximized() && !isFullScreen();
    m_maximizeWindowButton->setText(maximized ? QStringLiteral("❐") : QStringLiteral("□"));
    m_maximizeWindowButton->setToolTip(maximized ? QStringLiteral("还原") : QStringLiteral("最大化"));
    m_titleBar->setVisible(!m_terminalFullScreen);
}

void MainWindow::toggleWindowMaximizeRestore()
{
    if (isFullScreen()) {
        return;
    }

    if (isMaximized()) {
        showNormal();
    } else {
        showMaximized();
    }
    updateTitleBarUi();
}

void MainWindow::setThemeMode(UiChrome::ThemeMode mode)
{
    if (m_themeMode == mode) {
        return;
    }

    m_themeMode = mode;
    applyTheme();
    saveUiPreferences();
}

void MainWindow::toggleThemeMode()
{
    setThemeMode(m_themeMode == UiChrome::ThemeMode::Dark ? UiChrome::ThemeMode::Light
                                                          : UiChrome::ThemeMode::Dark);
}

void MainWindow::setSidebarExpanded(bool expanded)
{
    if (m_sidebarExpanded == expanded && m_sidebar != nullptr) {
        updateSidebarUi();
        return;
    }

    m_sidebarExpanded = expanded;
    updateSidebarUi();
    saveUiPreferences();
}

void MainWindow::toggleSidebar()
{
    setSidebarExpanded(!m_sidebarExpanded);
}

void MainWindow::updateSidebarUi()
{
    if (m_sidebar == nullptr || m_sidebarContent == nullptr || m_sidebarToggleButton == nullptr
        || m_themeToggleButton == nullptr) {
        return;
    }

    const bool sidebarVisible = !m_terminalFullScreen;
    m_sidebar->setVisible(sidebarVisible);
    if (!sidebarVisible) {
        return;
    }

    m_sidebarContent->setVisible(m_sidebarExpanded);
    m_sidebar->setMinimumWidth(m_sidebarExpanded ? 290 : 78);
    m_sidebar->setMaximumWidth(m_sidebarExpanded ? 340 : 78);
    m_sidebarToggleButton->setText(m_sidebarExpanded ? QStringLiteral("收起侧栏") : QStringLiteral("展开"));
    m_sidebarToggleButton->setToolTip(m_sidebarExpanded ? QStringLiteral("收起左侧会话栏") : QStringLiteral("展开左侧会话栏"));
    m_themeToggleButton->setText(
        m_sidebarExpanded ? (m_themeMode == UiChrome::ThemeMode::Dark ? QStringLiteral("浅色主题")
                                                                      : QStringLiteral("黑色主题"))
                          : (m_themeMode == UiChrome::ThemeMode::Dark ? QStringLiteral("浅")
                                                                      : QStringLiteral("黑")));
    m_themeToggleButton->setToolTip(m_themeMode == UiChrome::ThemeMode::Dark ? QStringLiteral("切换到浅色主题")
                                                                              : QStringLiteral("切换到黑色主题"));
}

void MainWindow::setStatusMessage(const QString &message)
{
    if (m_statusMessageLabel != nullptr) {
        m_statusMessageLabel->setText(message);
        m_statusMessageLabel->setToolTip(message);
    }
}

void MainWindow::loadProfiles()
{
    m_profiles = m_repository.load();
    populateSessionList();
    updateSelectionDependentUi();
    updateTabScrollButtons();
}

void MainWindow::saveProfiles()
{
    QString errorMessage;
    if (!m_repository.save(m_profiles, &errorMessage)) {
        execThemedMessageBox(this, QMessageBox::Critical, QStringLiteral("保存失败"), errorMessage);
    }
}

void MainWindow::updateRemoteStatsUi()
{
    if (m_cpuStatusLabel == nullptr) {
        return;
    }

    auto applyPlaceholder = [this](const QString &text, const QString &toolTip) {
        m_cpuStatusLabel->setText(text);
        m_memoryStatusLabel->setText(QStringLiteral("内存 --"));
        m_diskStatusLabel->setText(QStringLiteral("磁盘 --"));
        m_networkStatusLabel->setText(QStringLiteral("网络 --"));
        for (QLabel *label : {m_cpuStatusLabel, m_memoryStatusLabel, m_diskStatusLabel, m_networkStatusLabel}) {
            label->setToolTip(toolTip);
        }
    };

    ConnectionPane *pane = currentPane();
    if (pane == nullptr) {
        applyPlaceholder(QStringLiteral("CPU --"), QStringLiteral("当前没有打开的连接标签。"));
        return;
    }

    if (!pane->isConnected()) {
        applyPlaceholder(QStringLiteral("CPU --"), QStringLiteral("当前标签未连接。"));
        return;
    }

    if (!pane->hasRemoteStats()) {
        const QString tip = pane->remoteStatsStateText().isEmpty() ? QStringLiteral("正在采集远端资源状态。")
                                                                   : pane->remoteStatsStateText();
        applyPlaceholder(QStringLiteral("CPU 采集中"), tip);
        return;
    }

    const SystemStatsMonitor::Sample sample = pane->latestRemoteStats();
    const QString toolTip = pane->remoteStatsStateText().isEmpty() ? sample.summaryText : pane->remoteStatsStateText();

    m_cpuStatusLabel->setText(QStringLiteral("CPU %1").arg(formatPercentCompact(sample.cpuUsagePercent)));
    m_memoryStatusLabel->setText(sample.memoryTotalBytes > 0
                                     ? QStringLiteral("内存 %1/%2")
                                           .arg(formatBytesCompact(sample.memoryUsedBytes),
                                                formatBytesCompact(sample.memoryTotalBytes))
                                     : QStringLiteral("内存 --"));
    m_diskStatusLabel->setText(sample.diskTotalBytes > 0
                                   ? QStringLiteral("磁盘 %1/%2")
                                         .arg(formatBytesCompact(sample.diskUsedBytes),
                                              formatBytesCompact(sample.diskTotalBytes))
                                   : QStringLiteral("磁盘 --"));
    m_networkStatusLabel->setText(QStringLiteral("网络 ↑%1 ↓%2")
                                      .arg(formatRateCompact(sample.networkUploadBytesPerSecond),
                                           formatRateCompact(sample.networkDownloadBytesPerSecond)));
    for (QLabel *label : {m_cpuStatusLabel, m_memoryStatusLabel, m_diskStatusLabel, m_networkStatusLabel}) {
        label->setToolTip(toolTip);
    }
}

void MainWindow::setTerminalFullScreen(bool enabled)
{
    if (m_terminalFullScreen == enabled) {
        syncTerminalFullScreenState();
        return;
    }

    m_terminalFullScreen = enabled;
    if (enabled) {
        m_windowStateBeforeTerminalFullScreen = windowState();
        showFullScreen();
    } else {
        const bool wasMaximized = m_windowStateBeforeTerminalFullScreen.testFlag(Qt::WindowMaximized);
        if (wasMaximized) {
            showMaximized();
        } else {
            showNormal();
        }
    }

    syncTerminalFullScreenState();
}

void MainWindow::syncTerminalFullScreenState()
{
    if (m_contentLayout != nullptr) {
        m_contentLayout->setContentsMargins(m_terminalFullScreen ? 0 : 18, m_terminalFullScreen ? 0 : 18, m_terminalFullScreen ? 0 : 18, 0);
        m_contentLayout->setSpacing(m_terminalFullScreen ? 0 : 18);
    }

    if (m_bottomStatusBar != nullptr) {
        m_bottomStatusBar->setVisible(!m_terminalFullScreen);
    }
    if (m_titleBar != nullptr) {
        m_titleBar->setVisible(!m_terminalFullScreen);
    }
    if (m_tabWidget != nullptr) {
        m_tabWidget->tabBar()->setVisible(!m_terminalFullScreen);
    }

    updateSidebarUi();
    updateTitleBarUi();

    for (int index = 0; index < m_tabWidget->count(); ++index) {
        if (auto *pane = qobject_cast<ConnectionPane *>(m_tabWidget->widget(index))) {
            pane->setTerminalFocusMode(m_terminalFullScreen && pane == currentPane());
        }
    }
}

void MainWindow::updateTabScrollButtons()
{
    if (!m_tabWidget) {
        return;
    }

    const auto buttons = m_tabWidget->tabBar()->findChildren<QToolButton *>();
    for (QToolButton *button : buttons) {
        if (!button) {
            continue;
        }

        if (button->property("wjsshStyledTabScroller").toBool()) {
            continue;
        }

        const Qt::ArrowType arrowType = button->arrowType();
        if (arrowType != Qt::LeftArrow && arrowType != Qt::RightArrow) {
            continue;
        }

        button->setProperty("wjsshStyledTabScroller", true);
        button->setAutoRaise(false);
        button->setCursor(Qt::PointingHandCursor);
        button->setFixedSize(26, 26);
        button->setArrowType(Qt::NoArrow);
        button->setText(arrowType == Qt::LeftArrow ? QStringLiteral("‹") : QStringLiteral("›"));
    }
}

void MainWindow::populateSessionList()
{
    const QString selectedId = m_sessionList->currentItem() != nullptr
                                   ? m_sessionList->currentItem()->data(Qt::UserRole).toString()
                                   : QString();

    m_sessionList->clear();

    int rowToSelect = -1;
    for (int row = 0; row < m_profiles.size(); ++row) {
        const SessionProfile &profile = m_profiles[row];
        auto *item = new QListWidgetItem(QStringLiteral("%1\n%2").arg(profile.displayName(), profile.subtitle()),
                                         m_sessionList);
        item->setData(Qt::UserRole, profile.id);
        item->setSizeHint(QSize(item->sizeHint().width(), 62));

        if (!selectedId.isEmpty() && profile.id == selectedId) {
            rowToSelect = row;
        }
    }

    if (rowToSelect >= 0) {
        m_sessionList->setCurrentRow(rowToSelect);
    } else if (!m_profiles.isEmpty()) {
        m_sessionList->setCurrentRow(0);
    }
}

void MainWindow::updateWorkspaceState()
{
    const int tabCount = m_tabWidget->count();
    m_workspaceStack->setCurrentWidget(tabCount == 0 ? m_emptyState : m_tabWidget);
    m_closeTabButton->setEnabled(tabCount > 0);
}

int MainWindow::selectedProfileIndex() const
{
    const auto *item = m_sessionList->currentItem();
    if (item == nullptr) {
        return -1;
    }

    const QString id = item->data(Qt::UserRole).toString();
    for (int index = 0; index < m_profiles.size(); ++index) {
        if (m_profiles[index].id == id) {
            return index;
        }
    }

    return -1;
}

SessionProfile *MainWindow::selectedProfile()
{
    const int index = selectedProfileIndex();
    return index >= 0 ? &m_profiles[index] : nullptr;
}

const SessionProfile *MainWindow::selectedProfile() const
{
    const int index = selectedProfileIndex();
    return index >= 0 ? &m_profiles[index] : nullptr;
}

ConnectionPane *MainWindow::currentPane() const
{
    return qobject_cast<ConnectionPane *>(m_tabWidget->currentWidget());
}

int MainWindow::indexOfPane(ConnectionPane *pane) const
{
    return m_tabWidget->indexOf(pane);
}

QString MainWindow::promptSecret(const SessionProfile &profile, bool *accepted) const
{
    if (accepted != nullptr) {
        *accepted = true;
    }

    if (profile.authMode == AuthMode::Password) {
        if (profile.rememberPassword && !profile.password.isEmpty()) {
            return profile.password;
        }

        return execThemedTextInput(const_cast<MainWindow *>(this),
                                   QStringLiteral("登录密码"),
                                   QStringLiteral("请输入 %1 的登录密码").arg(profile.subtitle()),
                                   QLineEdit::Password,
                                   QString(),
                                   accepted);
    }

    const QString prompt = profile.privateKeyPath.isEmpty()
                               ? QStringLiteral("如果默认私钥或 SSH 代理需要口令，请输入；不需要可留空。")
                               : QStringLiteral("请输入该私钥的口令；如果没有口令可留空。");

    return execThemedTextInput(const_cast<MainWindow *>(this),
                               QStringLiteral("私钥口令"),
                               prompt,
                               QLineEdit::Password,
                               QString(),
                               accepted);
}

bool MainWindow::confirmHostKey(const QString &title,
                                const QString &message,
                                const QString &fingerprint) const
{
    if (qEnvironmentVariableIsSet("WJSSH_AUTO_TRUST_HOSTKEY")) {
        wjsshTrace(QStringLiteral("MainWindow::confirmHostKey auto accepted fingerprint=%1").arg(fingerprint));
        return true;
    }

    QMessageBox dialog(const_cast<MainWindow *>(this));
    dialog.setIcon(QMessageBox::Question);
    dialog.setWindowTitle(title);
    dialog.setText(message);
    dialog.setInformativeText(QStringLiteral("SHA256 指纹：%1").arg(fingerprint));
    dialog.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    dialog.button(QMessageBox::Yes)->setText(QStringLiteral("信任并继续"));
    dialog.button(QMessageBox::No)->setText(QStringLiteral("取消"));
    UiChrome::applyMessageBoxTheme(&dialog);
    return dialog.exec() == QMessageBox::Yes;
}

QString MainWindow::nextTabBaseTitle(const SessionProfile &profile) const
{
    const QString base = profile.displayName();
    int duplicateCount = 0;

    for (int index = 0; index < m_tabWidget->count(); ++index) {
        const QWidget *widget = m_tabWidget->widget(index);
        const QString existingBase = widget->property("tabBaseTitle").toString();
        if (existingBase.startsWith(base)) {
            ++duplicateCount;
        }
    }

    return duplicateCount == 0 ? base : QStringLiteral("%1 %2").arg(base).arg(duplicateCount + 1);
}

void MainWindow::addConnectedPane(ConnectionPane *pane, const QString &baseTitle)
{
    pane->setProperty("tabBaseTitle", baseTitle);

    connect(pane, &ConnectionPane::statusMessage, this, &MainWindow::handlePaneStatusMessage);
    connect(pane,
            &ConnectionPane::connectionStateChanged,
            this,
            &MainWindow::handlePaneConnectionStateChanged);
    connect(pane, &ConnectionPane::remoteStatsChanged, this, &MainWindow::handlePaneRemoteStatsChanged);
    connect(pane, &ConnectionPane::requestReconnect, this, &MainWindow::handlePaneReconnectRequested);
    connect(pane, &ConnectionPane::requestClose, this, &MainWindow::handlePaneCloseRequested);
    connect(pane,
            &ConnectionPane::requestToggleTerminalFullScreen,
            this,
            &MainWindow::handlePaneTerminalFullScreenRequested);

    const int tabIndex = m_tabWidget->addTab(pane, baseTitle);
    m_tabWidget->setCurrentIndex(tabIndex);
    updateTabLabel(pane);
    updateWorkspaceState();
    updateTabScrollButtons();
    updateRemoteStatsUi();
    syncTerminalFullScreenState();
    pane->focusCommandInput();
}

void MainWindow::updateTabLabel(ConnectionPane *pane)
{
    const int index = indexOfPane(pane);
    if (index < 0) {
        return;
    }

    const QString baseTitle = pane->property("tabBaseTitle").toString();
    const QString label = pane->isConnected() ? baseTitle : QStringLiteral("%1 [离线]").arg(baseTitle);
    m_tabWidget->setTabText(index, label);
    m_tabWidget->setTabToolTip(index, pane->profile().subtitle());
}

void MainWindow::addSession()
{
    SessionEditorDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    m_profiles.push_back(dialog.profile());
    saveProfiles();
    populateSessionList();
}

void MainWindow::editSelectedSession()
{
    SessionProfile *profile = selectedProfile();
    if (profile == nullptr) {
        return;
    }

    SessionEditorDialog dialog(this);
    dialog.setProfile(*profile);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    *profile = dialog.profile();
    saveProfiles();
    populateSessionList();
}

void MainWindow::deleteSelectedSession()
{
    const int index = selectedProfileIndex();
    if (index < 0) {
        return;
    }

    const auto answer = execThemedMessageBox(this,
                                             QMessageBox::Warning,
                                             QStringLiteral("删除会话"),
                                             QStringLiteral("确定删除会话“%1”吗？").arg(m_profiles[index].displayName()),
                                             QMessageBox::Yes | QMessageBox::No,
                                             QMessageBox::No);
    if (answer != QMessageBox::Yes) {
        return;
    }

    m_profiles.remove(index);
    saveProfiles();
    populateSessionList();
}

void MainWindow::connectSelectedSession()
{
    wjsshTrace(QStringLiteral("MainWindow::connectSelectedSession begin inProgress=%1").arg(m_connectionInProgress));
    if (m_connectionInProgress) {
        return;
    }
    const SessionProfile *profile = selectedProfile();
    if (profile == nullptr) {
        wjsshTrace(QStringLiteral("MainWindow::connectSelectedSession no profile selected"));
        execThemedMessageBox(this,
                             QMessageBox::Information,
                             QStringLiteral("选择会话"),
                             QStringLiteral("请先从左侧选择一个会话。"));
        return;
    }
    bool accepted = false;
    const QString secret = promptSecret(*profile, &accepted);
    wjsshTrace(QStringLiteral("MainWindow::connectSelectedSession secret accepted=%1 profile=%2")
                   .arg(accepted)
                   .arg(profile->subtitle()));
    if (!accepted) {
        return;
    }
    auto hostPrompt = [this](const QString &title, const QString &message, const QString &fingerprint) {
        return confirmHostKey(title, message, fingerprint);
    };
    m_connectionInProgress = true;
    updateSelectionDependentUi();
    QApplication::setOverrideCursor(Qt::WaitCursor);
    QApplication::processEvents();
    auto *pane = new ConnectionPane(*profile, m_tabWidget);
    wjsshTrace(QStringLiteral("MainWindow::connectSelectedSession pane created"));
    QString errorMessage;
    if (!pane->connectToHost(secret, hostPrompt, &errorMessage)) {
        wjsshTrace(QStringLiteral("MainWindow::connectSelectedSession connect failed error=%1").arg(errorMessage));
        QApplication::restoreOverrideCursor();
        m_connectionInProgress = false;
        updateSelectionDependentUi();
        pane->deleteLater();
        execThemedMessageBox(this, QMessageBox::Critical, QStringLiteral("连接失败"), errorMessage);
        return;
    }
    wjsshTrace(QStringLiteral("MainWindow::connectSelectedSession connected"));
    QApplication::restoreOverrideCursor();
    m_connectionInProgress = false;
    updateSelectionDependentUi();
    addConnectedPane(pane, nextTabBaseTitle(*profile));
    wjsshTrace(QStringLiteral("MainWindow::connectSelectedSession end"));
}

void MainWindow::closeCurrentTab()
{
    closeTab(m_tabWidget->currentIndex());
}

void MainWindow::closeTab(int index)
{
    if (index < 0 || index >= m_tabWidget->count()) {
        return;
    }

    auto *pane = qobject_cast<ConnectionPane *>(m_tabWidget->widget(index));
    if (pane == nullptr || m_connectionInProgress) {
        return;
    }

    if (pane->isConnected()) {
        const auto answer = execThemedMessageBox(
            this,
            QMessageBox::Warning,
            QStringLiteral("关闭标签"),
            QStringLiteral("这个标签当前仍保持连接。确定关闭并断开吗？"),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No);
        if (answer != QMessageBox::Yes) {
            return;
        }
    }

    pane->disconnectFromHost(true);
    m_tabWidget->removeTab(index);
    pane->deleteLater();
    if (m_tabWidget->count() == 0 && m_terminalFullScreen) {
        setTerminalFullScreen(false);
    }
    updateWorkspaceState();
    updateTabScrollButtons();
    handleCurrentTabChanged(m_tabWidget->currentIndex());
}

void MainWindow::handleCurrentTabChanged(int index)
{
    Q_UNUSED(index);

    updateWorkspaceState();
    updateTabScrollButtons();
    if (ConnectionPane *pane = currentPane()) {
        pane->focusCommandInput();
        setStatusMessage(QStringLiteral("当前标签：%1").arg(pane->profile().subtitle()));
    } else {
        setStatusMessage(QStringLiteral("就绪"));
    }
    updateRemoteStatsUi();
    syncTerminalFullScreenState();
}

void MainWindow::handlePaneStatusMessage(const QString &message)
{
    setStatusMessage(message);
}

void MainWindow::handlePaneConnectionStateChanged(ConnectionPane *pane)
{
    updateTabLabel(pane);
    if (pane == currentPane()) {
        updateRemoteStatsUi();
    }
}

void MainWindow::handlePaneRemoteStatsChanged(ConnectionPane *pane)
{
    if (pane == currentPane()) {
        updateRemoteStatsUi();
    }
}

void MainWindow::handlePaneReconnectRequested(ConnectionPane *pane)
{
    if (pane == nullptr || m_connectionInProgress) {
        return;
    }
    bool accepted = false;
    const QString secret = promptSecret(pane->profile(), &accepted);
    if (!accepted) {
        return;
    }
    auto hostPrompt = [this](const QString &title, const QString &message, const QString &fingerprint) {
        return confirmHostKey(title, message, fingerprint);
    };
    m_connectionInProgress = true;
    updateSelectionDependentUi();
    QApplication::setOverrideCursor(Qt::WaitCursor);
    QApplication::processEvents();
    QString errorMessage;
    if (!pane->connectToHost(secret, hostPrompt, &errorMessage)) {
        QApplication::restoreOverrideCursor();
        m_connectionInProgress = false;
        updateSelectionDependentUi();
        execThemedMessageBox(this, QMessageBox::Critical, QStringLiteral("连接失败"), errorMessage);
        return;
    }
    QApplication::restoreOverrideCursor();
    m_connectionInProgress = false;
    updateSelectionDependentUi();
    updateTabLabel(pane);
}

void MainWindow::handlePaneCloseRequested(ConnectionPane *pane)
{
    closeTab(indexOfPane(pane));
}

void MainWindow::handlePaneTerminalFullScreenRequested(ConnectionPane *pane)
{
    if (pane == nullptr) {
        return;
    }

    if (pane != currentPane()) {
        const int index = indexOfPane(pane);
        if (index >= 0) {
            m_tabWidget->setCurrentIndex(index);
        }
    }
    setTerminalFullScreen(!m_terminalFullScreen);
}

void MainWindow::toggleCurrentTerminalFullScreen()
{
    if (currentPane() == nullptr) {
        return;
    }
    setTerminalFullScreen(!m_terminalFullScreen);
}

void MainWindow::updateSelectionDependentUi()
{
    const bool hasProfileSelection = selectedProfileIndex() >= 0;
    m_editSessionButton->setEnabled(hasProfileSelection);
    m_deleteSessionButton->setEnabled(hasProfileSelection);
    m_connectButton->setEnabled(hasProfileSelection && !m_connectionInProgress);
    m_closeTabButton->setEnabled(m_tabWidget->count() > 0);
    m_sessionList->setEnabled(!m_connectionInProgress);
}


