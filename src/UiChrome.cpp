#include "UiChrome.h"

#include <QApplication>
#include <QPalette>
#include <QPointer>
#include <QStyle>
#include <QWidget>

#if defined(Q_OS_WIN)
#include <Windows.h>
#include <dwmapi.h>
#endif

namespace {

UiChrome::ThemeMode s_themeMode = UiChrome::ThemeMode::Light;

#if defined(Q_OS_WIN)
constexpr DWORD kDwmwaUseImmersiveDarkMode = 20;
constexpr DWORD kDwmwaUseImmersiveDarkModeLegacy = 19;
constexpr DWORD kDwmwaBorderColor = 34;
constexpr DWORD kDwmwaCaptionColor = 35;
constexpr DWORD kDwmwaTextColor = 36;
constexpr DWORD kDwmwaSystemBackdropType = 38;
constexpr DWORD kDwmSystemBackdropNone = 1;
constexpr COLORREF kDarkCaptionColor = RGB(31, 31, 31);
constexpr COLORREF kDarkBorderColor = RGB(31, 31, 31);
constexpr COLORREF kDarkTextColor = RGB(245, 247, 250);
constexpr COLORREF kLightCaptionColor = RGB(255, 255, 255);
constexpr COLORREF kLightBorderColor = RGB(255, 255, 255);
constexpr COLORREF kLightTextColor = RGB(31, 43, 54);
#endif

QString dialogStyleSheet()
{
    if (s_themeMode == UiChrome::ThemeMode::Dark) {
        return QStringLiteral(R"(
        QDialog {
            background: #242424;
            color: #EEF2F5;
        }
        QMessageBox, QInputDialog {
            background: #242424;
            color: #EEF2F5;
        }
        QFrame {
            border: none;
        }
        QLabel {
            color: #EEF2F5;
        }
        QLabel#qt_msgbox_label {
            color: #EEF2F5;
            font-size: 13px;
        }
        QLabel#qt_msgbox_informativelabel {
            color: rgba(229, 234, 239, 0.76);
        }
        QPushButton {
            background: #2E2E2E;
            color: #EEF2F5;
            border: 1px solid #3F3F3F;
            border-radius: 10px;
            padding: 7px 14px;
            min-height: 18px;
        }
        QPushButton:hover {
            background: #373737;
        }
        QPushButton:pressed {
            background: #282828;
        }
        QPushButton:disabled {
            color: #929292;
            background: #242424;
            border-color: #313131;
        }
        QLineEdit, QComboBox, QSpinBox, QPlainTextEdit, QTextEdit {
            background: #262626;
            color: #EEF2F5;
            border: 1px solid #393939;
            border-radius: 10px;
            padding: 7px 10px;
            selection-background-color: #454545;
            selection-color: white;
        }
        QComboBox::drop-down {
            border: none;
            width: 24px;
        }
        QComboBox QAbstractItemView {
            background: #242424;
            color: #EEF2F5;
            border: 1px solid #363636;
            selection-background-color: #343434;
        }
        QDialogButtonBox {
            spacing: 8px;
        }
    )");
    }

    return QStringLiteral(R"(
        QDialog {
            background: #F6F8FB;
            color: #1E2933;
        }
        QMessageBox, QInputDialog {
            background: #F6F8FB;
            color: #1E2933;
        }
        QFrame {
            border: none;
        }
        QLabel {
            color: #1E2933;
        }
        QLabel#qt_msgbox_label {
            color: #1E2933;
            font-size: 13px;
        }
        QLabel#qt_msgbox_informativelabel {
            color: #61707E;
        }
        QPushButton {
            background: #FFFFFF;
            color: #1E2933;
            border: 1px solid #CBD5DF;
            border-radius: 10px;
            padding: 7px 14px;
            min-height: 18px;
        }
        QPushButton:hover {
            background: #EFF4F8;
        }
        QPushButton:pressed {
            background: #E4EBF2;
        }
        QPushButton:disabled {
            color: #95A0AB;
            background: #F1F4F7;
            border-color: #DCE3EA;
        }
        QLineEdit, QComboBox, QSpinBox, QPlainTextEdit, QTextEdit {
            background: #FFFFFF;
            color: #1E2933;
            border: 1px solid #CCD5DE;
            border-radius: 10px;
            padding: 7px 10px;
            selection-background-color: #CFE2FF;
            selection-color: white;
        }
        QComboBox::drop-down {
            border: none;
            width: 24px;
        }
        QComboBox QAbstractItemView {
            background: #FFFFFF;
            color: #1E2933;
            border: 1px solid #CCD5DE;
            selection-background-color: #E3EDF8;
        }
        QDialogButtonBox {
            spacing: 8px;
        }
    )");
}

void applyPalette(QWidget *widget)
{
    if (widget == nullptr) {
        return;
    }

    QPalette palette = widget->palette();
    if (s_themeMode == UiChrome::ThemeMode::Dark) {
        palette.setColor(QPalette::Window, QColor("#242424"));
        palette.setColor(QPalette::WindowText, QColor("#EEF2F5"));
        palette.setColor(QPalette::Base, QColor("#262626"));
        palette.setColor(QPalette::Text, QColor("#EEF2F5"));
        palette.setColor(QPalette::Button, QColor("#2E2E2E"));
        palette.setColor(QPalette::ButtonText, QColor("#EEF2F5"));
        palette.setColor(QPalette::Highlight, QColor("#454545"));
        palette.setColor(QPalette::HighlightedText, Qt::white);
    } else {
        palette.setColor(QPalette::Window, QColor("#F6F8FB"));
        palette.setColor(QPalette::WindowText, QColor("#1E2933"));
        palette.setColor(QPalette::Base, QColor("#FFFFFF"));
        palette.setColor(QPalette::Text, QColor("#1E2933"));
        palette.setColor(QPalette::Button, QColor("#FFFFFF"));
        palette.setColor(QPalette::ButtonText, QColor("#1E2933"));
        palette.setColor(QPalette::Highlight, QColor("#D6E7FF"));
        palette.setColor(QPalette::HighlightedText, QColor("#123150"));
    }
    widget->setPalette(palette);
    widget->setAutoFillBackground(true);
}

} // namespace

namespace UiChrome {

void setThemeMode(ThemeMode mode)
{
    s_themeMode = mode;
}

ThemeMode themeMode()
{
    return s_themeMode;
}

void applyAppTheme(QApplication *app)
{
    if (app == nullptr) {
        return;
    }

    QPalette palette = app->palette();
    if (s_themeMode == ThemeMode::Dark) {
        palette.setColor(QPalette::Window, QColor("#1F1F1F"));
        palette.setColor(QPalette::WindowText, QColor("#EEF2F5"));
        palette.setColor(QPalette::Base, QColor("#262626"));
        palette.setColor(QPalette::AlternateBase, QColor("#242424"));
        palette.setColor(QPalette::Text, QColor("#EEF2F5"));
        palette.setColor(QPalette::Button, QColor("#2E2E2E"));
        palette.setColor(QPalette::ButtonText, QColor("#EEF2F5"));
        palette.setColor(QPalette::Highlight, QColor("#454545"));
        palette.setColor(QPalette::HighlightedText, Qt::white);
    } else {
        palette.setColor(QPalette::Window, QColor("#F3F6FA"));
        palette.setColor(QPalette::WindowText, QColor("#1E2933"));
        palette.setColor(QPalette::Base, QColor("#FFFFFF"));
        palette.setColor(QPalette::AlternateBase, QColor("#F6F8FB"));
        palette.setColor(QPalette::Text, QColor("#1E2933"));
        palette.setColor(QPalette::Button, QColor("#FFFFFF"));
        palette.setColor(QPalette::ButtonText, QColor("#1E2933"));
        palette.setColor(QPalette::Highlight, QColor("#D6E7FF"));
        palette.setColor(QPalette::HighlightedText, QColor("#123150"));
    }
    app->setPalette(palette);
}

void applyWindowChrome(QWidget *window)
{
    if (window == nullptr) {
        return;
    }

    applyPalette(window);
    setWindowsDarkTitleBar(window, s_themeMode == ThemeMode::Dark);
}

void applyDialogTheme(QWidget *dialogLike)
{
    if (dialogLike == nullptr) {
        return;
    }

    applyPalette(dialogLike);
    dialogLike->setStyleSheet(dialogStyleSheet());
    setWindowsDarkTitleBar(dialogLike, s_themeMode == ThemeMode::Dark);
}

void applyMessageBoxTheme(QMessageBox *box)
{
    applyDialogTheme(box);
}

void applyInputDialogTheme(QInputDialog *dialog)
{
    applyDialogTheme(dialog);
}

bool setWindowsDarkTitleBar(QWidget *window, bool enabled)
{
#if defined(Q_OS_WIN)
    if (window == nullptr) {
        return false;
    }

    const HWND hwnd = reinterpret_cast<HWND>(window->winId());
    if (hwnd == nullptr) {
        return false;
    }

    const BOOL darkMode = enabled ? TRUE : FALSE;
    const DWORD useImmersive = enabled ? 1u : 0u;
    bool applied = false;

    const HRESULT immersiveHr
        = DwmSetWindowAttribute(hwnd, kDwmwaUseImmersiveDarkMode, &useImmersive, sizeof(useImmersive));
    applied = SUCCEEDED(immersiveHr) || applied;

    const HRESULT legacyHr
        = DwmSetWindowAttribute(hwnd, kDwmwaUseImmersiveDarkModeLegacy, &darkMode, sizeof(darkMode));
    applied = SUCCEEDED(legacyHr) || applied;

    const DWORD captionColor
        = enabled ? static_cast<DWORD>(kDarkCaptionColor) : static_cast<DWORD>(kLightCaptionColor);
    const DWORD borderColor
        = enabled ? static_cast<DWORD>(kDarkBorderColor) : static_cast<DWORD>(kLightBorderColor);
    const DWORD textColor = enabled ? static_cast<DWORD>(kDarkTextColor) : static_cast<DWORD>(kLightTextColor);

    const HRESULT captionHr = DwmSetWindowAttribute(hwnd, kDwmwaCaptionColor, &captionColor, sizeof(captionColor));
    applied = SUCCEEDED(captionHr) || applied;

    const HRESULT borderHr = DwmSetWindowAttribute(hwnd, kDwmwaBorderColor, &borderColor, sizeof(borderColor));
    applied = SUCCEEDED(borderHr) || applied;

    const HRESULT textHr = DwmSetWindowAttribute(hwnd, kDwmwaTextColor, &textColor, sizeof(textColor));
    applied = SUCCEEDED(textHr) || applied;

    const DWORD backdropType = kDwmSystemBackdropNone;
    const HRESULT backdropHr
        = DwmSetWindowAttribute(hwnd, kDwmwaSystemBackdropType, &backdropType, sizeof(backdropType));
    applied = SUCCEEDED(backdropHr) || applied;

    return applied;
#else
    Q_UNUSED(window);
    Q_UNUSED(enabled);
    return false;
#endif
}

} // namespace UiChrome
