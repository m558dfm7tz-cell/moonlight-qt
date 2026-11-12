#include "wintitlebar.h"

#include "SDL_compat.h"

#include <QEvent>
#include <QGuiApplication>
#include <QWindow>

#ifdef Q_OS_WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <SDL_syswm.h>
#include <dwmapi.h>

namespace {

// Attribute ids are numeric so this still compiles against older Windows SDKs.
// DwmSetWindowAttribute returns an error on builds that do not know them; ignored.
constexpr DWORD kDwmwaUseImmersiveDarkModeBefore20H1 = 19;
constexpr DWORD kDwmwaUseImmersiveDarkMode = 20;          // DWMWA_USE_IMMERSIVE_DARK_MODE
constexpr DWORD kDwmwaWindowCornerPreference = 33;        // DWMWA_WINDOW_CORNER_PREFERENCE
constexpr DWORD kDwmwaBorderColor = 34;                   // DWMWA_BORDER_COLOR
constexpr DWORD kDwmwaCaptionColor = 35;                  // DWMWA_CAPTION_COLOR
constexpr DWORD kDwmwaTextColor = 36;                     // DWMWA_TEXT_COLOR
constexpr DWORD kDwmwcpRound = 2;                         // DWMWCP_ROUND
constexpr COLORREF kDwmwaColorNone = 0xFFFFFFFE;          // DWMWA_COLOR_NONE

} // namespace

void applyNativeWindowTitleBar(void* hwndPtr, bool borderless)
{
    HWND hwnd = reinterpret_cast<HWND>(hwndPtr);
    if (hwnd == nullptr) {
        return;
    }

    // Dark mode is what turns the system caption buttons light gray.
    const BOOL dark = TRUE;
    (void)DwmSetWindowAttribute(hwnd, kDwmwaUseImmersiveDarkModeBefore20H1, &dark, sizeof(dark));
    (void)DwmSetWindowAttribute(hwnd, kDwmwaUseImmersiveDarkMode, &dark, sizeof(dark));

    const COLORREF caption = RGB(0x25, 0x25, 0x26);
    const COLORREF text = RGB(0xE6, 0xE6, 0xE6);
    const COLORREF border = borderless ? kDwmwaColorNone : RGB(0x3C, 0x3C, 0x3C);
    const DWORD corner = kDwmwcpRound;
    (void)DwmSetWindowAttribute(hwnd, kDwmwaCaptionColor, &caption, sizeof(caption));
    (void)DwmSetWindowAttribute(hwnd, kDwmwaTextColor, &text, sizeof(text));
    (void)DwmSetWindowAttribute(hwnd, kDwmwaBorderColor, &border, sizeof(border));
    (void)DwmSetWindowAttribute(hwnd, kDwmwaWindowCornerPreference, &corner, sizeof(corner));
}

void applyQtWindowTitleBar(QWindow* window)
{
    if (window == nullptr || !window->isVisible()) {
        return;
    }
    const Qt::WindowStates states = window->windowStates();
    const bool borderless = states.testFlag(Qt::WindowMaximized) || states.testFlag(Qt::WindowFullScreen);
    applyNativeWindowTitleBar(reinterpret_cast<void*>(window->winId()), borderless);
}

void applySdlWindowTitleBar(SDL_Window* window)
{
    if (window == nullptr) {
        return;
    }
    SDL_SysWMinfo info;
    SDL_VERSION(&info.version);
    if (!SDL_GetWindowWMInfo(window, &info) || info.subsystem != SDL_SYSWM_WINDOWS) {
        return;
    }
    const Uint32 flags = SDL_GetWindowFlags(window);
    const bool borderless = (flags & (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_FULLSCREEN_DESKTOP | SDL_WINDOW_MAXIMIZED)) != 0;
    applyNativeWindowTitleBar(info.info.win.window, borderless);
}

namespace {

class QtTitleBarFilter : public QObject
{
public:
    explicit QtTitleBarFilter(QObject* parent)
        : QObject(parent)
    {
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        switch (event->type()) {
        case QEvent::Show:
        case QEvent::WindowStateChange:
        case QEvent::WinIdChange:
            if (QWindow* window = qobject_cast<QWindow*>(watched)) {
                applyQtWindowTitleBar(window);
            }
            break;
        default:
            break;
        }
        return QObject::eventFilter(watched, event);
    }
};

} // namespace

void installQtWindowTitleBarStyle()
{
    QGuiApplication* app = qobject_cast<QGuiApplication*>(QGuiApplication::instance());
    if (app == nullptr) {
        return;
    }
    QtTitleBarFilter* filter = new QtTitleBarFilter(app);
    const auto windows = QGuiApplication::topLevelWindows();
    for (QWindow* window : windows) {
        window->installEventFilter(filter);
        applyQtWindowTitleBar(window);
    }
}

#else

void applyNativeWindowTitleBar(void*, bool)
{
}

void applyQtWindowTitleBar(QWindow* window)
{
    Q_UNUSED(window);
}

void applySdlWindowTitleBar(SDL_Window*)
{
}

void installQtWindowTitleBarStyle()
{
}

#endif
