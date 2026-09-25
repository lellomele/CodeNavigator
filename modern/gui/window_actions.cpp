#include "localization.h"
#include "window.h"
#include "printing.h"
#include <QJsonDocument>
#include <QJsonObject>

void Window::planReplacement(bool printOnly) {
    if (operation || queryProcess) {
        if (printOnly) QMessageBox::information(this,ui(QStringLiteral("Stampa corrispondenze")),ui(QStringLiteral("Operazione in corso: attendi o interrompi.")));
        return;
    }
    if (searchScope->currentIndex() != 0) {
        if (printOnly) QMessageBox::information(this,ui(QStringLiteral("Stampa corrispondenze")),ui(QStringLiteral("Per stampare le corrispondenze, esegui una ricerca nel testo dei file.")));
        return;
    }
    if (!printOnly && !prepareFileOperation()) return;
    QJsonArray files;
    for (int i = 0; i < results->topLevelItemCount(); ++i) {
        auto item = results->topLevelItem(i);
        if (item->checkState(0) == Qt::Checked)
            files.append(item->data(0, Qt::UserRole).toJsonObject().value(QStringLiteral("path")));
    }
    // Printing defaults to all result files when no checkbox is selected.
    if (printOnly && files.isEmpty()) {
        for (int i=0;i<results->topLevelItemCount();++i)
            files.append(results->topLevelItem(i)->data(0,Qt::UserRole).toJsonObject().value(QStringLiteral("path")));
    }
    if (files.isEmpty()) {
        if (printOnly) QMessageBox::information(this,ui(QStringLiteral("Stampa corrispondenze")),ui(QStringLiteral("Nessuna corrispondenza da stampare. Esegui prima una ricerca.")));
        state->setText(ui(QStringLiteral("Seleziona i file con le checkbox dei risultati.")));
        return;
    }
    QJsonObject request{{QStringLiteral("db"), db},
                        {QStringLiteral("files"), files},
                        {QStringLiteral("pattern"), pattern->text()},
                        {QStringLiteral("replacement"), replacement->text()},
                        {QStringLiteral("mode"), mode->currentData().toString()},
                        {QStringLiteral("case_sensitive"), caseSensitive->isChecked()},
                        {QStringLiteral("whole_words"), wholeWords->isChecked()},
                        {QStringLiteral("preview_only"), printOnly}};
    const auto requestPath =
        QFileInfo(db).absolutePath() + QStringLiteral("/replacement-request.json");
    QSaveFile f(requestPath);
    auto data = QJsonDocument(request).toJson();
    if (!f.open(QIODevice::WriteOnly) || f.write(data) != data.size() || !f.commit()) {
        state->setText(f.errorString());
        return;
    }
    state->setText(ui(QStringLiteral("Preparazione anteprima e copie di recupero…")));
    run({QStringLiteral("replace-plan"), QStringLiteral("--request"), requestPath},
        [this, requestPath, printOnly](const QJsonObject &o) {
            QFile::remove(requestPath);
            auto entries = o.value(QStringLiteral("entries")).toArray();
            if (entries.isEmpty()) {
                if (printOnly) QMessageBox::information(this,ui(QStringLiteral("Stampa corrispondenze")),ui(QStringLiteral("Nessuna corrispondenza da stampare. Esegui prima una ricerca.")));
                state->setText(ui(QStringLiteral("Nessuna modifica da applicare")));
                return;
            }
            if (printOnly) { printReplacements(entries, root, this); return; }
            QDialog dialog(this);
            dialog.setWindowTitle(ui(QStringLiteral("Verifica sostituzione")));
            dialog.setObjectName(QStringLiteral("replacementReview"));
            dialog.resize(1050, 650);
            auto layout = new QVBoxLayout(&dialog);
            auto info = new QLabel(
                ui(QStringLiteral(
                    "%1 file da modificare. Tutte le corrispondenze nei file selezionati saranno "
                    "sostituite.\nOriginali disponibili in Strumenti → Ripristina sostituzione. "
                    "Anteprima: prime 200 righe modificate per file."))
                    .arg(entries.size()));
            info->setWordWrap(true);
            layout->addWidget(info);
            auto list = new QTreeWidget;
            list->setHeaderLabels(
                {ui(QStringLiteral("File / riga")), ui(QStringLiteral("Prima")), ui(QStringLiteral("Dopo"))});
            list->header()->setSectionResizeMode(QHeaderView::Stretch);
            layout->addWidget(list, 1);
            for (const auto &v : entries) {
                auto e = v.toObject();
                auto parent =
                    new QTreeWidgetItem(list, {QStringLiteral("%1 (%2)")
                                                   .arg(e.value(QStringLiteral("path")).toString())
                                                   .arg(e.value(QStringLiteral("count")).toInt())});
                for (const auto &pv : e.value(QStringLiteral("preview")).toArray()) {
                    auto row = pv.toObject();
                    new QTreeWidgetItem(parent,
                                        {QString::number(row.value(QStringLiteral("line")).toInt()),
                                         row.value(QStringLiteral("before")).toString(),
                                         row.value(QStringLiteral("after")).toString()});
                }
                parent->setExpanded(entries.size() < 15);
            }
            auto buttons = new QDialogButtonBox(QDialogButtonBox::Apply | QDialogButtonBox::Cancel);
            buttons->button(QDialogButtonBox::Apply)
                ->setText(ui(QStringLiteral("Applica ai %1 file")).arg(entries.size()));
            auto print = buttons->addButton(ui(QStringLiteral("Stampa…")), QDialogButtonBox::ActionRole);
            connect(print, &QPushButton::clicked, &dialog, [&, entries] { printReplacements(entries, root, &dialog); });
            layout->addWidget(buttons);
            connect(buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked, &dialog,
                    &QDialog::accept);
            connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
            if (dialog.exec() != QDialog::Accepted)
                return;
            run({QStringLiteral("replace-apply"), QStringLiteral("--journal"),
                 o.value(QStringLiteral("journal")).toString()},
                [this](const QJsonObject &r) {
                    state->setText(ui(QStringLiteral("Sostituzione completata: %1 file"))
                                       .arg(r.value(QStringLiteral("files")).toInt()));
                    refreshEditors();
                });
        });
}
void Window::recoverReplacement() {
    if (db.isEmpty() || operation || !prepareFileOperation())
        return;
    QDir changes(QFileInfo(db).absolutePath() + QStringLiteral("/changes"));
    QStringList labels, paths;
    for (const auto &d :
         changes.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::Reversed)) {
        auto p = changes.filePath(d + QStringLiteral("/journal.json"));
        QFile f(p);
        if (!f.open(QIODevice::ReadOnly))
            continue;
        auto o = QJsonDocument::fromJson(f.readAll()).object();
        auto s = o.value(QStringLiteral("state")).toString();
        if (s == QStringLiteral("planned") || s == QStringLiteral("restored"))
            continue;
        labels << ui(QStringLiteral("%1 · %2 · %3 file"))
                      .arg(d, s)
                      .arg(o.value(QStringLiteral("entries")).toArray().size());
        paths << p;
    }
    if (labels.isEmpty()) {
        state->setText(ui(QStringLiteral("Nessuna sostituzione da ripristinare")));
        return;
    }
    bool ok;
    auto choice = QInputDialog::getItem(
        this, ui(QStringLiteral("Ripristina originali")),
        ui(QStringLiteral("Operazione (i file modificati successivamente saranno protetti)")), labels,
        0, false, &ok);
    if (ok)
        run({QStringLiteral("replace-undo"), QStringLiteral("--journal"),
             paths[labels.indexOf(choice)]},
            [this](const QJsonObject &) {
                state->setText(ui(QStringLiteral("Originali ripristinati")));
                refreshEditors();
            });
}
void Window::configureExternal() {
    QDialog d(this);
    d.setWindowTitle(ui(QStringLiteral("Editor esterno")));
    d.resize(600, 350);
    auto form = new QFormLayout(&d);
    auto exe = new QLineEdit(QSettings().value(QStringLiteral("external/exe")).toString());
    auto row = new QHBoxLayout;
    row->addWidget(exe);
    auto browse = new QPushButton(ui(QStringLiteral("Sfoglia…")));
    row->addWidget(browse);
    form->addRow(ui(QStringLiteral("Eseguibile")), row);
    connect(browse, &QPushButton::clicked, &d, [&] {
        auto p = QFileDialog::getOpenFileName(&d, ui(QStringLiteral("Editor")), exe->text(),
                                              ui(QStringLiteral("Programmi (*.exe)")));
        if (!p.isEmpty())
            exe->setText(p);
    });
    auto args = new QPlainTextEdit(
        QSettings().value(QStringLiteral("external/args"), QStringLiteral("{file}")).toString());
    form->addRow(ui(QStringLiteral("Un argomento per riga")), args);
    form->addRow(new QLabel(ui(QStringLiteral(
        "Segnaposto: {file} percorso completo, {line} riga.\nNon aggiungere virgolette: ciascuna "
        "riga è un argomento.\nVS Code: --goto e {file}:{line} su due righe separate."))));
    auto buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &d, [&] {
        if (!QFileInfo(exe->text()).isFile()) {
            QMessageBox::warning(&d, ui(QStringLiteral("Editor")),
                                 ui(QStringLiteral("Seleziona un eseguibile esistente.")));
            return;
        }
        d.accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    if (d.exec() == QDialog::Accepted) {
        QSettings s;
        s.setValue(QStringLiteral("external/exe"), exe->text());
        s.setValue(QStringLiteral("external/args"), args->toPlainText());
    }
}
void Window::openExternal() {
    if (selectedPath.isEmpty())
        return;
    if (QSettings().value(QStringLiteral("external/exe")).toString().isEmpty())
        configureExternal();
    auto exe = QSettings().value(QStringLiteral("external/exe")).toString();
    if (exe.isEmpty())
        return;
    auto active = qobject_cast<Editor *>(tabs->currentWidget());
    if (active && active->dirty()) {
        QMessageBox::information(
            this, ui(QStringLiteral("Editor esterno")),
            ui(QStringLiteral("Salva le modifiche aperte prima di passare all'editor esterno.")));
        return;
    }
    if (active && active != preview)
        selectedLine = int(active->send(SCI_LINEFROMPOSITION, active->send(SCI_GETCURRENTPOS))) + 1;
    QStringList args;
    bool hasFile = false;
    for (auto a : QSettings()
                      .value(QStringLiteral("external/args"), QStringLiteral("{file}"))
                      .toString()
                      .split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        hasFile |= a.contains(QStringLiteral("{file}"));
        a.replace(QStringLiteral("{file}"), selectedPath)
            .replace(QStringLiteral("{line}"), QString::number(selectedLine));
        args << a;
    }
    if (!hasFile)
        args << selectedPath;
    if (!QProcess::startDetached(exe, args, root))
        QMessageBox::warning(this, ui(QStringLiteral("Editor esterno")),
                             ui(QStringLiteral("Impossibile avviare l'editor configurato.")));
}
void Window::setTheme(int i) {
    if (i >= 0 && i < 4)
        applyTheme(Theme::presets()[i]);
}
void Window::applyTheme(const Theme &t) {
    theme = t;
    theme.apply(*qApp);
    for (int i = 0; i < tabs->count(); ++i)
        qobject_cast<Editor *>(tabs->widget(i))->applyTheme(t);
    QSignalBlocker block(themes);
    int selected = -1;
    for (int i = 0; i < 4; ++i)
        if (Theme::presets()[i].name == t.name)
            selected = i;
    themes->setCurrentIndex(selected);
    QSettings().setValue(QStringLiteral("theme-v2"),
                         QJsonDocument(t.json()).toJson(QJsonDocument::Compact));
}
void Window::customizeTheme() {
    QDialog d(this);
    d.setWindowTitle(ui(QStringLiteral("Palette e contrasto")));
    auto layout = new QVBoxLayout(&d);
    auto form = new QFormLayout;
    layout->addLayout(form);
    Theme candidate = theme;
    QStringList labels = {ui(QStringLiteral("Sfondo")),
                          ui(QStringLiteral("Testo")),
                          ui(QStringLiteral("Pannelli / gradiente")),
                          ui(QStringLiteral("Testo secondario")),
                          ui(QStringLiteral("Accento")),
                          ui(QStringLiteral("Parole chiave")),
                          ui(QStringLiteral("Stringhe")),
                          ui(QStringLiteral("Commenti")),
                          ui(QStringLiteral("Numeri"))};
    QColor *colors[] = {&candidate.background,  &candidate.foreground, &candidate.panel,
                        &candidate.muted,       &candidate.accent,     &candidate.keyword,
                        &candidate.stringColor, &candidate.comment,    &candidate.number};
    for (int i = 0; i < labels.size(); ++i) {
        auto b = new QPushButton(colors[i]->name());
        form->addRow(labels[i], b);
        auto color = colors[i];
        connect(b, &QPushButton::clicked, &d, [&, b, color] {
            auto c = QColorDialog::getColor(*color, &d);
            if (c.isValid()) {
                *color = c;
                b->setText(c.name());
            }
        });
    }
    auto error = new QLabel;
    error->setWordWrap(true);
    layout->addWidget(error);
    auto buttons = new QDialogButtonBox(QDialogButtonBox::Apply | QDialogButtonBox::Cancel);
    layout->addWidget(buttons);
    connect(buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked, &d, [&] {
        auto message = candidate.validate();
        if (!message.isEmpty()) {
            error->setText(message);
            return;
        }
        candidate.name = ui(QStringLiteral("Personalizzato"));
        applyTheme(candidate);
        d.accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    d.exec();
}
void Window::exportTheme() {
    auto p =
        QFileDialog::getSaveFileName(this, ui(QStringLiteral("Esporta tema")),
                                     QStringLiteral("tema.json"), QStringLiteral("JSON (*.json)"));
    if (p.isEmpty())
        return;
    QSaveFile f(p);
    auto data = QJsonDocument(theme.json()).toJson();
    if (!f.open(QIODevice::WriteOnly) || f.write(data) != data.size() || !f.commit())
        QMessageBox::warning(this, ui(QStringLiteral("Tema")), f.errorString());
}
void Window::importTheme() {
    auto p = QFileDialog::getOpenFileName(this, ui(QStringLiteral("Importa tema")), {},
                                          QStringLiteral("JSON (*.json)"));
    if (p.isEmpty())
        return;
    QFile f(p);
    if (!f.open(QIODevice::ReadOnly) || f.size() > 65536)
        return;
    auto t = Theme::fromJson(QJsonDocument::fromJson(f.readAll()).object());
    auto error = t.validate();
    if (!error.isEmpty())
        QMessageBox::warning(this, ui(QStringLiteral("Tema")), error);
    else
        applyTheme(t);
}
