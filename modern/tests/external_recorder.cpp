#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    auto args = app.arguments();
    if (args.size() < 2)
        return 1;
    QFile f(args[1]);
    if (!f.open(QIODevice::WriteOnly))
        return 2;
    QJsonArray result;
    for (int i = 2; i < args.size(); ++i)
        result.append(args[i]);
    f.write(QJsonDocument(result).toJson());
    return 0;
}
