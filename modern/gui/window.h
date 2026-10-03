#pragma once
#include "editor.h"
#include "file_filter.h"
#include <QJsonArray>
#include <QProcess>
#include <QtWidgets>
#include <functional>
class Window : public QMainWindow {
    Q_OBJECT
  public:
    explicit Window(const QString &project = {}, QWidget *parent = nullptr);
    void openProject(const QString &);
    void openFile(const QString &, int line = 1, int column = 0);
    void setTheme(int);

  protected:
    void closeEvent(QCloseEvent *) override;

  private:
    Theme theme = Theme::presets()[3];
    QString searchDirectory;
    QLabel *searchDirectoryLabel;
    QString root, db, parsers, engine, cancelFile, selectedPath;
    QStringList extensions;
    int selectedLine = 1;
    QTreeView *tree;
    QFileSystemModel *fileModel;
    ProjectFileFilter *projectFileFilter;
    QTabWidget *tabs;
    Editor *preview;
    QLineEdit *pattern, *replacement, *pathFilter;
    QComboBox *mode, *themes, *searchScope;
    QCheckBox *caseSensitive, *wholeWords;
    QTreeWidget *results;
    QPlainTextEdit *diagnostics;
    QLabel *state, *projectLabel, *resultLabel, *previewLabel;
    QProgressBar *progress;
    QPushButton *replaceButton, *openMatchedFiles, *openFolder;
    QToolButton *replaceToggle;
    QWidget *replaceArea;
    QProcess *operation = nullptr, *queryProcess = nullptr;
    QTimer searchTimer, draftTimer;
    QJsonArray currentResults;
    void run(const QStringList &, std::function<void(const QJsonObject &)>);
    QJsonObject discoveryCache;
    void discover(bool refresh = false);
    void chooseFileTypes(const QJsonObject &);
    void manageExcludedExtensions();
    void indexProject();
    void cancelOperation();
    void search();
    void scheduleSearch();
    void showPreview(const QString &, int = 1, int = 0);
    void showResults(const QJsonObject &);
    void planReplacement(bool printOnly = false);
    void setupFileMenu();
    void showCrossReferences(const QString &subject = {});
    QString selectedProjectPathFilter() const;
    QString selectedProjectRelativeDirectory() const;
    void setSearchDirectory(const QString &);
    bool closeProject();
    void saveAs();
    void recoverReplacement();
    void configureExternal();
    void openExternal();
    void applyTheme(const Theme &);
    void customizeTheme();
    void exportTheme();
    void importTheme();
    bool saveEditor(Editor *);
    bool closeTab(int);
    bool prepareFileOperation();
    void refreshEditors();
    void record(const QString &);
};
