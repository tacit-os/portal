#include "dialogbridge.h"

#include <QJsonDocument>
#include <QLocalSocket>
#include <QDebug>
#include <QFile>

namespace {
constexpr int kProto = 1;
constexpr int kMaxLine = 1 << 20;
}

DialogBridge::DialogBridge(QObject *parent) : QObject(parent) {
    m_server.setSocketOptions(QLocalServer::UserAccessOption);
    connect(&m_server, &QLocalServer::newConnection, this, &DialogBridge::onConnection);
}

QString DialogBridge::defaultPath() {
    const QByteArray p = qgetenv("TACIT_DIALOGS_SOCK");
    if (!p.isEmpty()) return QString::fromLocal8Bit(p);
    return QString::fromLocal8Bit(qgetenv("XDG_RUNTIME_DIR")) + QStringLiteral("/tacit-dialogs.sock");
}

bool DialogBridge::listen(const QString &path) {
    QLocalServer::removeServer(path);   // a stale file from a crashed run; the bus name already keeps us single
    if (!m_server.listen(path)) {
        qWarning() << "tacit-portal: dialog socket" << path << ":" << m_server.errorString();
        return false;
    }
    QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    return true;
}

void DialogBridge::onConnection() {
    while (QLocalSocket *s = m_server.nextPendingConnection()) {
        m_role[s] = QString();
        connect(s, &QLocalSocket::readyRead, this, [this, s] {
            QByteArray &buf = m_buf[s];
            buf += s->readAll();
            int nl;
            while ((nl = buf.indexOf('\n')) >= 0) {
                const QByteArray line = buf.left(nl);
                buf.remove(0, nl + 1);
                onLine(s, line);
                if (!m_buf.contains(s)) return;   // dropped while handling the line
            }
            if (buf.size() > kMaxLine) s->abort();
        });
        connect(s, &QLocalSocket::disconnected, this, [this, s] { onDisconnect(s); });
    }
}

void DialogBridge::send(QLocalSocket *s, const QJsonObject &o) {
    s->write(QJsonDocument(o).toJson(QJsonDocument::Compact) + '\n');
}

void DialogBridge::onLine(QLocalSocket *s, const QByteArray &line) {
    if (line.trimmed().isEmpty()) return;
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(line, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        send(s, {{"op", "error"}, {"reason", "bad-json"}});
        return;
    }
    const QJsonObject o = doc.object();
    const QString op = o.value("op").toString();
    const QString role = m_role.value(s);

    if (op == "hello") {
        const QString want = o.value("role").toString();
        if (want != "ui" && want != "client") {
            send(s, {{"op", "error"}, {"reason", "bad-role"}});
            return;
        }
        if (want == "ui") {
            if (m_ui && m_ui != s) {   // a restarted shell replaces the old connection
                m_ui->abort();   // onDisconnect clears m_ui and rejects what the old shell still held
                if (m_ui) onDisconnect(m_ui);   // in case the signal is queued rather than synchronous
            }
            m_ui = s;
        }
        m_role[s] = want;
        send(s, {{"op", "hello"}, {"proto", kProto}, {"ui", m_ui != nullptr}});
        return;
    }
    if (role.isEmpty()) {
        send(s, {{"op", "error"}, {"reason", "hello-first"}});
        return;
    }
    if (role == "client" && op == "ask") {
        const QString id = o.value("id").toString();
        const QString kind = o.value("kind").toString();
        if (id.isEmpty() || kind.isEmpty()) {
            send(s, {{"op", "reject"}, {"id", id}, {"reason", "bad-ask"}});
            return;
        }
        submit(s, id, kind, o.value("params").toObject(), nullptr);
    } else if (role == "client" && op == "cancel") {
        const QString cid = o.value("id").toString();
        for (auto it = m_pending.cbegin(); it != m_pending.cend(); ++it)
            if (it->client == s && it->clientId == cid) {
                cancel(it.key());
                break;
            }
    } else if (role == "ui" && (op == "answer" || op == "reject")) {
        const QString id = o.value("id").toString();
        if (op == "answer") finish(id, true, o.value("result").toObject(), QString());
        else finish(id, false, {}, o.value("reason").toString("cancelled"));
    } else {
        send(s, {{"op", "error"}, {"reason", "bad-op"}});
    }
}

QString DialogBridge::submit(QLocalSocket *client, const QString &clientId, const QString &kind,
                             const QJsonObject &params, const Reply &reply) {
    const QString id = QString::number(++m_seq);
    m_pending.insert(id, {client, clientId, reply});
    if (!m_ui) {
        finish(id, false, {}, QStringLiteral("no-shell"));
        return id;
    }
    send(m_ui, {{"op", "ask"}, {"id", id}, {"kind", kind}, {"params", params}});
    return id;
}

QString DialogBridge::ask(const QString &kind, const QJsonObject &params, Reply reply) {
    return submit(nullptr, QString(), kind, params, reply);
}

void DialogBridge::cancel(const QString &id) {
    if (!m_pending.contains(id)) return;
    if (m_ui) send(m_ui, {{"op", "cancel"}, {"id", id}});
    finish(id, false, {}, QStringLiteral("cancelled"));
}

void DialogBridge::finish(const QString &id, bool ok, const QJsonObject &result, const QString &reason) {
    const auto it = m_pending.find(id);
    if (it == m_pending.end()) return;
    const Pending p = it.value();
    m_pending.erase(it);
    if (p.client) {
        if (ok) send(p.client, {{"op", "answer"}, {"id", p.clientId}, {"result", result}});
        else send(p.client, {{"op", "reject"}, {"id", p.clientId}, {"reason", reason}});
    } else if (p.reply) {
        p.reply(ok, result, reason);
    }
}

void DialogBridge::onDisconnect(QLocalSocket *s) {
    const bool wasUi = (s == m_ui);
    if (wasUi) m_ui = nullptr;
    m_buf.remove(s);
    m_role.remove(s);
    if (wasUi) {
        const QList<QString> ids = m_pending.keys();
        for (const QString &id : ids) finish(id, false, {}, QStringLiteral("shell-gone"));
    } else {
        // a requester that left: withdraw its dialogs so the shell does not keep asking nobody
        const QList<QString> ids = m_pending.keys();
        for (const QString &id : ids)
            if (m_pending.value(id).client == s) {
                m_pending[id].client = nullptr;
                m_pending[id].reply = nullptr;
                if (m_ui) send(m_ui, {{"op", "cancel"}, {"id", id}});
                m_pending.remove(id);
            }
    }
    s->deleteLater();
}
