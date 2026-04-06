#pragma once

#include "SessionProfile.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QStackedWidget>

class SessionEditorDialog : public QDialog {
    Q_OBJECT

public:
    explicit SessionEditorDialog(QWidget *parent = nullptr);

    void setProfile(const SessionProfile &profile);
    [[nodiscard]] SessionProfile profile() const;

private slots:
    void browsePrivateKey();
    void updateAuthModeUi();
    void validateAndAccept();

private:
    QLineEdit *m_nameEdit = nullptr;
    QLineEdit *m_hostEdit = nullptr;
    QSpinBox *m_portSpin = nullptr;
    QLineEdit *m_userEdit = nullptr;
    QComboBox *m_authModeCombo = nullptr;
    QStackedWidget *m_authDetailsStack = nullptr;
    QLineEdit *m_passwordEdit = nullptr;
    QCheckBox *m_rememberPasswordCheck = nullptr;
    QLineEdit *m_privateKeyEdit = nullptr;
    QPushButton *m_browsePrivateKeyButton = nullptr;
    QLineEdit *m_initialPathEdit = nullptr;

    QString m_profileId;
};
