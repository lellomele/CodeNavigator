#pragma once
#include <QColor>
#include <QJsonObject>
#include <QVector>
class QApplication;
struct Theme {
    QString name;
    QColor background, foreground, panel, muted, accent, keyword, stringColor, comment, number;
    static QVector<Theme> presets();
    static double contrast(const QColor &, const QColor &);
    QString validate() const;
    QJsonObject json() const;
    static Theme fromJson(const QJsonObject &);
    void apply(QApplication &) const;
};
