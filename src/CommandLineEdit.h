#pragma once

#include <QLineEdit>
#include <QStringList>

class CommandLineEdit : public QLineEdit {
    Q_OBJECT

public:
    explicit CommandLineEdit(QWidget *parent = nullptr);

    void addHistoryEntry(const QString &command);
    void clearNavigationState();

protected:
    void keyPressEvent(QKeyEvent *event) override;

private:
    void showHistoryEntry(int index);

    QStringList m_history;
    int m_historyIndex = -1;
    QString m_draftText;
};

