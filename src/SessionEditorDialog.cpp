#include "SessionEditorDialog.h"

#include "UiChrome.h"

#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QUuid>
#include <QVBoxLayout>

SessionEditorDialog::SessionEditorDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("连接会话"));
    setModal(true);
    resize(560, 400);

    auto *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(20, 20, 20, 20);
    rootLayout->setSpacing(16);

    auto *card = new QFrame(this);
    card->setObjectName("panelCard");

    auto *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(18, 18, 18, 18);
    cardLayout->setSpacing(14);

    auto *title = new QLabel(QStringLiteral("新建或编辑 SSH 会话"), card);
    title->setObjectName("dialogTitle");

    auto *hint = new QLabel(
        QStringLiteral("密码登录可直接在这里配置；勾选“记住密码”后，会使用当前 Windows 账户加密后保存到本地。"),
        card);
    hint->setWordWrap(true);

    cardLayout->addWidget(title);
    cardLayout->addWidget(hint);

    auto *form = new QFormLayout();
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignTop);
    form->setHorizontalSpacing(14);
    form->setVerticalSpacing(12);

    m_nameEdit = new QLineEdit(card);
    m_hostEdit = new QLineEdit(card);

    m_portSpin = new QSpinBox(card);
    m_portSpin->setRange(1, 65535);
    m_portSpin->setValue(22);

    m_userEdit = new QLineEdit(card);

    m_authModeCombo = new QComboBox(card);
    m_authModeCombo->addItem(QStringLiteral("密码认证"), static_cast<int>(AuthMode::Password));
    m_authModeCombo->addItem(QStringLiteral("私钥认证"), static_cast<int>(AuthMode::PrivateKey));

    m_authDetailsStack = new QStackedWidget(card);

    auto *passwordPage = new QWidget(m_authDetailsStack);
    auto *passwordLayout = new QVBoxLayout(passwordPage);
    passwordLayout->setContentsMargins(0, 0, 0, 0);
    passwordLayout->setSpacing(8);

    m_passwordEdit = new QLineEdit(passwordPage);
    m_passwordEdit->setEchoMode(QLineEdit::Password);
    m_passwordEdit->setPlaceholderText(QStringLiteral("留空则在连接时临时输入"));
    m_passwordEdit->setClearButtonEnabled(true);

    m_rememberPasswordCheck = new QCheckBox(QStringLiteral("记住密码"), passwordPage);

    passwordLayout->addWidget(m_passwordEdit);
    passwordLayout->addWidget(m_rememberPasswordCheck);
    passwordLayout->addStretch();

    auto *keyPage = new QWidget(m_authDetailsStack);
    auto *keyPageLayout = new QVBoxLayout(keyPage);
    keyPageLayout->setContentsMargins(0, 0, 0, 0);
    keyPageLayout->setSpacing(8);

    auto *keyRow = new QWidget(keyPage);
    auto *keyRowLayout = new QHBoxLayout(keyRow);
    keyRowLayout->setContentsMargins(0, 0, 0, 0);
    keyRowLayout->setSpacing(8);

    m_privateKeyEdit = new QLineEdit(keyRow);
    m_privateKeyEdit->setPlaceholderText(QStringLiteral("留空则尝试系统默认私钥或 SSH 代理"));
    m_privateKeyEdit->setClearButtonEnabled(true);

    m_browsePrivateKeyButton = new QPushButton(QStringLiteral("浏览"), keyRow);

    keyRowLayout->addWidget(m_privateKeyEdit, 1);
    keyRowLayout->addWidget(m_browsePrivateKeyButton);

    keyPageLayout->addWidget(keyRow);
    keyPageLayout->addStretch();

    m_authDetailsStack->addWidget(passwordPage);
    m_authDetailsStack->addWidget(keyPage);

    m_initialPathEdit = new QLineEdit(card);
    m_initialPathEdit->setPlaceholderText(QStringLiteral("留空时自动进入用户主目录"));

    form->addRow(QStringLiteral("名称"), m_nameEdit);
    form->addRow(QStringLiteral("主机"), m_hostEdit);
    form->addRow(QStringLiteral("端口"), m_portSpin);
    form->addRow(QStringLiteral("用户名"), m_userEdit);
    form->addRow(QStringLiteral("认证方式"), m_authModeCombo);
    form->addRow(QStringLiteral("认证信息"), m_authDetailsStack);
    form->addRow(QStringLiteral("初始目录"), m_initialPathEdit);

    cardLayout->addLayout(form);
    rootLayout->addWidget(card, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel | QDialogButtonBox::Ok, this);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("保存"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    rootLayout->addWidget(buttons);

    connect(m_browsePrivateKeyButton, &QPushButton::clicked, this, &SessionEditorDialog::browsePrivateKey);
    connect(m_authModeCombo,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this,
            &SessionEditorDialog::updateAuthModeUi);
    connect(buttons, &QDialogButtonBox::accepted, this, &SessionEditorDialog::validateAndAccept);
    connect(buttons, &QDialogButtonBox::rejected, this, &SessionEditorDialog::reject);

    UiChrome::applyDialogTheme(this);
    updateAuthModeUi();
}

void SessionEditorDialog::setProfile(const SessionProfile &profile)
{
    m_profileId = profile.id;
    m_nameEdit->setText(profile.name);
    m_hostEdit->setText(profile.host);
    m_portSpin->setValue(profile.port);
    m_userEdit->setText(profile.username);
    m_authModeCombo->setCurrentIndex(profile.authMode == AuthMode::PrivateKey ? 1 : 0);
    m_passwordEdit->setText(profile.password);
    m_rememberPasswordCheck->setChecked(profile.rememberPassword && !profile.password.isEmpty());
    m_privateKeyEdit->setText(profile.privateKeyPath);
    m_initialPathEdit->setText(profile.initialPath);
    updateAuthModeUi();
}

SessionProfile SessionEditorDialog::profile() const
{
    SessionProfile result;
    result.id = m_profileId.isEmpty() ? QUuid::createUuid().toString(QUuid::WithoutBraces) : m_profileId;
    result.name = m_nameEdit->text().trimmed();
    result.host = m_hostEdit->text().trimmed();
    result.port = m_portSpin->value();
    result.username = m_userEdit->text().trimmed();
    result.authMode = static_cast<AuthMode>(m_authModeCombo->currentData().toInt());
    result.rememberPassword = result.authMode == AuthMode::Password && m_rememberPasswordCheck->isChecked();
    result.password = result.rememberPassword ? m_passwordEdit->text() : QString();
    result.privateKeyPath = result.authMode == AuthMode::PrivateKey ? m_privateKeyEdit->text().trimmed() : QString();
    result.initialPath = m_initialPathEdit->text().trimmed();
    return result;
}

void SessionEditorDialog::browsePrivateKey()
{
    QFileDialog dialog(this, QStringLiteral("选择私钥文件"));
    dialog.setFileMode(QFileDialog::ExistingFile);
    dialog.setOption(QFileDialog::DontUseNativeDialog, true);
    UiChrome::applyDialogTheme(&dialog);
    if (dialog.exec() != QDialog::Accepted || dialog.selectedFiles().isEmpty()) {
        return;
    }
    m_privateKeyEdit->setText(dialog.selectedFiles().constFirst());
}

void SessionEditorDialog::updateAuthModeUi()
{
    const bool keyMode = static_cast<AuthMode>(m_authModeCombo->currentData().toInt()) == AuthMode::PrivateKey;
    m_authDetailsStack->setCurrentIndex(keyMode ? 1 : 0);
}

void SessionEditorDialog::validateAndAccept()
{
    const SessionProfile result = profile();

    auto showWarning = [this](const QString &message) {
        QMessageBox box(QMessageBox::Warning,
                        QStringLiteral("信息不完整"),
                        message,
                        QMessageBox::Ok,
                        this);
        box.button(QMessageBox::Ok)->setText(QStringLiteral("确定"));
        UiChrome::applyMessageBoxTheme(&box);
        box.exec();
    };

    if (result.name.isEmpty()) {
        showWarning(QStringLiteral("请填写会话名称。"));
        return;
    }

    if (result.host.isEmpty()) {
        showWarning(QStringLiteral("请填写主机地址。"));
        return;
    }

    if (result.username.isEmpty()) {
        showWarning(QStringLiteral("请填写用户名。"));
        return;
    }

    if (result.authMode == AuthMode::Password && result.rememberPassword && result.password.isEmpty()) {
        showWarning(QStringLiteral("勾选“记住密码”时，请同时填写登录密码。"));
        return;
    }

    accept();
}
