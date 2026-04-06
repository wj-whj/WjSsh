#pragma once

#include <QByteArray>
#include <QDialog>

class QLabel;
class QPlainTextEdit;
class QPushButton;

class RemoteFileEditorDialog : public QDialog {
    Q_OBJECT

public:
    explicit RemoteFileEditorDialog(QWidget *parent = nullptr);

    void setFilePath(const QString &path);
    void setContent(const QByteArray &data);
    [[nodiscard]] QByteArray content() const;
    [[nodiscard]] bool isProbablyBinary() const;

private:
    QString decodeBytes(const QByteArray &data) const;

    QLabel *m_pathLabel = nullptr;
    QLabel *m_hintLabel = nullptr;
    QPlainTextEdit *m_editor = nullptr;
    QPushButton *m_saveButton = nullptr;
    QByteArray m_originalBytes;
    bool m_binary = false;
};
