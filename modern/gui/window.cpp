#include "localization.h"
#include "window.h"
#include "panel_icons.h"
#include "storage.h"
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <memory>

static QPushButton *button(const QString &text, const QString &name = {}) {
    auto b = new QPushButton(text);
    b->setObjectName(name);
    return b;
}
static QString readError(const QByteArray &bytes) {
    return QJsonDocument::fromJson(bytes.trimmed())
        .object()
        .value(QStringLiteral("message"))
        .toString(QString::fromUtf8(bytes));
}
Window::Window(const QString &project, QWidget *parent) : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("Source Navigator"));
    resize(1460, 900);
    setMinimumSize(1080, 680);
    auto credit = new QPushButton(QStringLiteral("© 2026 Prof. ing. Raffaele Mele"));
    credit->setObjectName(QStringLiteral("authorCredit"));
    credit->setFlat(true);
    credit->setCursor(Qt::PointingHandCursor);
    credit->setToolTip(QStringLiteral("https://infotechlab.altervista.org/"));
    credit->setAccessibleName(ui(QStringLiteral("© 2026 Prof. ing. Raffaele Mele — apri il sito InfoTechLab")));
    QFont creditFont = menuBar()->font();
    creditFont.setUnderline(true);
    credit->setFont(creditFont);
    QPixmap flag(84, 56);
    flag.setDevicePixelRatio(2);
    flag.fill(Qt::transparent);
    {
        QPainter painter(&flag);
        painter.fillRect(QRectF(1, 1, 40.0 / 3, 26), QColor(QStringLiteral("#009246")));
        painter.fillRect(QRectF(1 + 40.0 / 3, 1, 40.0 / 3, 26), Qt::white);
        painter.fillRect(QRectF(1 + 80.0 / 3, 1, 40.0 / 3, 26), QColor(QStringLiteral("#CE2B37")));
        painter.setPen(QColor(QStringLiteral("#B8BEC6")));
        painter.drawRect(QRectF(0.5, 0.5, 41, 27));
    }
    credit->setIcon(QIcon(flag));
    credit->setIconSize(QSize(33, 22));
    menuBar()->setCornerWidget(credit, Qt::TopRightCorner);
    connect(credit, &QPushButton::clicked, this, [] {
        QDesktopServices::openUrl(QUrl(QStringLiteral("https://infotechlab.altervista.org/")));
    });
    engine = QCoreApplication::applicationDirPath() + QStringLiteral("/sn-index.exe");
    parsers = QCoreApplication::applicationDirPath() + QStringLiteral("/legacy/libexec/snavigator");
    if (!QFileInfo::exists(parsers))
        parsers = QDir(QCoreApplication::applicationDirPath())
                      .absoluteFilePath(QStringLiteral("../../outputs/libexec/snavigator"));
    auto central = new QWidget;
    auto main = new QVBoxLayout(central);
    main->setContentsMargins(16, 12, 16, 8);
    main->setSpacing(12);
    setCentralWidget(central);
    auto hero = new QFrame;
    hero->setObjectName(QStringLiteral("hero"));
    auto head = new QHBoxLayout(hero);
    head->setContentsMargins(20, 16, 20, 16);
    auto titles = new QVBoxLayout;
    auto title = new QLabel(QStringLiteral("SOURCE  /  NAVIGATOR"));
    title->setObjectName(QStringLiteral("brand"));
    projectLabel = new QLabel(
        ui(QStringLiteral("Esplora il codice. Trova ciò che serve. Modifica con controllo.")));
    projectLabel->setObjectName(QStringLiteral("subtitle"));
    projectLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    titles->addWidget(title);
    titles->addWidget(projectLabel);
    head->addLayout(titles, 1);
    auto open = button(ui(QStringLiteral("Apri progetto…")), QStringLiteral("openProject"));
    auto update = button(ui(QStringLiteral("Aggiorna indice")));
    auto cancel = button(ui(QStringLiteral("Interrompi")));
    themes = new QComboBox;
    themes->setObjectName(QStringLiteral("themes"));
    for (const auto &t : Theme::presets())
        themes->addItem(ui(t.name));
    head->addWidget(open);
    head->addWidget(update);
    head->addWidget(cancel);
    head->addWidget(themes);
    main->addWidget(hero);
    connect(open, &QPushButton::clicked, this, [this] {
        auto p = QFileDialog::getExistingDirectory(this, ui(QStringLiteral("Directory radice")), root);
        if (!p.isEmpty())
            openProject(p);
    });
    connect(update, &QPushButton::clicked, this, &Window::indexProject);
    connect(cancel, &QPushButton::clicked, this, &Window::cancelOperation);
    connect(themes, &QComboBox::currentIndexChanged, this, &Window::setTheme);
    auto split = new QSplitter;
    split->setObjectName(QStringLiteral("workspaceSplitter"));
    split->setHandleWidth(7);
    main->addWidget(split, 1);
    auto files = new QFrame;
    files->setObjectName(QStringLiteral("card"));
    auto fl = new QVBoxLayout(files);
    fl->setContentsMargins(10, 12, 10, 8);
    auto ft = new QLabel(ui(QStringLiteral("PROGETTO")));
    ft->setObjectName(QStringLiteral("section"));
    fl->addWidget(ft);
    auto types = button(ui(QStringLiteral("Tipi di file…")), QStringLiteral("fileTypes"));
    fl->addWidget(types);
    connect(types, &QPushButton::clicked, this, [this] { discover(); });
    fileModel = new QFileSystemModel(this);
    fileModel->setReadOnly(true);
    fileModel->setFilter(QDir::AllDirs | QDir::Files | QDir::NoDotAndDotDot);
    tree = new QTreeView;
    tree->setObjectName(QStringLiteral("projectTree"));
    tree->setModel(fileModel);
    tree->setHeaderHidden(true);
    for (int i = 1; i < 4; ++i)
        tree->hideColumn(i);
    fl->addWidget(tree, 1);
    split->addWidget(files);
    connect(tree->selectionModel(), &QItemSelectionModel::currentChanged, this,
            [this](const QModelIndex &i) {
                auto p = fileModel->filePath(i);
                if (QFileInfo(p).isFile())
                    showPreview(p);
            });
    connect(tree, &QTreeView::doubleClicked, this, [this](const QModelIndex &i) {
        auto p = fileModel->filePath(i);
        if (QFileInfo(p).isFile())
            openFile(p);
    });
    auto searchCard = new QFrame;
    searchCard->setObjectName(QStringLiteral("card"));
    auto sl = new QVBoxLayout(searchCard);
    sl->setContentsMargins(14, 12, 14, 10);
    sl->setSpacing(10);
    auto st = new QLabel(ui(QStringLiteral("CERCA & SOSTITUISCI")));
    st->setObjectName(QStringLiteral("section"));
    sl->addWidget(st);
    pattern = new QLineEdit;
    pattern->setObjectName(QStringLiteral("searchPattern"));
    pattern->setPlaceholderText(ui(QStringLiteral("Cerca nel progetto…  Ctrl+Shift+F")));
    pattern->setClearButtonEnabled(true);
    sl->addWidget(pattern);
    auto options = new QHBoxLayout;
    mode = new QComboBox;
    mode->addItem(ui(QStringLiteral("Testo")), QStringLiteral("literal"));
    mode->addItem(QStringLiteral("Wildcard"), QStringLiteral("glob"));
    mode->addItem(QStringLiteral("Regex"), QStringLiteral("regex"));
    mode->setObjectName(QStringLiteral("searchMode"));
    searchScope = new QComboBox;
    searchScope->addItems({ui(QStringLiteral("Nei file")), ui(QStringLiteral("Nei simboli"))});
    searchScope->setObjectName(QStringLiteral("searchScope"));
    caseSensitive = new QCheckBox(QStringLiteral("Aa"));
    caseSensitive->setToolTip(ui(QStringLiteral("Distingui maiuscole e minuscole")));
    caseSensitive->setObjectName(QStringLiteral("caseSensitive"));
    wholeWords = new QCheckBox(ui(QStringLiteral("Parole intere")));
    wholeWords->setObjectName(QStringLiteral("wholeWords"));
    wholeWords->setToolTip(ui(QStringLiteral("Confini di parola Unicode; lettere, cifre e underscore appartengono alla parola. Disponibile anche con wildcard e regex.")));
    options->addWidget(mode);
    options->addWidget(searchScope, 1);
    options->addWidget(caseSensitive);
    options->addWidget(wholeWords);
    sl->addLayout(options);
    pathFilter = new QLineEdit;
    pathFilter->setObjectName(QStringLiteral("pathFilter"));
    pathFilter->setPlaceholderText(ui(QStringLiteral("Filtra percorso · es. src/*.rs")));
    pathFilter->setClearButtonEnabled(true);
    sl->addWidget(pathFilter);
    replacement = new QLineEdit;
    replacement->setObjectName(QStringLiteral("replacement"));
    replacement->setPlaceholderText(ui(QStringLiteral("Sostituisci con…")));
    replacement->setToolTip(ui(QStringLiteral(
        "Regex: $1 o ${nome} per un gruppo; $$ per $. Testo vuoto: elimina le corrispondenze.")));
    sl->addWidget(replacement);
    replaceButton =
        button(ui(QStringLiteral("Anteprima sostituzione")), QStringLiteral("replacePreview"));
    sl->addWidget(replaceButton);
    connect(replaceButton, &QPushButton::clicked, this, &Window::planReplacement);
    auto selection = new QHBoxLayout;
    auto all = button(ui(QStringLiteral("Seleziona tutti")));
    auto none = button(ui(QStringLiteral("Nessuno")));
    selection->addWidget(all);
    selection->addWidget(none);
    sl->addLayout(selection);
    resultLabel = new QLabel(ui(QStringLiteral("I risultati appariranno qui")));
    resultLabel->setWordWrap(true);
    resultLabel->setObjectName(QStringLiteral("subtitle"));
    sl->addWidget(resultLabel);
    results = new QTreeWidget;
    results->setObjectName(QStringLiteral("searchResults"));
    results->setHeaderHidden(true);
    results->setUniformRowHeights(true);
    results->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    sl->addWidget(results, 1);
    for (auto pair : {qMakePair(all, Qt::Checked), qMakePair(none, Qt::Unchecked)})
        connect(pair.first, &QPushButton::clicked, this, [this, pair] {
            for (int i = 0; i < results->topLevelItemCount(); ++i)
                results->topLevelItem(i)->setCheckState(0, pair.second);
        });
    connect(results, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem *item) {
        if (!item)
            return;
        auto o = item->data(0, Qt::UserRole).toJsonObject();
        showPreview(QDir(root).filePath(o.value(QStringLiteral("path")).toString()),
                    o.value(QStringLiteral("line")).toInt(1),
                    o.value(QStringLiteral("column")).toInt());
    });
    connect(results, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *item) {
        auto o = item->data(0, Qt::UserRole).toJsonObject();
        openFile(QDir(root).filePath(o.value(QStringLiteral("path")).toString()),
                 o.value(QStringLiteral("line")).toInt(1),
                 o.value(QStringLiteral("column")).toInt());
    });
    split->addWidget(searchCard);
    auto content = new QFrame;
    content->setObjectName(QStringLiteral("card"));
    auto cl = new QVBoxLayout(content);
    cl->setContentsMargins(10, 10, 10, 8);
    auto actions = new QHBoxLayout;
    previewLabel = new QLabel(ui(QStringLiteral("Seleziona un file per l'anteprima")));
    previewLabel->setObjectName(QStringLiteral("subtitle"));
    previewLabel->setMinimumWidth(0);
    previewLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    actions->addWidget(previewLabel, 1);
    auto edit = button(ui(QStringLiteral("Modifica")));
    auto external = button(ui(QStringLiteral("Editor esterno ↗")), QStringLiteral("externalEditor"));
    auto save = button(ui(QStringLiteral("Salva")));
    actions->addWidget(edit);
    actions->addWidget(external);
    actions->addWidget(save);
    cl->addLayout(actions);
    connect(edit, &QPushButton::clicked, this, [this] {
        if (!selectedPath.isEmpty())
            openFile(selectedPath, selectedLine);
    });
    connect(external, &QPushButton::clicked, this, &Window::openExternal);
    connect(save, &QPushButton::clicked, this,
            [this] { saveEditor(qobject_cast<Editor *>(tabs->currentWidget())); });
    tabs = new QTabWidget;
    tabs->setObjectName(QStringLiteral("editorTabs"));
    tabs->setTabsClosable(true);
    tabs->setDocumentMode(true);
    preview = new Editor;
    connect(preview, &Editor::crossReferenceRequested, this, &Window::showCrossReferences);
    preview->setObjectName(QStringLiteral("previewEditor"));
    preview->send(SCI_SETREADONLY, 1);
    tabs->addTab(preview, ui(QStringLiteral("Anteprima")));
    tabs->tabBar()->setTabButton(0, QTabBar::RightSide, nullptr);
    cl->addWidget(tabs, 1);
    connect(tabs, &QTabWidget::tabCloseRequested, this, &Window::closeTab);
    connect(tabs, &QTabWidget::currentChanged, this, [this](int) {
        auto e = qobject_cast<Editor *>(tabs->currentWidget());
        if (e && !e->path().isEmpty()) {
            selectedPath = e->path();
            selectedLine = 1;
            previewLabel->setText(QDir(root).relativeFilePath(selectedPath));
        }
    });
    auto hint = new QLabel(
        ui(QStringLiteral("Un clic: anteprima   ·   Doppio clic: modifica   ·   Ctrl+S: salva")));
    hint->setObjectName(QStringLiteral("subtitle"));
    cl->addWidget(hint);
    split->addWidget(content);
    split->setSizes({220, 400, 790});
    split->setStretchFactor(2, 1);
    split->setChildrenCollapsible(false);
    setupFileMenu();
    auto view = menuBar()->addMenu(ui(QStringLiteral("Visualizza")));
    auto projectPanel = view->addAction(ui(QStringLiteral("Pannello progetto")));
    auto searchPanel = view->addAction(ui(QStringLiteral("Pannello ricerca")));
    for (auto action : {projectPanel, searchPanel}) {
        action->setCheckable(true);
        action->setChecked(true);
    }
    connect(projectPanel, &QAction::toggled, files, &QWidget::setVisible);
    connect(searchPanel, &QAction::toggled, searchCard, &QWidget::setVisible);
    auto focusEditor = view->addAction(ui(QStringLiteral("Espandi editor")));
    focusEditor->setObjectName(QStringLiteral("focusEditor"));
    focusEditor->setCheckable(true);
    focusEditor->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+E")));
    auto expand = button({}, QStringLiteral("expandEditor"));
    setPanelIcon(expand,QStringLiteral("maximize"));

    expand->setToolTip(ui(QStringLiteral("Espandi/ripristina editor · Ctrl+Shift+E")));
    actions->addWidget(expand);
    connect(expand, &QPushButton::clicked, focusEditor, &QAction::toggle);
    auto maximizedPanel = std::make_shared<QToolButton *>(nullptr);
    auto savedPanels = std::make_shared<QPair<bool, bool>>(true, true);
    connect(focusEditor, &QAction::toggled, this, [=](bool focused) {
        if (focused && *maximizedPanel) (*maximizedPanel)->setChecked(false);
        if (focused) *savedPanels = {projectPanel->isChecked(), searchPanel->isChecked()};
        projectPanel->setChecked(focused ? false : savedPanels->first);
        searchPanel->setChecked(focused ? false : savedPanels->second);
        projectPanel->setEnabled(!focused);
        searchPanel->setEnabled(!focused);
        hero->setVisible(!focused);
        setPanelIcon(expand,focused ? QStringLiteral("restore") : QStringLiteral("maximize"));
    });
    auto panelControls = [this, split, focusEditor, maximizedPanel](QVBoxLayout *layout, QWidget *panel, QAction *visibility) {
        auto row = new QHBoxLayout;
        auto item = layout->takeAt(0);
        row->addWidget(item->widget(), 1);
        delete item;
        auto maximize = new QToolButton;
        setPanelIcon(maximize,QStringLiteral("maximize"));
        maximize->setToolTip(ui(QStringLiteral("Espandi / ripristina pannello")));
        maximize->setCheckable(true);
        auto saved = std::make_shared<QList<int>>();
        connect(maximize, &QToolButton::toggled, this, [=](bool checked) {
            if (focusEditor->isChecked()) focusEditor->setChecked(false);
            if (checked) {
                if (*maximizedPanel && *maximizedPanel != maximize) (*maximizedPanel)->setChecked(false);
                *maximizedPanel = maximize;
                *saved = split->sizes();
                QList<int> sizes;
                for (int i=0;i<split->count();++i) sizes << (split->widget(i)==panel ? 10000 : 0);
                split->setChildrenCollapsible(true);
                split->setSizes(sizes);
            } else {
                split->setSizes(*saved);
                split->setChildrenCollapsible(false);
                *maximizedPanel = nullptr;
            }
            setPanelIcon(maximize,checked ? QStringLiteral("restore") : QStringLiteral("maximize"));
        });
        auto minimize = new QToolButton;
        setPanelIcon(minimize,QStringLiteral("minimize"));
        minimize->setToolTip(ui(QStringLiteral("Nascondi pannello · riapri dal menù Visualizza")));
        connect(minimize, &QToolButton::clicked, this, [=] { if (maximize->isChecked()) maximize->setChecked(false); visibility->setChecked(false); });
        row->addWidget(minimize); row->addWidget(maximize); layout->insertLayout(0,row);
    };
    panelControls(fl, files, projectPanel);
    panelControls(sl, searchCard, searchPanel);
    auto contentVisible = view->addAction(ui(QStringLiteral("Pannello editor")));
    contentVisible->setCheckable(true); contentVisible->setChecked(true);
    connect(contentVisible, &QAction::toggled, content, &QWidget::setVisible);
    connect(focusEditor,&QAction::toggled,this,[=](bool checked){if(checked)contentVisible->setChecked(true);});
    auto minimizeEditor = new QToolButton;
    setPanelIcon(minimizeEditor,QStringLiteral("minimize"));
    minimizeEditor->setToolTip(ui(QStringLiteral("Nascondi pannello · riapri dal menù Visualizza")));
    actions->insertWidget(actions->count()-1, minimizeEditor);
    connect(minimizeEditor, &QToolButton::clicked, this, [=] { focusEditor->setChecked(false); contentVisible->setChecked(false); });
    auto settings = menuBar()->addMenu(ui(QStringLiteral("Preferenze")));
    connect(settings->addAction(ui(QStringLiteral("Editor esterno…"))), &QAction::triggered, this,
            &Window::configureExternal);
    connect(settings->addAction(ui(QStringLiteral("Personalizza colori…"))), &QAction::triggered, this,
            &Window::customizeTheme);
    connect(settings->addAction(ui(QStringLiteral("Esporta tema…"))), &QAction::triggered, this,
            &Window::exportTheme);
    connect(settings->addAction(ui(QStringLiteral("Importa tema…"))), &QAction::triggered, this,
            &Window::importTheme);
    auto night = settings->addAction(ui(QStringLiteral("Giorno / notte")));
    night->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+L")));
    connect(night, &QAction::triggered, this,
            [this] { setTheme(theme.background.lightness() < 128 ? 0 : 3); });
    auto tools = menuBar()->addMenu(ui(QStringLiteral("Strumenti")));
    connect(tools->addAction(ui(QStringLiteral("Ripristina sostituzione…"))), &QAction::triggered, this,
            &Window::recoverReplacement);
    connect(tools->addAction(ui(QStringLiteral("Ricarica file dal disco"))), &QAction::triggered, this,
            [this] {
                auto e = qobject_cast<Editor *>(tabs->currentWidget());
                if (!e || e->path().isEmpty())
                    return;
                if (e->dirty() &&
                    QMessageBox::question(this, ui(QStringLiteral("Ricarica")),
                                          ui(QStringLiteral("Scartare le modifiche non salvate?"))) !=
                        QMessageBox::Yes)
                    return;
                QString error;
                if (e->load(e->path(), error)) {
                    e->applyTheme(theme);
                    if (e == preview)
                        e->send(SCI_SETREADONLY, 1);
                } else
                    QMessageBox::warning(this, ui(QStringLiteral("Ricarica")), error);
            });
    diagnostics = new QPlainTextEdit;
    diagnostics->setReadOnly(true);
    diagnostics->setMaximumBlockCount(2000);
    auto dock = new QDockWidget(ui(QStringLiteral("Diagnostica")), this);
    dock->setWidget(diagnostics);
    addDockWidget(Qt::BottomDockWidgetArea, dock);
    dock->hide();
    tools->addAction(dock->toggleViewAction());
    connect(
        tools->addAction(ui(QStringLiteral("Apri cartella dei log"))), &QAction::triggered, this, [] {
            QDesktopServices::openUrl(QUrl::fromLocalFile(storageRoot() + QStringLiteral("/logs")));
        });
    connect(tools->addAction(ui(QStringLiteral("Apri log del progetto"))), &QAction::triggered, this,
            [this] {
                if (!db.isEmpty())
                    QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(db).absolutePath() +
                                                                  QStringLiteral("/logs")));
            });
    auto cross = tools->addAction(ui(QStringLiteral("Riferimenti incrociati…")));
    cross->setObjectName(QStringLiteral("crossReferences"));
    cross->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+R")));
    connect(cross, &QAction::triggered, this, [this] { showCrossReferences(); });
    auto reopen = settings->addAction(ui(QStringLiteral("Riapri ultimo progetto all’avvio")));
    reopen->setObjectName(QStringLiteral("reopenLastProject"));
    reopen->setCheckable(true);
    reopen->setChecked(QSettings().value(QStringLiteral("session/reopen"), true).toBool());
    connect(reopen, &QAction::toggled, this, [](bool value) { QSettings().setValue(QStringLiteral("session/reopen"), value); });
    auto languages = settings->addMenu(ui(QStringLiteral("Lingua")));
    auto group = new QActionGroup(this);
    const QStringList codes = {QStringLiteral("it"), QStringLiteral("en"), QStringLiteral("fr"), QStringLiteral("de")};
    const QStringList names = {QStringLiteral("Italiano"), QStringLiteral("English"), QStringLiteral("Français"), QStringLiteral("Deutsch")};
    for (int i = 0; i < codes.size(); ++i) {
        auto action = languages->addAction(names[i]);
        action->setCheckable(true);
        action->setChecked(QSettings().value(QStringLiteral("language"), QStringLiteral("it")).toString() == codes[i]);
        group->addAction(action);
        connect(action, &QAction::triggered, this, [this, code=codes[i]] {
            QSettings().setValue(QStringLiteral("language"), code);
            QMessageBox::information(this, ui(QStringLiteral("Lingua")), ui(QStringLiteral("La lingua selezionata sarà applicata al prossimo avvio.")));
        });
    }
    auto help = menuBar()->addMenu(ui(QStringLiteral("Guida")));
    connect(
        help->addAction(ui(QStringLiteral("Sintassi di ricerca"))), &QAction::triggered, this, [this] {
            QMessageBox::information(
                this, ui(QStringLiteral("Ricerca e sostituzione")),
                ui(QStringLiteral(
                    "Testo: sequenza letterale.\nWildcard: * indica zero o più caratteri, ? un "
                    "carattere, nella singola riga.\nRegex: gruppi e alternative; niente "
                    "lookaround o backreference nel pattern. Ricerca per riga.\nPercorso: wildcard "
                    "sull'intero percorso relativo, / separa le cartelle.\nSostituzione regex: $1, "
                    "${nome}; $$ inserisce $.\nLe checkbox selezionano i file: verranno sostituite "
                    "tutte le loro corrispondenze.\nCodifiche: UTF-8, UTF-16 con BOM e "
                    "Windows-1252; massimo 32 MiB.")));
        });
    connect(help->addAction(ui(QStringLiteral("Informazioni su…"))), &QAction::triggered, this, [this] {
        QMessageBox::about(this, ui(QStringLiteral("Informazioni su Source Navigator")),
            ui(QStringLiteral("Source Navigator %1<br>© 2026 Prof. ing. Raffaele Mele<br><a href='https://infotechlab.altervista.org/'>InfoTechLab</a><br>Qt 6 · Rust · SQLite · Scintilla / Lexilla<br>Parser inclusi nella distribuzione.<br>Licenze: cartella licenses della distribuzione.")).arg(QCoreApplication::applicationVersion()));
    });
    state = new QLabel(ui(QStringLiteral("Scegli una directory radice per iniziare")));
    progress = new QProgressBar;
    progress->setMaximumWidth(180);
    progress->hide();
    statusBar()->addWidget(state, 1);
    statusBar()->addPermanentWidget(progress);
    auto shortcut = [this](const QString &key, std::function<void()> callback) {
        auto s = new QShortcut(QKeySequence(key), this);
        connect(s, &QShortcut::activated, this, callback);
    };
    shortcut(QStringLiteral("Ctrl+Shift+F"), [this] {
        pattern->setFocus();
        pattern->selectAll();
    });

    shortcut(QStringLiteral("F5"), [this] { indexProject(); });
    shortcut(QStringLiteral("Ctrl+F"), [this] {
        auto e = qobject_cast<Editor *>(tabs->currentWidget());
        if (!e)
            return;
        bool ok;
        auto q = QInputDialog::getText(this, ui(QStringLiteral("Trova nel file")),
                                       ui(QStringLiteral("Testo")), QLineEdit::Normal, {}, &ok);
        if (ok && !q.isEmpty()) {
            e->send(SCI_SEARCHANCHOR);
            auto b = q.toUtf8();
            if (e->sends(SCI_SEARCHNEXT, 0, b.constData()) < 0) {
                e->send(SCI_GOTOPOS, 0);
                e->send(SCI_SEARCHANCHOR);
                e->sends(SCI_SEARCHNEXT, 0, b.constData());
            }
            e->send(SCI_SCROLLCARET);
        }
    });
    searchTimer.setSingleShot(true);
    searchTimer.setInterval(160);
    connect(&searchTimer, &QTimer::timeout, this, &Window::search);
    for (auto e : {pattern, pathFilter})
        connect(e, &QLineEdit::textChanged, this, [this] {
            results->clear();
            currentResults = {};
            replaceButton->setEnabled(false);
            searchTimer.start();
        });
    for (auto c : {mode, searchScope})
        connect(c, &QComboBox::currentIndexChanged, this, [this] {
            results->clear();
            currentResults = {};
            replacement->setEnabled(searchScope->currentIndex() == 0);
            replaceButton->setEnabled(false);
            searchTimer.start();
        });
    for (auto check : {caseSensitive, wholeWords})
    connect(check, &QCheckBox::toggled, this, [this] {
        results->clear();
        currentResults = {};
        replaceButton->setEnabled(false);
        searchTimer.start();
    });
    draftTimer.setInterval(10000);
    connect(&draftTimer, &QTimer::timeout, this, [this] {
        for (int i = 1; i < tabs->count(); ++i)
            qobject_cast<Editor *>(tabs->widget(i))->saveDraft();
    });
    draftTimer.start();
    auto custom =
        QJsonDocument::fromJson(QSettings().value(QStringLiteral("theme-v2")).toByteArray())
            .object();
    if (!custom.isEmpty()) {
        auto t = Theme::fromJson(custom);
        if (t.validate().isEmpty())
            theme = t;
    }
    for (auto combo : {themes, mode, searchScope})
        combo->setView(new QListView(combo));
    applyTheme(theme);
    if (!project.isEmpty())
        openProject(project);
}
void Window::record(const QString &s) {
    diagnostics->appendPlainText(s);
    qInfo().noquote() << s;
}
void Window::run(const QStringList &args, std::function<void(const QJsonObject &)> done) {
    if (operation)
        return;
    auto p = new QProcess(this);
    operation = p;
    tabs->setEnabled(false);
    replaceButton->setEnabled(false);
    progress->setRange(0, 0);
    progress->show();
    auto bytes = std::make_shared<QByteArray>();
    auto errors = std::make_shared<QByteArray>();
    connect(p, &QProcess::readyReadStandardOutput, this, [this, p, bytes] {
        *bytes += p->readAllStandardOutput();
        if (bytes->size() > 64 * 1024 * 1024) {
            p->kill();
            record(ui(QStringLiteral("Risposta motore oltre il limite")));
        }
    });
    connect(p, &QProcess::readyReadStandardError, this, [p, errors] {
        *errors += p->readAllStandardError();
        if (errors->size() > 1024 * 1024)
            p->kill();
    });
    connect(p, &QProcess::errorOccurred, this, [this, p](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            record(p->errorString());
            state->setText(p->errorString());
            operation = nullptr;
            tabs->setEnabled(true);
            progress->hide();
            replaceButton->setEnabled(searchScope->currentIndex() == 0);
            p->deleteLater();
        }
    });
    connect(p, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this, p, bytes, errors, done](int code, QProcess::ExitStatus status) {
                *bytes += p->readAllStandardOutput();
                *errors += p->readAllStandardError();
                operation = nullptr;
                tabs->setEnabled(true);
                progress->hide();
                replaceButton->setEnabled(searchScope->currentIndex() == 0);
                p->deleteLater();
                if (p->property("userCanceled").toBool()) {
                    record(QStringLiteral("operation_canceled"));
                    state->setText(ui(QStringLiteral("Operazione annullata")));
                    return;
                }
                if (code || status == QProcess::CrashExit) {
                    record(QStringLiteral("engine_failed operation=%1 exit=%2 status=%3")
                               .arg(p->arguments().value(0))
                               .arg(code)
                               .arg(int(status)));
                    auto message = readError(*errors);
                    if (message.isEmpty())
                        message = ui(QStringLiteral(
                            "Motore interrotto. Indice precedente e copie di recupero conservati."));
                    record(message);
                    const auto summary = uiLanguage()==QStringLiteral("it") ? message : ui(QStringLiteral("Operazione non completata. Consulta i dettagli diagnostici."));
                    state->setText(summary);
                    QMessageBox box(QMessageBox::Warning,ui(QStringLiteral("Operazione non completata")),summary,QMessageBox::Ok,this);
                    box.setDetailedText(message);box.exec();
                    return;
                }
                QJsonObject last;
                for (const auto &line : bytes->split('\n')) {
                    auto o = QJsonDocument::fromJson(line).object();
                    if (!o.isEmpty())
                        last = o;
                }
                record(QStringLiteral("engine_completed %1")
                           .arg(last.value(QStringLiteral("event")).toString()));
                done(last);
            });
    record(QStringLiteral("engine_start %1").arg(args.value(0)));
    p->start(engine, args);
}
void Window::openProject(const QString &p) {
    if (operation || !QFileInfo(p).isDir())
        return;
    while (tabs->count() > 1)
        if (!closeTab(tabs->count() - 1))
            return;
    if (queryProcess) {
        queryProcess->disconnect(this);
        queryProcess->kill();
        queryProcess->deleteLater();
        queryProcess = nullptr;
    }
    root = QFileInfo(p).canonicalFilePath();
    QSettings().setValue(QStringLiteral("session/lastProject"), root);
    auto id = QString::fromLatin1(
        QCryptographicHash::hash(root.toUtf8(), QCryptographicHash::Sha256).toHex());
    auto base = storageRoot() + QStringLiteral("/projects/") + id;
    QDir().mkpath(base);
    db = base + QStringLiteral("/index.sqlite");
    cancelFile = base + QStringLiteral("/cancel");
    tree->setRootIndex(fileModel->setRootPath(root));
    projectLabel->setText(QFileInfo(root).fileName() + QStringLiteral("   /   ") + root);
    projectLabel->setToolTip(root);
    setWindowTitle(QFileInfo(root).fileName() + QStringLiteral(" — Source Navigator"));
    results->clear();
    currentResults = {};
    selectedPath.clear();
    preview->send(SCI_SETREADONLY, 0);
    preview->sends(SCI_SETTEXT, 0, "");
    preview->send(SCI_SETREADONLY, 1);
    previewLabel->setText(ui(QStringLiteral("Seleziona un file per l'anteprima")));
    QSettings settings(base + QStringLiteral("/project.ini"), QSettings::IniFormat);
    discoveryCache = {};
    QFile cachedDiscovery(base + QStringLiteral("/discovery.json"));
    if (cachedDiscovery.open(QIODevice::ReadOnly))
        discoveryCache = QJsonDocument::fromJson(cachedDiscovery.readAll()).object();
    extensions = settings.value(QStringLiteral("extensions")).toStringList();
    if (extensions.isEmpty())
        discover();
    else {
        state->setText(ui(QStringLiteral("Progetto aperto · F5 aggiorna l'indice")));
        search();
    }
    QDir changes(base + QStringLiteral("/changes"));
    for (const auto &folder : changes.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        QFile f(changes.filePath(folder + QStringLiteral("/journal.json")));
        if (f.open(QIODevice::ReadOnly)) {
            auto s = QJsonDocument::fromJson(f.readAll())
                         .object()
                         .value(QStringLiteral("state"))
                         .toString();
            if (s == QStringLiteral("applying") || s == QStringLiteral("restoring"))
                state->setText(
                    ui(QStringLiteral("Sostituzione interrotta: Strumenti → Ripristina sostituzione")));
        }
    }
}
void Window::discover(bool refresh) {
    if (root.isEmpty() || operation)
        return;
    if (!refresh && discoveryCache.contains(QStringLiteral("extensions"))) {
        chooseFileTypes(discoveryCache);
        return;
    }
    state->setText(ui(QStringLiteral("Analisi dei tipi di file…")));
    run({QStringLiteral("discover"), QStringLiteral("--root"), root}, [this](const QJsonObject &o) {
        discoveryCache = o;
        QSaveFile cache(QFileInfo(db).absolutePath() + QStringLiteral("/discovery.json"));
        const auto bytes = QJsonDocument(o).toJson();
        if (cache.open(QIODevice::WriteOnly) && cache.write(bytes) == bytes.size())
            cache.commit();
        chooseFileTypes(o);
    });
}
void Window::chooseFileTypes(const QJsonObject &o) {
        QDialog d(this);
        d.setWindowTitle(ui(QStringLiteral("File da includere")));
        d.resize(540, 570);
        auto layout = new QVBoxLayout(&d);
        auto info =
            new QLabel(ui(QStringLiteral("Seleziona le estensioni da indicizzare.\nI file testuali "
                                      "sono ricercabili anche senza parser di simboli.")));
        info->setWordWrap(true);
        layout->addWidget(info);
        auto list = new QTreeWidget;
        list->setHeaderLabels(
            {ui(QStringLiteral("Estensione")), ui(QStringLiteral("File")), QStringLiteral("KiB")});
        layout->addWidget(list, 1);
        for (const auto &v : o.value(QStringLiteral("extensions")).toArray()) {
            auto e = v.toObject();
            auto ext = e.value(QStringLiteral("extension")).toString();
            auto row = new QTreeWidgetItem(
                list,
                {ext, QString::number(e.value(QStringLiteral("files")).toInt()),
                 QString::number(e.value(QStringLiteral("bytes")).toDouble() / 1024, 'f', 0)});
            row->setData(0, Qt::UserRole, ext);
            row->setCheckState(0,
                               (extensions.isEmpty() ? e.value(QStringLiteral("selected")).toBool()
                                                     : extensions.contains(ext))
                                   ? Qt::Checked
                                   : Qt::Unchecked);
        }
        auto summary = new QLabel(
            ui(QStringLiteral(
                "%1 file esclusi (binari, codifica, dimensione o accesso). %2 errori di scansione."))
                .arg(o.value(QStringLiteral("skipped")).toInt())
                .arg(o.value(QStringLiteral("errors")).toArray().size()));
        summary->setWordWrap(true);
        layout->addWidget(summary);
        auto buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        buttons->button(QDialogButtonBox::Ok)->setText(ui(QStringLiteral("Applica selezione")));
        auto refresh = buttons->addButton(ui(QStringLiteral("Rianalizza cartelle")), QDialogButtonBox::ActionRole);
        connect(refresh, &QPushButton::clicked, &d, [&d] { d.done(2); });
        layout->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::accepted, &d, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &d, &QDialog::reject);
        const int result = d.exec();
        if (result == 2) { discover(true); return; }
        if (result != QDialog::Accepted)
            return;
        QStringList chosen;
        for (int i = 0; i < list->topLevelItemCount(); ++i) {
            auto row = list->topLevelItem(i);
            if (row->checkState(0) == Qt::Checked)
                chosen << row->data(0, Qt::UserRole).toString();
        }
        if (chosen.isEmpty()) {
            state->setText(ui(QStringLiteral("Nessuna estensione selezionata")));
            return;
        }
        if (chosen == extensions)
            return;
        extensions = chosen;
        QSettings(QFileInfo(db).absolutePath() + QStringLiteral("/project.ini"),
                  QSettings::IniFormat)
            .setValue(QStringLiteral("extensions"), extensions);
        indexProject();
}
void Window::indexProject() {
    if (root.isEmpty() || operation)
        return;
    if (extensions.isEmpty()) {
        discover();
        return;
    }
    QFile::remove(cancelFile);
    state->setText(ui(QStringLiteral("Indicizzazione in corso…")));
    run({QStringLiteral("index"), QStringLiteral("--root"), root, QStringLiteral("--db"), db,
         QStringLiteral("--parsers"), parsers, QStringLiteral("--extensions"),
         extensions.join(QLatin1Char(',')), QStringLiteral("--cancel-file"), cancelFile},
        [this](const QJsonObject &o) {
            state->setText(
                o.value(QStringLiteral("event")) == QStringLiteral("canceled")
                    ? ui(QStringLiteral("Annullato: indice precedente conservato"))
                    : ui(QStringLiteral("Indice aggiornato · %1 file · %2 invariati · %3 errori"))
                          .arg(o.value(QStringLiteral("files")).toInt())
                          .arg(o.value(QStringLiteral("unchanged")).toInt())
                          .arg(o.value(QStringLiteral("errors")).toInt()));
            search();
        });
}
void Window::cancelOperation() {
    if (!operation)
        return;
    if (operation->arguments().value(0) == QStringLiteral("index")) {
        QFile f(cancelFile);
        if (f.open(QIODevice::WriteOnly))
            f.write("cancel");
    } else if (operation->arguments().value(0).startsWith(QStringLiteral("replace-")))
        state->setText(ui(QStringLiteral(
            "Attendi il completamento della scrittura; copie di recupero conservate.")));
    else {
        operation->setProperty("userCanceled", true);
        operation->kill();
    }
}
void Window::search() {
    searchTimer.stop();
    if (queryProcess) {
        queryProcess->disconnect(this);
        queryProcess->kill();
        queryProcess->deleteLater();
        queryProcess = nullptr;
    }
    if (db.isEmpty() || !QFileInfo::exists(db) || pattern->text().isEmpty()) {
        results->clear();
        currentResults = {};
        resultLabel->setText(ui(QStringLiteral("Inserisci il testo da cercare")));
        return;
    }
    auto p = new QProcess(this);
    queryProcess = p;
    auto started = std::make_shared<QElapsedTimer>();
    started->start();
    connect(p, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this, p, started](int code, QProcess::ExitStatus status) {
                if (code || status == QProcess::CrashExit) {
                    auto error = readError(p->readAllStandardError());
                    resultLabel->setText(uiLanguage()==QStringLiteral("it") ? error : ui(QStringLiteral("Operazione non completata. Consulta i dettagli diagnostici.")));
                    results->clear();
                    currentResults = {};
                    record(error);
                } else {
                    auto obj = QJsonDocument::fromJson(p->readAllStandardOutput()).object();
                    obj.insert(QStringLiteral("ui_ms"), started->elapsed());
                    showResults(obj);
                }
                if (queryProcess == p)
                    queryProcess = nullptr;
                p->deleteLater();
            });
    connect(p, &QProcess::errorOccurred, this, [this, p](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            resultLabel->setText(p->errorString());
            queryProcess = nullptr;
            p->deleteLater();
        }
    });
    QStringList args = {searchScope->currentIndex() == 0 ? QStringLiteral("grep")
                                                         : QStringLiteral("query"),
                        QStringLiteral("--db"),
                        db,
                        QStringLiteral("--pattern"),
                        pattern->text(),
                        QStringLiteral("--mode"),
                        mode->currentData().toString(),
                        QStringLiteral("--path"),
                        pathFilter->text(),
                        QStringLiteral("--limit"),
                        QStringLiteral("5000")};
    if (wholeWords->isChecked())
        args << QStringLiteral("--whole-words");
    if (caseSensitive->isChecked())
        args << QStringLiteral("--case-sensitive");
    p->start(engine, args);
}
void Window::showResults(const QJsonObject &o) {
    replaceButton->setEnabled(!operation && searchScope->currentIndex() == 0);
    currentResults = o.value(QStringLiteral("results")).toArray();
    results->clear();
    QMap<QString, QTreeWidgetItem *> files;
    for (const auto &v : currentResults) {
        auto hit = v.toObject();
        auto path = hit.value(QStringLiteral("path")).toString();
        auto parent = files.value(path);
        if (!parent) {
            parent = new QTreeWidgetItem(results, {path});
            parent->setData(0, Qt::UserRole, hit);
            parent->setToolTip(0, path);
            if (searchScope->currentIndex() == 0)
                parent->setCheckState(0, Qt::Unchecked);
            QFont font = parent->font(0);
            font.setBold(true);
            parent->setFont(0, font);
            files.insert(path, parent);
        }
        auto item = new QTreeWidgetItem(
            parent, {QStringLiteral("%1   %2")
                         .arg(hit.value(QStringLiteral("line")).toInt())
                         .arg(hit.value(QStringLiteral("qualified")).toString().trimmed())});
        item->setData(0, Qt::UserRole, hit);
        item->setToolTip(0, item->text(0));
    }
    for (auto i = files.begin(); i != files.end(); ++i) {
        i.value()->setText(0,
                           QStringLiteral("%1  ·  %2").arg(i.key()).arg(i.value()->childCount()));
        if (files.size() < 25)
            i.value()->setExpanded(true);
    }
    resultLabel->setText(ui(QStringLiteral("%1 corrispondenze · %2 file · %3 ms%4%5"))
                             .arg(currentResults.size())
                             .arg(files.size())
                             .arg(o.value(QStringLiteral("ui_ms")).toInt())
                             .arg(o.value(QStringLiteral("truncated")).toBool()
                                      ? ui(QStringLiteral("\nRisultati parziali: affina la ricerca."))
                                      : QString())
                             .arg(o.value(QStringLiteral("skipped")).toInt()
                                      ? ui(QStringLiteral("\n%1 file non letti; consulta Diagnostica."))
                                            .arg(o.value(QStringLiteral("skipped")).toInt())
                                      : QString()));
    for (const auto &error : o.value(QStringLiteral("errors")).toArray())
        record(QString::fromUtf8(QJsonDocument(error.toObject()).toJson(QJsonDocument::Compact)));
}
void Window::showPreview(const QString &p, int line, int column) {
    QPointer<QWidget> previousFocus = QApplication::focusWidget();
    QString error;
    if (!preview->load(p, error)) {
        state->setText(error);
        return;
    }
    selectedPath = preview->path();
    selectedLine = line;
    preview->applyTheme(theme);
    preview->send(SCI_SETREADONLY, 1);
    preview->send(SCI_SETINDICATORCURRENT, 8);
    preview->send(SCI_INDICSETSTYLE, 8, INDIC_ROUNDBOX);
    preview->send(SCI_INDICSETFORE, 8,
                  theme.accent.red() | (theme.accent.green() << 8) | (theme.accent.blue() << 16));
    preview->send(SCI_INDICSETALPHA, 8, 45);
    const auto relative = QDir(root).relativeFilePath(p);
    for (const auto &v : currentResults) {
        auto hit = v.toObject();
        if (hit.value(QStringLiteral("path")).toString() != relative)
            continue;
        auto start =
            preview->send(SCI_POSITIONFROMLINE, hit.value(QStringLiteral("line")).toInt() - 1) +
            hit.value(QStringLiteral("column")).toInt();
        preview->send(SCI_INDICATORFILLRANGE, start, hit.value(QStringLiteral("length")).toInt(1));
    }
    tabs->setCurrentIndex(0);
    selectedLine = line;
    previewLabel->setText(relative);
    previewLabel->setToolTip(p);
    preview->goTo(line, column);
    if (previousFocus)
        previousFocus->setFocus();
    if (!error.isEmpty())
        state->setText(error);
}
void Window::openFile(const QString &p, int line, int column) {
    const auto canonical = QFileInfo(p).canonicalFilePath();
    for (int i = 1; i < tabs->count(); ++i) {
        auto e = qobject_cast<Editor *>(tabs->widget(i));
        if (e->path() == canonical) {
            tabs->setCurrentIndex(i);
            e->goTo(line, column);
            return;
        }
    }
    auto e = new Editor;
    connect(e, &Editor::crossReferenceRequested, this, &Window::showCrossReferences);
    QString error;
    if (!e->load(p, error)) {
        delete e;
        QMessageBox::warning(this, ui(QStringLiteral("Apertura file")), error);
        return;
    }
    tabs->addTab(e, QFileInfo(p).fileName());
    tabs->setCurrentWidget(e);
    e->applyTheme(theme);
    e->goTo(line, column);
    connect(e, &Editor::draftFailed, this, &Window::record);
    connect(e, &ScintillaEditBase::savePointChanged, this, [this, e](bool dirty) {
        int i = tabs->indexOf(e);
        if (i >= 0)
            tabs->setTabText(i, QFileInfo(e->path()).fileName() +
                                    (dirty ? QStringLiteral(" •") : QString()));
    });
    if (e->hasDraft() &&
        QMessageBox::question(
            this, ui(QStringLiteral("Recupero bozza")),
            ui(QStringLiteral("Recuperare le modifiche non salvate per questo file?"))) ==
            QMessageBox::Yes)
        e->restoreDraft(error);
    if (!error.isEmpty())
        state->setText(error);
}
bool Window::saveEditor(Editor *e) {
    if (!e || e == preview)
        return true;
    QString error;
    if (!e->save(error)) {
        QMessageBox::warning(this, ui(QStringLiteral("Salvataggio")), error);
        return false;
    }
    state->setText(ui(QStringLiteral("File salvato · F5 aggiorna i simboli")));
    searchTimer.start();
    return true;
}
bool Window::closeTab(int i) {
    if (i <= 0)
        return false;
    auto e = qobject_cast<Editor *>(tabs->widget(i));
    if (e->dirty()) {
        auto answer = QMessageBox::question(
            this, ui(QStringLiteral("Modifiche non salvate")),
            ui(QStringLiteral("Salvare %1?")).arg(QFileInfo(e->path()).fileName()),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
        if (answer == QMessageBox::Cancel)
            return false;
        if (answer == QMessageBox::Save && !saveEditor(e))
            return false;
        if (answer == QMessageBox::Discard)
            e->discardDraft();
    }
    tabs->removeTab(i);
    delete e;
    return true;
}
void Window::closeEvent(QCloseEvent *e) {
    if (operation) {
        cancelOperation();
        state->setText(ui(QStringLiteral("Operazione in corso: chiudi al termine.")));
        e->ignore();
        return;
    }
    while (tabs->count() > 1)
        if (!closeTab(tabs->count() - 1)) {
            e->ignore();
            return;
        }
    if (queryProcess)
        queryProcess->kill();
    e->accept();
}
bool Window::prepareFileOperation() {
    for (int i = 1; i < tabs->count(); ++i)
        if (qobject_cast<Editor *>(tabs->widget(i))->dirty()) {
            QMessageBox::information(this, ui(QStringLiteral("Modifiche aperte")),
                                     ui(QStringLiteral("Salva o chiudi le schede modificate prima "
                                                    "della sostituzione o del ripristino.")));
            return false;
        }
    return true;
}
void Window::refreshEditors() {
    for (int i = 1; i < tabs->count(); ++i) {
        auto e = qobject_cast<Editor *>(tabs->widget(i));
        QString error;
        if (!e->dirty() && e->load(e->path(), error))
            e->applyTheme(theme);
    }
    if (!selectedPath.isEmpty())
        showPreview(selectedPath, selectedLine);
    indexProject();
}
