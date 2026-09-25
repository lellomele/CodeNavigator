#include "editor.h"
#include "printing.h"
#include "localization.h"
#include "session.h"
#include <QPrinter>
#include <QTextDocument>
#include <QContextMenuEvent>
#include "theme.h"
#include "window.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtTest>
class UiTest : public QObject {
    Q_OBJECT
  private slots:
    void initTestCase() {
        qApp->setStyle(QStringLiteral("Fusion"));
        qApp->setFont(QFont(QStringLiteral("Segoe UI"), 10));
        QCoreApplication::setOrganizationName(QStringLiteral("SourceNavigatorTests"));
        QCoreApplication::setApplicationName(QStringLiteral("Workflow"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
    }
    void startupPreferenceAndMissingDirectory() {
        QTemporaryDir d;
        QSettings settings;
        settings.setValue(QStringLiteral("session/lastProject"), d.path());
        settings.setValue(QStringLiteral("session/reopen"), true);
        QCOMPARE(startupProject(),d.path());
        settings.setValue(QStringLiteral("session/reopen"),false);
        QVERIFY(startupProject().isEmpty());
        QCOMPARE(startupProject(d.path()),d.path());
        settings.setValue(QStringLiteral("session/reopen"),true);
        settings.setValue(QStringLiteral("session/lastProject"),d.filePath(QStringLiteral("missing")));
        QVERIFY(startupProject().isEmpty());
        settings.remove(QStringLiteral("session"));
    }
    void printEscapingAndPagination() {
        QTemporaryDir d;
        QJsonArray rows;
        for(int i=1;i<=200;++i) rows.append(QJsonObject{{QStringLiteral("line"),i},{QStringLiteral("before"),QStringLiteral("if (a < b && b > 0) <script>text</script>")},{QStringLiteral("after"),QStringLiteral("new_value")}});
        QJsonArray entries{QJsonObject{{QStringLiteral("path"),QStringLiteral("file<&>.cpp")},{QStringLiteral("count"),200},{QStringLiteral("preview"),rows}}};
        auto html=replacementHtml(entries,QStringLiteral("root<&>"));
        QVERIFY(html.contains(QStringLiteral("&lt;script&gt;")));
        QVERIFY(!html.contains(QStringLiteral("<script>")));
        QCOMPARE(html.count(QStringLiteral("new_value")),200);
        QPrinter printer;printer.setOutputFormat(QPrinter::PdfFormat);printer.setOutputFileName(d.filePath(QStringLiteral("matches.pdf")));
        QTextDocument document;document.setHtml(html);document.print(&printer);
        QVERIFY(QFileInfo(printer.outputFileName()).size()>10000);
        auto out=qEnvironmentVariable("SN_SCREENSHOT_DIR");
        if(!out.isEmpty()) {QDir().mkpath(out);QFile::remove(out+QStringLiteral("/print-preview.pdf"));QFile::copy(printer.outputFileName(),out+QStringLiteral("/print-preview.pdf"));}
    }
    void languageMenusAndPanelIcons() {
        const QStringList languages={QStringLiteral("it"),QStringLiteral("en"),QStringLiteral("fr"),QStringLiteral("de")};
        const QStringList fileNames={QStringLiteral("File"),QStringLiteral("File"),QStringLiteral("Fichier"),QStringLiteral("Datei")};
        for(int i=0;i<languages.size();++i) {
            setUiLanguage(languages[i]);Window w;w.show();QTest::qWait(50);
            QCOMPARE(w.menuBar()->actions().first()->text(),fileNames[i]);
            QVERIFY(!w.findChild<QPushButton *>(QStringLiteral("expandEditor"))->icon().isNull());
            auto out=qEnvironmentVariable("SN_SCREENSHOT_DIR");
            if(!out.isEmpty()) {QDir().mkpath(out);w.grab().save(out+QStringLiteral("/language-%1.png").arg(languages[i]));}
        }
        setUiLanguage(QStringLiteral("it"));
    }
    void editorContextMenuUsesClickedWord() {
        Editor editor;editor.resize(600,200);editor.show();
        editor.sends(SCI_SETTEXT,0,"first alpha last");editor.send(SCI_GOTOPOS,0);
        QCOMPARE(editor.wordAtPosition(8),QStringLiteral("alpha"));
        QVERIFY(editor.wordAtPosition(5).isEmpty());
        QSignalSpy spy(&editor,&Editor::crossReferenceRequested);
        bool actionFound=false;
        QTest::qWait(50);
        QTimer::singleShot(0,&editor,[&]{
            auto menu=qobject_cast<QMenu *>(QApplication::activePopupWidget());
            QVERIFY(menu);QTimer::singleShot(5000,menu,&QMenu::close);
            auto action=menu->findChild<QAction *>(QStringLiteral("editorCrossReference"));
            QVERIFY(action && action->isEnabled());actionFound=true;
            menu->setActiveAction(action);QTest::keyClick(menu,Qt::Key_Return);
        });
        QPoint point(int(editor.send(SCI_POINTXFROMPOSITION,0,8)),int(editor.send(SCI_POINTYFROMPOSITION,0,8))+5);
        QContextMenuEvent event(QContextMenuEvent::Mouse,point,editor.viewport()->mapToGlobal(point));
        QApplication::sendEvent(editor.viewport(),&event);
        QVERIFY(actionFound);QCOMPARE(spy.count(),1);QCOMPARE(spy.takeFirst().at(0).toString(),QStringLiteral("alpha"));
    }
    void pdfExportDoesNotNeedAPrinter() {
        QTemporaryDir d;
        QJsonObject row{{QStringLiteral("line"),9},{QStringLiteral("before"),QStringLiteral("before <&>")},{QStringLiteral("after"),QStringLiteral("after")}};
        QJsonObject file{{QStringLiteral("path"),QStringLiteral("src/file.cpp")},{QStringLiteral("count"),1},{QStringLiteral("preview"),QJsonArray{row}}};
        QJsonArray entries{file};
        QString error;const auto path=d.filePath(QStringLiteral("matches.pdf"));
        QVERIFY2(exportReplacementsPdf(entries,QStringLiteral("project"),path,error),qPrintable(error));
        QFile pdf(path);QVERIFY(pdf.open(QIODevice::ReadOnly));QVERIFY(pdf.read(8).startsWith("%PDF-"));QVERIFY(pdf.size()>1000);
        QVERIFY(!exportReplacementsPdf(entries,QStringLiteral("project"),d.filePath(QStringLiteral("missing/matches.pdf")),error));
    }
    void rejectLowContrast() {
        auto t = Theme::presets()[0];
        t.foreground = t.background;
        QVERIFY(!t.validate().isEmpty());
    }
    void utf16PreservesEncoding() {
        QTemporaryDir d;
        auto p = d.filePath(QStringLiteral("unicode.rs"));
        QFile f(p);
        QVERIFY(f.open(QIODevice::WriteOnly));
        const auto original = QByteArray::fromHex("fffe6c0065007400200078003b000d000a00");
        f.write(original);
        f.close();
        Editor e;
        QString error;
        QVERIFY2(e.load(p, error), qPrintable(error));
        QCOMPARE(e.send(SCI_GETEOLMODE), sptr_t(SC_EOL_CRLF));
        QVERIFY(e.save(error));
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), original);
    }
    void unrepresentableCharacterDoesNotChangeFile() {
        QTemporaryDir d;
        auto p = d.filePath(QStringLiteral("ansi.c"));
        QFile f(p);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("//\xe8");
        f.close();
        Editor e;
        QString error;
        QVERIFY(e.load(p, error));
        auto extra = QStringLiteral("日本").toUtf8();
        e.sends(SCI_APPENDTEXT, extra.size(), extra.constData());
        QVERIFY(!e.save(error));
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), QByteArray("//\xe8"));
    }
    void binaryRejected() {
        QTemporaryDir d;
        auto p = d.filePath(QStringLiteral("fake.c"));
        QFile f(p);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QByteArray("a\0b", 3));
        f.close();
        Editor e;
        QString error;
        QVERIFY(!e.load(p, error));
    }
    void projectSearchPreviewReplaceAndExternalEditor() {
        QTemporaryDir d;
        QVERIFY(d.isValid());
        qputenv("SN_DATA_DIR", d.filePath(QStringLiteral("profile")).toUtf8());
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                           d.filePath(QStringLiteral("settings")));
        auto root = d.filePath(QStringLiteral("Progetto à 日本"));
        QDir().mkpath(root);
        auto a = root + QStringLiteral("/catalog.rs");
        auto b = root + QStringLiteral("/client.ts");
        QFile fa(a);
        QVERIFY(fa.open(QIODevice::WriteOnly));
        fa.write("// Catalog service\nuse std::collections::HashMap;\n\npub struct Catalog {\n    "
                 "alpha: HashMap<String, String>,\n}\n\nimpl Catalog {\n    pub fn new() -> Self "
                 "{\n        Self { alpha: HashMap::new() }\n    }\n\n    pub fn get(&self, key: "
                 "&str) -> Option<&String> {\n        self.alpha.get(key)\n    }\n}\n");
        fa.close();
        QFile fb(b);
        QVERIFY(fb.open(QIODevice::WriteOnly));
        fb.write("export const alpha = 1;\n");
        fb.close();
        Window w;
        w.show();
        bool discoveryShown = false, reviewShown = false;
        QTimer dialogs;
        dialogs.setInterval(20);
        connect(&dialogs, &QTimer::timeout, &w, [&] {
            auto dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (!dialog)
                return;
            if (dialog->windowTitle() == QStringLiteral("File da includere")) {
                discoveryShown = true;
                dialog->accept();
            } else if (dialog->objectName() == QStringLiteral("replacementReview")) {
                reviewShown = true;
                dialog->accept();
            }
        });
        dialogs.start();
        w.openProject(root);
        auto search = w.findChild<QLineEdit *>(QStringLiteral("searchPattern"));
        auto results = w.findChild<QTreeWidget *>(QStringLiteral("searchResults"));
        auto replace = w.findChild<QPushButton *>(QStringLiteral("replacePreview"));
        QVERIFY(search && results && replace);
        search->setText(QStringLiteral("alpha"));
        QTRY_VERIFY_WITH_TIMEOUT(discoveryShown, 10000);
        QTRY_COMPARE_WITH_TIMEOUT(results->topLevelItemCount(), 2, 15000);
        // Reopening cached file types must not scan newly added extensions.
        QFile added(root + QStringLiteral("/later.xyz"));
        QVERIFY(added.open(QIODevice::WriteOnly));
        added.write("plain text");
        added.close();
        dialogs.stop();
        bool cachedDialog = false;
        QTimer::singleShot(0, &w, [&] {
            auto dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            QVERIFY(dialog);
            auto types = dialog->findChild<QTreeWidget *>();
            QVERIFY(types);
            for (int i = 0; i < types->topLevelItemCount(); ++i)
                QVERIFY(types->topLevelItem(i)->text(0) != QStringLiteral("xyz"));
            cachedDialog = true;
            dialog->reject();
        });
        w.findChild<QPushButton *>(QStringLiteral("fileTypes"))->click();
        QVERIFY(cachedDialog);
        auto projectTree=w.findChild<QTreeView *>(QStringLiteral("projectTree"));
        auto model=qobject_cast<QFileSystemModel *>(projectTree->model());
        const auto folder=root+QStringLiteral("/nested folder");QDir().mkpath(folder);
        QTRY_VERIFY(model->index(folder).isValid());projectTree->setCurrentIndex(model->index(folder));
        bool filterChecked=false;
        QTimer::singleShot(0,&w,[&]{
            auto dialog=qobject_cast<QDialog *>(QApplication::activeModalWidget());QVERIFY(dialog);
            QTimer::singleShot(5000,dialog,&QDialog::reject);
            QCOMPARE(dialog->findChild<QLineEdit *>(QStringLiteral("xrefPathFilter"))->text(),QStringLiteral("nested folder/*"));
            filterChecked=true;dialog->reject();
        });
        w.findChild<QAction *>(QStringLiteral("crossReferences"))->trigger();QVERIFY(filterChecked);
        projectTree->setCurrentIndex(model->index(a));
        bool xrefsShown = false;
        QTimer::singleShot(0, &w, [&] {
            auto dialog=qobject_cast<QDialog *>(QApplication::activeModalWidget());
            QVERIFY(dialog);
            QTimer::singleShot(10000,dialog,&QDialog::reject);
            auto subject=dialog->findChild<QLineEdit *>(QStringLiteral("xrefSubject"));
            auto list=dialog->findChild<QTreeWidget *>(QStringLiteral("xrefResults"));
            QVERIFY(subject && list);
            QCOMPARE(dialog->findChild<QLineEdit *>(QStringLiteral("xrefPathFilter"))->text(),QStringLiteral("*"));
            subject->setText(QStringLiteral("alpha"));
            dialog->findChild<QPushButton *>(QStringLiteral("xrefSearch"))->click();
            QTRY_VERIFY_WITH_TIMEOUT(list->topLevelItemCount()>0,8000);
            xrefsShown=true;
            auto imageDir=qEnvironmentVariable("SN_SCREENSHOT_DIR");
            if(!imageDir.isEmpty()) dialog->grab().save(imageDir+QStringLiteral("/cross-references.png"));
            dialog->accept();
        });
        w.findChild<QAction *>(QStringLiteral("crossReferences"))->trigger();
        QVERIFY(xrefsShown);
        bool printShown=false;
        QTimer printWatcher;printWatcher.setInterval(20);
        connect(&printWatcher,&QTimer::timeout,&w,[&]{
            auto dialog=qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if(dialog && dialog->objectName()==QStringLiteral("printMatchesDialog")) {
                QVERIFY(dialog->findChild<QPushButton *>(QStringLiteral("choosePrinter")));
                QVERIFY(dialog->findChild<QPushButton *>(QStringLiteral("exportMatchesPdf")));
                QVERIFY(dialog->findChild<QTextBrowser *>()->toPlainText().contains(QStringLiteral("catalog.rs")));
                printShown=true;dialog->reject();
            }
        });
        printWatcher.start();w.findChild<QAction *>(QStringLiteral("printMatches"))->trigger();
        QTRY_VERIFY_WITH_TIMEOUT(printShown,15000);printWatcher.stop();
        QVERIFY(fa.open(QIODevice::ReadOnly));QVERIFY(fa.readAll().contains("self.alpha"));fa.close();
        dialogs.start();
        auto splitter = w.findChild<QSplitter *>(QStringLiteral("workspaceSplitter"));
        auto expand = w.findChild<QPushButton *>(QStringLiteral("expandEditor"));
        QVERIFY(splitter && expand);
        const int originalWidth = splitter->widget(2)->width();
        expand->click();
        QTest::qWait(50);
        QVERIFY(splitter->widget(0)->isHidden());
        QVERIFY(splitter->widget(1)->isHidden());
        QVERIFY(splitter->widget(2)->width() > originalWidth);
        expand->click();
        QVERIFY(!splitter->widget(0)->isHidden());
        QVERIFY(!splitter->widget(1)->isHidden());
        auto item = results->topLevelItem(0);
        QCOMPARE(item->checkState(0), Qt::Unchecked);
        item->setCheckState(0, Qt::Checked);
        results->setCurrentItem(item);
        auto preview = w.findChild<Editor *>(QStringLiteral("previewEditor"));
        QVERIFY(preview);
        QCOMPARE(preview->path(), QFileInfo(a).canonicalFilePath());
        QVERIFY(preview->send(SCI_GETREADONLY));
        QVERIFY(preview->bytes().contains("self.alpha"));
        auto imageDir = qEnvironmentVariable("SN_SCREENSHOT_DIR");
        if (!imageDir.isEmpty()) {
            QDir().mkpath(imageDir);
            w.setTheme(3);
            QTest::qWait(200);
            QVERIFY(w.grab().save(imageDir + QStringLiteral("/notte.png")));
            w.setTheme(0);
            QTest::qWait(200);
            QVERIFY(w.grab().save(imageDir + QStringLiteral("/giorno.png")));
            for (int theme = 0; theme < 4; ++theme) {
                w.setTheme(theme);
                auto combo = w.findChild<QComboBox *>(QStringLiteral("themes"));
                combo->showPopup();
                combo->view()->setCurrentIndex(combo->model()->index(theme, 0));
                QTest::qWait(80);
                QVERIFY(combo->view()->window()->grab().save(imageDir + QStringLiteral("/popup-%1.png").arg(theme)));
                combo->hidePopup();
            }
            w.setTheme(3);
        }
        auto output = d.filePath(QStringLiteral("external.json"));
        QSettings settings;
        settings.setValue(QStringLiteral("external/exe"),
                          QCoreApplication::applicationDirPath() +
                              QStringLiteral("/external-recorder.exe"));
        settings.setValue(QStringLiteral("external/args"),
                          output + QStringLiteral("\n{file}\n{line}"));
        w.findChild<QPushButton *>(QStringLiteral("externalEditor"))->click();
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(output), 10000);
        QTRY_VERIFY_WITH_TIMEOUT(([&] {QFile pending(output);if(!pending.open(QIODevice::ReadOnly))return false;return QJsonDocument::fromJson(pending.readAll()).array().size()==2;})(),10000);
        QFile captured(output);
        QVERIFY(captured.open(QIODevice::ReadOnly));
        auto args = QJsonDocument::fromJson(captured.readAll()).array();
        QCOMPARE(args.size(),2);
        QCOMPARE(args[0].toString(), QFileInfo(a).canonicalFilePath());
        QCOMPARE(args[1].toString(), QStringLiteral("5"));
        captured.close();
        w.findChild<QLineEdit *>(QStringLiteral("replacement"))->setText(QStringLiteral("entries"));
        QVERIFY(replace->isEnabled());
        replace->click();
        QTRY_VERIFY_WITH_TIMEOUT(reviewShown, 10000);
        auto changed = [&] {
            QFile f(a);
            if (!f.open(QIODevice::ReadOnly))
                return false;
            return f.readAll().contains("self.entries");
        };
        QTRY_VERIFY_WITH_TIMEOUT(changed(), 10000);
        QTRY_COMPARE_WITH_TIMEOUT(results->topLevelItemCount(), 1, 10000);
        QVERIFY(fb.open(QIODevice::ReadOnly));
        QCOMPARE(fb.readAll(), QByteArray("export const alpha = 1;\n"));
        fb.close();
        dialogs.stop();
        w.close();
        qunsetenv("SN_DATA_DIR");
    }
    void themeContrast() {
        for (const auto &t : Theme::presets()) {
            QVERIFY2(t.validate().isEmpty(),
                     qPrintable(t.name + QStringLiteral(": ") + t.validate()));
            QCOMPARE(Theme::fromJson(t.json()).json(), t.json());
        }
    }
    void savePreservesCrLfAndDetectsConflict() {
        QTemporaryDir d;
        auto p = d.filePath(QStringLiteral("sample.cpp"));
        QFile f(p);
        QVERIFY(f.open(QIODevice::WriteOnly));
        QByteArray original("\xef\xbb\xbfint value;\r\n");
        f.write(original);
        f.close();
        Editor e;
        QString error;
        QVERIFY(e.load(p, error));
        e.sends(SCI_APPENDTEXT, 4, "//x\n");
        QVERIFY(e.dirty());
        QVERIFY(e.save(error));
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), original + QByteArray("//x\n"));
        f.close();
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("external");
        f.close();
        e.sends(SCI_APPENDTEXT, 1, "x");
        QVERIFY(!e.save(error));
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), QByteArray("external"));
    }
    void legacyEncodingRoundtrip() {
        QTemporaryDir d;
        auto p = d.filePath(QStringLiteral("old.c"));
        QFile f(p);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("//\xff");
        f.close();
        Editor e;
        QString error;
        QVERIFY(e.load(p, error));
        QVERIFY(!e.send(SCI_GETREADONLY));
        QVERIFY(e.save(error));
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), QByteArray("//\xff"));
    }
    void recoverDraftAfterReopening() {
        QTemporaryDir d;
        qputenv("SN_DATA_DIR", d.path().toUtf8());
        auto path = d.filePath(QStringLiteral("draft.c"));
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("int value;\n");
        f.close();
        Editor original;
        QString error;
        QVERIFY(original.load(path, error));
        original.sends(SCI_APPENDTEXT, 8, "// draft");
        original.saveDraft();
        QVERIFY(original.hasDraft());
        Editor recovered;
        QVERIFY(recovered.load(path, error));
        QVERIFY(recovered.restoreDraft(error));
        QCOMPARE(recovered.bytes(), QByteArray("int value;\n// draft"));
        QVERIFY(recovered.dirty());
        QVERIFY(recovered.save(error));
        QVERIFY(!recovered.hasDraft());
        qunsetenv("SN_DATA_DIR");
    }
    void draftDoesNotOverwriteExternalChanges() {
        QTemporaryDir d;
        qputenv("SN_DATA_DIR", d.path().toUtf8());
        auto p = d.filePath(QStringLiteral("file.rs"));
        QFile f(p);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("original");
        f.close();
        Editor a;
        QString error;
        QVERIFY(a.load(p, error));
        a.sends(SCI_APPENDTEXT, 5, "draft");
        a.saveDraft();
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("external");
        f.close();
        Editor b;
        QVERIFY(b.load(p, error));
        QVERIFY(!b.restoreDraft(error));
        QCOMPARE(b.bytes(), QByteArray("external"));
        QVERIFY(b.hasDraft());
        qunsetenv("SN_DATA_DIR");
    }
};
QTEST_MAIN(UiTest)
#include "ui_test.moc"
