#include "Localization.h"

#include <QAbstractButton>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QSpinBox>
#include <QTableWidget>
#include <QTextEdit>
#include <QToolButton>
#include <QWidget>

#include <array>
#include <algorithm>

namespace {

using Localization::Language;

Language s_language = Language::Chinese;

struct TextPair {
    const char *zh;
    const char *en;
};

static const auto kTextPairs = std::to_array<TextPair>({
    {"确定", "OK"},
    {"取消", "Cancel"},
    {"是", "Yes"},
    {"否", "No"},
    {"最小化", "Minimize"},
    {"最大化", "Maximize"},
    {"还原", "Restore"},
    {"关闭", "Close"},
    {"多会话 SSH 远程工作台", "Multi-session SSH workspace"},
    {"连接会话", "Sessions"},
    {"新建", "New"},
    {"编辑", "Edit"},
    {"删除", "Delete"},
    {"打开连接", "Open Connection"},
    {"关闭当前标签", "Close Current Tab"},
    {"暂无打开的连接", "No open connections"},
    {"从左侧选择一个会话，然后点击“打开连接”来新建 SSH 标签页。", "Choose a session on the left, then click \"Open Connection\" to open a new SSH tab."},
    {"会话配置保存位置：", "Session config path: "},
    {"就绪", "Ready"},
    {"CPU --", "CPU --"},
    {"内存 --", "Memory --"},
    {"磁盘 --", "Disk --"},
    {"网络 --", "Network --"},
    {"收起侧栏", "Hide Sidebar"},
    {"展开", "Show"},
    {"收起左侧会话栏", "Hide the left session panel"},
    {"展开左侧会话栏", "Show the left session panel"},
    {"浅色主题", "Light Theme"},
    {"黑色主题", "Dark Theme"},
    {"浅", "Light"},
    {"黑", "Dark"},
    {"切换到浅色主题", "Switch to the light theme"},
    {"切换到黑色主题", "Switch to the dark theme"},
    {"中文", "Chinese"},
    {"英文", "English"},
    {"切换到中文界面", "Switch to Chinese"},
    {"切换到英文界面", "Switch to English"},
    {"当前没有打开的连接标签。", "There is no open connection tab."},
    {"当前标签未连接。", "The current tab is not connected."},
    {"正在采集远端资源状态。", "Collecting remote server metrics."},
    {"CPU 采集中", "CPU Collecting"},
    {"内存 ", "Memory "},
    {"磁盘 ", "Disk "},
    {"网络 ↑", "Network Up "},
    {" ↓", " Down "},
    {"CPU ", "CPU "},
    {"登录密码", "Login Password"},
    {"请输入 ", "Enter "},
    {" 的登录密码", "'s login password"},
    {"如果默认私钥或 SSH 代理需要口令，请输入；不需要可留空。", "Enter the passphrase if the default private key or SSH agent needs one; otherwise leave it empty."},
    {"请输入该私钥的口令；如果没有口令可留空。", "Enter the private key passphrase, or leave it empty if the key has no passphrase."},
    {"私钥口令", "Private Key Passphrase"},
    {"SHA256 指纹：", "SHA256 fingerprint: "},
    {"信任并继续", "Trust and Continue"},
    {"选择会话", "Choose Session"},
    {"请先从左侧选择一个会话。", "Select a session on the left first."},
    {"连接失败", "Connection Failed"},
    {"这个标签当前仍保持连接。确定关闭并断开吗？", "This tab is still connected. Close it and disconnect?"},
    {"关闭标签", "Close Tab"},
    {"当前标签：", "Current Tab: "},
    {" [离线]", " [Offline]"},
    {"保存失败", "Save Failed"},
    {"在线", "Online"},
    {"离线", "Offline"},
    {"重新连接", "Reconnect"},
    {"断开", "Disconnect"},
    {"终端", "Terminal"},
    {"直接在终端内输入。Tab 补全，Ctrl+C 中断，Ctrl+L 远端清屏，F11 终端全屏", "Type directly in the terminal. Tab completes, Ctrl+C interrupts, Ctrl+L clears remotely, F11 toggles terminal full screen."},
    {"全屏 F11", "Full Screen F11"},
    {"进入终端全屏", "Enter terminal full screen"},
    {"退出全屏 Esc", "Exit Full Screen Esc"},
    {"退出终端全屏", "Exit terminal full screen"},
    {"清屏", "Clear"},
    {"连接后，直接在这里输入命令。", "Type commands here after connecting."},
    {"可拖动表头分隔线调整列宽", "Drag header separators to resize columns"},
    {"输入远程目录路径", "Enter remote directory path"},
    {"打开", "Open"},
    {"上一级", "Up"},
    {"刷新", "Refresh"},
    {"上传文件", "Upload File"},
    {"上传目录", "Upload Folder"},
    {"下载", "Download"},
    {"预览/编辑", "Preview/Edit"},
    {"新建目录", "New Folder"},
    {"新建文件", "New File"},
    {"重命名", "Rename"},
    {"名称", "Name"},
    {"类型", "Type"},
    {"大小", "Size"},
    {"修改时间", "Modified"},
    {"权限", "Permissions"},
    {"拖动列标题之间的分隔线可调整宽度", "Drag the separators between header labels to resize columns"},
    {"显示 SFTP", "Show SFTP"},
    {"隐藏 SFTP", "Hide SFTP"},
    {"未连接", "Disconnected"},
    {"终端：", "Terminal: "},
    {"    SFTP：", "    SFTP: "},
    {"已连接", "Connected"},
    {"终端输出已清空", "terminal output cleared"},
    {"当前已有传输任务正在进行。", "Another transfer task is already running."},
    {"上传中", "Uploading"},
    {"正在上传到 ", "Uploading to "},
    {"已取消上传", "Upload cancelled"},
    {"已上传 ", "Uploaded "},
    {" 个项目到 ", " item(s) to "},
    {"无法创建本地临时导出目录。", "Unable to create the local temporary export directory."},
    {"导出中", "Exporting"},
    {"正在导出到拖拽目标", "Exporting to the drag target"},
    {"已导出到目标：", "Exported to target: "},
    {"已取消导出", "Export cancelled"},
    {"导出失败", "Export Failed"},
    {"已取消拖拽", "Drag cancelled"},
    {"准备导出", "Preparing Export"},
    {"正在准备拖拽导出", "Preparing drag export"},
    {"导出已准备", "Export Ready"},
    {"文件已经临时导出到本地：\n", "The file has been exported temporarily to:\n"},
    {"\n\n如果这次目标没有接住拖拽，现在再次拖拽会直接使用这个本地缓存，不会重复下载。", "\n\nIf the target did not accept the drag this time, dragging again will reuse this local cache without downloading it again."},
    {"未放置到目标，已缓存到：", "Not dropped on a target. Cached at: "},
    {"选择要上传的本地文件", "Choose local files to upload"},
    {"上传目录到 ", "Uploading folder to "},
    {"选择要上传的本地目录", "Choose a local folder to upload"},
    {"下载中", "Downloading"},
    {"选择本地保存目录", "Choose a local destination folder"},
    {"正在下载目录到 ", "Downloading folder to "},
    {"已取消下载", "Download cancelled"},
    {"目录已下载到 ", "Folder downloaded to "},
    {"保存到本地", "Save to Local"},
    {"正在下载到 ", "Downloading to "},
    {"已下载到 ", "Downloaded to "},
    {"复制导出中", "Preparing Clipboard Export"},
    {"正在准备复制到系统剪贴板", "Preparing to copy to the system clipboard"},
    {"已取消复制导出", "Clipboard export cancelled"},
    {"已复制到系统剪贴板：", "Copied to clipboard: "},
    {"无法粘贴", "Cannot Paste"},
    {"系统剪贴板中没有可导入的本地文件或目录。", "The system clipboard does not contain importable local files or folders."},
    {"打开大文件", "Open Large File"},
    {"该文件约 ", "This file is about "},
    {"，继续载入到编辑器中吗？", ". Continue loading it into the editor?"},
    {"打开失败", "Open Failed"},
    {"已保存 ", "Saved "},
    {"目录名称", "Folder Name"},
    {"文件名称", "File Name"},
    {"新的名称", "New Name"},
    {"确认删除", "Confirm Delete"},
    {"确定递归删除目录“", "Delete the folder \""},
    {"”及其内容吗？", "\" and all of its contents recursively?"},
    {"确定删除文件“", "Delete the file \""},
    {"”吗？", "\"?"},
    {"复制导出", "Copy Export"},
    {"从剪贴板导入", "Import from Clipboard"},
    {"[本地] ", "[Local] "},
    {"已连接到 ", "Connected to "},
    {"连接中断：", "connection interrupted: "},
    {"远程文件预览", "Remote File Preview"},
    {"文件内容", "File Content"},
    {"保存到远端", "Save to Remote"},
    {"检测到该文件很可能是二进制内容，已切换为只读预览。建议直接下载到本地处理。", "This file appears to be binary. The editor has switched to read-only preview. Download it locally to edit it."},
    {"[二进制内容已隐藏]", "[Binary content hidden]"},
    {"文本内容已载入。保存时会以 UTF-8 编码写回远端文件。", "Text content loaded. Saving writes the file back to the remote host in UTF-8."},
    {"连接会话", "Connection Session"},
    {"新建或编辑 SSH 会话", "Create or Edit an SSH Session"},
    {"密码登录可直接在这里配置；勾选“记住密码”后，会使用当前 Windows 账户加密后保存到本地。", "You can configure password login directly here. When \"Remember password\" is enabled, the password is encrypted with the current Windows account and stored locally."},
    {"密码认证", "Password Authentication"},
    {"私钥认证", "Private Key Authentication"},
    {"留空则在连接时临时输入", "Leave empty to enter it when connecting"},
    {"记住密码", "Remember Password"},
    {"留空则尝试系统默认私钥或 SSH 代理", "Leave empty to try the system default private key or SSH agent"},
    {"浏览", "Browse"},
    {"留空时自动进入用户主目录", "Leave empty to start in the user's home directory"},
    {"主机", "Host"},
    {"端口", "Port"},
    {"用户名", "Username"},
    {"认证方式", "Authentication"},
    {"认证信息", "Credential"},
    {"初始目录", "Initial Directory"},
    {"保存", "Save"},
    {"选择私钥文件", "Choose Private Key File"},
    {"信息不完整", "Incomplete Information"},
    {"请填写会话名称。", "Enter a session name."},
    {"请填写主机地址。", "Enter a host address."},
    {"请填写用户名。", "Enter a username."},
    {"勾选“记住密码”时，请同时填写登录密码。", "When \"Remember password\" is enabled, enter the login password too."},
    {"目录", "Folder"},
    {"文件", "File"},
    {" 文件", " File"},
    {"名称：", "Name: "},
    {"完整路径：", "Full Path: "},
    {"类型：", "Type: "},
    {"大小：", "Size: "},
    {"修改时间：", "Modified: "},
    {"权限：", "Permissions: "},
    {"当前 SFTP 会话仍然使用最初登录的 SSH 账号权限。请使用具备目标目录权限的账号重新建立连接。", "The current SFTP session still uses the permissions of the original SSH account. Reconnect with an account that has access to the target directory."},
    {"会话配置保存位置：", "Session configuration path: "},
    {"远端资源采集中", "Collecting remote metrics"},
    {"远端资源状态", "Remote server status"},
    {"未连接。", "Disconnected."},
    {"当前标签未连接。", "The current tab is not connected."},
    {"当前没有打开的连接标签。", "There is no open connection tab."}
});

QString fromUtf8(const char *text)
{
    return QString::fromUtf8(text);
}

QString translateExact(const QString &text, Language targetLanguage)
{
    for (const TextPair &pair : kTextPairs) {
        const QString zh = fromUtf8(pair.zh);
        const QString en = fromUtf8(pair.en);
        if (targetLanguage == Language::English && text == zh) {
            return en;
        }
        if (targetLanguage == Language::Chinese && text == en) {
            return zh;
        }
    }

    return text;
}

QString translateFragments(QString text, Language targetLanguage)
{
    std::array<TextPair, kTextPairs.size()> sortedPairs = kTextPairs;
    std::sort(sortedPairs.begin(), sortedPairs.end(), [](const TextPair &lhs, const TextPair &rhs) {
        return QString::fromUtf8(lhs.zh).size() > QString::fromUtf8(rhs.zh).size();
    });

    for (const TextPair &pair : sortedPairs) {
        const QString source = targetLanguage == Language::English ? fromUtf8(pair.zh) : fromUtf8(pair.en);
        const QString replacement = targetLanguage == Language::English ? fromUtf8(pair.en) : fromUtf8(pair.zh);
        if (!source.isEmpty()) {
            text.replace(source, replacement);
        }
    }

    return text;
}

void translateWidget(QWidget *widget)
{
    if (widget == nullptr) {
        return;
    }

    if (!widget->windowTitle().isEmpty()) {
        widget->setWindowTitle(Localization::translateText(widget->windowTitle()));
    }
    if (!widget->toolTip().isEmpty()) {
        widget->setToolTip(Localization::translateText(widget->toolTip()));
    }

    if (auto *label = qobject_cast<QLabel *>(widget)) {
        label->setText(Localization::translateText(label->text()));
    } else if (auto *button = qobject_cast<QAbstractButton *>(widget)) {
        button->setText(Localization::translateText(button->text()));
    } else if (auto *lineEdit = qobject_cast<QLineEdit *>(widget)) {
        if (!lineEdit->placeholderText().isEmpty()) {
            lineEdit->setPlaceholderText(Localization::translateText(lineEdit->placeholderText()));
        }
    } else if (auto *plainText = qobject_cast<QPlainTextEdit *>(widget)) {
        if (!plainText->placeholderText().isEmpty()) {
            plainText->setPlaceholderText(Localization::translateText(plainText->placeholderText()));
        }
    } else if (auto *textEdit = qobject_cast<QTextEdit *>(widget)) {
        if (!textEdit->placeholderText().isEmpty()) {
            textEdit->setPlaceholderText(Localization::translateText(textEdit->placeholderText()));
        }
    } else if (auto *groupBox = qobject_cast<QGroupBox *>(widget)) {
        groupBox->setTitle(Localization::translateText(groupBox->title()));
    } else if (auto *comboBox = qobject_cast<QComboBox *>(widget)) {
        for (int index = 0; index < comboBox->count(); ++index) {
            comboBox->setItemText(index, Localization::translateText(comboBox->itemText(index)));
        }
    } else if (auto *table = qobject_cast<QTableWidget *>(widget)) {
        for (int column = 0; column < table->columnCount(); ++column) {
            if (QTableWidgetItem *headerItem = table->horizontalHeaderItem(column)) {
                headerItem->setText(Localization::translateText(headerItem->text()));
                headerItem->setToolTip(Localization::translateText(headerItem->toolTip()));
            }
        }
        if (table->horizontalHeader() != nullptr && !table->horizontalHeader()->toolTip().isEmpty()) {
            table->horizontalHeader()->setToolTip(Localization::translateText(table->horizontalHeader()->toolTip()));
        }
    } else if (auto *dialogButtons = qobject_cast<QDialogButtonBox *>(widget)) {
        const auto buttons = dialogButtons->buttons();
        for (QAbstractButton *button : buttons) {
            button->setText(Localization::translateText(button->text()));
            button->setToolTip(Localization::translateText(button->toolTip()));
        }
    }
}

} // namespace

namespace Localization {

void setLanguage(Language language)
{
    s_language = language;
}

Language language()
{
    return s_language;
}

QString languageKey(Language language)
{
    return language == Language::English ? QStringLiteral("en") : QStringLiteral("zh");
}

Language languageFromKey(const QString &value)
{
    return value.compare(QStringLiteral("en"), Qt::CaseInsensitive) == 0 ? Language::English
                                                                          : Language::Chinese;
}

QString translateText(const QString &text)
{
    if (text.isEmpty()) {
        return text;
    }

    if (s_language == Language::Chinese) {
        return translateExact(text, Language::Chinese);
    }

    const QString exact = translateExact(text, Language::English);
    if (exact != text) {
        return exact;
    }

    return translateFragments(text, Language::English);
}

void applyWidgetTexts(QWidget *root)
{
    if (root == nullptr) {
        return;
    }

    translateWidget(root);
    const auto widgets = root->findChildren<QWidget *>();
    for (QWidget *widget : widgets) {
        translateWidget(widget);
    }
}

} // namespace Localization
