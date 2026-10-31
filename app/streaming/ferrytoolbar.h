#pragma once

#include "SDL_compat.h"

#include <QString>
#include <QVector>
#include <QHash>
#include <QPair>
#include <QImage>
#include <QJsonObject>

class Session;

// Ferry in-window toolbar.
//
// A frosted-glass capsule (semi-transparent, rounded, anti-aliased) drawn in
// its own borderless, always-on-top, non-activating SDL window. On Windows the
// window is presented as a per-pixel-alpha layered window (UpdateLayeredWindow)
// so the capsule corners are truly transparent and anti-aliased and the
// background is genuinely translucent — unlike the previous whole-window-opacity
// + SetWindowRgn approach which produced an opaque dark rectangle with aliased
// corners. Button icons are pre-rendered PNG assets (4x logical size) that are
// scaled per devicePixelRatio so they stay crisp at 100/150/200% scaling.
//
// Button order: drag handle / fullscreen / black-screen privacy / quality (HD) /
// audio | separator | transfer (with progress bar) | separator | close. The bar
// can be dragged and docked to any of the four edges of the stream window; when
// docked left or right the buttons are laid out vertically. Moving the pointer
// away collapses it to a small handle; hovering expands it again.
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

    bool init(SDL_Window* streamWindow);
    void destroy();

    Uint32 windowId() const { return m_windowId; }
    bool isValid() const { return m_window != nullptr; }

    void follow();
    void followTo(int wx, int wy, int ww);
    void setFocused(bool focused);
    void tick();

    bool handleMouseButton(const SDL_MouseButtonEvent* ev);
    bool handleMouseMotion(const SDL_MouseMotionEvent* ev);

    // State updates.
    void setFullscreen(bool fullscreen);
    void setPrivacy(bool on);
    void setQuality(const QString& resKey, int fps, int bitrateMbps, bool bitrateAuto);
    // Set the codec ("h264" | "hevc" | "hevc444") and audio-on-host state from
    // the launch preferences. audioOnHost only drives the speaker's enabled
    // state (gray when true); it no longer triggers a reconnect.
    void setStreamOptions(const QString& codec, bool audioOnHost);
    void setStatus(const QString& text);

    void handleFerryMessage(const QJsonObject& obj);
    void handleTransferProgress(const QJsonObject& obj);

    // Development-only preview hook, used solely by the FERRY_TOOLBAR_PREVIEW
    // entry point to render fixed states for screenshots. Never called during
    // normal streaming. Sets dock/expand/privacy/audio/quality/progress and
    // re-renders.
    void debugSetState(DockEdge edge, bool expanded, bool privacyOn, bool audioOnHost,
                       bool audioMutedLocal, bool qualityOpen, int transferPercent);

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
    void present(); // per-pixel-alpha layered present (Windows) / SDL surface present
    void blitText(SDL_Surface* dst, const QString& text, SDL_Color color, int x, int y);

    // Per-pixel alpha drawing helpers (straight-alpha source-over onto ARGB8888).
    static void blendPixel(SDL_Surface* dst, int x, int y, Uint8 r, Uint8 g, Uint8 b, Uint8 a);
    static void fillRoundedRectAA(SDL_Surface* dst, int x0, int y0, int x1, int y1,
                                  int radius, Uint8 r, Uint8 g, Uint8 b, Uint8 a);
    static void strokeRoundedRectAA(SDL_Surface* dst, int x0, int y0, int x1, int y1,
                                    int radius, float thickness, Uint8 r, Uint8 g, Uint8 b, Uint8 a);
    static void blitImage(SDL_Surface* dst, const QImage& img, int dx, int dy);

    // Icon assets (PNG, 4x logical size) loaded from qrc and cached.
    QImage loadIcon(const QString& name);
    const QImage& iconAsset(const QString& name);
    void blitIcon(SDL_Surface* dst, ButtonId id, const SDL_Rect& r);

    // DPI helper: logical -> physical pixels on this monitor.
    int S(int v) const;

    Item* itemAt(int x, int y);
    Item* itemById(ButtonId id);
    void onButton(ButtonId id);
    QString qualityLabel() const;
    void sendReconnectCurrent();

    void computeClientRect(int& cx, int& cy, int& cw, int& ch);
    void reposition(int cx, int cy, int cw, int ch);
    void dockToNearestEdge();
    bool isHorizontal() const { return m_dockEdge == DockTop || m_dockEdge == DockBottom; }

    Session* m_session;
    SDL_Window* m_streamWindow;
    SDL_Window* m_window;
    Uint32 m_windowId;
    SDL_Surface* m_surface; // offscreen ARGB8888 backbuffer

#ifdef Q_OS_WIN32
    static void SDLCALL winMessageHook(void* userdata, void* hWnd,
                                       unsigned int message, Uint64 wParam, Sint64 lParam);
    void* m_streamHwnd;
    void* m_hwnd;       // toolbar HWND (layered presentation)
    bool m_layered;
#endif

    void* m_font; // TTF_Font*
    int m_fontSize;
    int m_surfaceW;
    int m_surfaceH;
    int m_barW; // capsule (main bar) size, excludes panel/tooltip
    int m_barH;
    float m_dpr; // devicePixelRatio of the stream window's monitor

    bool m_fullscreen;
    bool m_focused;
    bool m_privacy;
    bool m_qualityOpen;
    QString m_status;
    Uint32 m_statusTick;

    QString m_resKey;
    int m_fps;
    int m_bitrateMbps;
    bool m_bitrateAuto;

    QString m_pendingResKey;
    int m_pendingFps;
    int m_pendingBitrateMbps;
    bool m_pendingBitrateAuto;

    QString m_codec;
    QString m_pendingCodec;
    // audioOnHost=true -> sound stays on the remote host (speaker grayed/disabled).
    // audioOnHost=false -> sound streams here; m_audioMutedLocal toggles local mute.
    bool m_audioOnHost;
    bool m_audioMutedLocal;

    bool m_hevcAvailable;
    bool m_hevc444Available;

    QVector<SDL_Rect> m_separators;

    // Quality panel card + its row labels (text, y-center).
    SDL_Rect m_panelRect;
    QVector<QPair<QString, int>> m_panelLabels;

    DockEdge m_dockEdge;
    bool m_expanded;
    Uint32 m_lastActivityTick;
    bool m_dragging;
    int m_dragGrabX;
    int m_dragGrabY;

    bool m_transferActive;
    int m_transferPercent;
    Uint32 m_lastTransferTick;
    Uint32 m_transferDoneTick;
    bool m_showProgress;
    bool m_progressVertical;
    SDL_Rect m_progressTrack;
    SDL_Rect m_progressFill;

    // Tooltip (e.g. disabled audio hint).
    QString m_tooltipText;
    SDL_Rect m_tooltipRect;
    bool m_tooltipVisible;

    int m_lastTx;
    int m_lastTy;

    QVector<Item> m_items;
    QHash<QString, QImage> m_iconCache;
};
