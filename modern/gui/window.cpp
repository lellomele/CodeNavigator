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
    setWindowTitle(QStringLiteral("Code Navigator %1").arg(QString::fromLatin1(SOURCE_NAVIGATOR_VERSION)));
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
    auto title = new QLabel(QStringLiteral("CODE  /  NAVIGATOR"));
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
    projectFileFilter = new ProjectFileFilter(this);
    projectFileFilter->setSourceModel(fileModel);
    projectFileFilter->setExcluded(QSettings().value(QStringLiteral("files/excludedExtensions")).toStringList());
    tree->setModel(projectFileFilter);
    tree->setHeaderHidden(true);
    for (int i = 1; i < 4; ++i)
        tree->hideColumn(i);
    fl->addWidget(tree, 1);
    split->addWidget(files);
    connect(tree->selectionModel(), &QItemSelectionModel::currentChanged, this,
            [this](const QModelIndex &i) {
                if (!i.isValid()) return;
                auto p = fileModel->filePath(projectFileFilter->mapToSource(i));
                setSearchDirectory(QFileInfo(p).isDir() ? p : QFileInfo(p).absolutePath());
                if (QFileInfo(p).isFile())
                    showPreview(p);
            });
    connect(tree, &QTreeView::doubleClicked, this, [this](const QModelIndex &i) {
        auto p = fileModel->filePath(projectFileFilter->mapToSource(i));
        setSearchDirectory(QFileInfo(p).isDir() ? p : QFileInfo(p).absolutePath());
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
    searchScope = new QComboBox;
    searchScope->addItems({ui(QStringLiteral("Contenuto dei file")), ui(QStringLiteral("Simboli")),
                           ui(QStringLiteral("Nomi dei file"))});
    searchScope->setObjectName(QStringLiteral("searchScope"));
    sl->addWidget(searchScope);
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
    caseSensitive = new QCheckBox(QStringLiteral("Aa"));
    caseSensitive->setToolTip(ui(QStringLiteral("Distingui maiuscole e minuscole")));
    caseSensitive->setObjectName(QStringLiteral("caseSensitive"));
    wholeWords = new QCheckBox(ui(QStringLiteral("Parole intere")));
    wholeWords->setObjectName(QStringLiteral("wholeWords"));
    wholeWords->setToolTip(ui(QStringLiteral("Confini di parola Unicode; lettere, cifre e underscore appartengono alla parola. Disponibile anche con wildcard e regex.")));
    options->addWidget(mode, 1);
    options->addWidget(caseSensitive);
    options->addWidget(wholeWords);
    sl->addLayout(options);
    pathFilter = new QLineEdit;
    pathFilter->setObjectName(QStringLiteral("pathFilter"));
    pathFilter->setPlaceholderText(ui(QStringLiteral("Filtra percorso · es. src/*.rs")));
    pathFilter->setClearButtonEnabled(true);
    sl->addWidget(pathFilter);
    auto directoryRow = new QHBoxLayout;
    searchDirectoryLabel = new QLabel(ui(QStringLiteral("Intero progetto")));
    searchDirectoryLabel->setObjectName(QStringLiteral("searchDirectoryLabel"));
    searchDirectoryLabel->setWordWrap(true);
    directoryRow->addWidget(searchDirectoryLabel, 1);
    auto resetDirectory = new QPushButton(ui(QStringLiteral("Intero progetto")));
    resetDirectory->setObjectName(QStringLiteral("resetSearchDirectory"));
    connect(resetDirectory, &QPushButton::clicked, this, [this] {
        tree->clearSelection(); tree->setCurrentIndex(QModelIndex()); setSearchDirectory(root);
    });
    directoryRow->addWidget(resetDirectory);
    sl->addLayout(directoryRow);
    replaceToggle = new QToolButton;
    replaceToggle->setObjectName(QStringLiteral("replaceToggle"));
    replaceToggle->setText(ui(QStringLiteral("Mostra opzioni di sostituzione")));
    replaceToggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    replaceToggle->setArrowType(Qt::RightArrow);
    replaceToggle->setCheckable(true);
    replaceToggle->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    sl->addWidget(replaceToggle);
    replaceArea = new QWidget;
    replaceArea->setObjectName(QStringLiteral("replaceArea"));
    auto replaceLayout = new QVBoxLayout(replaceArea);
    replaceLayout->setContentsMargins(0, 0, 0, 0);
    replacement = new QLineEdit;
    replacement->setObjectName(QStringLiteral("replacement"));
    replacement->setPlaceholderText(ui(QStringLiteral("Sostituisci con…")));
    replacement->setToolTip(ui(QStringLiteral(
        "Regex: $1 o ${nome} per un gruppo; $$ per $. Testo vuoto: elimina le corrispondenze.")));
    replaceLayout->addWidget(replacement);
    replaceButton =
        button(ui(QStringLiteral("Anteprima sostituzione")), QStringLiteral("replacePreview"));
    replaceLayout->addWidget(replaceButton);
    sl->addWidget(replaceArea);
    replaceArea->hide();
    connect(replaceToggle, &QToolButton::toggled, this, [this](bool expanded) {
        replaceArea->setVisible(expanded && searchScope->currentIndex() == 0);
        replaceToggle->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
        replaceToggle->setText(ui(expanded ? QStringLiteral("Nascondi opzioni di sostituzione")
                                            : QStringLiteral("Mostra opzioni di sostituzione")));
    });
    connect(replaceButton, &QPushButton::clicked, this, &Window::planReplacement);
    auto fileSearchHint = new QLabel(ui(QStringLiteral("Nomi: *.cpp o test?.rs. Percorsi: src/*test*.cpp. Un clic mostra l’anteprima; un doppio clic apre l’editor.")));
    fileSearchHint->setObjectName(QStringLiteral("fileSearchHint"));
    fileSearchHint->setWordWrap(true);
    fileSearchHint->hide();
    sl->addWidget(fileSearchHint);
    auto selection = new QHBoxLayout;
    auto all = button(ui(QStringLiteral("Seleziona tutti")));
    auto none = button(ui(QStringLiteral("Nessuno")));
    selection->addWidget(all);
    selection->addWidget(none);
    sl->addLayout(selection);
    openMatchedFiles = button(ui(QStringLiteral("Apri file selezionati")), QStringLiteral("openMatchedFiles"));
    openMatchedFiles->hide();
    openFolder = button(ui(QStringLiteral("Apri cartella")), QStringLiteral("openFolder"));
    openFolder->setToolTip(ui(QStringLiteral("Apri la cartella del file selezionato")));
    openFolder->setEnabled(false);
    openFolder->hide();
    auto fileActions = new QHBoxLayout;
    fileActions->addWidget(openMatchedFiles, 1);
    fileActions->addWidget(openFolder);
    sl->addLayout(fileActions);
    connect(openFolder, &QPushButton::clicked, this, [this] {
        auto item = results->currentItem();
        if (!item || searchScope->currentIndex() != 2) return;
        const auto file = item->data(0, Qt::UserRole).toJsonObject().value(QStringLiteral("path")).toString();
        const auto directory = QFileInfo(QDir(root).filePath(file)).absolutePath();
        if (!QFileInfo(directory).isDir() || !QDesktopServices::openUrl(QUrl::fromLocalFile(directory)))
            QMessageBox::warning(this, ui(QStringLiteral("Apri cartella")), ui(QStringLiteral("Impossibile aprire la cartella selezionata.")));
    });
    connect(openMatchedFiles, &QPushButton::clicked, this, [this] {
        if (operation || searchScope->currentIndex() != 2) return;
        QStringList paths;
        for (int i = 0; i < results->topLevelItemCount(); ++i) {
            auto item = results->topLevelItem(i);
            if (item->checkState(0) == Qt::Checked)
                paths << item->data(0, Qt::UserRole).toJsonObject().value(QStringLiteral("path")).toString();
        }
        if (paths.isEmpty() && results->currentItem())
            paths << results->currentItem()->data(0, Qt::UserRole).toJsonObject().value(QStringLiteral("path")).toString();
        if (paths.size() > 32) {
            QMessageBox::information(this, ui(QStringLiteral("Apertura file")), ui(QStringLiteral("Seleziona al massimo 32 file per apertura.")));
            return;
        }
        for (const auto &path : paths) openFile(QDir(root).filePath(path));
    });
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
        openFolder->setEnabled(item && searchScope->currentIndex() == 2);
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
    auto excludedAction = settings->addAction(ui(QStringLiteral("Estensioni escluse…")));
    excludedAction->setObjectName(QStringLiteral("excludedExtensionsAction"));
    connect(excludedAction, &QAction::triggered, this, &Window::manageExcludedExtensions);
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
            [this] { setTheme(theme.background.lightness() < 128 ? 4 : 3); });
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
        QMessageBox::about(this, ui(QStringLiteral("Informazioni su Code Navigator")),
            ui(QStringLiteral("Code Navigator %1<br>© 2026 Prof. ing. Raffaele Mele<br><a href='https://infotechlab.altervista.org/'>InfoTechLab</a><br>Ispirato a Source-Navigator 4.5.<br>Qt 6 · Rust · SQLite · Scintilla / Lexilla<br>Parser inclusi nella distribuzione.<br>Licenze: cartella licenses della distribuzione.")).arg(QCoreApplication::applicationVersion()));
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
        connect(e, &QLineEdit::textChanged, this, &Window::scheduleSearch);
    auto searchModes = std::make_shared<QList<int>>(QList<int>{0, 0, 1});
    connect(mode, &QComboBox::currentIndexChanged, this, [this, searchModes](int index) {
        (*searchModes)[searchScope->currentIndex()] = index;
        scheduleSearch();
    });
    connect(searchScope, &QComboBox::currentIndexChanged, this,
            [this, searchModes, fileSearchHint, all, none](int scope) {
        const bool names = scope == 2;
        QSignalBlocker blocked(mode);
        mode->setCurrentIndex((*searchModes)[scope]);
        pattern->setPlaceholderText(names ? ui(QStringLiteral("Nome file… · *.cpp · src/*test*.rs"))
                                         : ui(QStringLiteral("Cerca nel progetto…  Ctrl+Shift+F")));
        replaceToggle->setVisible(scope == 0);
        replaceArea->setVisible(scope == 0 && replaceToggle->isChecked());
        replacement->setEnabled(scope == 0);
        openMatchedFiles->setVisible(names);
        openFolder->setVisible(names);
        openFolder->setEnabled(names && results->currentItem());
        openMatchedFiles->setEnabled(!operation);
        fileSearchHint->setVisible(names);
        all->setVisible(scope != 1);
        none->setVisible(scope != 1);
        wholeWords->setEnabled(!names);
        scheduleSearch();
    });
    for (auto check : {caseSensitive, wholeWords})
        connect(check, &QCheckBox::toggled, this, &Window::scheduleSearch);
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
    openMatchedFiles->setEnabled(false);
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
            openMatchedFiles->setEnabled(true);
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
                openMatchedFiles->setEnabled(true);
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
    searchDirectory = root;
    searchDirectoryLabel->setText(ui(QStringLiteral("Intero progetto")));
    QSettings().setValue(QStringLiteral("session/lastProject"), root);
    auto id = QString::fromLatin1(
        QCryptographicHash::hash(root.toUtf8(), QCryptographicHash::Sha256).toHex());
    auto base = storageRoot() + QStringLiteral("/projects/") + id;
    QDir().mkpath(base);
    db = base + QStringLiteral("/index.sqlite");
    cancelFile = base + QStringLiteral("/cancel");
    tree->setRootIndex(projectFileFilter->mapFromSource(fileModel->setRootPath(root)));
    projectLabel->setText(QFileInfo(root).fileName() + QStringLiteral("   /   ") + root);
    projectLabel->setToolTip(root);
    setWindowTitle(QFileInfo(root).fileName() + QStringLiteral(" — Code Navigator %1").arg(QString::fromLatin1(SOURCE_NAVIGATOR_VERSION)));
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
    const auto previousExtensions = extensions;
    projectFileFilter->setExcluded(QSettings().value(QStringLiteral("files/excludedExtensions")).toStringList());
    const auto excludedList = QSettings().value(QStringLiteral("files/excludedExtensions")).toStringList();
    const QSet<QString> excluded(excludedList.cbegin(), excludedList.cend());
    for (const auto &ext : previousExtensions)
        if (excluded.contains(ext)) extensions.removeAll(ext);
    if (extensions != previousExtensions)
        settings.setValue(QStringLiteral("extensions"), extensions);
    if (extensions.isEmpty())
        discover();
    else if (extensions != previousExtensions)
        indexProject();
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
class FileTypeItem : public QTreeWidgetItem {
  public:
    using QTreeWidgetItem::QTreeWidgetItem;
    bool operator<(const QTreeWidgetItem &other) const override {
        const int column = treeWidget()->sortColumn();
        if (column == 1 || column == 2) {
            const auto left = data(column, Qt::UserRole).toLongLong();
            const auto right = other.data(column, Qt::UserRole).toLongLong();
            if (left != right) return left < right;
        } else if (column == 3 && checkState(3) != other.checkState(3)) {
            return checkState(3) < other.checkState(3);
        }
        return QString::localeAwareCompare(text(0), other.text(0)) < 0;
    }
};
void Window::manageExcludedExtensions() {
    QDialog dialog(this);
    dialog.setWindowTitle(ui(QStringLiteral("Estensioni escluse")));
    dialog.setObjectName(QStringLiteral("excludedExtensionsDialog"));
    dialog.resize(420, 420);
    auto layout = new QVBoxLayout(&dialog);
    auto info = new QLabel(ui(QStringLiteral("Le estensioni selezionate restano nascoste in tutti i progetti. Deseleziona quelle da ripristinare.")));
    info->setWordWrap(true);
    layout->addWidget(info);
    auto list = new QTreeWidget;
    list->setObjectName(QStringLiteral("excludedExtensionsList"));
    list->setHeaderLabels({ui(QStringLiteral("Estensione"))});
    layout->addWidget(list, 1);
    const auto excluded = QSettings().value(QStringLiteral("files/excludedExtensions")).toStringList();
    for (const auto &ext : excluded) {
        auto row = new QTreeWidgetItem(list, {ext});
        row->setCheckState(0, Qt::Checked);
    }
    auto buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Save)->setText(ui(QStringLiteral("Salva")));
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) return;
    QStringList remaining;
    for (int i = 0; i < list->topLevelItemCount(); ++i)
        if (list->topLevelItem(i)->checkState(0) == Qt::Checked)
            remaining << list->topLevelItem(i)->text(0);
    QSettings().setValue(QStringLiteral("files/excludedExtensions"), remaining);
    projectFileFilter->setExcluded(remaining);
}
void Window::chooseFileTypes(const QJsonObject &o) {
    QDialog dialog(this);
    dialog.setWindowTitle(ui(QStringLiteral("File da includere")));
    dialog.resize(670, 570);
    auto layout = new QVBoxLayout(&dialog);
    auto info = new QLabel(ui(QStringLiteral("Seleziona le estensioni da indicizzare.\nI file testuali sono ricercabili anche senza parser di simboli.")));
    info->setWordWrap(true);
    layout->addWidget(info);
    auto list = new QTreeWidget;
    list->setObjectName(QStringLiteral("fileTypeList"));
    list->setHeaderLabels({ui(QStringLiteral("Estensione")), ui(QStringLiteral("File")),
                           QStringLiteral("KiB"), ui(QStringLiteral("Escludi sempre"))});
    list->header()->setSortIndicatorShown(true);
    layout->addWidget(list, 1);
    const auto excludedList = QSettings().value(QStringLiteral("files/excludedExtensions")).toStringList();
    const QSet<QString> excluded(excludedList.cbegin(), excludedList.cend());
    for (const auto &value : o.value(QStringLiteral("extensions")).toArray()) {
        const auto entry = value.toObject();
        const auto ext = entry.value(QStringLiteral("extension")).toString();
        if (excluded.contains(ext)) continue;
        const auto count = entry.value(QStringLiteral("files")).toInt();
        const auto bytes = qint64(entry.value(QStringLiteral("bytes")).toDouble());
        auto row = new FileTypeItem(list, {ext, QString::number(count),
                                           QString::number(bytes / 1024.0, 'f', 0), QString()});
        row->setData(0, Qt::UserRole, ext);
        row->setData(1, Qt::UserRole, count);
        row->setData(2, Qt::UserRole, bytes);
        row->setCheckState(0, (extensions.isEmpty() ? entry.value(QStringLiteral("selected")).toBool()
                                                 : extensions.contains(ext)) ? Qt::Checked : Qt::Unchecked);
        row->setCheckState(3, Qt::Unchecked);
    }
    list->setSortingEnabled(true);
    list->sortByColumn(0, Qt::AscendingOrder);
    connect(list, &QTreeWidget::itemChanged, &dialog, [list](QTreeWidgetItem *item, int column) {
        if (column == 3 && item->checkState(3) == Qt::Checked)
            item->setCheckState(0, Qt::Unchecked);
        else if (column == 0 && item->checkState(0) == Qt::Checked)
            item->setCheckState(3, Qt::Unchecked);
    });
    auto summary = new QLabel(ui(QStringLiteral("%1 file esclusi (binari, codifica, dimensione o accesso). %2 errori di scansione."))
                                  .arg(o.value(QStringLiteral("skipped")).toInt())
                                  .arg(o.value(QStringLiteral("errors")).toArray().size()));
    summary->setWordWrap(true);
    layout->addWidget(summary);
    auto buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(ui(QStringLiteral("Applica selezione")));
    auto refresh = buttons->addButton(ui(QStringLiteral("Rianalizza cartelle")), QDialogButtonBox::ActionRole);
    connect(refresh, &QPushButton::clicked, &dialog, [&dialog] { dialog.done(2); });
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    const int result = dialog.exec();
    if (result == 2) { discover(true); return; }
    if (result != QDialog::Accepted) {
        state->setText(ui(QStringLiteral("Progetto aperto · F5 aggiorna l'indice")));
        return;
    }
    QStringList chosen, newlyExcluded;
    for (int i = 0; i < list->topLevelItemCount(); ++i) {
        auto row = list->topLevelItem(i);
        const auto ext = row->data(0, Qt::UserRole).toString();
        if (row->checkState(3) == Qt::Checked) newlyExcluded << ext;
        else if (row->checkState(0) == Qt::Checked) chosen << ext;
    }
    if (chosen.isEmpty()) {
        state->setText(ui(QStringLiteral("Nessuna estensione selezionata")));
        return;
    }
    QStringList allExcluded = excludedList;
    for (const auto &ext : newlyExcluded)
        if (!allExcluded.contains(ext)) allExcluded << ext;
    allExcluded.sort(Qt::CaseInsensitive);
    QSettings().setValue(QStringLiteral("files/excludedExtensions"), allExcluded);
    projectFileFilter->setExcluded(allExcluded);
    if (!newlyExcluded.isEmpty()) {
        results->clear();
        currentResults = {};
    }
    const QSet<QString> selectedBefore(extensions.cbegin(), extensions.cend());
    const QSet<QString> selectedAfter(chosen.cbegin(), chosen.cend());
    if (selectedBefore == selectedAfter) return;
    chosen.sort(Qt::CaseInsensitive);
    extensions = chosen;
    QSettings(QFileInfo(db).absolutePath() + QStringLiteral("/project.ini"), QSettings::IniFormat)
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
void Window::scheduleSearch() {
    if (queryProcess) {
        queryProcess->disconnect(this);
        queryProcess->kill();
        queryProcess->deleteLater();
        queryProcess = nullptr;
    }
    results->clear();
    currentResults = {};
    replaceButton->setEnabled(false);
    searchTimer.start();
}
void Window::search() {
    searchTimer.stop();
    if (queryProcess) {
        queryProcess->disconnect(this);
        queryProcess->kill();
        queryProcess->deleteLater();
        queryProcess = nullptr;
    }
    const bool fileSearch = searchScope->currentIndex() == 2;
    if (root.isEmpty() || (!fileSearch && (db.isEmpty() || !QFileInfo::exists(db) || pattern->text().isEmpty()))) {
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
                    auto error = p->property("timedOut").toBool() ? ui(QStringLiteral("Tempo limite superato"))
                                                                : readError(p->readAllStandardError());
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
    QStringList args;
    if (fileSearch) {
        args << QStringLiteral("find-files") << QStringLiteral("--root") << root;
        for (const auto &ext : QSettings().value(QStringLiteral("files/excludedExtensions")).toStringList())
            args << QStringLiteral("--exclude-extension") << ext;
    } else {
        args << (searchScope->currentIndex() == 0 ? QStringLiteral("grep") : QStringLiteral("query"))
             << QStringLiteral("--db") << db;
    }
    args << QStringLiteral("--pattern") << (fileSearch && pattern->text().isEmpty() && mode->currentData().toString() == QStringLiteral("glob") ? QStringLiteral("*") : pattern->text())
         << QStringLiteral("--mode") << mode->currentData().toString()
         << QStringLiteral("--within") << selectedProjectRelativeDirectory()
         << QStringLiteral("--path") << pathFilter->text()
         << QStringLiteral("--limit") << QStringLiteral("5000");
    if (wholeWords->isChecked() && !fileSearch)
        args << QStringLiteral("--whole-words");
    if (caseSensitive->isChecked())
        args << QStringLiteral("--case-sensitive");
    if (fileSearch) QTimer::singleShot(15000, p, [p] {
        if (p->state() != QProcess::NotRunning) {
            p->setProperty("timedOut", true);
            p->kill();
        }
    });
    p->start(engine, args);
}
void Window::showResults(const QJsonObject &o) {
    replaceButton->setEnabled(!operation && searchScope->currentIndex() == 0);
    currentResults = o.value(QStringLiteral("results")).toArray();
    results->clear();
    if (o.value(QStringLiteral("event")).toString() == QStringLiteral("file_results")) {
        for (const auto &value : currentResults) {
            const auto file = value.toObject();
            auto item = new QTreeWidgetItem(results, {file.value(QStringLiteral("path")).toString()});
            item->setData(0, Qt::UserRole, file);
            item->setCheckState(0, Qt::Unchecked);
            item->setToolTip(0, item->text(0));
        }
        resultLabel->setText(ui(QStringLiteral("%1 file · %2 ms%3%4"))
            .arg(currentResults.size()).arg(o.value(QStringLiteral("ui_ms")).toInt())
            .arg(o.value(QStringLiteral("truncated")).toBool() ? ui(QStringLiteral("\nRisultati parziali: affina la ricerca.")) : QString())
            .arg(o.value(QStringLiteral("skipped")).toInt() ? ui(QStringLiteral("\n%1 voci non accessibili; consulta Diagnostica.")).arg(o.value(QStringLiteral("skipped")).toInt()) : QString()));
        for (const auto &error : o.value(QStringLiteral("errors")).toArray())
            record(QString::fromUtf8(QJsonDocument(error.toObject()).toJson(QJsonDocument::Compact)));
        return;
    }
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
        preview->send(SCI_SETREADONLY, 0);
        preview->sends(SCI_SETTEXT, 0, "");
        preview->send(SCI_SETREADONLY, 1);
        tabs->setCurrentIndex(0);
        selectedPath = p;
        selectedLine = line;
        previewLabel->setText(QDir(root).relativeFilePath(p) + QStringLiteral(" · ") + error);
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
        if (hit.value(QStringLiteral("kind")).toString() == QStringLiteral("file")) continue;
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
