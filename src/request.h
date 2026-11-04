#pragma once

#include <QDBusAbstractAdaptor>
#include <QObject>

// org.freedesktop.impl.portal.Request, exported at the handle path of one call.
class Request : public QObject {
    Q_OBJECT
public:
    explicit Request(const QString &handle, QObject *parent = nullptr);
    ~Request() override;
    QString handle() const { return m_handle; }
    void close();   // callers: the adaptor's Close, and the end of the call
signals:
    void closeRequested();
private:
    QString m_handle;
};

class RequestAdaptor : public QDBusAbstractAdaptor {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.impl.portal.Request")
public:
    explicit RequestAdaptor(Request *request);
public slots:
    void Close();
private:
    Request *m_request;
};
