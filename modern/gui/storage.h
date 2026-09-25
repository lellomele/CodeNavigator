#pragma once
#include <QStandardPaths>
#include <QString>
inline QString storageRoot() {
    auto overridePath = qEnvironmentVariable("SN_DATA_DIR");
    return overridePath.isEmpty()
               ? QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
               : overridePath;
}
