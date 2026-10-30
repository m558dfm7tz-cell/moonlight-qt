#include "ferry.h"

#include <QTcpSocket>
#include <QJsonDocument>
#include <QJsonArray>

bool FerryBridge::enabled = false;
int FerryBridge::ipcPort = 0;
QString FerryBridge::peerName;
QTcpSocket* FerryBridge::s_socket = nullptr;
bool FerryBridge::s_connected = false;

QTcpSocket* FerryBridge::socket()
{
    if (s_socket == nullptr) {
        s_socket = new QTcpSocket();
        s_socket->connectToHost(QStringLiteral("127.0.0.1"), (quint16)ipcPort);
        // Block briefly; this is a local loopback connect so it resolves
        // immediately. If Ferry is gone we simply drop the message.
        s_connected = s_socket->waitForConnected(2000);
    }
    return s_socket;
}

void FerryBridge::send(const QJsonObject& obj)
{
    if (!isEnabled()) {
        return;
    }

    QTcpSocket* s = socket();
    if (s == nullptr || !s_connected) {
        return;
    }

    QByteArray line = QJsonDocument(obj).toJson(QJsonDocument::Compact);
    line.append('\n');
    s->write(line);
    s->flush();
}

void FerryBridge::sendHello()
{
    QJsonObject obj;
    obj.insert("type", "hello");
    obj.insert("peer", peerName);
    send(obj);
}

void FerryBridge::sendBlank(bool on)
{
    QJsonObject obj;
    obj.insert("type", "blank");
    obj.insert("on", on);
    send(obj);
}

void FerryBridge::sendQuit()
{
    QJsonObject obj;
    obj.insert("type", "quit");
    send(obj);
}

void FerryBridge::sendReconnect(const QString& resolution, int fps, int bitrateMbps, bool bitrateAuto)
{
    QJsonObject obj;
    obj.insert("type", "reconnect");
    obj.insert("resolution", resolution);
    obj.insert("fps", fps);
    obj.insert("bitrateMbps", bitrateMbps);
    obj.insert("bitrateAuto", bitrateAuto);
    send(obj);
}

void FerryBridge::sendFiles(const QStringList& paths)
{
    QJsonObject obj;
    obj.insert("type", "files");
    obj.insert("paths", QJsonArray::fromStringList(paths));
    send(obj);
}

void FerryBridge::sendStatus(const QString& text)
{
    QJsonObject obj;
    obj.insert("type", "status");
    obj.insert("text", text);
    send(obj);
}
