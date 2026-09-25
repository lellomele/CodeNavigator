#include "localization.h"
#include "printing.h"
#include <QPrintDialog>
#include <QPdfWriter>
#include <QSaveFile>
#include <QtWidgets>
#include <QPrinter>
#include <QPrinterInfo>
#include <QTextDocument>
#include <QJsonObject>
#include <QDateTime>
QString replacementHtml(const QJsonArray &entries, const QString &root) {
    QString html = QStringLiteral("<html><head><style>body{font-family:Arial;font-size:10pt;}table{border-collapse:collapse;width:100%;}td,th{border:1px solid #777;padding:5px;}pre{white-space:pre-wrap;font-family:monospace;}h2{font-size:12pt;}</style></head><body>");
    html += QStringLiteral("<h1>%1</h1><p>%2<br>%3</p>").arg(ui(QStringLiteral("Anteprima sostituzione")), root.toHtmlEscaped(), QDateTime::currentDateTime().toString(Qt::ISODate));
    html += QStringLiteral("<p>%1</p>").arg(ui(QStringLiteral("Stesse righe dell’anteprima: massimo 200 righe modificate per file, testo abbreviato a 500 caratteri.")));
    for (const auto &v : entries) {
        auto e = v.toObject();
        html += QStringLiteral("<h2>%1 (%2)</h2><table><thead><tr><th>%3</th><th>%4</th><th>%5</th></tr></thead><tbody>").arg(e.value(QStringLiteral("path")).toString().toHtmlEscaped()).arg(e.value(QStringLiteral("count")).toInt()).arg(ui(QStringLiteral("Riga")), ui(QStringLiteral("Prima")), ui(QStringLiteral("Dopo")));
        for (const auto &value : e.value(QStringLiteral("preview")).toArray()) {
            auto row = value.toObject();
            html += QStringLiteral("<tr><td>%1</td><td><pre>%2</pre></td><td><pre>%3</pre></td></tr>").arg(row.value(QStringLiteral("line")).toInt()).arg(row.value(QStringLiteral("before")).toString().toHtmlEscaped(), row.value(QStringLiteral("after")).toString().toHtmlEscaped());
        }
        html += QStringLiteral("</tbody></table>");
    }
    return html + QStringLiteral("</body></html>");
}
static void prepareDocument(QTextDocument &document, const QJsonArray &entries, const QString &root) {
    document.setDefaultFont(QFont(QStringLiteral("Arial"),9));
    QTextOption options;options.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    document.setDefaultTextOption(options);
    document.setHtml(replacementHtml(entries,root));
}
bool exportReplacementsPdf(const QJsonArray &entries, const QString &root, const QString &path, QString &error) {
    QSaveFile file(path);file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly)) {error=file.errorString();return false;}
    {
        QPdfWriter writer(&file);writer.setTitle(ui(QStringLiteral("Anteprima sostituzione")));
        writer.setPageSize(QPageSize(QPageSize::A4));writer.setPageOrientation(QPageLayout::Landscape);
        QTextDocument document;prepareDocument(document,entries,root);document.print(&writer);
    }
    if(file.error()!=QFileDevice::NoError || !file.commit()){error=file.errorString();return false;}
    return true;
}
void printReplacements(const QJsonArray &entries, const QString &root, QWidget *parent) {
    QDialog dialog(parent);dialog.setObjectName(QStringLiteral("printMatchesDialog"));
    dialog.setWindowTitle(ui(QStringLiteral("Stampa corrispondenze")));dialog.resize(1050,750);
    auto layout=new QVBoxLayout(&dialog);
    auto info=new QLabel(ui(QStringLiteral("%1 file · Scegli una stampante di Windows oppure salva direttamente in PDF. La stampa non modifica i sorgenti.")).arg(entries.size()));
    info->setWordWrap(true);layout->addWidget(info);
    auto browser=new QTextBrowser;browser->setOpenLinks(false);browser->setHtml(replacementHtml(entries,root));layout->addWidget(browser,1);
    auto status=new QLabel;status->setWordWrap(true);status->setObjectName(QStringLiteral("printStatus"));layout->addWidget(status);
    auto buttons=new QDialogButtonBox(QDialogButtonBox::Close);layout->addWidget(buttons);
    auto print=buttons->addButton(ui(QStringLiteral("Stampante…")),QDialogButtonBox::ActionRole);print->setObjectName(QStringLiteral("choosePrinter"));
    auto pdf=buttons->addButton(ui(QStringLiteral("Salva PDF…")),QDialogButtonBox::ActionRole);pdf->setObjectName(QStringLiteral("exportMatchesPdf"));
    QObject::connect(print,&QPushButton::clicked,&dialog,[&]{
        if(QPrinterInfo::availablePrinterNames().isEmpty()){status->setText(ui(QStringLiteral("Nessuna stampante configurata. Puoi usare Salva PDF.")));return;}
        QPrinter printer(QPrinter::HighResolution);printer.setOutputFormat(QPrinter::NativeFormat);
        printer.setDocName(ui(QStringLiteral("Anteprima sostituzione")));
        QPrintDialog chooser(&printer,&dialog);
        if(chooser.exec()!=QDialog::Accepted)return;
        QTextDocument document;prepareDocument(document,entries,root);document.print(&printer);
        status->setText(printer.printerState()==QPrinter::Error?ui(QStringLiteral("Stampa non riuscita. Verifica la stampante o salva in PDF.")):ui(QStringLiteral("Documento inviato alla stampante.")));
    });
    QObject::connect(pdf,&QPushButton::clicked,&dialog,[&]{
        QFileDialog save(&dialog,ui(QStringLiteral("Salva PDF…")),QStringLiteral("corrispondenze.pdf"),QStringLiteral("PDF (*.pdf)"));
        save.setAcceptMode(QFileDialog::AcceptSave);save.setDefaultSuffix(QStringLiteral("pdf"));
        if(save.exec()!=QDialog::Accepted || save.selectedFiles().isEmpty())return;
        const auto path=save.selectedFiles().first();
        QString error;
        if(!exportReplacementsPdf(entries,root,path,error)){QMessageBox::warning(&dialog,ui(QStringLiteral("Salvataggio")),error);return;}
        status->setText(ui(QStringLiteral("PDF salvato: %1")).arg(QDir::toNativeSeparators(path)));
    });
    QObject::connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    dialog.exec();
}
