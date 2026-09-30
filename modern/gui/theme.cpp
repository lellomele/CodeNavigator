#include "localization.h"
#include "theme.h"
#include "panel_icons.h"
#include <QApplication>
#include <QPalette>
#include <QPushButton>
#include <cmath>

QVector<Theme> Theme::presets() {
    return {{QStringLiteral("Giorno neutro"), QColor("#FFFFFF"), QColor("#1F2328"),
             QColor("#F0F3F6"), QColor("#586069"), QColor("#0969DA"), QColor("#6639BA"),
             QColor("#116329"), QColor("#586069"), QColor("#953800")},
            {QStringLiteral("Giorno caldo"), QColor("#FFF8EE"), QColor("#302B25"),
             QColor("#F2E9DC"), QColor("#60564B"), QColor("#005A70"), QColor("#6C3483"),
             QColor("#386324"), QColor("#60564B"), QColor("#8B3A14")},
            {QStringLiteral("Notte grafite"), QColor("#161B22"), QColor("#E6EDF3"),
             QColor("#21262D"), QColor("#A8B3BF"), QColor("#79C0FF"), QColor("#D2A8FF"),
             QColor("#A5D6A7"), QColor("#A8B3BF"), QColor("#FFA657")},
            {QStringLiteral("Notte blu"), QColor("#0B1220"), QColor("#E2E8F0"), QColor("#182236"),
             QColor("#A6B6CD"), QColor("#67E8F9"), QColor("#C4B5FD"), QColor("#86EFAC"),
             QColor("#A6B6CD"), QColor("#FCD34D")}};
}
static double luminance(const QColor &c) {
    auto channel = [](double x) {
        return x <= 0.04045 ? x / 12.92 : std::pow((x + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * channel(c.redF()) + 0.7152 * channel(c.greenF()) + 0.0722 * channel(c.blueF());
}
double Theme::contrast(const QColor &a, const QColor &b) {
    double x = luminance(a), y = luminance(b);
    return (std::max(x, y) + 0.05) / (std::min(x, y) + 0.05);
}
QString Theme::validate() const {
    const QVector<QPair<QString, QColor>> colors = {
        {ui(QStringLiteral("testo")), foreground},     {ui(QStringLiteral("testo secondario")), muted},
        {ui(QStringLiteral("accento")), accent},       {ui(QStringLiteral("parole chiave")), keyword},
        {ui(QStringLiteral("stringhe")), stringColor}, {ui(QStringLiteral("commenti")), comment},
        {ui(QStringLiteral("numeri")), number}};
    if (!background.isValid() || !panel.isValid())
        return ui(QStringLiteral("Sfondo non valido"));
    for (const auto &item : colors)
        if (!item.second.isValid() || contrast(item.second, background) < 4.5)
            return ui(QStringLiteral("Contrasto insufficiente per %1: minimo 4,5:1")).arg(item.first);
    if (contrast(foreground, panel) < 4.5 || contrast(muted, panel) < 4.5)
        return ui(QStringLiteral("Contrasto insufficiente sui pannelli"));
    return {};
}
QJsonObject Theme::json() const {
    return {{QStringLiteral("name"), name},
            {QStringLiteral("background"), background.name()},
            {QStringLiteral("foreground"), foreground.name()},
            {QStringLiteral("panel"), panel.name()},
            {QStringLiteral("muted"), muted.name()},
            {QStringLiteral("accent"), accent.name()},
            {QStringLiteral("keyword"), keyword.name()},
            {QStringLiteral("string"), stringColor.name()},
            {QStringLiteral("comment"), comment.name()},
            {QStringLiteral("number"), number.name()}};
}
Theme Theme::fromJson(const QJsonObject &o) {
    return {o.value(QStringLiteral("name")).toString(),
            QColor(o.value(QStringLiteral("background")).toString()),
            QColor(o.value(QStringLiteral("foreground")).toString()),
            QColor(o.value(QStringLiteral("panel")).toString()),
            QColor(o.value(QStringLiteral("muted")).toString()),
            QColor(o.value(QStringLiteral("accent")).toString()),
            QColor(o.value(QStringLiteral("keyword")).toString()),
            QColor(o.value(QStringLiteral("string")).toString()),
            QColor(o.value(QStringLiteral("comment")).toString()),
            QColor(o.value(QStringLiteral("number")).toString())};
}
void Theme::apply(QApplication &app) const {
    static const bool resources = [] {
        Q_INIT_RESOURCE(icons);
        return true;
    }();
    Q_UNUSED(resources);
    QPalette p;
    p.setColor(QPalette::Window, panel);
    p.setColor(QPalette::WindowText, foreground);
    p.setColor(QPalette::Base, background);
    p.setColor(QPalette::AlternateBase, panel);
    p.setColor(QPalette::Text, foreground);
    p.setColor(QPalette::Button, panel);
    p.setColor(QPalette::ButtonText, foreground);
    p.setColor(QPalette::Highlight, accent);
    p.setColor(QPalette::HighlightedText,
               contrast(Qt::black, accent) > contrast(Qt::white, accent) ? Qt::black : Qt::white);
    p.setColor(QPalette::ToolTipBase, background);
    p.setColor(QPalette::ToolTipText, foreground);
    p.setColor(QPalette::Link, accent);
    p.setColor(QPalette::PlaceholderText, muted);
    p.setColor(QPalette::Disabled, QPalette::WindowText, muted);
    p.setColor(QPalette::Disabled, QPalette::Text, muted);
    p.setColor(QPalette::Disabled, QPalette::ButtonText, muted);
    app.setPalette(p);
    const auto selected = contrast(Qt::black, accent) > contrast(Qt::white, accent)
                              ? QStringLiteral("#000000")
                              : QStringLiteral("#FFFFFF");
    app.setStyleSheet(
        QStringLiteral(
            "QMainWindow {background:%1;} QFrame#card {background:%1; border:1px solid %2; "
            "border-radius:10px;} "
            "QFrame#hero {background:qlineargradient(x1:0,y1:0,x2:1,y2:0,stop:0 %2,stop:1 %1); "
            "border-radius:12px; border-left:4px solid %5;} "
            "QLabel#brand {font-size:20px; font-weight:700; letter-spacing:2px; color:%3;} "
            "QLabel#section {font-size:11px; font-weight:700; letter-spacing:1px; color:%4; "
            "padding:4px 0;} "
            "QLabel#subtitle {color:%4; font-size:11px;} "
            "QLineEdit,QComboBox {padding:8px; border:1px solid %4; border-radius:6px; "
            "background:%1; color:%3;} "
            "QComboBox {padding-right:28px;} "
            "QComboBox::drop-down {subcontrol-origin:padding; subcontrol-position:top right; width:24px; border:0; background:transparent;} "
            "QComboBox::down-arrow {image:url(:/icons/arrow-%8.xpm); width:9px; height:5px;} "
            "QComboBox QAbstractItemView {background:%1; color:%3; selection-background-color:%5; selection-color:%6; outline:0; border:1px solid %4;} "
            "QComboBox QAbstractItemView::item:selected {background:%5; color:%6;} "
            "QLineEdit:focus {border:1px solid %5;} QPushButton {padding:8px 12px; border:1px "
            "solid %4; border-radius:6px; background:%2;} "
            "QPushButton[panelControl=true],QToolButton[panelControl=true] {padding:3px;border:1px solid transparent;border-radius:3px;background:transparent;} "
            "QPushButton[panelControl=true]:hover,QToolButton[panelControl=true]:hover {border:1px solid %5;background:%2;} "
            "QPushButton:hover {border-color:%5;} "
            "QToolButton#replaceToggle {padding:8px; border:1px solid %4; border-radius:6px; background:%2; color:%3; text-align:left;} "
            "QToolButton#replaceToggle:hover,QToolButton#replaceToggle:checked {border-color:%5;} "
            "QPushButton#replacePreview,QPushButton#openProject {background:%5; color:%6; "
            "border:0; font-weight:600;} "
            "QPushButton:disabled {color:%4; background:%2;} "
            "QTreeView {border:0; background:%1; outline:0;} QTreeView::item {padding:5px 2px;} "
            "QTreeView::item:selected {background:%5; color:%6;} "
            "QCheckBox::indicator,QTreeView::indicator {width:14px;height:14px;"
            "border:1px solid %4;border-radius:3px;background:%1;} "
            "QCheckBox::indicator:checked,QTreeView::indicator:checked {background:%5;"
            "border:2px solid %5;image:url(:/icons/check-%7.xpm);} "
            "QHeaderView::section {padding:8px; background:%2; color:%3; border:0;} "
            "QTabWidget::pane {border:0;} QTabBar::tab {padding:10px 16px; background:%2;} "
            "QTabBar::tab:selected {background:%1; border-bottom:2px solid %5;} "
            "QSplitter::handle {background:%2; width:5px;} QStatusBar {padding:5px; "
            "background:%2;} "
            "QMenuBar {background:%2; color:%3;} QMenuBar::item {padding:6px 12px; background:transparent; color:%3;} "
            "QMenuBar::item:selected,QMenuBar::item:pressed,QMenu::item:selected {background:%5; color:%6;} "
            "QMenu {background:%2; color:%3; border:1px solid %4;} QMenu::item {padding:6px 24px;} "
            "QMenu::item:disabled {color:%4;} "
            "QProgressBar {border:0; background:%2; border-radius:3px; text-align:center;} "
            "QProgressBar::chunk {background:%5;}")
            .arg(background.name(), panel.name(), foreground.name(), muted.name(), accent.name(),
                 selected,
                 selected == QStringLiteral("#000000") ? QStringLiteral("dark")
                                                       : QStringLiteral("light"),
                 luminance(background) < 0.4 ? QStringLiteral("light") : QStringLiteral("dark")));
    auto creditColor = accent;
    if (contrast(creditColor, panel) < 4.5)
        creditColor = foreground;
    for (auto widget : app.allWidgets()) {
        if (auto button = qobject_cast<QAbstractButton *>(widget); button && button->property("panelControl").toBool())
            setPanelIcon(button,button->property("panelAction").toString());
        if (widget->objectName() == QStringLiteral("authorCredit"))
            widget->setStyleSheet(QStringLiteral(
                "QPushButton {background:transparent; border:1px solid transparent; "
                "border-radius:0; padding:1px 12px 1px 6px; color:%1;} "
                "QPushButton:hover,QPushButton:focus {border-bottom:1px solid %1;}")
                .arg(creditColor.name()));
    }
}
