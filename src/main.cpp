#include "appchooser.h"
#include "chooser.h"
#include "dialogbridge.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDebug>

// org.freedesktop.impl.portal.desktop.tacit: xdg-desktop-portal's FileChooser backend for Tacit.
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QDBusConnection bus = QDBusConnection::sessionBus();
    Chooser chooser;
    DialogBridge bridge;   // the shell's dialogs; a failure to listen is logged and does not stop the portal
    bridge.listen(DialogBridge::defaultPath());
    AppChooser appChooser(&bridge);
    new ChooserAdaptor(&chooser);
    new AppChooserAdaptor(&chooser, &appChooser);
    if (!bus.registerObject(QStringLiteral("/org/freedesktop/portal/desktop"), &chooser, QDBusConnection::ExportAdaptors)
        || !bus.registerService(QStringLiteral("org.freedesktop.impl.portal.desktop.tacit"))) {
        qCritical() << "tacit-portal: cannot register on the session bus:" << bus.lastError().message();
        return 1;
    }
    return app.exec();
}
