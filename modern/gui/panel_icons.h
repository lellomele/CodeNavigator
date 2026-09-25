#pragma once
#include <QAbstractButton>
#include <QApplication>
#include <QPainter>
inline void setPanelIcon(QAbstractButton *button, const QString &kind) {
    button->setProperty("panelControl",true);button->setProperty("panelAction",kind);
    QPixmap icon(32,32);icon.setDevicePixelRatio(2);icon.fill(Qt::transparent);
    QPainter painter(&icon);painter.setPen(QPen(qApp->palette().color(QPalette::WindowText),1.5));
    if(kind==QStringLiteral("minimize")) painter.drawLine(QPointF(3,12),QPointF(13,12));
    else if(kind==QStringLiteral("restore")){painter.drawRect(QRectF(6,3,7,7));painter.fillRect(QRectF(3,6,7,7),qApp->palette().color(QPalette::Window));painter.drawRect(QRectF(3,6,7,7));}
    else painter.drawRect(QRectF(3,3,10,10));
    button->setIcon(QIcon(icon));button->setIconSize(QSize(16,16));button->setFixedSize(26,26);
}
