#include "localization.h"
#include "editor.h"
#include "ILexer.h"
#include "Lexilla.h"
#include "SciLexer.h"
#include "storage.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTextCodec>
#include <QContextMenuEvent>
#include <QMenu>
#include <QRegularExpression>

static long sciColor(const QColor &c) {
    return c.red() | (c.green() << 8) | (c.blue() << 16);
}
static QByteArray hash(const QByteArray &b) {
    return QCryptographicHash::hash(b, QCryptographicHash::Sha256);
}
Editor::Editor(QWidget *parent) : ScintillaEditBase(parent) {
    send(SCI_SETCODEPAGE, SC_CP_UTF8);
    send(SCI_SETTABWIDTH, 4);
    send(SCI_SETUSETABS, 0);
    send(SCI_SETMARGINTYPEN, 0, SC_MARGIN_NUMBER);
    send(SCI_SETMARGINWIDTHN, 0, 56);
    send(SCI_SETSCROLLWIDTHTRACKING, 1);
    send(SCI_SETSCROLLWIDTH, 1);
    send(SCI_SETWRAPMODE, SC_WRAP_NONE);
}
QByteArray Editor::bytes() const {
    const auto n = send(SCI_GETLENGTH);
    QByteArray data(static_cast<int>(n) + 1, '\0');
    send(SCI_GETTEXT, n + 1, reinterpret_cast<sptr_t>(data.data()));
    data.resize(static_cast<int>(n));
    return data;
}
bool Editor::dirty() const {
    return send(SCI_GETMODIFY) != 0;
}
bool Editor::load(const QString &path, QString &error) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        error = f.errorString();
        return false;
    }
    if (f.size() > 32 * 1024 * 1024) {
        error = ui(QStringLiteral("Il file supera il limite di 32 MiB dell'editor"));
        return false;
    }
    QByteArray content = f.readAll();
    if (f.error() != QFile::NoError) {
        error = f.errorString();
        return false;
    }
    filePath = QFileInfo(path).canonicalFilePath();
    originalHash = hash(content);
    codecName = "UTF-8";
    bomPrefix.clear();
    if (content.startsWith("\xef\xbb\xbf")) {
        bomPrefix = content.left(3);
        content.remove(0, 3);
    } else if (content.startsWith("\xff\xfe")) {
        codecName = "UTF-16LE";
        bomPrefix = content.left(2);
        content.remove(0, 2);
    } else if (content.startsWith("\xfe\xff")) {
        codecName = "UTF-16BE";
        bomPrefix = content.left(2);
        content.remove(0, 2);
    }
    QTextCodec::ConverterState state(QTextCodec::IgnoreHeader);
    auto text =
        QTextCodec::codecForName(codecName)->toUnicode(content.constData(), content.size(), &state);
    if ((state.invalidChars || state.remainingChars) && codecName == "UTF-8" &&
        bomPrefix.isEmpty()) {
        codecName = "Windows-1252";
        text = QTextCodec::codecForName(codecName)->toUnicode(content);
    } else if (state.invalidChars || state.remainingChars) {
        error = ui(QStringLiteral("Codifica non valida o incompleta"));
        return false;
    }
    for (const auto c : text)
        if (c.category() == QChar::Other_Control && c != QLatin1Char('\n') &&
            c != QLatin1Char('\r') && c != QLatin1Char('\t') && c != QLatin1Char('\f')) {
            error = ui(QStringLiteral("File binario o codifica non supportata"));
            return false;
        }
    readOnlyEncoding = false;
    content = text.toUtf8();
    send(SCI_SETREADONLY, 0);
    sends(SCI_SETTEXT, 0, content.constData());
    send(SCI_SETEOLMODE, content.contains("\r\n") ? SC_EOL_CRLF : SC_EOL_LF);
    send(SCI_EMPTYUNDOBUFFER);
    send(SCI_SETSAVEPOINT);
    send(SCI_SETREADONLY, readOnlyEncoding);
    const auto ext = QFileInfo(path).suffix().toLower();
    if (QStringList{QStringLiteral("c"), QStringLiteral("h"), QStringLiteral("cpp"),
                    QStringLiteral("hpp"), QStringLiteral("cc"), QStringLiteral("java"),
                    QStringLiteral("js"), QStringLiteral("jsx"), QStringLiteral("ts"),
                    QStringLiteral("tsx"), QStringLiteral("cs"), QStringLiteral("go"),
                    QStringLiteral("kt")}
            .contains(ext))
        lexer = QStringLiteral("cpp");
    else if (ext == QStringLiteral("py"))
        lexer = QStringLiteral("python");
    else if (QStringList{QStringLiteral("tcl"), QStringLiteral("tk"), QStringLiteral("itcl")}
                 .contains(ext))
        lexer = QStringLiteral("tcl");
    else if (ext.startsWith(QStringLiteral("php")))
        lexer = QStringLiteral("phpscript");
    else if (ext == QStringLiteral("f") || ext == QStringLiteral("for"))
        lexer = QStringLiteral("fortran");
    else if (ext == QStringLiteral("cob") || ext == QStringLiteral("cbl"))
        lexer = QStringLiteral("cobol");
    else if (ext == QStringLiteral("asm") || ext == QStringLiteral("s"))
        lexer = QStringLiteral("asm");
    else if (ext == QStringLiteral("rs"))
        lexer = QStringLiteral("rust");
    else if (ext == QStringLiteral("json") || ext == QStringLiteral("jsonc"))
        lexer = QStringLiteral("json");
    else if (ext == QStringLiteral("sql"))
        lexer = QStringLiteral("sql");
    else if (ext == QStringLiteral("css") || ext == QStringLiteral("scss"))
        lexer = QStringLiteral("css");
    else if (ext == QStringLiteral("html") || ext == QStringLiteral("htm") ||
             ext == QStringLiteral("xml"))
        lexer = QStringLiteral("hypertext");
    else if (ext == QStringLiteral("sh") || ext == QStringLiteral("bash"))
        lexer = QStringLiteral("bash");
    else if (ext == QStringLiteral("ps1") || ext == QStringLiteral("psm1"))
        lexer = QStringLiteral("powershell");
    else if (ext == QStringLiteral("yaml") || ext == QStringLiteral("yml"))
        lexer = QStringLiteral("yaml");
    else
        lexer = QStringLiteral("null");
    send(SCI_SETILEXER, 0, reinterpret_cast<sptr_t>(CreateLexer(lexer.toUtf8().constData())));
    if (lexer == QStringLiteral("cpp"))
        sends(SCI_SETKEYWORDS, 0,
              "alignas auto bool break case catch char class const constexpr continue decltype "
              "default delete do double else enum explicit extern false float for friend if inline "
              "int long namespace new noexcept nullptr operator override private protected public "
              "register return short signed sizeof static struct switch template this throw true "
              "try typedef typename union unsigned using virtual void volatile while function let "
              "var async await export import from extends implements interface package null "
              "undefined of in type const func defer go map chan range val fun object when");
    if (lexer == QStringLiteral("python"))
        sends(SCI_SETKEYWORDS, 0,
              "and as assert async await break class continue def del elif else except False "
              "finally for from global if import in is lambda None nonlocal not or pass raise "
              "return True try while with yield");
    if (lexer == QStringLiteral("rust"))
        sends(SCI_SETKEYWORDS, 0,
              "as async await break const continue crate dyn else enum extern false fn for if impl "
              "in let loop match mod move mut pub ref return self Self static struct super trait "
              "true type unsafe use where while");
    if (lexer == QStringLiteral("sql"))
        sends(SCI_SETKEYWORDS, 0,
              "select from where join left right inner outer on insert into values update set "
              "delete create table index primary key foreign references null not and or order by "
              "group having limit distinct as union alter drop default");
    if (lexer == QStringLiteral("tcl"))
        sends(SCI_SETKEYWORDS, 0,
              "proc set if else elseif foreach for while return namespace variable global expr "
              "catch try switch class method constructor");
    if (readOnlyEncoding)
        error = ui(QStringLiteral("Codifica non UTF-8: file aperto in sola lettura"));
    return true;
}
bool Editor::save(QString &error, bool allowExternalChange) {
    if (readOnlyEncoding) {
        error = ui(QStringLiteral("Salvataggio disabilitato per questa codifica"));
        return false;
    }
    QFile current(filePath);
    if (!current.open(QIODevice::ReadOnly)) {
        error = current.errorString();
        return false;
    }
    auto disk = current.readAll();
    current.close();
    if (!allowExternalChange && hash(disk) != originalHash) {
        error = ui(QStringLiteral(
            "Il file è stato modificato da un altro programma. Ricaricalo prima di salvare."));
        return false;
    }
    const auto text = QString::fromUtf8(bytes());
    QTextCodec::ConverterState conversion(QTextCodec::IgnoreHeader);
    auto data = QTextCodec::codecForName(codecName)->fromUnicode(text.constData(), text.size(),
                                                                 &conversion);
    if (conversion.invalidChars) {
        error =
            ui(QStringLiteral("Alcuni caratteri non sono rappresentabili nella codifica originale"));
        return false;
    }
    data.prepend(bomPrefix);
    QSaveFile file(filePath);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
        error = file.errorString();
        return false;
    }
    originalHash = hash(data);
    send(SCI_SETSAVEPOINT);
    discardDraft();
    return true;
}
void Editor::goTo(int line, int column) {
    auto pos = send(SCI_POSITIONFROMLINE, std::max(0, line - 1));
    auto end = send(SCI_GETLINEENDPOSITION, std::max(0, line - 1));
    send(SCI_GOTOPOS, std::min(pos + column, end));
    send(SCI_SCROLLCARET);
    setFocus();
}
QString Editor::draftPath() const {
    const auto dir = storageRoot() + QStringLiteral("/drafts");
    QDir().mkpath(dir);
    return dir + QStringLiteral("/") + QString::fromLatin1(hash(filePath.toUtf8()).toHex()) +
           QStringLiteral(".draft");
}
void Editor::saveDraft() {
    if (!dirty() || filePath.isEmpty())
        return;
    QSaveFile f(draftPath());
    auto b =
        QJsonDocument(
            QJsonObject{{QStringLiteral("schema"), 1},
                        {QStringLiteral("baseHash"), QString::fromLatin1(originalHash.toHex())},
                        {QStringLiteral("content"), QString::fromLatin1(bytes().toBase64())}})
            .toJson(QJsonDocument::Compact);
    if (!f.open(QIODevice::WriteOnly) || f.write(b) != b.size() || !f.commit())
        emit draftFailed(ui(QStringLiteral("Copia di recupero non salvata per %1: %2"))
                             .arg(filePath, f.errorString()));
}
bool Editor::hasDraft() const {
    return QFileInfo::exists(draftPath());
}
bool Editor::restoreDraft(QString &error) {
    if (readOnlyEncoding) {
        error = ui(QStringLiteral("La bozza richiede un file UTF-8 modificabile"));
        return false;
    }
    QFile f(draftPath());
    if (!f.open(QIODevice::ReadOnly)) {
        error = f.errorString();
        return false;
    }
    if (f.size() > 48 * 1024 * 1024) {
        error = ui(QStringLiteral("Bozza oltre il limite"));
        return false;
    }
    const auto draft = QJsonDocument::fromJson(f.readAll()).object();
    if (draft.value(QStringLiteral("schema")).toInt() != 1 ||
        draft.value(QStringLiteral("baseHash")).toString() !=
            QString::fromLatin1(originalHash.toHex())) {
        error = ui(QStringLiteral("Il file è cambiato dopo la creazione della bozza. La copia di "
                               "recupero è conservata, senza sovrascrivere il file."));
        return false;
    }
    auto decoded =
        QByteArray::fromBase64Encoding(draft.value(QStringLiteral("content")).toString().toLatin1(),
                                       QByteArray::AbortOnBase64DecodingErrors);
    if (!decoded || decoded.decoded.size() > 32 * 1024 * 1024) {
        error = ui(QStringLiteral("Bozza non valida"));
        return false;
    }
    auto b = decoded.decoded;
    sends(SCI_SETTEXT, 0, b.constData());
    return true;
}
void Editor::discardDraft() {
    if (!filePath.isEmpty())
        QFile::remove(draftPath());
}
void Editor::applyTheme(const Theme &t) {
    send(SCI_STYLESETFORE, STYLE_DEFAULT, sciColor(t.foreground));
    send(SCI_STYLESETBACK, STYLE_DEFAULT, sciColor(t.background));
    sends(SCI_STYLESETFONT, STYLE_DEFAULT, "Consolas");
    send(SCI_STYLESETSIZE, STYLE_DEFAULT, 11);
    send(SCI_STYLECLEARALL);
    send(SCI_STYLESETFORE, STYLE_LINENUMBER, sciColor(t.muted));
    send(SCI_STYLESETBACK, STYLE_LINENUMBER, sciColor(t.panel));
    send(SCI_SETCARETFORE, sciColor(t.foreground));
    send(SCI_SETSELBACK, 1, sciColor(t.accent));
    auto selected = Theme::contrast(Qt::black, t.accent) > Theme::contrast(Qt::white, t.accent)
                        ? QColor(Qt::black)
                        : QColor(Qt::white);
    send(SCI_SETSELFORE, 1, sciColor(selected));
    auto color = [this](int s, const QColor &c) { send(SCI_STYLESETFORE, s, sciColor(c)); };
    if (lexer == QStringLiteral("cpp")) {
        for (int s : {SCE_C_COMMENT, SCE_C_COMMENTLINE, SCE_C_COMMENTDOC})
            color(s, t.comment);
        for (int s : {SCE_C_WORD, SCE_C_WORD2, SCE_C_PREPROCESSOR})
            color(s, t.keyword);
        for (int s : {SCE_C_STRING, SCE_C_CHARACTER, SCE_C_STRINGRAW})
            color(s, t.stringColor);
        color(SCE_C_NUMBER, t.number);
    } else if (lexer == QStringLiteral("python")) {
        color(SCE_P_COMMENTLINE, t.comment);
        color(SCE_P_WORD, t.keyword);
        for (int s : {SCE_P_STRING, SCE_P_CHARACTER, SCE_P_TRIPLE, SCE_P_TRIPLEDOUBLE})
            color(s, t.stringColor);
        color(SCE_P_NUMBER, t.number);
    } else if (lexer == QStringLiteral("tcl")) {
        color(SCE_TCL_COMMENT, t.comment);
        color(SCE_TCL_COMMENTLINE, t.comment);
        color(SCE_TCL_WORD, t.keyword);
        color(SCE_TCL_IN_QUOTE, t.stringColor);
        color(SCE_TCL_NUMBER, t.number);
    } else if (lexer == QStringLiteral("phpscript")) {
        color(SCE_HPHP_COMMENT, t.comment);
        color(SCE_HPHP_COMMENTLINE, t.comment);
        color(SCE_HPHP_WORD, t.keyword);
        color(SCE_HPHP_HSTRING, t.stringColor);
        color(SCE_HPHP_SIMPLESTRING, t.stringColor);
        color(SCE_HPHP_NUMBER, t.number);
    } else if (lexer == QStringLiteral("fortran")) {
        color(SCE_F_COMMENT, t.comment);
        color(SCE_F_WORD, t.keyword);
        color(SCE_F_WORD2, t.keyword);
        color(SCE_F_STRING1, t.stringColor);
        color(SCE_F_STRING2, t.stringColor);
        color(SCE_F_NUMBER, t.number);
    } else if (lexer == QStringLiteral("cobol")) {
        color(SCE_COBOL_COMMENT, t.comment);
        color(SCE_COBOL_COMMENTLINE, t.comment);
        color(SCE_COBOL_WORD, t.keyword);
        color(SCE_COBOL_STRING, t.stringColor);
        color(SCE_COBOL_CHARACTER, t.stringColor);
        color(SCE_COBOL_NUMBER, t.number);
    } else if (lexer == QStringLiteral("asm")) {
        color(SCE_ASM_COMMENT, t.comment);
        color(SCE_ASM_COMMENTBLOCK, t.comment);
        color(SCE_ASM_CPUINSTRUCTION, t.keyword);
        color(SCE_ASM_DIRECTIVE, t.keyword);
        color(SCE_ASM_STRING, t.stringColor);
        color(SCE_ASM_NUMBER, t.number);
    }
    if (lexer == QStringLiteral("rust")) {
        for (int v : {SCE_RUST_COMMENTBLOCK, SCE_RUST_COMMENTLINE, SCE_RUST_COMMENTBLOCKDOC,
                      SCE_RUST_COMMENTLINEDOC})
            color(v, t.comment);
        color(SCE_RUST_WORD, t.keyword);
        color(SCE_RUST_STRING, t.stringColor);
        color(SCE_RUST_STRINGR, t.stringColor);
        color(SCE_RUST_NUMBER, t.number);
    } else if (lexer == QStringLiteral("json")) {
        color(SCE_JSON_STRING, t.stringColor);
        color(SCE_JSON_PROPERTYNAME, t.keyword);
        color(SCE_JSON_NUMBER, t.number);
        color(SCE_JSON_KEYWORD, t.keyword);
        color(SCE_JSON_LINECOMMENT, t.comment);
        color(SCE_JSON_BLOCKCOMMENT, t.comment);
    } else if (lexer == QStringLiteral("sql")) {
        color(SCE_SQL_WORD, t.keyword);
        color(SCE_SQL_STRING, t.stringColor);
        color(SCE_SQL_CHARACTER, t.stringColor);
        color(SCE_SQL_COMMENT, t.comment);
        color(SCE_SQL_COMMENTLINE, t.comment);
        color(SCE_SQL_NUMBER, t.number);
    } else if (lexer == QStringLiteral("css")) {
        color(SCE_CSS_TAG, t.keyword);
        color(SCE_CSS_CLASS, t.keyword);
        color(SCE_CSS_COMMENT, t.comment);
        color(SCE_CSS_DOUBLESTRING, t.stringColor);
        color(SCE_CSS_SINGLESTRING, t.stringColor);
    } else if (lexer == QStringLiteral("hypertext")) {
        color(SCE_H_TAG, t.keyword);
        color(SCE_H_ATTRIBUTE, t.keyword);
        color(SCE_H_COMMENT, t.comment);
        color(SCE_H_DOUBLESTRING, t.stringColor);
        color(SCE_H_SINGLESTRING, t.stringColor);
    } else if (lexer == QStringLiteral("bash")) {
        color(SCE_SH_WORD, t.keyword);
        color(SCE_SH_STRING, t.stringColor);
        color(SCE_SH_CHARACTER, t.stringColor);
        color(SCE_SH_COMMENTLINE, t.comment);
        color(SCE_SH_NUMBER, t.number);
    } else if (lexer == QStringLiteral("powershell")) {
        color(SCE_POWERSHELL_KEYWORD, t.keyword);
        color(SCE_POWERSHELL_STRING, t.stringColor);
        color(SCE_POWERSHELL_COMMENT, t.comment);
        color(SCE_POWERSHELL_NUMBER, t.number);
    } else if (lexer == QStringLiteral("yaml")) {
        color(SCE_YAML_IDENTIFIER, t.keyword);
        color(SCE_YAML_COMMENT, t.comment);
        color(SCE_YAML_NUMBER, t.number);
    }
    send(SCI_COLOURISE, 0, -1);
}

bool Editor::saveCopy(const QString &path, QString &error) {
    const auto text = QString::fromUtf8(bytes());
    QTextCodec::ConverterState conversion(QTextCodec::IgnoreHeader);
    auto data = QTextCodec::codecForName(codecName)->fromUnicode(text.constData(), text.size(),
                                                                 &conversion);
    if (conversion.invalidChars) {
        error =
            ui(QStringLiteral("Alcuni caratteri non sono rappresentabili nella codifica originale"));
        return false;
    }
    data.prepend(bomPrefix);
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
        error = file.errorString();
        return false;
    }
    return true;
}

QString Editor::wordAtPosition(sptr_t position) const {
    if (position < 0 || position >= send(SCI_GETLENGTH)) return {};
    const auto character = static_cast<unsigned char>(send(SCI_GETCHARAT,position));
    if (character < 128 && !QChar(char16_t(character)).isLetterOrNumber() && character != '_' && character != '$') return {};
    const auto start = send(SCI_WORDSTARTPOSITION, position, 1);
    const auto end = send(SCI_WORDENDPOSITION, position, 1);
    if (end <= start || end-start > 4096) return {};
    const auto word = QString::fromUtf8(reinterpret_cast<const char *>(send(SCI_GETRANGEPOINTER, start, end-start)), int(end-start));
    static const QRegularExpression identifier(QStringLiteral("^[\\p{L}_$][\\p{L}\\p{N}_$]*$"));
    return identifier.match(word).hasMatch() ? word : QString();
}
void Editor::contextMenuEvent(QContextMenuEvent *event) {
    const auto position = event->reason()==QContextMenuEvent::Keyboard ? send(SCI_GETCURRENTPOS) : send(SCI_POSITIONFROMPOINTCLOSE,event->pos().x(),event->pos().y());
    const auto word = wordAtPosition(position);
    if (position>=0 && (position<send(SCI_GETSELECTIONSTART) || position>=send(SCI_GETSELECTIONEND)))
        send(SCI_SETSEL,position,position);
    QMenu menu(this);
    auto add = [&](const QString &label, unsigned int command, bool enabled) {
        auto action=menu.addAction(label);action->setEnabled(enabled);
        connect(action,&QAction::triggered,this,[this,command]{send(command);});
    };
    const bool editable=!send(SCI_GETREADONLY), selected=send(SCI_GETSELECTIONSTART)!=send(SCI_GETSELECTIONEND);
    add(ui(QStringLiteral("Annulla")),SCI_UNDO,editable && send(SCI_CANUNDO));
    add(ui(QStringLiteral("Ripeti")),SCI_REDO,editable && send(SCI_CANREDO));
    menu.addSeparator();
    add(ui(QStringLiteral("Taglia")),SCI_CUT,editable && selected);
    add(ui(QStringLiteral("Copia")),SCI_COPY,selected);
    add(ui(QStringLiteral("Incolla")),SCI_PASTE,editable && send(SCI_CANPASTE));
    add(ui(QStringLiteral("Elimina")),SCI_CLEAR,editable && selected);
    add(ui(QStringLiteral("Seleziona tutti")),SCI_SELECTALL,true);
    menu.addSeparator();
    auto xref=menu.addAction(ui(QStringLiteral("Cerca riferimento incrociato")));
    xref->setObjectName(QStringLiteral("editorCrossReference"));xref->setEnabled(!word.isEmpty());
    if(menu.exec(event->globalPos())==xref && !word.isEmpty()) emit crossReferenceRequested(word);
    event->accept();
}
