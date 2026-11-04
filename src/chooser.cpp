#include "chooser.h"

#include "request.h"

#include <QDBusConnection>
#include <QDBusObjectPath>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUrl>
#include <QDebug>

// Where SaveFile and SaveFiles go when Files cannot be called (no shell running).
static const QString kdeBackend = QStringLiteral("org.freedesktop.impl.portal.desktop.kde");
static const QString portalPath = QStringLiteral("/org/freedesktop/portal/desktop");
static const QString chooserInterface = QStringLiteral("org.freedesktop.impl.portal.FileChooser");
static const QString requestInterface = QStringLiteral("org.freedesktop.impl.portal.Request");

static const QByteArray marker = "portal-picked ";

// The relay: a Quickshell instance of its own that Files' Choose, Cancel and window close call `picked` on
// (services/Files.qml pickDone); it prints the JSON list on a line of its own. Same shape as the browser's.
static const char relayShell[] = R"(import Quickshell
import Quickshell.Io

ShellRoot {
    IpcHandler {
        target: "%1"
        function picked(json: string): void {
            let paths;
            try { paths = JSON.parse(json); } catch (e) { paths = json; }
            if (typeof paths === "string") paths = paths.split("\n");
            console.log("portal-picked " + JSON.stringify(paths));
        }
    }
}
)";

Chooser::Chooser(QObject *parent) : QObject(parent) {
    m_qs = qEnvironmentVariable("TACIT_PORTAL_QS", QStringLiteral("qs"));
    // Tacit's shell by the folder it runs from (qs -p), as Thyris finds it (tacit-opsis src/shell.h): an install's runs as
    // /usr/share/tacit/shell (TACIT_SHELL, set by the session), the dev machine's as ~/.config/quickshell/tacit. `qs -c
    // end4-pC` reached it only through a link the dev machine kept from the old name: every Open dialog failed on installs.
    m_shell = qEnvironmentVariable("TACIT_PORTAL_SHELL", qEnvironmentVariable("TACIT_SHELL"));
    if (m_shell.isEmpty())
        m_shell = QFileInfo::exists(QStringLiteral("/usr/share/tacit/shell")) ? QStringLiteral("/usr/share/tacit/shell")
                                                                               : QDir::homePath() + QStringLiteral("/.config/quickshell/tacit");
}

// current_folder and current_file are NUL-terminated byte strings
static QString bytePath(const QVariant &v) {
    return QString::fromLocal8Bit(v.toByteArray()).section(QChar(0), 0, 0);
}

void Chooser::openFile(const QDBusMessage &message) { enqueue(message, Job::Open); }
void Chooser::saveFile(const QDBusMessage &message) { enqueue(message, Job::Save); }
void Chooser::saveFiles(const QDBusMessage &message) { enqueue(message, Job::SaveFiles); }

void Chooser::enqueue(const QDBusMessage &message, Job::Kind kind) {
    const QList<QVariant> args = message.arguments();
    const QVariantMap options = qdbus_cast<QVariantMap>(args.value(4));
    Job job;
    job.kind = kind;
    job.message = message;
    job.handle = qdbus_cast<QDBusObjectPath>(args.value(0)).path();
    job.title = args.value(3).toString();
    job.multiple = options.value(QStringLiteral("multiple")).toBool();
    job.directory = options.value(QStringLiteral("directory")).toBool();
    QString folder = bytePath(options.value(QStringLiteral("current_folder")));
    if (kind == Job::Save) {
        // the file being saved over, when the app names one, says both folder and name
        const QString file = bytePath(options.value(QStringLiteral("current_file")));
        job.name = options.value(QStringLiteral("current_name")).toString();
        if (!file.isEmpty()) {
            folder = QFileInfo(file).absolutePath();
            if (job.name.isEmpty())
                job.name = QFileInfo(file).fileName();
        }
    } else if (kind == Job::SaveFiles) {
        for (const QByteArray &b : qdbus_cast<QList<QByteArray>>(options.value(QStringLiteral("files"))))
            job.names.append(QString::fromLocal8Bit(b).section(QChar(0), 0, 0));
    }
    job.folder = QFileInfo(folder).isDir() ? folder : QDir::homePath();
    job.request = new Request(job.handle, this);
    connect(job.request, &Request::closeRequested, this, [this, request = job.request] {
        for (int i = 0; i < m_queue.size(); ++i) {
            if (m_queue[i].request != request)
                continue;
            if (i == 0 && m_busy) {
                finish({}, 2);
            } else {
                const Job gone = m_queue.takeAt(i);
                QDBusConnection::sessionBus().send(gone.message.createReply(QList<QVariant>{2u, QVariantMap()}));
                gone.request->deleteLater();
            }
            return;
        }
    });
    m_queue.append(job);
    if (!m_busy)
        next();
}

void Chooser::next() {
    if (m_queue.isEmpty())
        return;
    m_busy = true;
    const Job &job = m_queue.first();
    const int serial = ++m_serial;
    const QString target = QStringLiteral("portal%1").arg(serial);

    QString dir = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    if (dir.isEmpty())
        dir = QDir::tempPath();
    dir += QStringLiteral("/tacit-portal");
    QDir().mkpath(dir);
    m_relayFile = dir + QStringLiteral("/pick-relay-%1.qml").arg(serial);
    QFile file(m_relayFile);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) || file.write(QByteArray(relayShell).replace("%1", target.toUtf8())) < 0) {
        finish({}, 2);
        return;
    }
    file.close();

    m_relay = new QProcess(this);
    m_relay->setProcessChannelMode(QProcess::MergedChannels);
    connect(m_relay, &QProcess::readyReadStandardOutput, this, [this] {
        while (m_relay && m_relay->canReadLine()) {
            const QByteArray line = m_relay->readLine();
            const qsizetype at = line.indexOf(marker);
            if (at < 0)
                continue;
            // the log line may carry colour codes after the list
            const QByteArray rest = line.mid(at + marker.size());
            const QJsonArray list = QJsonDocument::fromJson(rest.left(rest.lastIndexOf(']') + 1)).array();
            QStringList paths;
            for (const auto &v : list)
                if (v.isString() && !v.toString().trimmed().isEmpty())
                    paths.append(v.toString());
            finish(paths, paths.isEmpty() ? 1 : 0);   // an empty list is Cancel (or a closed window)
            return;
        }
    });
    connect(m_relay, &QProcess::errorOccurred, this, [this, serial](QProcess::ProcessError error) {
        qWarning() << "tacit-portal: relay error" << error << m_relay->errorString();
        if (error == QProcess::FailedToStart && serial == m_serial)
            finish({}, 2);
    });
    m_relay->start(m_qs, {QStringLiteral("-p"), m_relayFile});

    auto *call = new QProcess(this);
    // no shell to open Files in: the call is over (unless a later one has begun)
    connect(call, &QProcess::finished, this, [this, call, serial](int code, QProcess::ExitStatus status) {
        call->deleteLater();
        if ((code != 0 || status != QProcess::NormalExit) && serial == m_serial && m_busy)
            callFailed(serial);
    });
    connect(call, &QProcess::errorOccurred, this, [this, call, serial](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        call->deleteLater();
        if (serial == m_serial && m_busy)
            callFailed(serial);
    });
    QStringList ipc{QStringLiteral("-p"), m_shell, QStringLiteral("ipc"), QStringLiteral("call"), QStringLiteral("files")};
    switch (job.kind) {
    case Job::Open:
        ipc << (job.directory ? QStringLiteral("pickdir") : QStringLiteral("pick")) << job.folder << m_relayFile << target
            << (job.title.isEmpty() ? (job.directory ? QStringLiteral("Choose a folder") : QStringLiteral("Choose files")) : job.title);
        break;
    case Job::Save:
        ipc << QStringLiteral("save") << job.folder << job.name << m_relayFile << target << QStringLiteral("Save");
        break;
    case Job::SaveFiles:
        ipc << QStringLiteral("savefolder") << job.folder << m_relayFile << target
            << (job.names.size() == 1 ? QStringLiteral("Save 1 file") : QStringLiteral("Save %1 files").arg(job.names.size()));
        break;
    }
    call->start(m_qs, ipc);
}

// "name", "name 2", "name 3" ... as Files' job runner names a copy kept beside the original (scripts/files/job.sh
// keep_name): only a short letters-and-digits tail counts as an extension. `taken` holds names given out in this call.
static QString freePath(const QString &dir, const QString &name, QStringList *taken) {
    auto free = [&](const QString &n) { return !QFileInfo::exists(dir + QLatin1Char('/') + n) && !taken->contains(n); };
    QString result = name;
    if (!free(name)) {
        static const QRegularExpression ext(QStringLiteral("^(.+)\\.([A-Za-z0-9]{1,5})$"));
        const QRegularExpressionMatch m = ext.match(name);
        const bool dirName = QFileInfo(dir + QLatin1Char('/') + name).isDir();
        const QString stem = m.hasMatch() && !dirName ? m.captured(1) : name;
        const QString tail = m.hasMatch() && !dirName ? QLatin1Char('.') + m.captured(2) : QString();
        int n = 2;
        while (!free(QStringLiteral("%1 %2%3").arg(stem).arg(n).arg(tail)))
            ++n;
        result = QStringLiteral("%1 %2%3").arg(stem).arg(n).arg(tail);
    }
    taken->append(result);
    return dir + QLatin1Char('/') + result;
}

void Chooser::endRelay() {
    if (m_relay) {
        QProcess *relay = m_relay;
        m_relay = nullptr;
        relay->disconnect(this);
        if (relay->state() == QProcess::NotRunning) {
            relay->deleteLater();
        } else {
            connect(relay, &QProcess::finished, relay, &QObject::deleteLater);
            relay->terminate();
        }
    }
    QFile::remove(m_relayFile);
}

// Files could not be called (no shell running). An Open is over; a Save goes to the KDE backend instead.
void Chooser::callFailed(int) {
    if (m_queue.first().kind == Job::Open) {
        finish({}, 2);
        return;
    }
    m_busy = false;
    const Job job = m_queue.takeFirst();
    endRelay();
    delete job.request;   // forward() takes the handle over
    forward(job.message);
    QMetaObject::invokeMethod(this, &Chooser::next, Qt::QueuedConnection);
}

// Answers the pick in progress, then starts the next. Response 0 success, 1 cancelled, 2 ended otherwise.
void Chooser::finish(const QStringList &paths, uint response) {
    if (!m_busy || m_queue.isEmpty())
        return;
    m_busy = false;
    const Job job = m_queue.takeFirst();
    endRelay();

    QVariantMap results;
    if (response == 0) {
        QStringList uris;
        if (job.kind == Job::SaveFiles) {
            // the folder is the answer; a name already in it gets the next free one, as Files' Keep both does
            QStringList taken;
            for (const QString &n : job.names)
                uris.append(QString::fromLatin1(QUrl::fromLocalFile(freePath(paths.first(), n, &taken)).toEncoded()));
        } else {
            for (const QString &p : job.multiple ? paths : paths.mid(0, 1))
                uris.append(QString::fromLatin1(QUrl::fromLocalFile(p).toEncoded()));
        }
        results.insert(QStringLiteral("uris"), uris);
        if (job.kind == Job::Open)
            results.insert(QStringLiteral("writable"), false);
    }
    QDBusConnection::sessionBus().send(job.message.createReply(QList<QVariant>{response, results}));
    job.request->deleteLater();
    QMetaObject::invokeMethod(this, &Chooser::next, Qt::QueuedConnection);
}

// Sends the call on unchanged and relays the answer. The Request object stays up until the answer is in, and
// its Close goes on to the KDE backend's Request at the same handle.
void Chooser::forward(const QDBusMessage &message) {
    const QString handle = qdbus_cast<QDBusObjectPath>(message.arguments().value(0)).path();
    auto *request = new Request(handle, this);
    connect(request, &Request::closeRequested, this, [handle] {
        QDBusConnection::sessionBus().asyncCall(QDBusMessage::createMethodCall(kdeBackend, handle, requestInterface, QStringLiteral("Close")));
    });
    QDBusMessage call = QDBusMessage::createMethodCall(kdeBackend, portalPath, chooserInterface, message.member());
    call.setArguments(message.arguments());
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(call, 24 * 3600 * 1000), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [message, request, watcher] {
        const QDBusMessage answer = watcher->reply();
        if (answer.type() == QDBusMessage::ReplyMessage)
            QDBusConnection::sessionBus().send(message.createReply(answer.arguments()));
        else
            QDBusConnection::sessionBus().send(message.createReply(QList<QVariant>{2u, QVariantMap()}));
        request->deleteLater();
        watcher->deleteLater();
    });
}

ChooserAdaptor::ChooserAdaptor(Chooser *chooser) : QDBusAbstractAdaptor(chooser), m_chooser(chooser) {}

// The answer goes out later, from the Chooser: the return values here are not used.
uint ChooserAdaptor::handle(void (Chooser::*call)(const QDBusMessage &), QVariantMap &results) {
    // the call's context is set on the exported object, not on its adaptor
    m_chooser->setDelayedReply(true);
    (m_chooser->*call)(m_chooser->message());
    results = {};
    return 0;
}

uint ChooserAdaptor::OpenFile(const QDBusObjectPath &, const QString &, const QString &, const QString &, const QVariantMap &, QVariantMap &results) {
    return handle(&Chooser::openFile, results);
}

uint ChooserAdaptor::SaveFile(const QDBusObjectPath &, const QString &, const QString &, const QString &, const QVariantMap &, QVariantMap &results) {
    return handle(&Chooser::saveFile, results);
}

uint ChooserAdaptor::SaveFiles(const QDBusObjectPath &, const QString &, const QString &, const QString &, const QVariantMap &, QVariantMap &results) {
    return handle(&Chooser::saveFiles, results);
}
