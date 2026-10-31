#pragma once

#include <QString>
#include <QStringList>
#include <QJsonObject>

class QTcpSocket;

// Ferry integration bridge.
//
// When Moonlight is launched by Ferry with --ferry and --ferry-ipc-port, it
// connects back to Ferry over a loopback TCP socket and reports session
// lifecycle / toolbar events as newline-delimited JSON. Ferry is the sole
// owner of the paired LAN channel, so features that must reach the remote
// Ferry (black-screen privacy, file transfer) and the "reconnect with a new
// quality" handoff are routed through this channel rather than duplicated.
//
// The bridge is send-only: Moonlight never needs to read from Ferry here.
class FerryBridge
{
public:
    // Configured from the command line before the stream starts.
    static bool enabled;
    static int ipcPort;
    static QString peerName;

    static bool isEnabled() { return enabled && ipcPort > 0; }

    // Connect (lazily, once) and send a JSON object followed by a newline.
    static void send(const QJsonObject& obj);

    static void sendHello();
    static void sendBlank(bool on);
    static void sendQuit();
    static void sendReconnect(const QString& resolution, int fps, int bitrateMbps, bool bitrateAuto);
    // Send dropped file paths together with the drop position in the remote
    // stream's coordinate space (pixels of the stream, 0..streamWidth/Height)
    // and the stream resolution, so the receiving Ferry can resolve which
    // Explorer window / desktop the file was dropped onto.
    static void sendFiles(const QStringList& paths, int x, int y, int streamWidth, int streamHeight);
    static void sendStatus(const QString& text);
    static void sendNeedPair();
    // The pointer left the stream window while the left button was held: the
    // user is dragging a file OUT of the remote desktop back to this machine.
    static void sendDragOut();

private:
    static QTcpSocket* socket();
    static QTcpSocket* s_socket;
    static bool s_connected;
};
