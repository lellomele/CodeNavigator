#include <QGuiApplication>
#include <QPainter>
#include <QBuffer>
#include <QDataStream>
#include <QDir>
#include <QFile>

// Vector geometry is the editable source for every icon size.
static QImage render(int size) {
    QImage img(size,size,QImage::Format_ARGB32_Premultiplied);img.fill(Qt::transparent);
    QPainter p(&img);p.setRenderHint(QPainter::Antialiasing);p.scale(size/256.0,size/256.0);
    QLinearGradient bg(20,16,228,240);bg.setColorAt(0,QColor("#315FCA"));bg.setColorAt(1,QColor("#102443"));
    p.setPen(Qt::NoPen);p.setBrush(bg);p.drawRoundedRect(QRectF(10,10,236,236),48,48);
    p.setPen(QPen(QColor("#6CE6F5"),17,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));
    QPolygonF left;left<<QPointF(80,64)<<QPointF(45,102)<<QPointF(80,140);p.drawPolyline(left);
    QPolygonF right;right<<QPointF(141,64)<<QPointF(176,102)<<QPointF(141,140);p.drawPolyline(right);
    p.setPen(QPen(QColor("#FFFFFF"),12,Qt::SolidLine,Qt::RoundCap));p.drawLine(QPointF(120,66),QPointF(100,136));
    p.setPen(QPen(QColor("#0B1A32"),24,Qt::SolidLine,Qt::RoundCap));p.drawLine(QPointF(181,180),QPointF(220,219));
    p.setPen(QPen(QColor("#FFAF45"),16,Qt::SolidLine,Qt::RoundCap));p.drawLine(QPointF(181,180),QPointF(220,219));
    p.setPen(QPen(QColor("#0B1A32"),23));p.setBrush(QColor("#19385E"));p.drawEllipse(QPointF(157,157),38,38);
    p.setPen(QPen(QColor("#FFAF45"),12));p.drawEllipse(QPointF(157,157),38,38);
    p.end();return img;
}
int main(int argc,char **argv){
    QGuiApplication app(argc,argv);if(app.arguments().size()!=2)return 1;
    QDir dir(app.arguments()[1]);if(!dir.mkpath("."))return 2;
    if(!render(512).save(dir.filePath("source-navigator.png")))return 3;
    const QList<int> sizes={16,24,32,48,64,128,256};QList<QByteArray> images;
    for(int size:sizes){QByteArray bytes;QBuffer buffer(&bytes);buffer.open(QIODevice::WriteOnly);if(!render(size).save(&buffer,"PNG"))return 4;images<<bytes;}
    QFile ico(dir.filePath("source-navigator.ico"));if(!ico.open(QIODevice::WriteOnly))return 5;
    QDataStream out(&ico);out.setByteOrder(QDataStream::LittleEndian);out<<quint16(0)<<quint16(1)<<quint16(sizes.size());
    quint32 offset=6+16*sizes.size();
    for(int i=0;i<sizes.size();++i){out<<quint8(sizes[i]==256?0:sizes[i])<<quint8(sizes[i]==256?0:sizes[i])<<quint8(0)<<quint8(0)<<quint16(1)<<quint16(32)<<quint32(images[i].size())<<offset;offset+=images[i].size();}
    for(const auto &bytes:images)out.writeRawData(bytes.constData(),bytes.size());
    return out.status()==QDataStream::Ok?0:6;
}
