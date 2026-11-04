// Test client for tests/run.sh: one call to org.freedesktop.portal.FileChooser. It stays on the bus until the
// Response signal arrives (the portal closes a request whose caller has left the bus) and prints it.
//   portal-call METHOD TITLE [multiple] [directory] [folder=PATH] [name=NAME] [file=PATH] [files=A,B] [close=MS] [token=T]
// close=MS calls Request.Close that many ms after the call and exits without waiting for a Response.
#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QTextStream>
#include <QTimer>
#include <QVariantMap>

class Receiver : public QObject {
    Q_OBJECT
public slots:
    void Response(uint response, const QVariantMap &results) {
        QTextStream out(stdout);
        out << "Response: " << response;
        for (auto it = results.begin(); it != results.end(); ++it) {
            const QVariant v = it.value();
            out << "  " << it.key() << "=" << (v.canConvert<QStringList>() && v.typeId() == QMetaType::QStringList ? v.toStringList().join(QStringLiteral(" | ")) : v.toString());
        }
        out << "\n";
        out.flush();
        QCoreApplication::exit(0);
    }
};

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    qDBusRegisterMetaType<QList<QByteArray>>();
    const QStringList args = app.arguments().mid(1);
    if (args.size() < 2)
        return 2;
    QVariantMap options;
    QString token = QStringLiteral("t%1").arg(QCoreApplication::applicationPid());
    int closeAfter = -1;
    for (const QString &a : args.mid(2)) {
        if (a == QLatin1String("multiple") || a == QLatin1String("directory"))
            options.insert(a, true);
        else if (a.startsWith(QLatin1String("folder=")))
            options.insert(QStringLiteral("current_folder"), a.mid(7).toLocal8Bit() + '\0');
        else if (a.startsWith(QLatin1String("name=")))
            options.insert(QStringLiteral("current_name"), a.mid(5));
        else if (a.startsWith(QLatin1String("file=")))
            options.insert(QStringLiteral("current_file"), a.mid(5).toLocal8Bit() + '\0');
        else if (a.startsWith(QLatin1String("files="))) {
            QList<QByteArray> names;
            for (const QString &n : a.mid(6).split(QLatin1Char(',')))
                names.append(n.toLocal8Bit() + '\0');
            options.insert(QStringLiteral("files"), QVariant::fromValue(names));
        } else if (a.startsWith(QLatin1String("close=")))
            closeAfter = a.mid(6).toInt();
        else if (a.startsWith(QLatin1String("token=")))
            token = a.mid(6);
    }
    options.insert(QStringLiteral("handle_token"), token);
    auto bus = QDBusConnection::sessionBus();
    QString sender = bus.baseService().mid(1);
    sender.replace('.', '_');
    const QString handle = QStringLiteral("/org/freedesktop/portal/desktop/request/%1/%2").arg(sender, token);
    const QString service = QStringLiteral("org.freedesktop.portal.Desktop");

    Receiver receiver;
    bus.connect(service, handle, QStringLiteral("org.freedesktop.portal.Request"), QStringLiteral("Response"), &receiver, SLOT(Response(uint, QVariantMap)));
    QDBusMessage call = QDBusMessage::createMethodCall(service, QStringLiteral("/org/freedesktop/portal/desktop"), QStringLiteral("org.freedesktop.portal.FileChooser"), args[0]);
    call.setArguments({QString(), args[1], options});
    const QDBusMessage reply = bus.call(call);
    if (reply.type() != QDBusMessage::ReplyMessage) {
        QTextStream(stdout) << "call failed: " << reply.errorMessage() << "\n";
        return 1;
    }
    QTextStream(stdout) << "request " << reply.arguments().value(0).value<QDBusObjectPath>().path() << "\n";
    QTextStream(stdout).flush();
    if (closeAfter >= 0)
        QTimer::singleShot(closeAfter, &app, [&] {
            bus.call(QDBusMessage::createMethodCall(service, handle, QStringLiteral("org.freedesktop.portal.Request"), QStringLiteral("Close")));
            QTextStream(stdout) << "closed\n";
            QCoreApplication::exit(0);
        });
    QTimer::singleShot(30000, &app, [] { QTextStream(stdout) << "no Response in 30 s\n"; QCoreApplication::exit(3); });
    return app.exec();
}

#include "portal-call.moc"
