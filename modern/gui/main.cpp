#include "crash.h"
#include "localization.h"
#include "session.h"
#include "storage.h"
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#include "window.h"
#include <QApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFont>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QSettings>
#include <QStandardPaths>
#include <QStyleFactory>
#include <QTimer>
#include <QTranslator>
#include <cstdio>
static QFile diagnosticFile;
static QMutex diagnosticMutex;
static void diagnosticMessage(QtMsgType type, const QMessageLogContext &context,
                              const QString &message) {
    QMutexLocker lock(&diagnosticMutex);
    QJsonObject event{
        {QStringLiteral("time"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
        {QStringLiteral("version"), QString::fromLatin1(SOURCE_NAVIGATOR_VERSION)},
        {QStringLiteral("severity"), int(type)},
        {QStringLiteral("message"), message},
        {QStringLiteral("file"), QString::fromUtf8(context.file ? context.file : "")},
        {QStringLiteral("line"), context.line}};
    if (diagnosticFile.isOpen()) {
        diagnosticFile.write(QJsonDocument(event).toJson(QJsonDocument::Compact));
        diagnosticFile.write("\n");
        diagnosticFile.flush();
    } else {
        auto bytes = QJsonDocument(event).toJson(QJsonDocument::Compact);
        std::fprintf(stderr, "%s\n", bytes.constData());
    }
}
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("SourceNavigator"));
    app.setApplicationName(QStringLiteral("SourceNavigator"));
    app.setApplicationVersion(QString::fromLatin1(SOURCE_NAVIGATOR_VERSION));
    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    app.setFont(QFont(QStringLiteral("Segoe UI"), 10));
    QString project, capture, file;
    bool crashTest = false;
    int theme = -1;
    const auto arguments = QCoreApplication::arguments();
    for (int i = 1; i < arguments.size(); ++i) {
        auto arg = arguments[i];
        if (arg == QStringLiteral("--capture") && i + 1 < arguments.size())
            capture = arguments[++i];
        else if (arg == QStringLiteral("--file") && i + 1 < arguments.size())
            file = arguments[++i];
        else if (arg == QStringLiteral("--theme") && i + 1 < arguments.size())
            theme = arguments[++i].toInt();
        else if (arg == QStringLiteral("--diagnostic-crash"))
            crashTest = true;
        else
            project = arg;
    }
    if (!capture.isEmpty())
        app.setApplicationName(QStringLiteral("SourceNavigatorPreview"));
    if (!qEnvironmentVariable("SN_DATA_DIR").isEmpty()) {
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, storageRoot());
    }
    setUiLanguage(QSettings().value(QStringLiteral("language"), QStringLiteral("it")).toString());
    QTranslator translations;
    if (translations.load(QCoreApplication::applicationDirPath() + QStringLiteral("/translations/qtbase_%1.qm").arg(uiLanguage())))
        app.installTranslator(&translations);
    if (theme >= 0 && theme < 4)
        QSettings().setValue(
            QStringLiteral("theme-v2"),
            QJsonDocument(Theme::presets()[theme].json()).toJson(QJsonDocument::Compact));
    auto logDir = storageRoot() + QStringLiteral("/logs");
    QDir().mkpath(logDir);
    diagnosticFile.setFileName(
        logDir + QStringLiteral("/gui-%1.jsonl").arg(QDateTime::currentMSecsSinceEpoch()));
    if (!diagnosticFile.open(QIODevice::WriteOnly))
        std::fprintf(stderr, "Impossibile aprire il log GUI\n");
    installCrashDiagnostics(logDir);
    qInstallMessageHandler(diagnosticMessage);
    qInfo() << "session_started" << QT_VERSION_STR;
#ifdef Q_OS_WIN
    if (crashTest)
        RaiseException(0xE0424242, EXCEPTION_NONCONTINUABLE, 0, nullptr);
#endif
    project = startupProject(project, capture.isEmpty());
    Window window(project);
    app.setWindowIcon(QIcon(QStringLiteral(":/icons/source-navigator.png")));
    if (theme >= 0)
        window.setTheme(theme);
    if (!file.isEmpty())
        window.openFile(file);
    window.show();
    if (!capture.isEmpty())
        QTimer::singleShot(1500, &app, [&] {
            window.grab().save(capture);
            app.quit();
        });
    const auto result = app.exec();
    qInfo() << "session_completed" << result;
    return result;
}
