#pragma once

#include <QDBusAbstractAdaptor>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QVariantMap>

class Chooser;
class DialogBridge;
class Request;

// org.freedesktop.impl.portal.AppChooser.ChooseApplication: the "Open with" menu. The apps and their order come
// from here, the sheet from the shell through DialogBridge kind "open.choose" (DIALOGS.md). With no shell, or
// nothing that opens the type, the call goes to the GTK backend unchanged so Open with never stops working.
class AppChooser : public QObject {
    Q_OBJECT
public:
    AppChooser(DialogBridge *bridge, QObject *parent = nullptr);
    void choose(const QDBusMessage &message);

    // Pure helpers, kept apart so the tests can call them without a bus.
    struct App {
        QString id;      // as the portal gave it (with or without .desktop)
        QString name;
        QString icon;
        bool tacit = false;
    };
    static QList<App> appsFor(const QStringList &choices, const QString &contentType);
    static QString kindLabel(const QString &filename, const QString &contentType);
    static int preselect(const QList<App> &apps, const QString &lastChoice, const QString &contentType);

private:
    void forward(const QDBusMessage &message, Request *request);
    void ask(const QDBusMessage &message, Request *request, const QJsonObject &asker, const QList<App> &apps);

    DialogBridge *m_bridge;
};

class AppChooserAdaptor : public QDBusAbstractAdaptor {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.impl.portal.AppChooser")
    Q_PROPERTY(uint version READ version)
public:
    // context: the exported object, whose QDBusContext carries the call being served
    AppChooserAdaptor(Chooser *context, AppChooser *logic);
    uint version() const { return 2; }
public slots:
    uint ChooseApplication(const QDBusObjectPath &handle, const QString &app_id, const QString &parent_window,
                           const QStringList &choices, const QVariantMap &options, QVariantMap &results);
    // The list is fixed once the sheet is up; the portal only sends this while it is still looking for apps.
    void UpdateChoices(const QDBusObjectPath &handle, const QStringList &choices);
private:
    Chooser *m_context;
    AppChooser *m_logic;
};
