#pragma once

#include <QDBusAbstractAdaptor>
#include <QDBusContext>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QList>
#include <QProcess>
#include <QVariantMap>

class Request;

// One OpenFile, SaveFile or SaveFiles: waits its turn, asks Files, answers the portal.
struct Job {
    enum Kind { Open, Save, SaveFiles };
    Kind kind = Open;
    QDBusMessage message;
    QString handle;
    QString title;
    QString folder;
    QString name;           // Save: the suggested name
    QStringList names;      // SaveFiles: the names to write
    bool multiple = false;
    bool directory = false;
    Request *request = nullptr;
};

// org.freedesktop.impl.portal.FileChooser.
//   OpenFile, SaveFile, SaveFiles: Files' pick and save modes (services/Files.qml), one at a time.
//   forward() sends a Save call to the KDE backend; it runs only when Files cannot be called (no shell running).
class Chooser : public QObject, public QDBusContext {
    Q_OBJECT
public:
    explicit Chooser(QObject *parent = nullptr);
    void openFile(const QDBusMessage &message);
    void saveFile(const QDBusMessage &message);
    void saveFiles(const QDBusMessage &message);
    void forward(const QDBusMessage &message);

private:
    void enqueue(const QDBusMessage &message, Job::Kind kind);
    void next();
    void endRelay();
    void callFailed(int serial);
    void finish(const QStringList &paths, uint response);

    QList<Job> m_queue;   // the first is the pick in progress once m_busy
    bool m_busy = false;
    int m_serial = 0;
    QProcess *m_relay = nullptr;
    QString m_relayFile;
    QString m_qs;
    QString m_shell;        // the folder Tacit's shell runs from
};

class ChooserAdaptor : public QDBusAbstractAdaptor {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.impl.portal.FileChooser")
    Q_PROPERTY(uint version READ version)
public:
    explicit ChooserAdaptor(Chooser *chooser);
    uint version() const { return 3; }
public slots:
    uint OpenFile(const QDBusObjectPath &handle, const QString &app_id, const QString &parent_window, const QString &title,
                  const QVariantMap &options, QVariantMap &results);
    uint SaveFile(const QDBusObjectPath &handle, const QString &app_id, const QString &parent_window, const QString &title,
                  const QVariantMap &options, QVariantMap &results);
    uint SaveFiles(const QDBusObjectPath &handle, const QString &app_id, const QString &parent_window, const QString &title,
                   const QVariantMap &options, QVariantMap &results);
private:
    uint handle(void (Chooser::*call)(const QDBusMessage &), QVariantMap &results);
    Chooser *m_chooser;
};
