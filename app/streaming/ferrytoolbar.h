#pragma once

#include "SDL_compat.h"

#include <QString>
#include <QVector>

class Session;

// Ferry in-window toolbar.
//
// Renders a dark bar (peer name + 全屏 / 黑屏隐私 / 清晰度 / 结束远程 buttons
// plus an inline quality panel) in its own borderless, always-on-top,
// non-activating SDL window that tracks the top-centre of Moonlight's stream
// window. All actions are performed by Moonlight itself (fullscreen toggle,
// black-screen privacy, end session, quality reconnect via FerryBridge), so
// Ferry no longer needs an external overlay window or any SetForegroundWindow
// focus stealing.
class FerryToolbar
{
public:
    explicit FerryToolbar(Session* session);
    ~FerryToolbar();

    // Create the toolbar window (after the stream window exists).
    bool init(SDL_Window* streamWindow);

    // Destroy the toolbar window (before the stream window is destroyed).
    void destroy();

    Uint32 windowId() const { return m_windowId; }
    bool isValid() const { return m_window != nullptr; }

    // Reposition the toolbar over the top-centre of the stream window.
    void follow();

    // Update focus state and recompute visibility (auto-hide in fullscreen).
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
    void setStatus(const QString& text);

private:
    enum ButtonId {
        BtnFullscreen,
        BtnPrivacy,
        BtnQuality,
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
    };

    void layout();
    void render();
    void blitText(struct SDL_Surface* dst, const QString& text, SDL_Color color, int x, int y);
    void drawButton(struct SDL_Surface* dst, const Item& item);

    Item* itemAt(int x, int y);
    Item* itemById(ButtonId id);
    void onButton(ButtonId id);
    void syncLabels();

    Session* m_session;
    SDL_Window* m_streamWindow;
    SDL_Window* m_window;
    Uint32 m_windowId;

    void* m_font; // TTF_Font*
    int m_fontSize;
    int m_barHeight;
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

    // Auto-hide state (fullscreen reveal-on-top).
    bool m_visible;
    Uint32 m_lastTopTick;

    QVector<Item> m_items;
};
