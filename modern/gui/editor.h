#pragma once
#include "ScintillaEditBase.h"
#include "theme.h"
#include <QByteArray>
class Editor : public ScintillaEditBase {
    Q_OBJECT
  public:
    explicit Editor(QWidget *parent = nullptr);
    bool load(const QString &path, QString &error);
    bool save(QString &error, bool allowExternalChange = false);
    bool saveCopy(const QString &path, QString &error);
    QByteArray bytes() const;
    QString path() const { return filePath; }
    bool dirty() const;
    QString wordAtPosition(sptr_t position) const;
    void applyTheme(const Theme &);
    void goTo(int line, int byteColumn = 0);
    void saveDraft();
    bool hasDraft() const;
    bool restoreDraft(QString &error);
    void discardDraft();
  protected:
    void contextMenuEvent(QContextMenuEvent *) override;
  signals:
    void crossReferenceRequested(const QString &word);
    void draftFailed(const QString &error);

  private:
    QString filePath, lexer;
    QByteArray originalHash;
    QByteArray bomPrefix, codecName = "UTF-8";
    bool readOnlyEncoding = false;
    QString draftPath() const;
};
