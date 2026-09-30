#include "localization.h"
#include "window.h"
#include <QSaveFile>
void Window::setupFileMenu() {
    auto file = menuBar()->addMenu(ui(QStringLiteral("File")));
    auto add = [this, file](const QString &text, const QKeySequence &key, auto fn) {
        auto action = file->addAction(text); action->setShortcut(key);
        connect(action, &QAction::triggered, this, fn); return action;
    };
    auto openProjectDialog = [this] { auto path=QFileDialog::getExistingDirectory(this, ui(QStringLiteral("Directory radice")),root); if (!path.isEmpty()) openProject(path); };
    add(ui(QStringLiteral("Nuovo progetto…")), QKeySequence(QStringLiteral("Ctrl+Shift+N")), openProjectDialog);
    add(ui(QStringLiteral("Apri progetto…")), QKeySequence(QStringLiteral("Ctrl+Shift+O")), openProjectDialog);
    add(ui(QStringLiteral("Apri file…")), QKeySequence::Open, [this] { auto path=QFileDialog::getOpenFileName(this, ui(QStringLiteral("Apri file")),root); if (!path.isEmpty()) openFile(path); });
    file->addSeparator();
    add(ui(QStringLiteral("Salva")), QKeySequence::Save, [this] { saveEditor(qobject_cast<Editor *>(tabs->currentWidget())); });
    add(ui(QStringLiteral("Salva con nome…")), QKeySequence::SaveAs, [this] { saveAs(); });
    add(ui(QStringLiteral("Salva tutti")), QKeySequence(QStringLiteral("Ctrl+Alt+S")), [this] { for (int i=1;i<tabs->count();++i) if (!saveEditor(qobject_cast<Editor *>(tabs->widget(i)))) break; });
    file->addSeparator();
    auto print=add(ui(QStringLiteral("Stampa corrispondenze…")), QKeySequence::Print, [this] {planReplacement(true);});
    print->setObjectName(QStringLiteral("printMatches"));
    file->addSeparator();
    add(ui(QStringLiteral("Chiudi file")), QKeySequence::Close, [this] {closeTab(tabs->currentIndex());});
    add(ui(QStringLiteral("Chiudi progetto")), {}, [this] {closeProject();});
    add(ui(QStringLiteral("Esci")), QKeySequence(QStringLiteral("Alt+F4")), [this] {close();});
}
bool Window::closeProject() {
    if (operation) { state->setText(ui(QStringLiteral("Operazione in corso: attendi o interrompi."))); return false; }
    while (tabs->count()>1) if (!closeTab(tabs->count()-1)) return false;
    searchTimer.stop();
    if (queryProcess) { queryProcess->disconnect(this); queryProcess->kill(); queryProcess->deleteLater(); queryProcess=nullptr; }
    root.clear(); db.clear(); extensions.clear(); discoveryCache={}; selectedPath.clear();
    results->clear(); currentResults={}; replaceButton->setEnabled(false);
    tree->setRootIndex(projectFileFilter->mapFromSource(fileModel->setRootPath(QString())));
    preview->send(SCI_SETREADONLY,0); preview->sends(SCI_SETTEXT,0,""); preview->send(SCI_SETREADONLY,1);
    projectLabel->clear(); previewLabel->clear(); resultLabel->clear();
    state->setText(ui(QStringLiteral("Scegli una directory radice per iniziare")));
    setWindowTitle(QStringLiteral("Source Navigator %1").arg(QString::fromLatin1(SOURCE_NAVIGATOR_VERSION)));
    QSettings().remove(QStringLiteral("session/lastProject"));
    return true;
}
void Window::saveAs() {
    auto editor=qobject_cast<Editor *>(tabs->currentWidget());
    if (!editor || editor==preview) return;
    auto path=QFileDialog::getSaveFileName(this,ui(QStringLiteral("Salva con nome")),editor->path());
    if (path.isEmpty()) return;
    if (QFileInfo(path).canonicalFilePath()==editor->path()) {saveEditor(editor);return;}
    // Export a copy, preserving the source tab and its draft until explicitly saved.
    QString error;
    if (!editor->saveCopy(path,error)) {QMessageBox::warning(this,ui(QStringLiteral("Salvataggio")),error);return;}
    openFile(path);
}
QString Window::selectedProjectPathFilter() const {
    const auto index = tree->currentIndex();
    QFileInfo selected(index.isValid() ? fileModel->filePath(projectFileFilter->mapToSource(index)) : root);
    const auto directory = selected.isDir() ? selected.absoluteFilePath() : selected.absolutePath();
    const auto relative = QDir(root).relativeFilePath(directory);
    if (relative == QStringLiteral(".") || relative == QStringLiteral("..") || relative.startsWith(QStringLiteral("../")) || QDir::isAbsolutePath(relative))
        return QStringLiteral("*");
    return QDir::fromNativeSeparators(relative) + QStringLiteral("/*");
}
void Window::showCrossReferences(const QString &initialSubject) {
    if (root.isEmpty()) {state->setText(ui(QStringLiteral("Apri prima un progetto.")));return;}
    QDialog dialog(this); dialog.setObjectName(QStringLiteral("crossReferenceDialog"));
    dialog.setWindowTitle(ui(QStringLiteral("Riferimenti incrociati"))); dialog.resize(1150,750);
    auto layout=new QVBoxLayout(&dialog);
    auto rootLabel=new QLabel(root); rootLabel->setTextInteractionFlags(Qt::TextSelectableByMouse); layout->addWidget(rootLabel);
    auto note=new QLabel(ui(QStringLiteral("Analisi sintattica dell’ultimo indice: i nomi omonimi non sono risolti semanticamente. Macro, overload e chiamate dinamiche possono produrre risultati incompleti. F5 aggiorna l’indice. Linguaggi: C/C++, Java, C#, JavaScript/TypeScript, Python, Rust, PHP."))); note->setWordWrap(true);layout->addWidget(note);
    auto row=new QHBoxLayout; layout->addLayout(row);
    auto subject=new QLineEdit;subject->setObjectName(QStringLiteral("xrefSubject"));subject->setPlaceholderText(ui(QStringLiteral("Nome esatto del simbolo o dipendenza"))); row->addWidget(subject,1);
    auto relation=new QComboBox; relation->setObjectName(QStringLiteral("xrefRelation"));
    const QStringList labels={ui(QStringLiteral("Tutte le relazioni")),ui(QStringLiteral("Chiamate")),ui(QStringLiteral("Dichiarazioni")),ui(QStringLiteral("Letture")),ui(QStringLiteral("Scritture")),ui(QStringLiteral("Ereditarietà / interfacce")),ui(QStringLiteral("Dipendenze dei file"))};
    const QStringList codes={QStringLiteral("all"),QStringLiteral("calls"),QStringLiteral("declaration"),QStringLiteral("read"),QStringLiteral("write"),QStringLiteral("inherits"),QStringLiteral("depends")};
    for(int i=0;i<labels.size();++i) relation->addItem(labels[i],codes[i]);
    relation->setView(new QListView(relation));row->addWidget(relation);
    auto direction=new QComboBox;direction->addItem(ui(QStringLiteral("Verso il simbolo")),QStringLiteral("incoming"));direction->addItem(ui(QStringLiteral("Dal simbolo")),QStringLiteral("outgoing")); direction->setView(new QListView(direction));row->addWidget(direction);
    auto path=new QLineEdit;path->setPlaceholderText(ui(QStringLiteral("Filtro percorso, es. src/*")));layout->addWidget(path);
    path->setObjectName(QStringLiteral("xrefPathFilter"));
    path->setText(selectedProjectPathFilter());
    subject->setText(initialSubject);
    auto query=new QPushButton(ui(QStringLiteral("Cerca riferimenti")));query->setObjectName(QStringLiteral("xrefSearch"));row->addWidget(query);
    auto status=new QLabel;layout->addWidget(status);
    auto list=new QTreeWidget;list->setObjectName(QStringLiteral("xrefResults"));list->setHeaderLabels({ui(QStringLiteral("File")),ui(QStringLiteral("Riga")),ui(QStringLiteral("Origine")),ui(QStringLiteral("Destinazione")),ui(QStringLiteral("Relazione")),ui(QStringLiteral("Stato"))});list->header()->setSectionResizeMode(QHeaderView::ResizeToContents);layout->addWidget(list,1);
    QProcess process; QByteArray output,errors;
    connect(&process,&QProcess::readyReadStandardOutput,&dialog,[&]{output+=process.readAllStandardOutput();if(output.size()>32*1024*1024) process.kill();});
    connect(&process,&QProcess::readyReadStandardError,&dialog,[&]{errors+=process.readAllStandardError();if(errors.size()>1024*1024)process.kill();});
    QTimer timeout;timeout.setSingleShot(true);connect(&timeout,&QTimer::timeout,&dialog,[&]{process.kill();status->setText(ui(QStringLiteral("Tempo limite superato")));});
    connect(query,&QPushButton::clicked,&dialog,[&]{
        if(process.state()!=QProcess::NotRunning)return;
        output.clear();errors.clear();list->clear();query->setEnabled(false);
        status->setText(ui(QStringLiteral("Ricerca riferimenti…")));
        process.start(engine,{QStringLiteral("xref"),QStringLiteral("--db"),db,QStringLiteral("--subject"),subject->text(),QStringLiteral("--relation"),relation->currentData().toString(),QStringLiteral("--direction"),direction->currentData().toString(),QStringLiteral("--path"),path->text()});timeout.start(15000);
    });
    connect(subject,&QLineEdit::returnPressed,query,&QPushButton::click);
    connect(&process,&QProcess::errorOccurred,&dialog,[&](QProcess::ProcessError e){if(e==QProcess::FailedToStart){timeout.stop();query->setEnabled(true);status->setText(process.errorString());}});
    connect(&process,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),&dialog,[&](int code,QProcess::ExitStatus){
        timeout.stop();query->setEnabled(true);output+=process.readAllStandardOutput();errors+=process.readAllStandardError();
        if(code){status->setText(ui(QStringLiteral("Impossibile leggere i riferimenti. Aggiorna l’indice con F5.")));record(QString::fromUtf8(errors));return;}
        auto data=QJsonDocument::fromJson(output.trimmed()).object();
        for(const auto &value:data.value(QStringLiteral("results")).toArray()){
            auto r=value.toObject();int kind=codes.indexOf(r.value(QStringLiteral("kind")).toString());
            auto item=new QTreeWidgetItem(list,{r.value(QStringLiteral("path")).toString(),QString::number(r.value(QStringLiteral("line")).toInt()),r.value(QStringLiteral("source")).toString(),r.value(QStringLiteral("target")).toString(),labels.value(kind),r.value(QStringLiteral("status")).toString()==QStringLiteral("ready")?ui(QStringLiteral("Sintattico")):ui(QStringLiteral("Indice precedente"))});
            item->setData(0,Qt::UserRole,r);
        }
        status->setText(ui(QStringLiteral("%1 riferimenti")).arg(list->topLevelItemCount())+(data.value(QStringLiteral("truncated")).toBool()?ui(QStringLiteral(" · risultati parziali")):QString()));
    });
    connect(list,&QTreeWidget::itemDoubleClicked,&dialog,[&](QTreeWidgetItem *item,int){auto r=item->data(0,Qt::UserRole).toJsonObject();openFile(QDir(root).filePath(r.value(QStringLiteral("path")).toString()),r.value(QStringLiteral("line")).toInt(),r.value(QStringLiteral("column")).toInt());dialog.accept();});
    auto close=new QDialogButtonBox(QDialogButtonBox::Close);layout->addWidget(close);connect(close,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    if (!initialSubject.isEmpty()) QTimer::singleShot(0,query,&QPushButton::click);
    dialog.exec();timeout.stop();process.disconnect();if(process.state()!=QProcess::NotRunning){process.kill();process.waitForFinished(2000);}
}
