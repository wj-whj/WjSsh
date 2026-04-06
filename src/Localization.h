#pragma once

#include <QString>

class QWidget;

namespace Localization {

enum class Language {
    Chinese,
    English
};

void setLanguage(Language language);
Language language();
QString languageKey(Language language);
Language languageFromKey(const QString &value);
QString translateText(const QString &text);
void applyWidgetTexts(QWidget *root);

} // namespace Localization
