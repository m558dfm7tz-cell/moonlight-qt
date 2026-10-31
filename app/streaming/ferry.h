#pragma once

#include <QString>
#include <QStringList>
#include <QJsonObject>
#include <QByteArray>
#include <functional>

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
// The loopback TCP socket is reused bidirectionally: Moonlight -> Ferry
// (send(), below) and Ferry -> Moonlight (readMessages(), which dispatches
// transferProgress updates to the toolbar). Because the Qt event loop is
// suspended while streaming, readMessages() must be pumped manually from the
// SDL event loop; readyRead signals will not be delivered.
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
    // Reconnect with a new quality/codec handoff. The codec is one of
    // "h264" | "hevc" | "hevc444" (see wb_AI_ipc_spec.md). Audio is no longer
    // part of reconnect (the toolbar's speaker only mutes locally).
    static void sendReconnect(const QString& resolution, int fps, int bitrateMbps, bool bitrateAuto,
                              const QString& codec);
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

    // Register a handler for Ferry -> Moonlight messages (currently only
    // transferProgress). Called on the main thread from readMessages().
    static void setMessageHandler(const std::function<void(const QJsonObject&)>& handler);

    // Non-blocking read of pending Ferry -> Moonlight lines. Must be called
    // from the SDL event loop each iteration (see class comment).
    static void readMessages();

private:
    static QTcpSocket* socket();
    static QTcpSocket* s_socket;
    static bool s_connected;
    static std::function<void(const QJsonObject&)> s_messageHandler;
    static QByteArray s_readBuffer;
    static qint64 s_lastDragOutMs; // de-duplicate rapid double dragout triggers
};
