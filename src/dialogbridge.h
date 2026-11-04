#pragma once

#include <QHash>
#include <QJsonObject>
#include <QLocalServer>
#include <QObject>
#include <functional>

class QLocalSocket;

// Broker between everything that wants to show a system dialog (the portal's own AppChooser, the keyring
// prompter, the share picker) and the shell, which draws them. One socket, JSON lines, see DIALOGS.md.
// The shell connects as role "ui"; requesters connect as role "client". With no ui connected an ask is
// rejected at once, and when the ui goes away every ask it still held is rejected.
class DialogBridge : public QObject {
    Q_OBJECT
public:
    // Reply to an in-process ask: ok = answered (result), otherwise reject reason.
    using Reply = std::function<void(bool ok, const QJsonObject &result, const QString &reason)>;

    explicit DialogBridge(QObject *parent = nullptr);
    static QString defaultPath();           // $TACIT_DIALOGS_SOCK or $XDG_RUNTIME_DIR/tacit-dialogs.sock
    bool listen(const QString &path);
    bool uiConnected() const { return m_ui != nullptr; }
    // For code in this process. Returns the id to cancel() with.
    QString ask(const QString &kind, const QJsonObject &params, Reply reply);
    void cancel(const QString &id);

private:
    struct Pending {
        QLocalSocket *client = nullptr;   // null: in-process
        QString clientId;
        Reply reply;
    };
    void onConnection();
    void onLine(QLocalSocket *s, const QByteArray &line);
    void onDisconnect(QLocalSocket *s);
    void send(QLocalSocket *s, const QJsonObject &o);
    QString submit(QLocalSocket *client, const QString &clientId, const QString &kind, const QJsonObject &params,
                   const Reply &reply);
    void finish(const QString &id, bool ok, const QJsonObject &result, const QString &reason);

    QLocalServer m_server;
    QLocalSocket *m_ui = nullptr;
    QHash<QLocalSocket *, QByteArray> m_buf;
    QHash<QLocalSocket *, QString> m_role;     // "" until hello
    QHash<QString, Pending> m_pending;         // global id -> who asked
    quint64 m_seq = 0;
};
