#pragma once

#include <QDialog>
#include <QInputDialog>
#include <QMessageBox>
#include <QWidget>

class QApplication;

namespace UiChrome {

enum class ThemeMode {
    Dark,
    Light
};

void setThemeMode(ThemeMode mode);
[[nodiscard]] ThemeMode themeMode();
void applyAppTheme(QApplication *app);
void applyWindowChrome(QWidget *window);
void applyDialogTheme(QWidget *dialogLike);
void applyMessageBoxTheme(QMessageBox *box);
void applyInputDialogTheme(QInputDialog *dialog);
bool setWindowsDarkTitleBar(QWidget *window, bool enabled = true);

} // namespace UiChrome
