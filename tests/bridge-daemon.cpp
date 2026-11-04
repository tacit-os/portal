// DialogBridge on its own: listens on $TACIT_DIALOGS_SOCK. Each line "ask" on stdin makes one in-process ask
// (kind test.inproc); how it ended is printed to stdout, for tests/bridge-test.py.
#include "../src/dialogbridge.h"
#include <QCoreApplication>
#include <QJsonObject>
#include <QSocketNotifier>
#include <cstdio>
#include <iostream>
#include <string>

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    DialogBridge b;
    if (!b.listen(DialogBridge::defaultPath())) return 1;
    QSocketNotifier in(0, QSocketNotifier::Read);
    QObject::connect(&in, &QSocketNotifier::activated, [&] {
        std::string l;
        if (!std::getline(std::cin, l)) { in.setEnabled(false); return; }
        if (l != "ask") return;
        b.ask("test.inproc", {{"n", 1}}, [](bool ok, const QJsonObject &r, const QString &why) {
            printf("inproc %s %s %s\n", ok ? "ok" : "rejected", qPrintable(why), qPrintable(r.value("v").toString()));
            fflush(stdout);
        });
    });
    printf("ready\n");
    fflush(stdout);
    return app.exec();
}
