#include "qt_demo_window.h"
#include <QApplication>
#include <QCommandLineParser>
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("m3_qt_demo"));
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("M3 Qt mind-map editor demo"));
    parser.addHelpOption();
    parser.addPositionalArgument(QStringLiteral("file"), QStringLiteral("Optional JSON mind map"), QStringLiteral("[file]"));
    parser.process(app);
    const QStringList files = parser.positionalArguments();
    if (files.size() > 1) parser.showHelp(2);
    DemoWindow window;
    if (!files.isEmpty()) window.openFile(files.front());
    window.show();
    return app.exec();
}
