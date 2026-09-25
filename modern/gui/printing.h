#pragma once
#include <QJsonArray>
#include <QString>
class QWidget;
QString replacementHtml(const QJsonArray &, const QString &root);
bool exportReplacementsPdf(const QJsonArray &, const QString &root, const QString &path, QString &error);
void printReplacements(const QJsonArray &, const QString &root, QWidget *parent);
