#include "RemoteFileEditorDialog.h"

#include "UiChrome.h"

#include <QDialogButtonBox>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStringConverter>
#include <QVBoxLayout>

namespace {
bool looksBinary(const QByteArray &data)
{
    if (data.contains('\0')) {
        return true;
    }

    int suspicious = 0;
    const int sample = qMin(data.size(), 2048);
    for (int index = 0; index < sample; ++index) {
        const char value = data.at(index);
        const bool allowed =
            value == '\n' || value == '\r' || value == '\t' || (value >= 32 && value != 127);
        if (!allowed) {
            ++suspicious;
        }
    }
    return sample > 0 && suspicious > sample / 10;
}
}

RemoteFileEditorDialog::RemoteFileEditorDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("远程文件预览"));
    resize(920, 700);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(18, 18, 18, 18);
    layout->setSpacing(12);

    m_pathLabel = new QLabel(this);
    m_pathLabel->setObjectName("sectionTitle");
    m_hintLabel = new QLabel(this);
    m_hintLabel->setWordWrap(true);
    m_hintLabel->setObjectName("mutedText");

    m_editor = new QPlainTextEdit(this);
    m_editor->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_editor->setPlaceholderText(QStringLiteral("文件内容"));

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Close, this);
    m_saveButton = buttons->button(QDialogButtonBox::Save);
    m_saveButton->setText(QStringLiteral("保存到远端"));
    buttons->button(QDialogButtonBox::Close)->setText(QStringLiteral("关闭"));

    layout->addWidget(m_pathLabel);
    layout->addWidget(m_hintLabel);
    layout->addWidget(m_editor, 1);
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, this, &RemoteFileEditorDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &RemoteFileEditorDialog::reject);

    UiChrome::applyDialogTheme(this);
}

void RemoteFileEditorDialog::setFilePath(const QString &path)
{
    m_pathLabel->setText(path);
}

void RemoteFileEditorDialog::setContent(const QByteArray &data)
{
    m_originalBytes = data;
    m_binary = looksBinary(data);

    if (m_binary) {
        m_hintLabel->setText(QStringLiteral("检测到该文件很可能是二进制内容，已切换为只读预览。建议直接下载到本地处理。"));
        m_editor->setPlainText(QStringLiteral("[二进制内容已隐藏]"));
        m_editor->setReadOnly(true);
        m_saveButton->setEnabled(false);
        return;
    }

    m_editor->setReadOnly(false);
    m_saveButton->setEnabled(true);
    m_editor->setPlainText(decodeBytes(data));
    m_hintLabel->setText(QStringLiteral("文本内容已载入。保存时会以 UTF-8 编码写回远端文件。"));
}

QByteArray RemoteFileEditorDialog::content() const
{
    if (m_binary) {
        return m_originalBytes;
    }
    return m_editor->toPlainText().toUtf8();
}

bool RemoteFileEditorDialog::isProbablyBinary() const
{
    return m_binary;
}

QString RemoteFileEditorDialog::decodeBytes(const QByteArray &data) const
{
    QStringDecoder utf8Decoder(QStringDecoder::Utf8);
    const QString utf8 = utf8Decoder.decode(data);
    if (!utf8Decoder.hasError()) {
        return utf8;
    }
    return QString::fromLocal8Bit(data);
}
