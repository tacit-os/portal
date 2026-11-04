#include "request.h"

#include <QDBusConnection>

Request::Request(const QString &handle, QObject *parent) : QObject(parent), m_handle(handle) {
    new RequestAdaptor(this);
    QDBusConnection::sessionBus().registerObject(handle, this, QDBusConnection::ExportAdaptors);
}

Request::~Request() {
    QDBusConnection::sessionBus().unregisterObject(m_handle);
}

RequestAdaptor::RequestAdaptor(Request *request) : QDBusAbstractAdaptor(request), m_request(request) {}

void RequestAdaptor::Close() {
    emit m_request->closeRequested();
}
