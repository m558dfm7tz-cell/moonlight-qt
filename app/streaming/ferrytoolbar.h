#pragma once

#include "SDL_compat.h"

#include <QString>
#include <QVector>
#include <QJsonObject>

class Session;

// Ferry in-window toolbar.
//
// A frosted-glass (semi-transparent, rounded) dark capsule drawn with white
// line icons, in its own borderless, always-on-top, non-activating SDL window.
// Button order: drag handle / fullscreen / black-screen privacy / quality /
// transfer (with progress bar) / end session. The bar can be dragged and docked
// to any of the four edges of the stream window; when docked left or right the
// buttons are laid out vertically. Moving the pointer away collapses it to a
// small drag handle; hovering (or, in fullscreen, moving to the middle of the
// docked edge) expands it again.
//
// All actions are performed by Moonlight itself (fullscreen toggle, black
// screen privacy, end session, quality reconnect via FerryBridge), so Ferry no
// longer needs an external overlay window or focus stealing. Transfer progress
// arrives from Ferry over the loopback IPC channel (transferProgress messages).
class FerryToolbar
{
public:
    enum DockEdge {
        DockTop,
        DockBottom,
        DockLeft,
        DockRight
    };

    explicit FerryToolbar(Session* session);
    ~FerryToolbar();

    // Create the toolbar window (after the stream window exists).
    bool init(SDL_Window* streamWindow);

    // Destroy the toolbar window (before the stream window is destroyed).
    void destroy();

    Uint32 windowId() const { return m_windowId; }
    bool isValid() const { return m_window != nullptr; }

    // Reposition the toolbar against its docked edge of the stream window.
    void follow();

    // Reposition the toolbar from an explicit stream-window rectangle. Used by
    // the Windows message hook while the window is being dragged, when the SDL
    // event loop is blocked inside the modal move loop and follow() is never
    // reached.
    void followTo(int wx, int wy, int ww);

    // Update focus state. Visibility is now driven by the pointer position
    // (hover-to-expand, leave-to-collapse) rather than by focus, so this only
    // records the state for callers that still query it.
    void setFocused(bool focused);
    void tick();

    // Mouse routing from the session loop. Returns true when the event was
    // consumed by the toolbar (so it is not forwarded to the input handler).
    bool handleMouseButton(const SDL_MouseButtonEvent* ev);
    bool handleMouseMotion(const SDL_MouseMotionEvent* ev);

    // State updates.
    void setFullscreen(bool fullscreen);
    void setPrivacy(bool on);
    void setQuality(const QString& resKey, int fps, int bitrateMbps, bool bitrateAuto);
    // Set the codec ("h264" | "hevc" | "hevc444") and audio-on-host state from
    // the launch preferences, at stream start and after a reconnect.
    void setStreamOptions(const QString& codec, bool audioOnHost);
    void setStatus(const QString& text);

    // Ferry -> Moonlight messages. Dispatches transferProgress (see
    // wb_AG_ipc_spec.md) plus the new privacyState / audioState / codecCaps
    // messages (see wb_AH_ipc_spec.md).
    void handleFerryMessage(const QJsonObject& obj);
    void handleTransferProgress(const QJsonObject& obj);

private:
    enum ButtonId {
        BtnDrag,
        BtnFullscreen,
        BtnPrivacy,
        BtnQuality,
        BtnAudio,
        BtnSeparator,
        BtnTransfer,
        BtnEnd,
        BtnResRemote,
        BtnResLocal,
        BtnRes720,
        BtnRes1080,
        BtnRes1440,
        BtnRes4K,
        BtnFps30,
        BtnFps60,
        BtnFps120,
        BtnRateAuto,
        BtnRate10,
        BtnRate20,
        BtnRate40,
        BtnRate80,
        BtnCodecH264,
        BtnCodecHevc,
        BtnCodecHevc444,
        BtnApply,
        BtnCancel,
        BtnCount
    };

    struct Item
    {
        ButtonId id;
        SDL_Rect rect;
        QString label;
        bool visible;
        bool selected;
        bool hover;
        bool danger;
        bool disabled;
    };

    void layout();
    void render();
    void setShape(); // rounded-corner + semi-transparent dark-glass window shape
    void blitText(struct SDL_Surface* dst, const QString& text, SDL_Color color, int x, int y);

    // White line-icon drawing primitives (software surface).
    static void putPixel(struct SDL_Surface* dst, int x, int y, SDL_Color c);
    static void drawLine(struct SDL_Surface* dst, int x0, int y0, int x1, int y1, SDL_Color c);
    static void fillCircle(struct SDL_Surface* dst, int cx, int cy, int r, SDL_Color c);
    static void fillRoundedRect(struct SDL_Surface* dst, const SDL_Rect& r, int radius, SDL_Color c);
    void drawIcon(struct SDL_Surface* dst, ButtonId id, const SDL_Rect& r, SDL_Color c);

    Item* itemAt(int x, int y);
    Item* itemById(ButtonId id);
    void onButton(ButtonId id);
    QString qualityLabel() const;
    void sendReconnectCurrent();

    // Position/size helpers.
    void computeClientRect(int& cx, int& cy, int& cw, int& ch);
    void reposition(int cx, int cy, int cw, int ch);
    void dockToNearestEdge();
    bool isHorizontal() const { return m_dockEdge == DockTop || m_dockEdge == DockBottom; }

    Session* m_session;
    SDL_Window* m_streamWindow;
    SDL_Window* m_window;
    Uint32 m_windowId;

#ifdef Q_OS_WIN32
    // Windows message hook so the toolbar keeps following the stream window
    // while the user drags it. During a title-bar drag Windows enters a modal
    // move loop and the SDL event loop is blocked, so WM_WINDOWPOSCHANGED /
    // WM_MOVING must be handled from inside the message hook.
    static void SDLCALL winMessageHook(void* userdata, void* hWnd,
                                       unsigned int message, Uint64 wParam, Sint64 lParam);
    void* m_streamHwnd;
#endif

    void* m_font; // TTF_Font*
    int m_fontSize;
    int m_surfaceW;
    int m_surfaceH;

    bool m_fullscreen;
    bool m_focused;
    bool m_privacy;
    bool m_qualityOpen;
    QString m_status;
    Uint32 m_statusTick;

    // Current quality.
    QString m_resKey;
    int m_fps;
    int m_bitrateMbps;
    bool m_bitrateAuto;

    // Pending quality (panel edits).
    QString m_pendingResKey;
    int m_pendingFps;
    int m_pendingBitrateMbps;
    bool m_pendingBitrateAuto;

    // Current codec ("h264" | "hevc" | "hevc444") and pending panel value.
    // Audio: true = sound stays on the remote host (default), false = sound
    // plays on this machine. See wb_AH_ipc_spec.md.
    QString m_codec;
    QString m_pendingCodec;
    bool m_audioOnHost;

    // Codec availability reported by Ferry (codecCaps). H.264 is always
    // available; HEVC 4:4:4 defaults to unavailable until confirmed.
    bool m_hevcAvailable;
    bool m_hevc444Available;

    // Vertical/horizontal divider line rects between button groups.
    QVector<SDL_Rect> m_separators;

    // Dock / expand state.
    DockEdge m_dockEdge;
    bool m_expanded;
    Uint32 m_lastActivityTick;
    bool m_dragging;
    int m_dragGrabX;
    int m_dragGrabY;

    // Transfer progress (driven by Ferry -> Moonlight transferProgress).
    bool m_transferActive;
    int m_transferPercent;
    Uint32 m_lastTransferTick;
    Uint32 m_transferDoneTick;
    bool m_showProgress;
    bool m_progressVertical;
    SDL_Rect m_progressTrack;
    SDL_Rect m_progressFill;

    // Last toolbar window position, used to avoid redundant SetWindowPosition
    // calls while the pointer is near the toolbar (which could otherwise stutter).
    int m_lastTx;
    int m_lastTy;

    QVector<Item> m_items;
};
