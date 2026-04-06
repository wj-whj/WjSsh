#include "MainWindow.h"

#include "UiChrome.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QIcon>
#include <QSettings>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setOrganizationName("WJ");
    app.setApplicationName("WjSsh");
    app.setApplicationDisplayName("WjSsh");
    app.setStyle("Fusion");
    const QString savedTheme = QSettings().value(QStringLiteral("ui/theme"), QStringLiteral("light")).toString();
    UiChrome::setThemeMode(savedTheme.compare(QStringLiteral("dark"), Qt::CaseInsensitive) == 0
                               ? UiChrome::ThemeMode::Dark
                               : UiChrome::ThemeMode::Light);
    app.setWindowIcon(QIcon(QStringLiteral(":/icons/app.svg")));
    UiChrome::applyAppTheme(&app);

    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOption({QStringList() << "self-check", "Start and exit after a lightweight startup check."});
    parser.process(app);

    MainWindow window;
    if (parser.isSet("self-check")) {
        return 0;
    }

    window.show();
    return app.exec();
}
