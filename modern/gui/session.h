#pragma once
#include <QFileInfo>
#include <QSettings>
inline QString startupProject(const QString &explicitProject = {}, bool allowRestore = true) {
    if (!explicitProject.isEmpty()) return explicitProject;
    if (!allowRestore || !QSettings().value(QStringLiteral("session/reopen"), true).toBool()) return {};
    auto last=QSettings().value(QStringLiteral("session/lastProject")).toString();
    return QFileInfo(last).isDir()?last:QString();
}
