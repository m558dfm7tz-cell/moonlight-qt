#include "ferrytoolbar_preview.h"

#include "ferrytoolbar.h"

#include "SDL_compat.h"
#include <SDL_ttf.h>

#include <QDir>
#include <QImage>
#include <QJsonObject>

#ifdef Q_OS_WIN32
#include <windows.h>
#endif

#ifdef Q_OS_WIN32
// Capture a screen region to a PNG via GDI BitBlt + QImage. This is equivalent
// to CopyFromScreen and proves the composited result (rounded corners /
// translucency) as it actually appears on screen.
static void saveScreenPng(const QString& path, int x, int y, int w, int h)
{
    if (w <= 0 || h <= 0) {
        return;
    }
    HDC screen = GetDC(nullptr);
    HDC mem = CreateCompatibleDC(screen);
    BITMAPINFO bi;
    ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h; // top-down DIB
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (bmp != nullptr && bits != nullptr) {
        HGDIOBJ old = SelectObject(mem, bmp);
        BitBlt(mem, 0, 0, w, h, screen, x, y, SRCCOPY | CAPTUREBLT);
        // BitBlt may leave the reserved alpha byte at 0 in a 32bpp DIB; force
        // it opaque so QImage::Format_ARGB32 reads correct colors.
        uchar* p = (uchar*)bits;
        for (int i = 3; i < w * h * 4; i += 4) {
            p[i] = 0xFF;
        }
        QImage img((const uchar*)bits, w, h, w * 4, QImage::Format_ARGB32);
        img.save(path, "PNG");
        SelectObject(mem, old);
        DeleteObject(bmp);
    }
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);
}
#endif

int runFerryToolbarPreview(const QString& outDir)
{
#ifdef Q_OS_WIN32
    QDir().mkpath(outDir);

    // The toolbar loads a CJK system font via SDL_ttf; in normal streaming the
    // Session's OverlayManager initializes SDL_ttf, but the preview has no
    // Session so we initialize it here.
    if (TTF_Init() != 0) {
        return 1;
    }

    // A plain "stream" window behind the toolbar so the translucent capsule and
    // its rounded (transparent) corners are clearly visible against a known
    // light background. Always-on-top keeps it above any maximized app window
    // on the test machine (the toolbar itself is created afterwards, so it
    // still stacks above this window).
    SDL_Window* streamWin = SDL_CreateWindow("FerryPreviewStream",
                                             100, 100, 1100, 760,
                                             SDL_WINDOW_BORDERLESS | SDL_WINDOW_ALWAYS_ON_TOP);
    if (streamWin == nullptr) {
        TTF_Quit();
        return 1;
    }
    SDL_Surface* ws = SDL_GetWindowSurface(streamWin);
    if (ws != nullptr) {
        SDL_FillRect(ws, nullptr, SDL_MapRGB(ws->format, 205, 212, 222));
        SDL_UpdateWindowSurface(streamWin);
    }
    SDL_RaiseWindow(streamWin);
    SDL_PumpEvents();
    SDL_Delay(300);

    FerryToolbar* tb = new FerryToolbar(nullptr);
    if (!tb->init(streamWin)) {
        SDL_DestroyWindow(streamWin);
        return 1;
    }
    tb->setQuality("1080", 60, 20, false);
    tb->setStreamOptions("h264", false); // audioOnHost=false -> speaker active

    auto capture = [&](const QString& name) {
        SDL_Window* tw = SDL_GetWindowFromID(tb->windowId());
        int wx = 0, wy = 0, ww = 1, wh = 1;
        if (tw != nullptr) {
            SDL_GetWindowPosition(tw, &wx, &wy);
            SDL_GetWindowSize(tw, &ww, &wh);
        }
        SDL_PumpEvents();
        SDL_Delay(350); // let the layered window composite
        saveScreenPng(outDir + "/" + name, wx - 28, wy - 28, ww + 56, wh + 56);
    };

    // A: expanded, docked top.
    tb->debugSetState(FerryToolbar::DockTop, true, false, false, false, false, -1);
    capture("A_expanded.png");

    // B: collapsed handle.
    tb->debugSetState(FerryToolbar::DockTop, false, false, false, false, false, -1);
    capture("B_collapsed.png");

    // C: docked right (vertical).
    tb->debugSetState(FerryToolbar::DockRight, true, false, false, false, false, -1);
    capture("C_docked_right.png");

    // C: docked left (vertical).
    tb->debugSetState(FerryToolbar::DockLeft, true, false, false, false, false, -1);
    capture("C_docked_left.png");

    // Quality panel open (HEVC 4:4:4 grayed out via codecCaps).
    QJsonObject codecCaps;
    codecCaps.insert("type", "codecCaps");
    codecCaps.insert("hevc", true);
    codecCaps.insert("hevc444", false);
    tb->handleFerryMessage(codecCaps);
    tb->debugSetState(FerryToolbar::DockTop, true, false, false, false, true, -1);
    capture("D_quality_panel.png");

    // Transfer progress at 62%.
    tb->debugSetState(FerryToolbar::DockTop, true, false, false, false, false, 62);
    capture("E_transfer_62.png");

    // Privacy (black screen) on.
    tb->debugSetState(FerryToolbar::DockTop, true, true, false, false, false, -1);
    capture("F_privacy_on.png");

    // Speaker disabled (sound stays on host).
    tb->debugSetState(FerryToolbar::DockTop, true, false, true, false, false, -1);
    capture("G_audio_onhost_gray.png");

    // Speaker muted (local mute).
    tb->debugSetState(FerryToolbar::DockTop, true, false, false, true, false, -1);
    capture("H_audio_muted.png");

    tb->destroy();
    delete tb;
    SDL_DestroyWindow(streamWin);
    TTF_Quit();
    return 0;
#else
    return 1;
#endif
}
