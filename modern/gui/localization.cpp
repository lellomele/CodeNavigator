#include "localization.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
static QString language=QStringLiteral("it");
void setUiLanguage(const QString &code) {language=QStringList({QStringLiteral("it"),QStringLiteral("en"),QStringLiteral("fr"),QStringLiteral("de")}).contains(code)?code:QStringLiteral("it");}
QString uiLanguage() {return language;}
QString ui(const QString &source) {
    if (language==QStringLiteral("it")) return source;
    static const QJsonObject catalog=[] {
        Q_INIT_RESOURCE(icons);
        QFile file(QStringLiteral(":/icons/translations.json"));
        if (!file.open(QIODevice::ReadOnly)) return QJsonObject();
        return QJsonDocument::fromJson(file.readAll()).object();
    }();
    return catalog.value(source).toObject().value(language).toString(source);
}
