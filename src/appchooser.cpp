#include "appchooser.h"

#include "chooser.h"
#include "dialogbridge.h"
#include "request.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusPendingCallWatcher>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMimeDatabase>
#include <QProcess>
#include <QTimer>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QDebug>

static const QString gtkBackend = QStringLiteral("org.freedesktop.impl.portal.desktop.gtk");
static const QString portalPath = QStringLiteral("/org/freedesktop/portal/desktop");
static const QString appChooserInterface = QStringLiteral("org.freedesktop.impl.portal.AppChooser");
static const QString requestInterface = QStringLiteral("org.freedesktop.impl.portal.Request");

// Tacit's own apps, in the order they are recommended: the first of them that opens the type is filled with the
// accent and preselected. (tacit-viewer is the viewer app, not built yet; an absent desktop entry is skipped.)
static const QStringList tacitApps = {
    QStringLiteral("tacit-viewer.desktop"), QStringLiteral("browser.desktop"), QStringLiteral("code.desktop"),
    QStringLiteral("claude-app.desktop"), QStringLiteral("quiet-files.desktop"),
};

static QStringList appDirs() {
    return QStandardPaths::standardLocations(QStandardPaths::ApplicationsLocation);
}

// the desktop entry's file, or empty. A subfolder is written with dashes in the id (kde-foo.desktop).
static QString desktopFile(const QString &id) {
    for (const QString &dir : appDirs()) {
        QString rel = id;
        while (true) {
            if (QFileInfo::exists(dir + QLatin1Char('/') + rel))
                return dir + QLatin1Char('/') + rel;
            const int dash = rel.indexOf(QLatin1Char('-'));
            if (dash < 0)
                break;
            rel[dash] = QLatin1Char('/');
            if (QFileInfo::exists(dir + QLatin1Char('/') + rel))
                return dir + QLatin1Char('/') + rel;
        }
    }
    return {};
}

// [Desktop Entry] keys of one file, first value wins (translations are not read)
static QHash<QString, QString> readEntry(const QString &path) {
    QHash<QString, QString> kv;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return kv;
    bool inEntry = false;
    for (const QByteArray &raw : f.readAll().split('\n')) {
        const QString line = QString::fromUtf8(raw).trimmed();
        if (line.startsWith(QLatin1Char('['))) {
            inEntry = line == QLatin1String("[Desktop Entry]");
            continue;
        }
        const int eq = line.indexOf(QLatin1Char('='));
        if (inEntry && eq > 0 && !kv.contains(line.left(eq)))
            kv.insert(line.left(eq), line.mid(eq + 1));
    }
    return kv;
}

static QString withSuffix(const QString &id) {
    return id.endsWith(QLatin1String(".desktop")) ? id : id + QStringLiteral(".desktop");
}

static QString xdgDefault(const QString &contentType) {
    if (contentType.isEmpty())
        return {};
    QProcess p;
    p.start(QStringLiteral("xdg-mime"), {QStringLiteral("query"), QStringLiteral("default"), contentType});
    if (!p.waitForFinished(3000))
        return {};
    return QString::fromUtf8(p.readAllStandardOutput()).trimmed();
}

// Same set as Pinax's Open with (FilesWindow.qml openWithProbe): the system default, then every visible entry whose
// MimeType lists the type. Used when the portal passed no choices.
static QStringList scanForType(const QString &contentType) {
    QStringList ids;
    const QString def = xdgDefault(contentType);
    if (!def.isEmpty())
        ids << def;
    if (contentType.isEmpty())
        return ids;
    for (const QString &dir : appDirs()) {
        for (const QString &name : QDir(dir).entryList({QStringLiteral("*.desktop")}, QDir::Files)) {
            const auto kv = readEntry(dir + QLatin1Char('/') + name);
            if (kv.value(QStringLiteral("NoDisplay")) == QLatin1String("true") || kv.value(QStringLiteral("Hidden")) == QLatin1String("true"))
                continue;
            if (kv.value(QStringLiteral("MimeType")).split(QLatin1Char(';')).contains(contentType) && !ids.contains(name))
                ids << name;
        }
    }
    return ids;
}

QList<AppChooser::App> AppChooser::appsFor(const QStringList &given, const QString &contentType) {
    const QStringList choices = given.isEmpty() ? scanForType(contentType) : given;
    QList<App> tacit, other;
    QSet<QString> seen;
    for (const QString &c : choices) {
        const QString full = withSuffix(c);
        if (seen.contains(full))
            continue;
        const auto kv = readEntry(desktopFile(full));
        if (kv.isEmpty() || kv.value(QStringLiteral("Name")).isEmpty())
            continue;
        seen.insert(full);
        App a;
        a.id = c;
        a.name = kv.value(QStringLiteral("Name"));
        a.icon = kv.value(QStringLiteral("Icon"));
        a.tacit = tacitApps.contains(full);
        (a.tacit ? tacit : other).append(a);
    }
    std::stable_sort(tacit.begin(), tacit.end(), [](const App &x, const App &y) {
        return tacitApps.indexOf(withSuffix(x.id)) < tacitApps.indexOf(withSuffix(y.id));
    });
    return tacit + other;
}

// "Always use for PDF", "Always use for .7z": short extensions of letters read as the name of the kind. A tarball
// ("tar.gz" would read as "GZ") or a file with no extension uses the MIME database's own words ("Compressed archive").
QString AppChooser::kindLabel(const QString &filename, const QString &contentType) {
    static const QRegularExpression tarball(QStringLiteral("\\.tar\\.[A-Za-z0-9]+$"), QRegularExpression::CaseInsensitiveOption);
    QString ext = tarball.match(filename).hasMatch() ? QString() : QFileInfo(filename).suffix();
    const QMimeType mime = contentType.isEmpty() ? QMimeType() : QMimeDatabase().mimeTypeForName(contentType);
    if (ext.isEmpty() && !tarball.match(filename).hasMatch())
        ext = mime.preferredSuffix();
    if (ext.isEmpty()) {
        const QString comment = mime.comment();
        return !comment.isEmpty() && comment != contentType ? comment : contentType;
    }
    static const QRegularExpression letters(QStringLiteral("^[A-Za-z]{2,4}$"));
    return letters.match(ext).hasMatch() ? ext.toUpper() : QLatin1Char('.') + ext;
}

// the app opened last time, else the recommended Tacit app, else the system default, else the first
int AppChooser::preselect(const QList<App> &apps, const QString &lastChoice, const QString &contentType) {
    if (apps.isEmpty())
        return -1;
    if (!lastChoice.isEmpty())
        for (int i = 0; i < apps.size(); ++i)
            if (withSuffix(apps[i].id) == withSuffix(lastChoice))
                return i;
    if (apps.first().tacit)
        return 0;
    for (const QString &want : {xdgDefault(contentType)}) {
        if (want.isEmpty())
            continue;
        for (int i = 0; i < apps.size(); ++i)
            if (withSuffix(apps[i].id) == withSuffix(want))
                return i;
    }
    return 0;
}

AppChooser::AppChooser(DialogBridge *bridge, QObject *parent) : QObject(parent), m_bridge(bridge) {}

static void reply(const QDBusMessage &message, uint response, const QVariantMap &results = {}) {
    QDBusConnection::sessionBus().send(message.createReply(QList<QVariant>{response, results}));
}

void AppChooser::choose(const QDBusMessage &message) {
    const QList<QVariant> args = message.arguments();
    const QString handle = qdbus_cast<QDBusObjectPath>(args.value(0)).path();
    auto *request = new Request(handle, this);
    const QStringList choices = qdbus_cast<QStringList>(args.value(3));
    const QVariantMap options = qdbus_cast<QVariantMap>(args.value(4));
    const QString contentType = options.value(QStringLiteral("content_type")).toString();

    const QList<App> apps = appsFor(choices, contentType);
    if (apps.isEmpty() || !m_bridge->uiConnected()) {
        forward(message, request);
        return;
    }

    // Who asked is not in the call; the focused window is the best guess, as in the share picker.
    // A Close that arrives while hyprctl runs ends the call here; hyprctl gets a second at most.
    auto *hypr = new QProcess(this);
    auto *limit = new QTimer(hypr);
    limit->setSingleShot(true);
    auto started = QSharedPointer<bool>::create(false);
    auto go = [this, message, request, apps, hypr, started](const QJsonObject &win) {
        if (*started)
            return;
        *started = true;
        hypr->disconnect();
        hypr->kill();
        hypr->deleteLater();
        ask(message, request, win, apps);
    };
    connect(request, &Request::closeRequested, hypr, [this, message, request, hypr, started] {
        if (*started)
            return;
        *started = true;
        hypr->disconnect();
        hypr->kill();
        hypr->deleteLater();
        reply(message, 2);
        request->deleteLater();
    });
    connect(limit, &QTimer::timeout, this, [go] { go({}); });
    connect(hypr, &QProcess::errorOccurred, this, [go] { go({}); });
    connect(hypr, &QProcess::finished, this, [go, hypr] {
        const QJsonObject w = QJsonDocument::fromJson(hypr->readAllStandardOutput()).object();
        QJsonObject asker;
        const QJsonArray at = w.value(QStringLiteral("at")).toArray(), size = w.value(QStringLiteral("size")).toArray();
        if (at.size() == 2 && size.size() == 2)
            asker = {{QStringLiteral("x"), at[0]}, {QStringLiteral("y"), at[1]}, {QStringLiteral("w"), size[0]},
                     {QStringLiteral("h"), size[1]}, {QStringLiteral("monitor"), w.value(QStringLiteral("monitor"))}};
        go(asker);
    });
    hypr->start(QStringLiteral("hyprctl"), {QStringLiteral("-j"), QStringLiteral("activewindow")});
    limit->start(1000);
}

void AppChooser::ask(const QDBusMessage &message, Request *request, const QJsonObject &asker, const QList<App> &apps) {
    const QList<QVariant> args = message.arguments();
    const QVariantMap options = qdbus_cast<QVariantMap>(args.value(4));
    const QString contentType = options.value(QStringLiteral("content_type")).toString();
    const QString filename = options.value(QStringLiteral("filename")).toString();
    const QString lastChoice = options.value(QStringLiteral("last_choice")).toString();

    QJsonArray rows;
    for (const App &a : apps)
        rows.append(QJsonObject{{QStringLiteral("id"), a.id}, {QStringLiteral("name"), a.name}, {QStringLiteral("icon"), a.icon},
                                {QStringLiteral("tacit"), a.tacit}});
    QJsonObject params{
        {QStringLiteral("file"), QFileInfo(filename).fileName()},
        {QStringLiteral("kind"), kindLabel(filename, contentType)},
        {QStringLiteral("contentType"), contentType},
        {QStringLiteral("apps"), rows},
        {QStringLiteral("selected"), preselect(apps, lastChoice, contentType)},
        {QStringLiteral("asker"), asker.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(asker)},
    };

    auto done = QSharedPointer<bool>::create(false);
    auto askId = QSharedPointer<QString>::create();
    *askId = m_bridge->ask(QStringLiteral("open.choose"), params, [this, message, request, done, contentType, apps](bool ok, const QJsonObject &result, const QString &why) {
        if (*done)
            return;
        *done = true;
        if (!ok && (why == QLatin1String("no-shell") || why == QLatin1String("shell-gone") || why == QLatin1String("unsupported"))) {
            forward(message, request);
            return;
        }
        QVariantMap results;
        uint response = ok ? 0 : (why == QLatin1String("cancelled") || why == QLatin1String("refused") ? 1u : 2u);
        const QString id = result.value(QStringLiteral("id")).toString();
        if (ok) {
            const App *picked = nullptr;
            for (const App &a : apps)
                if (a.id == id)
                    picked = &a;
            if (!picked) {
                response = 2;
            } else {
                results.insert(QStringLiteral("choice"), picked->id);
                // The portal has no "always" flag; the backend makes it the default itself, as the GTK one does.
                if (result.value(QStringLiteral("always")).toBool() && !contentType.isEmpty())
                    QProcess::startDetached(QStringLiteral("xdg-mime"), {QStringLiteral("default"), withSuffix(picked->id), contentType});
            }
        }
        reply(message, response, results);
        request->deleteLater();
    });
    connect(request, &Request::closeRequested, this, [this, message, request, done, askId] {
        if (*done)
            return;
        *done = true;
        m_bridge->cancel(*askId);
        reply(message, 2);
        request->deleteLater();
    });
}

// Sends the call on to the GTK backend and relays the answer, as Chooser::forward does for the KDE one.
void AppChooser::forward(const QDBusMessage &message, Request *request) {
    const QString handle = request->handle();
    connect(request, &Request::closeRequested, this, [handle] {
        QDBusConnection::sessionBus().asyncCall(QDBusMessage::createMethodCall(gtkBackend, handle, requestInterface, QStringLiteral("Close")));
    });
    QDBusMessage call = QDBusMessage::createMethodCall(gtkBackend, portalPath, appChooserInterface, message.member());
    call.setArguments(message.arguments());
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(call, 24 * 3600 * 1000), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [message, request, watcher] {
        const QDBusMessage answer = watcher->reply();
        if (answer.type() == QDBusMessage::ReplyMessage)
            QDBusConnection::sessionBus().send(message.createReply(answer.arguments()));
        else
            reply(message, 2);
        request->deleteLater();
        watcher->deleteLater();
    });
}

AppChooserAdaptor::AppChooserAdaptor(Chooser *context, AppChooser *logic)
    : QDBusAbstractAdaptor(context), m_context(context), m_logic(logic) {}

uint AppChooserAdaptor::ChooseApplication(const QDBusObjectPath &, const QString &, const QString &, const QStringList &, const QVariantMap &,
                                          QVariantMap &results) {
    m_context->setDelayedReply(true);
    m_logic->choose(m_context->message());
    results = {};
    return 0;
}

void AppChooserAdaptor::UpdateChoices(const QDBusObjectPath &, const QStringList &) {}
