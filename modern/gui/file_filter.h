#pragma once
#include <QFileSystemModel>
#include <QSet>
#include <QSortFilterProxyModel>

class ProjectFileFilter : public QSortFilterProxyModel {
  public:
    explicit ProjectFileFilter(QObject *parent = nullptr) : QSortFilterProxyModel(parent) {}
    void setExcluded(const QStringList &extensions) {
        QSet<QString> next;
        for (const auto &extension : extensions)
            next.insert(extension.toLower());
        if (next == excluded)
            return;
        beginFilterChange();
        excluded = next;
        endFilterChange(QSortFilterProxyModel::Direction::Rows);
    }

  protected:
    bool filterAcceptsRow(int row, const QModelIndex &parent) const override {
        auto files = qobject_cast<const QFileSystemModel *>(sourceModel());
        if (!files)
            return true;
        const auto source = files->index(row, 0, parent);
        const auto info = files->fileInfo(source);
        const auto extension = info.suffix().isEmpty() ? QStringLiteral("@") + info.fileName().toLower()
                                                       : info.suffix().toLower();
        return info.isDir() || !excluded.contains(extension);
    }

  private:
    QSet<QString> excluded;
};
