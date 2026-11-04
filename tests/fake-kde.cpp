// Stand-in for org.freedesktop.impl.portal.desktop.kde's FileChooser, for tests/jail-test.sh: answers
// SaveFile and SaveFiles with a file named after the call's options, and logs what it was sent.
#include <QCoreApplication>
#include <QDBusAbstractAdaptor>
#include <QDBusConnection>
#include <QDBusContext>
#include <QDBusObjectPath>
#include <QFile>
#include <QTextStream>
#include <QTimer>
#include <QDir>
#include <QUrl>
#include <QVariantMap>

class FakeRequest : public QObject {
    Q_OBJECT
};
class RequestAdaptor : public QDBusAbstractAdaptor {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.impl.portal.Request")
public:
    RequestAdaptor(QObject *p) : QDBusAbstractAdaptor(p) {}
public slots:
    void Close() { log(QStringLiteral("kde: Request.Close")); }
    static void log(const QString &line) {
        QFile f(qEnvironmentVariable("FAKE_KDE_LOG"));
        if (f.open(QIODevice::Append)) QTextStream(&f) << line << "\n";
    }
};

class Adaptor : public QDBusAbstractAdaptor, protected QDBusContext {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.impl.portal.FileChooser")
public:
    Adaptor(QObject *p) : QDBusAbstractAdaptor(p) {}
public slots:
    uint SaveFile(const QDBusObjectPath &handle, const QString &app, const QString &parent, const QString &title, const QVariantMap &options, QVariantMap &results) { return answer(QStringLiteral("SaveFile"), handle, app, parent, title, options, results); }
    uint SaveFiles(const QDBusObjectPath &handle, const QString &app, const QString &parent, const QString &title, const QVariantMap &options, QVariantMap &results) { return answer(QStringLiteral("SaveFiles"), handle, app, parent, title, options, results); }
private:
    uint answer(const QString &name, const QDBusObjectPath &handle, const QString &app, const QString &parent, const QString &title, const QVariantMap &options, QVariantMap &results) {
        const QString current = options.value(QStringLiteral("current_name")).toString();
        RequestAdaptor::log(QStringLiteral("kde: %1 handle=%2 app=%3 parent=%4 title=%5 keys=%6 current_name=%7").arg(name, handle.path(), app, parent, title, QStringList(options.keys()).join(','), current));
        results.insert(QStringLiteral("uris"), QStringList{QString::fromLatin1(QUrl::fromLocalFile(QDir::homePath() + QStringLiteral("/Documents/") + (current.isEmpty() ? QStringLiteral("saved by kde") : current)).toEncoded())});
        return 0;
    }
};

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QObject chooser;
    new Adaptor(&chooser);
    auto bus = QDBusConnection::sessionBus();
    bus.registerObject(QStringLiteral("/org/freedesktop/portal/desktop"), &chooser, QDBusConnection::ExportAdaptors);
    FakeRequest request;
    new RequestAdaptor(&request);
    bus.registerService(QStringLiteral("org.freedesktop.impl.portal.desktop.kde"));
    return app.exec();
}

#include "fake-kde.moc"
