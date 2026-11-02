#include "ferry.h"

#include <QTcpSocket>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonParseError>
#include <QDateTime>

bool FerryBridge::enabled = false;
int FerryBridge::ipcPort = 0;
QString FerryBridge::peerName;
QTcpSocket* FerryBridge::s_socket = nullptr;
bool FerryBridge::s_connected = false;
std::function<void(const QJsonObject&)> FerryBridge::s_messageHandler;
QByteArray FerryBridge::s_readBuffer;
qint64 FerryBridge::s_lastDragOutMs = 0;

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

void FerryBridge::sendReconnect(const QString& resolution, int fps, int bitrateMbps, bool bitrateAuto,
                                const QString& codec)
{
    QJsonObject obj;
    obj.insert("type", "reconnect");
    obj.insert("resolution", resolution);
    obj.insert("fps", fps);
    obj.insert("bitrateMbps", bitrateMbps);
    obj.insert("bitrateAuto", bitrateAuto);
    obj.insert("codec", codec);
    send(obj);
}

void FerryBridge::sendFiles(const QStringList& paths, int x, int y, int streamWidth, int streamHeight)
{
    QJsonObject obj;
    obj.insert("type", "files");
    obj.insert("paths", QJsonArray::fromStringList(paths));
    obj.insert("x", x);
    obj.insert("y", y);
    obj.insert("streamWidth", streamWidth);
    obj.insert("streamHeight", streamHeight);
    send(obj);
}

void FerryBridge::sendStatus(const QString& text)
{
    QJsonObject obj;
    obj.insert("type", "status");
    obj.insert("text", text);
    send(obj);
}

void FerryBridge::sendNeedPair()
{
    QJsonObject obj;
    obj.insert("type", "needpair");
    qInfo() << "[FerryPair] authentication requires re-pairing";
    send(obj);
    // Do not exit before Ferry consumes the recovery request.
    QTcpSocket* s = socket();
    if (!s_connected) return;
    if (s->bytesToWrite() > 0) s->waitForBytesWritten(1000);
    QByteArray replies;
    for (int i = 0; i < 10; ++i) {
        if (s->bytesAvailable() == 0) s->waitForReadyRead(100);
        replies.append(s->readAll());
        if (replies.contains("needpairAck")) {
            qInfo() << "[FerryPair] recovery request acknowledged";
            break;
        }
    }
}

void FerryBridge::sendDragOut()
{
    // The drag-out can be detected both by the SDL window-leave event and by
    // the absolute-mouse video-region transition; de-duplicate so a single
    // gesture doesn't ask Ferry twice.
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - s_lastDragOutMs < 1000) {
        return;
    }
    s_lastDragOutMs = now;

    qInfo() << "[FerryDragOut] dragout sent";

    QJsonObject obj;
    obj.insert("type", "dragout");
    send(obj);
}

void FerryBridge::setMessageHandler(const std::function<void(const QJsonObject&)>& handler)
{
    s_messageHandler = handler;
}

void FerryBridge::readMessages()
{
    if (!isEnabled()) {
        return;
    }

    QTcpSocket* s = socket();
    if (s == nullptr || !s_connected) {
        return;
    }

    // The Qt event loop is suspended while streaming, so the socket notifier
    // never fires and QTcpSocket won't fill its buffer on its own. Force a
    // non-blocking poll+read of the fd before consuming whatever arrived.
    s->waitForReadyRead(0);
    s_readBuffer.append(s->readAll());

    int newlineIndex;
    while ((newlineIndex = s_readBuffer.indexOf('\n')) >= 0) {
        QByteArray line = s_readBuffer.left(newlineIndex).trimmed();
        s_readBuffer.remove(0, newlineIndex + 1);
        if (line.isEmpty()) {
            continue;
        }

        QJsonParseError parseError;
        QJsonDocument doc = QJsonDocument::fromJson(line, &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
            continue;
        }

        if (s_messageHandler) {
            s_messageHandler(doc.object());
        }
    }
}
