// Stand-in for the GTK backend's AppChooser, for tests/open-with-test.py: answers every ChooseApplication with choice "gtkpick".
#include <QCoreApplication>
#include <QDBusAbstractAdaptor>
#include <QDBusConnection>
#include <QDBusObjectPath>
#include <QVariantMap>

class Adaptor : public QDBusAbstractAdaptor {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.impl.portal.AppChooser")
public:
    Adaptor(QObject *p) : QDBusAbstractAdaptor(p) {}
public slots:
    uint ChooseApplication(const QDBusObjectPath &, const QString &, const QString &, const QStringList &, const QVariantMap &, QVariantMap &results) {
        results = {{QStringLiteral("choice"), QStringLiteral("gtkpick")}};
        return 0;
    }
};

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QObject root;
    new Adaptor(&root);
    auto bus = QDBusConnection::sessionBus();
    if (!bus.registerObject(QStringLiteral("/org/freedesktop/portal/desktop"), &root, QDBusConnection::ExportAdaptors)
        || !bus.registerService(QStringLiteral("org.freedesktop.impl.portal.desktop.gtk")))
        return 1;
    return app.exec();
}
#include "fake-gtk.moc"
