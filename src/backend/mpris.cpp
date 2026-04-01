#include "mpris.hpp"

#include <QDBusConnection>
#include <QCoreApplication>
#include <QProcess>

MprisRootAdaptor::MprisRootAdaptor(QObject *parent)
    : QDBusAbstractAdaptor(parent)
{
}

MprisPlayerAdaptor::MprisPlayerAdaptor(QObject *parent)
    : QDBusAbstractAdaptor(parent)
{
}

Mpris::Mpris(QObject *parent)
    : QObject(parent)
{
    m_root = new MprisRootAdaptor(this);
    m_player = new MprisPlayerAdaptor(this);

    QDBusConnection dbus = QDBusConnection::sessionBus();
    QString serviceName = QString("org.mpris.MediaPlayer2.qobuz_qt.instance%1").arg(QCoreApplication::applicationPid());
    
    dbus.registerService(serviceName);
    dbus.registerObject("/org/mpris/MediaPlayer2", this, 
                        QDBusConnection::ExportAdaptors);
}
