#include "ferrytoolbar.h"
#include "ferrytoolbar_geometry.h"
#include <QPainter>
#include <QPainterPath>

#include "ferry.h"
#include "streaming/session.h"
#include "path.h"

#include <SDL_ttf.h>
#include <SDL_syswm.h>

#ifdef Q_OS_WIN32
#include <windows.h>
#endif

#include <QPair>
#include <QHash>
#include <QtGlobal>
#include <QtMath>
#include <cmath>

// Logical sizes (scaled by devicePixelRatio at layout/render time).
#define TOOLBAR_FONT_SIZE 13
#define TOOLBAR_MARGIN 8
#define TOOLBAR_GAP 4
#define TOOLBAR_CELL 40
#define TOOLBAR_ICON 26
#define TOOLBAR_COLLAPSED_RADIUS 13
#define TOOLBAR_STATUS_MS 6000
#define TOOLBAR_FS_HIDE_MS 1500
#define TOOLBAR_WIN_HIDE_MS 250
#define TOOLBAR_TRANSFER_TIMEOUT 15000
#define TOOLBAR_DONE_FLASH_MS 1000
#define TOOLBAR_PROGRESS_W 56
#define TOOLBAR_PROGRESS_H 44
#define TOOLBAR_PROGRESS_THICK 6
#define TOOLBAR_SEP_W 8
#define TOOLBAR_SEP_H 8

// Palette: light translucent capsule (much lighter than the old near-opaque
// dark bar), Ferry dark-card panel, white icon stroke, cyan progress, red X.
static SDL_Color C_BAR_BG     = { 46, 57, 72, 168 };
static SDL_Color C_BAR_BORDER = { 255, 255, 255, 44 };
static SDL_Color C_HOVER      = { 255, 255, 255, 30 };
static SDL_Color C_ACCENT     = { 37, 102, 179, 245 };
static SDL_Color C_DANGER     = { 229, 72, 77, 255 };
static SDL_Color C_ICON       = { 241, 246, 253, 255 };
static SDL_Color C_MUTED      = { 149, 168, 185, 255 };
static SDL_Color C_TRACK      = { 255, 255, 255, 36 };
static SDL_Color C_FILL       = { 56, 200, 255, 255 };
static SDL_Color C_PANEL_BG   = { 16, 27, 38, 242 };
static SDL_Color C_PANEL_BORDER = { 42, 59, 75, 255 };
static SDL_Color C_CHIP_BG    = { 255, 255, 255, 20 };
static SDL_Color C_TOOLTIP_BG = { 20, 30, 42, 245 };

FerryToolbar::FerryToolbar(Session* session)
    : m_session(session),
      m_streamWindow(nullptr),
      m_window(nullptr),
      m_windowId(0),
      m_surface(nullptr),
      m_font(nullptr),
      m_fontSize(TOOLBAR_FONT_SIZE),
      m_surfaceW(0),
      m_surfaceH(0),
      m_barW(0),
      m_barH(0),
      m_dpr(1.0f),
      m_fullscreen(false),
      m_focused(false),
      m_privacy(false),
      m_qualityOpen(false),
      m_statusTick(0),
      m_resKey("1080"),
      m_fps(60),
      m_bitrateMbps(20),
      m_bitrateAuto(false),
      m_pendingResKey("1080"),
      m_pendingFps(60),
      m_pendingBitrateMbps(20),
      m_pendingBitrateAuto(false),
      m_codec("h264"),
      m_pendingCodec("h264"),
      m_audioOnHost(true),
      m_audioMutedLocal(false),
      m_hevcAvailable(true),
      m_hevc444Available(false),
      m_dockEdge(DockTop),
      m_expanded(true),
      m_lastActivityTick(0),
      m_dragging(false),
      m_dragGrabX(0),
      m_dragGrabY(0),
      m_transferActive(false),
      m_transferPercent(0),
      m_lastTransferTick(0),
      m_transferDoneTick(0),
      m_showProgress(false),
      m_progressVertical(false),
      m_tooltipVisible(false),
      m_lastTx(-1),
      m_lastTy(-1)
{
    m_progressTrack = { 0, 0, 0, 0 };
    m_progressFill = { 0, 0, 0, 0 };
    m_panelRect = { 0, 0, 0, 0 };
    m_tooltipRect = { 0, 0, 0, 0 };
#ifdef Q_OS_WIN32
    m_streamHwnd = nullptr;
    m_hwnd = nullptr;
    m_layered = false;
#endif
}

FerryToolbar::~FerryToolbar()
{
    destroy();
}

static TTF_Font* loadToolbarFont(int size)
{
    const char* candidates[] = {
        "C:/Windows/Fonts/msyh.ttc",
        "C:/Windows/Fonts/msyhbd.ttc",
        "C:/Windows/Fonts/simhei.ttf",
        "C:/Windows/Fonts/simsun.ttc",
        "C:/Windows/Fonts/Deng.ttf",
    };
    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
        const char* path = candidates[i];
        TTF_Font* font = nullptr;
        if (QString(path).endsWith(".ttc", Qt::CaseInsensitive)) {
            font = TTF_OpenFontIndex(path, size, 0);
        }
        else {
            font = TTF_OpenFont(path, size);
        }
        if (font != nullptr) {
            return font;
        }
    }

    QByteArray fontData = Path::readDataFile("ModeSeven.ttf");
    if (!fontData.isEmpty()) {
        return TTF_OpenFontRW(SDL_RWFromConstMem(fontData.constData(), fontData.size()), 1, size);
    }
    return nullptr;
}

int FerryToolbar::S(int v) const
{
    return (int)qRound((double)v * (double)m_dpr);
}

bool FerryToolbar::init(SDL_Window* streamWindow, bool offscreen)
{
    m_streamWindow = streamWindow;
    m_offscreen = offscreen;

#ifdef Q_OS_WIN32
    // Determine the monitor DPI of the stream window so the toolbar renders at
    // physical resolution (crisp at 100/150/200% scaling).
    m_dpr = 1.0f;
    SDL_SysWMinfo streamInfo;
    SDL_VERSION(&streamInfo.version);
    if (SDL_GetWindowWMInfo(m_streamWindow, &streamInfo) && streamInfo.subsystem == SDL_SYSWM_WINDOWS) {
        m_streamHwnd = streamInfo.info.win.window;
        HMODULE hUser32 = GetModuleHandleW(L"user32.dll");
        typedef UINT (WINAPI* GetDpiForWindowFn)(HWND);
        GetDpiForWindowFn getDpi = (GetDpiForWindowFn)(void*)GetProcAddress(hUser32, "GetDpiForWindow");
        UINT dpi = 96;
        if (getDpi != nullptr) {
            dpi = getDpi((HWND)m_streamHwnd);
        }
        else {
            HDC dc = GetDC(nullptr);
            if (dc != nullptr) {
                dpi = GetDeviceCaps(dc, LOGPIXELSX);
                ReleaseDC(nullptr, dc);
            }
        }
        if (dpi > 0) {
            m_dpr = (float)dpi / 96.0f;
        }
    }
#endif

    m_fontSize = S(TOOLBAR_FONT_SIZE);
    m_font = loadToolbarFont(m_fontSize);
    if (m_font == nullptr) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Ferry toolbar: no font available");
    }

    m_window = SDL_CreateWindow("FerryRemoteToolbar",
                                0, 0,
                                S(TOOLBAR_CELL) + S(TOOLBAR_MARGIN) * 2,
                                S(TOOLBAR_CELL) + S(TOOLBAR_MARGIN) * 2,
                                SDL_WINDOW_BORDERLESS | SDL_WINDOW_SKIP_TASKBAR | (offscreen ? SDL_WINDOW_HIDDEN : 0));
    if (m_window == nullptr) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Ferry toolbar window creation failed: %s", SDL_GetError());
        return false;
    }
    m_windowId = SDL_GetWindowID(m_window);

#ifdef Q_OS_WIN32
    SDL_SysWMinfo info;
    SDL_VERSION(&info.version);
    if (SDL_GetWindowWMInfo(m_window, &info) && info.subsystem == SDL_SYSWM_WINDOWS) {
        m_hwnd = info.info.win.window;
        // Owned by the stream, above that window without floating over other apps.
        if (m_streamHwnd != nullptr) SetWindowLongPtrW((HWND)m_hwnd, GWLP_HWNDPARENT, (LONG_PTR)m_streamHwnd);
        LONG_PTR ex = GetWindowLongPtrW((HWND)m_hwnd, GWL_EXSTYLE);
        // NOACTIVATE/TOOLWINDOW keep the toolbar from stealing focus or showing
        // in the taskbar; LAYERED makes it a per-pixel-alpha window so the
        // rounded corners are truly transparent and anti-aliased.
        ex |= WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_LAYERED;
        SetWindowLongPtrW((HWND)m_hwnd, GWL_EXSTYLE, ex);
        m_layered = true;
    }
    SDL_SetWindowsMessageHook(winMessageHook, this);
#endif

    m_lastActivityTick = SDL_GetTicks();
    if(!offscreen)m_revealTick=m_lastActivityTick;

    return true;
}

void FerryToolbar::destroy()
{
#ifdef Q_OS_WIN32
    if (m_streamHwnd != nullptr) {
        SDL_SetWindowsMessageHook(nullptr, nullptr);
        m_streamHwnd = nullptr;
    }
    m_hwnd = nullptr;
    m_layered = false;
#endif
    if (m_surface != nullptr) {
        SDL_FreeSurface(m_surface);
        m_surface = nullptr;
    }
    m_iconCache.clear();
    if (m_window != nullptr) {
        SDL_DestroyWindow(m_window);
        m_window = nullptr;
        m_windowId = 0;
    }
    if (m_font != nullptr) {
        TTF_CloseFont((TTF_Font*)m_font);
        m_font = nullptr;
    }
    m_streamWindow = nullptr;
}

void FerryToolbar::setFullscreen(bool fullscreen)
{
    m_fullscreen = fullscreen;
    m_lastActivityTick = SDL_GetTicks();
    render();
}

void FerryToolbar::setFocused(bool focused)
{
    m_focused = focused;
    if (!focused && !m_dragging) {
        m_expanded = false;
        render();
    }
}

void FerryToolbar::setPrivacy(bool on)
{
    m_privacy = on;
    render();
}

void FerryToolbar::setStatus(const QString& text)
{
    m_status = text;
    m_statusTick = SDL_GetTicks();
    render();
}

void FerryToolbar::setQuality(const QString& resKey, int fps, int bitrateMbps, bool bitrateAuto)
{
    m_resKey = resKey;
    m_fps = fps;
    m_bitrateMbps = bitrateMbps;
    m_bitrateAuto = bitrateAuto;
    m_pendingResKey = resKey;
    m_pendingFps = fps;
    m_pendingBitrateMbps = bitrateMbps;
    m_pendingBitrateAuto = bitrateAuto;
    render();
}

void FerryToolbar::setStreamOptions(const QString& codec, bool audioOnHost)
{
    m_codec = codec;
    m_pendingCodec = codec;
    m_audioOnHost = audioOnHost;
    m_audioMutedLocal = false;
    render();
}

void FerryToolbar::debugSetState(DockEdge edge, bool expanded, bool privacyOn, bool audioOnHost,
                                 bool audioMutedLocal, bool qualityOpen, int transferPercent)
{
    m_dockEdge = edge;
    m_expanded = expanded;
    m_privacy = privacyOn;
    m_audioOnHost = audioOnHost;
    m_audioMutedLocal = audioMutedLocal;
    m_qualityOpen = qualityOpen;
    if (m_session != nullptr) {
        m_session->setAudioMuted(audioMutedLocal && !audioOnHost);
    }
    if (transferPercent >= 0) {
        m_transferActive = true;
        m_transferPercent = qBound(0, transferPercent, 100);
        m_lastTransferTick = SDL_GetTicks();
    }
    else {
        m_transferActive = false;
        m_transferPercent = 0;
        m_lastTransferTick = 0;
    }
    render();
}

void FerryToolbar::handleTransferProgress(const QJsonObject& obj)
{
    if (obj.value("type").toString() != QLatin1String("transferProgress")) {
        return;
    }

    const QString state = obj.value("state").toString();
    if (state == QLatin1String("sending")) {
        m_transferActive = true;
        m_transferPercent = qBound(0, obj.value("percent").toInt(), 100);
        m_lastTransferTick = SDL_GetTicks();
    }
    else if (state == QLatin1String("done") || state == QLatin1String("failed")) {
        m_transferActive = false;
        m_transferPercent = 0;
        m_lastTransferTick = 0;
        if (state == QLatin1String("done")) {
            m_transferDoneTick = SDL_GetTicks();
        }
    }
    render();
}

void FerryToolbar::handleFerryMessage(const QJsonObject& obj)
{
    const QString type = obj.value("type").toString();

    if (type == QLatin1String("transferProgress")) {
        handleTransferProgress(obj);
    }
    else if (type == QLatin1String("privacyState")) {
        m_privacy = obj.value("on").toBool();
        render();
    }
    else if (type == QLatin1String("audioState")) {
        m_audioOnHost = obj.value("onHost").toBool();
        // Sound stays on host -> speaker disabled. Sound streams here -> local
        // mute toggle, starting unmuted. No reconnect in either case.
        m_audioMutedLocal = false;
        if (m_session != nullptr) {
            m_session->setAudioMuted(false);
        }
        render();
    }
    else if (type == QLatin1String("codecCaps")) {
        m_hevcAvailable = obj.value("hevc").toBool(true);
        m_hevc444Available = obj.value("hevc444").toBool(false);
        auto fallback = [&](QString& codec) {
            if ((codec == QLatin1String("hevc") && !m_hevcAvailable) ||
                (codec == QLatin1String("hevc444") && !m_hevc444Available)) {
                codec = QLatin1String("h264");
            }
        };
        fallback(m_codec);
        fallback(m_pendingCodec);
        render();
    }
}

static int textWidth(TTF_Font* font, const QString& text)
{
    int w, h;
    if (font != nullptr && TTF_SizeUTF8(font, text.toUtf8().constData(), &w, &h) == 0) {
        return w;
    }
    return text.length() * 8;
}

FerryToolbar::Item* FerryToolbar::itemById(ButtonId id)
{
    for (Item& item : m_items) {
        if (item.id == id) {
            return &item;
        }
    }
    return nullptr;
}

FerryToolbar::Item* FerryToolbar::itemAt(int x, int y)
{
    for (Item& item : m_items) {
        if (item.visible && x >= item.rect.x && x < item.rect.x + item.rect.w &&
                y >= item.rect.y && y < item.rect.y + item.rect.h) {
            return &item;
        }
    }
    return nullptr;
}

QString FerryToolbar::qualityLabel() const
{
    if (m_resKey == "remote") return QStringLiteral("自动");
    if (m_resKey == "local")  return QStringLiteral("本机");
    if (m_resKey == "720")    return QStringLiteral("HD");
    if (m_resKey == "1080")   return QStringLiteral("HD");
    if (m_resKey == "1440")   return QStringLiteral("2K");
    if (m_resKey == "4K")     return QStringLiteral("4K");
    return QStringLiteral("HD");
}

void FerryToolbar::layout()
{
    TTF_Font* font = (TTF_Font*)m_font;
    m_items.clear();
    m_separators.clear();
    m_panelLabels.clear();
    m_showProgress = false;
    m_progressVertical = !isHorizontal();

    const int M = S(TOOLBAR_MARGIN);
    const int C = S(TOOLBAR_CELL);
    const int G = S(TOOLBAR_GAP);
    const bool horiz = isHorizontal();

    if (!m_expanded) {
        // Collapsed: a single rounded handle with an expand chevron.
        Item it;
        it.id = BtnDrag;
        it.label = QString();
        it.visible = true;
        it.selected = false;
        it.hover = false;
        it.danger = false;
        it.disabled = false;
        m_barW = m_surfaceW = horiz ? S(84) : S(20);
        m_barH = m_surfaceH = horiz ? S(20) : S(84);
        it.rect = { 0, 0, m_barW, m_barH };
        m_items.append(it);
        return;
    }

    int x = M;
    int y = M;

    // Main button + separator sequence: drag / fullscreen / privacy / quality /
    // audio | transfer (+progress) | end.
    const ButtonId sequence[] = {
        BtnDrag, BtnFullscreen, BtnPrivacy, BtnQuality, BtnAudio,
        BtnSeparator, BtnTransfer, BtnSeparator, BtnEnd
    };

    for (ButtonId sid : sequence) {
        if (sid == BtnSeparator) {
            SDL_Rect sr;
            if (horiz) {
                sr = { x + S(TOOLBAR_SEP_W) / 2, y + (C - S(24)) / 2, 1, S(24) };
                x += S(TOOLBAR_SEP_W) + G;
            }
            else {
                sr = { x + (C - S(24)) / 2, y + S(TOOLBAR_SEP_H) / 2, S(24), 1 };
                y += S(TOOLBAR_SEP_H) + G;
            }
            m_separators.append(sr);
            continue;
        }

        Item it;
        it.id = sid;
        it.label = (sid == BtnQuality) ? qualityLabel() : QString();
        it.visible = true;
        it.selected = false;
        it.hover = false;
        it.danger = (sid == BtnEnd);
        it.disabled = (sid == BtnAudio && m_audioOnHost);
        it.rect = { x, y, C, C };
        m_items.append(it);

        if (horiz) x += C + G;
        else y += C + G;

        // Reserve inline space for the transfer progress bar.
        if (sid == BtnTransfer && m_transferActive) {
            if (horiz) x += S(TOOLBAR_PROGRESS_W) + G;
            else y += S(TOOLBAR_PROGRESS_H) + G;
        }
    }

    if (Item* p = itemById(BtnPrivacy)) p->selected = m_privacy;
    if (Item* p = itemById(BtnFullscreen)) p->selected = m_fullscreen;
    if (Item* p = itemById(BtnQuality)) p->selected = m_qualityOpen;
    if (Item* p = itemById(BtnAudio)) p->selected = !m_audioOnHost && m_audioMutedLocal;

    int mainW = horiz ? (x - G + M) : (C + 2 * M);
    int mainH = horiz ? (C + 2 * M) : (y - G + M);

    m_barW = mainW;
    m_barH = mainH;
    m_surfaceW = mainW;
    m_surfaceH = mainH;

    if (m_qualityOpen) {
        // Dark card panel below the bar with labelled chip rows. The chips use
        // the same dark-card + solid-highlight style as the Ferry settings page.
        const int padX = S(12);
        const int padY = S(10);
        const int rowH = S(28);
        const int rowGap = S(6);
        const int labelW = S(48);
        const int chipGap = S(6);
        const int chipPadH = S(12);

        int panelX = 0;
        int py = m_barH + S(6);
        int contentX = panelX + padX;
        int cursor = py + padY;
        int panelW = 0;
        const int maxPanelW=qMax(S(180),qMin(S(680),screenBounds().width()-S(12)));

        struct ChipRow { const char* title; QVector<QPair<ButtonId, const char*>> chips; };
        QVector<ChipRow> rows;
        rows.append({ "分辨率", {
            qMakePair(BtnResRemote, "对方屏幕"), qMakePair(BtnResLocal, "本机屏幕"),
            qMakePair(BtnRes720, "720p"), qMakePair(BtnRes1080, "1080p"),
            qMakePair(BtnRes1440, "1440p"), qMakePair(BtnRes4K, "4K") } });
        rows.append({ "帧率", {
            qMakePair(BtnFps30, "30 fps"), qMakePair(BtnFps60, "60 fps"),
            qMakePair(BtnFps120, "120 fps") } });
        rows.append({ "码率", {
            qMakePair(BtnRateAuto, "自动"), qMakePair(BtnRate10, "10 Mbps"),
            qMakePair(BtnRate20, "20 Mbps"), qMakePair(BtnRate40, "40 Mbps"),
            qMakePair(BtnRate80, "80 Mbps") } });
        rows.append({ "编码", {
            qMakePair(BtnCodecH264, "H.264"), qMakePair(BtnCodecHevc, "HEVC"),
            qMakePair(BtnCodecHevc444, "HEVC 4:4:4") } });

        for (const ChipRow& row : rows) {
            int cx = contentX + labelW;
            m_panelLabels.append(qMakePair(QString::fromUtf8(row.title), cursor + rowH / 2));
            for (const auto& chip : row.chips) {
                QString label = QString::fromUtf8(chip.second);
                int w = qMin(textWidth(font, label) + 2 * chipPadH,maxPanelW-2*padX-labelW);
                if(cx+w+padX>maxPanelW && cx>contentX+labelW){cx=contentX+labelW;cursor+=rowH+rowGap;}
                Item it;
                it.id = chip.first;
                it.label = label;
                it.visible = true;
                it.selected = false;
                it.hover = false;
                it.danger = false;
                it.disabled = false;
                if (chip.first == BtnCodecHevc) it.disabled = !m_hevcAvailable;
                else if (chip.first == BtnCodecHevc444) it.disabled = !m_hevc444Available;
                it.rect = { cx, cursor, w, rowH };
                m_items.append(it);
                cx += w + chipGap;
            }
            if (cx - panelX + padX > panelW) panelW = cx - panelX + padX;
            cursor += rowH + rowGap;
        }

        // Apply / collapse actions.
        const int actionPadH = S(14);
        int ax = contentX + labelW;
        Item apply;
        apply.id = BtnApply;
        apply.label = QStringLiteral("应用并重连");
        apply.visible = true;
        apply.selected = false;
        apply.hover = false;
        apply.danger = false;
        apply.disabled = false;
        apply.rect = { ax, cursor, textWidth(font, apply.label) + 2 * actionPadH, rowH };
        m_items.append(apply);

        Item cancel;
        cancel.id = BtnCancel;
        cancel.label = QStringLiteral("收起");
        cancel.visible = true;
        cancel.selected = false;
        cancel.hover = false;
        cancel.danger = false;
        cancel.disabled = false;
        cancel.rect = { apply.rect.x + apply.rect.w + chipGap, cursor,
                        textWidth(font, cancel.label) + 2 * actionPadH, rowH };
        m_items.append(cancel);

        int endX = cancel.rect.x + cancel.rect.w + padX;
        int endY = cancel.rect.y + cancel.rect.h + padY;
        if (endX > panelW) panelW = endX;
        if (panelW < m_surfaceW) panelW = m_surfaceW;

        int panelH = endY - py;
        m_panelRect = { panelX, py, panelW, panelH };
        m_surfaceW = panelW;
        m_surfaceH = py + panelH;
    }

    auto sel = [&](ButtonId id) {
        if (Item* it = itemById(id)) it->selected = true;
    };
    if (m_qualityOpen) {
        if (m_pendingResKey == "remote") sel(BtnResRemote);
        else if (m_pendingResKey == "local") sel(BtnResLocal);
        else if (m_pendingResKey == "720") sel(BtnRes720);
        else if (m_pendingResKey == "1080") sel(BtnRes1080);
        else if (m_pendingResKey == "1440") sel(BtnRes1440);
        else if (m_pendingResKey == "4K") sel(BtnRes4K);

        if (m_pendingFps == 30) sel(BtnFps30);
        else if (m_pendingFps == 60) sel(BtnFps60);
        else if (m_pendingFps == 120) sel(BtnFps120);

        if (m_pendingBitrateAuto) sel(BtnRateAuto);
        else if (m_pendingBitrateMbps == 10) sel(BtnRate10);
        else if (m_pendingBitrateMbps == 20) sel(BtnRate20);
        else if (m_pendingBitrateMbps == 40) sel(BtnRate40);
        else if (m_pendingBitrateMbps == 80) sel(BtnRate80);

        if (m_pendingCodec == QLatin1String("h264")) sel(BtnCodecH264);
        else if (m_pendingCodec == QLatin1String("hevc") && m_hevcAvailable) sel(BtnCodecHevc);
        else if (m_pendingCodec == QLatin1String("hevc444") && m_hevc444Available) sel(BtnCodecHevc444);
    }
}

// ---------------------------------------------------------------------------
// Per-pixel alpha rasterization (straight-alpha source-over onto ARGB8888).

void FerryToolbar::blendPixel(SDL_Surface* dst, int x, int y, Uint8 r, Uint8 g, Uint8 b, Uint8 a)
{
    if (dst == nullptr || x < 0 || y < 0 || x >= dst->w || y >= dst->h || a == 0) {
        return;
    }
    Uint32* px = (Uint32*)((Uint8*)dst->pixels + y * dst->pitch) + x;
    Uint32 cur = *px;
    Uint8 da = (cur >> 24) & 0xFF;
    Uint8 dr = (cur >> 16) & 0xFF;
    Uint8 dg = (cur >> 8) & 0xFF;
    Uint8 db = cur & 0xFF;
    if (a == 255 || da == 0) {
        *px = ((Uint32)a << 24) | ((Uint32)r << 16) | ((Uint32)g << 8) | (Uint32)b;
        return;
    }
    int sa = a;
    int outA = sa + da * (255 - sa) / 255;
    if (outA == 0) {
        *px = 0;
        return;
    }
    int outR = (r * sa + dr * da * (255 - sa) / 255) / outA;
    int outG = (g * sa + dg * da * (255 - sa) / 255) / outA;
    int outB = (b * sa + db * da * (255 - sa) / 255) / outA;
    *px = ((Uint32)outA << 24) | ((Uint32)outR << 16) | ((Uint32)outG << 8) | (Uint32)outB;
}

// Signed distance to a rounded rectangle, used for anti-aliased fills/strokes.
static float sdRoundRect(float px, float py, float hw, float hh, float rr)
{
    float qx = fabsf(px) - hw;
    float qy = fabsf(py) - hh;
    float ox = qx > 0 ? qx : 0;
    float oy = qy > 0 ? qy : 0;
    float outside = sqrtf(ox * ox + oy * oy);
    float inside = qMin(qMax(qx, qy), 0.0f);
    return outside + inside - rr;
}

void FerryToolbar::fillRoundedRectAA(SDL_Surface* dst, int x0, int y0, int x1, int y1,
                                     int radius, Uint8 r, Uint8 g, Uint8 b, Uint8 a)
{
    if (dst == nullptr) return;
    if (x1 <= x0 || y1 <= y0) return;
    int w = x1 - x0;
    int h = y1 - y0;
    int rr = qMin(radius, qMin(w, h) / 2);
    float cx = (x0 + x1) * 0.5f;
    float cy = (y0 + y1) * 0.5f;
    float hw = w * 0.5f - rr;
    float hh = h * 0.5f - rr;
    for (int y = y0; y < y1; y++) {
        for (int x = x0; x < x1; x++) {
            float dist = sdRoundRect(x - cx + 0.5f, y - cy + 0.5f, hw, hh, (float)rr);
            float cov = 0.5f - dist;
            if (cov <= 0) continue;
            if (cov >= 1) {
                blendPixel(dst, x, y, r, g, b, a);
            }
            else {
                blendPixel(dst, x, y, r, g, b, (Uint8)(a * cov));
            }
        }
    }
}

void FerryToolbar::strokeRoundedRectAA(SDL_Surface* dst, int x0, int y0, int x1, int y1,
                                       int radius, float thickness,
                                       Uint8 r, Uint8 g, Uint8 b, Uint8 a)
{
    if (dst == nullptr) return;
    int w = x1 - x0;
    int h = y1 - y0;
    int rr = qMin(radius, qMin(w, h) / 2);
    float cx = (x0 + x1) * 0.5f;
    float cy = (y0 + y1) * 0.5f;
    float hw = w * 0.5f - rr;
    float hh = h * 0.5f - rr;
    float half = thickness * 0.5f;
    for (int y = y0; y < y1; y++) {
        for (int x = x0; x < x1; x++) {
            float dist = sdRoundRect(x - cx + 0.5f, y - cy + 0.5f, hw, hh, (float)rr);
            float d = fabsf(dist);
            float cov = 0.5f + half - d;
            if (cov <= 0) continue;
            if (cov >= 1) {
                blendPixel(dst, x, y, r, g, b, a);
            }
            else {
                blendPixel(dst, x, y, r, g, b, (Uint8)(a * cov));
            }
        }
    }
}

void FerryToolbar::blitImage(SDL_Surface* dst, const QImage& img, int dx, int dy)
{
    if (dst == nullptr || img.isNull()) return;
    for (int y = 0; y < img.height(); y++) {
        const uchar* row = img.constScanLine(y);
        for (int x = 0; x < img.width(); x++) {
            Uint8 a = row[x * 4 + 3];
            if (a == 0) continue;
            blendPixel(dst, dx + x, dy + y, row[x * 4 + 0], row[x * 4 + 1], row[x * 4 + 2], a);
        }
    }
}

QImage FerryToolbar::loadIcon(const QString& name)
{
    QImage img(QStringLiteral(":/res/toolbar/") + name);
    if (img.isNull()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Ferry toolbar: missing icon %s", qPrintable(name));
        return QImage();
    }
    return img.convertToFormat(QImage::Format_RGBA8888);
}

const QImage& FerryToolbar::iconAsset(const QString& name)
{
    QImage& img = m_iconCache[name];
    if (img.isNull()) {
        img = loadIcon(name);
    }
    return img;
}

void FerryToolbar::blitIcon(SDL_Surface* dst, ButtonId id, const SDL_Rect& r)
{
    int size=qRound(S(TOOLBAR_ICON)*(float)r.w/S(TOOLBAR_CELL));
    if(size<=0)return;
    QImage icon(size,size,QImage::Format_ARGB32_Premultiplied);icon.fill(Qt::transparent);
    QPainter p(&icon);p.setRenderHint(QPainter::Antialiasing);p.setWindow(0,0,26,26);
    QColor color=id==BtnEnd?QColor(229,72,77):(id==BtnAudio&&m_audioOnHost?QColor(149,168,185):QColor(241,246,253));
    p.setPen(QPen(color,1.75,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));p.setBrush(Qt::NoBrush);
    auto line=[&](double x,double y,double xx,double yy){p.drawLine(QPointF(x,y),QPointF(xx,yy));};
    switch(id){
    case BtnDrag:p.setBrush(color);for(int x:{7,13,19})p.drawEllipse(QPointF(x,13),.9,.9);break;
    case BtnFullscreen:
        for(int x:{5,21})for(int y:{5,21}){line(x,y,x+(x==5?5:-5),y);line(x,y,x,y+(y==5?5:-5));}break;
    case BtnPrivacy:{
        p.drawRoundedRect(QRectF(3,4,17,13),2,2);line(11,17,11,21);line(7,21,14,21);
        p.setCompositionMode(QPainter::CompositionMode_Clear);p.fillRect(QRectF(15,10,11,15),Qt::transparent);p.setCompositionMode(QPainter::CompositionMode_SourceOver);
        p.drawRoundedRect(QRectF(16,14,8,8),1.5,1.5);p.drawArc(QRectF(18,10,4,7),0,180*16);line(20,17,20,19);break;
    }
    case BtnQuality:
        for(auto pair:{qMakePair(7,9),qMakePair(13,17),qMakePair(19,11)}){line(4,pair.first,pair.second-2,pair.first);line(pair.second+2,pair.first,22,pair.first);p.drawEllipse(QPointF(pair.second,pair.first),2,2);}break;
    case BtnAudio:{
        QPainterPath path;path.moveTo(3,10);path.lineTo(7,10);path.lineTo(12,6);path.lineTo(12,20);path.lineTo(7,16);path.lineTo(3,16);path.closeSubpath();p.drawPath(path);
        if(m_audioMutedLocal&&!m_audioOnHost)line(4,3,23,23);
        else {p.drawArc(QRectF(10,8,10,10),-55*16,110*16);p.drawArc(QRectF(10,4,16,18),-55*16,110*16);}break;
    }
    case BtnTransfer:{QPainterPath path;path.moveTo(4,17);path.lineTo(4,22);path.lineTo(22,22);path.lineTo(22,17);p.drawPath(path);line(13,17,13,4);line(8,9,13,4);line(13,4,18,9);break;}
    case BtnEnd:line(6,6,20,20);line(20,6,6,20);break;
    default:break;
    }
    p.end();QImage straight=icon.convertToFormat(QImage::Format_RGBA8888);
    blitImage(dst,straight,r.x+(r.w-size)/2,r.y+(r.h-size)/2);
}

void FerryToolbar::blitText(SDL_Surface* dst, const QString& text, SDL_Color color, int x, int y)
{
    TTF_Font* font = (TTF_Font*)m_font;
    if (font == nullptr) {
        return;
    }
    SDL_Surface* s = TTF_RenderUTF8_Blended(font, text.toUtf8().constData(), color);
    if (s == nullptr) {
        return;
    }
    SDL_SetSurfaceBlendMode(s, SDL_BLENDMODE_BLEND);
    SDL_Rect dr = { x, y, s->w, s->h };
    SDL_BlitSurface(s, nullptr, dst, &dr);
    SDL_FreeSurface(s);
}

// ---------------------------------------------------------------------------

QRect FerryToolbar::screenBounds()
{
    SDL_Rect display;int index=SDL_GetWindowDisplayIndex(m_streamWindow);
    if(index>=0&&SDL_GetDisplayBounds(index,&display)==0)return QRect(display.x,display.y,display.w,display.h);
    int x,y,w,h;computeClientRect(x,y,w,h);return QRect(x,y,qMax(1,w),qMax(1,h));
}
QRect FerryToolbar::placementBounds()
{
    int x,y,w,h;computeClientRect(x,y,w,h);QRect bounds(x,y,qMax(1,w),qMax(1,h));
    SDL_Rect display;int index=SDL_GetWindowDisplayIndex(m_streamWindow);
    if(index>=0&&SDL_GetDisplayBounds(index,&display)==0){QRect visible=bounds.intersected(QRect(display.x,display.y,display.w,display.h));if(!visible.isEmpty())bounds=visible;}
    return bounds;
}
void FerryToolbar::placeLayout()
{
    m_layoutBounds=placementBounds();
    auto placement=FerryToolbarLayout::place(m_layoutBounds,QSize(m_barW,m_barH),m_qualityOpen?QSize(m_panelRect.w,m_panelRect.h):QSize(),(int)m_dockEdge,S(6),screenBounds());
    m_canvasX=placement.canvas.x();m_canvasY=placement.canvas.y();m_barX=placement.bar.x()-m_canvasX;m_barY=placement.bar.y()-m_canvasY;
    int panelDX=0,panelDY=0;
    if(m_qualityOpen){panelDX=placement.panel.x()-m_canvasX-m_panelRect.x;panelDY=placement.panel.y()-m_canvasY-m_panelRect.y;}
    for(Item& item:m_items){bool main=item.id<=BtnEnd;item.rect.x+=main?m_barX:panelDX;item.rect.y+=main?m_barY:panelDY;}
    for(SDL_Rect& separator:m_separators){separator.x+=m_barX;separator.y+=m_barY;}
    if(m_qualityOpen){m_panelRect.x+=panelDX;m_panelRect.y+=panelDY;for(auto& label:m_panelLabels)label.second+=panelDY;}
    m_surfaceW=placement.canvas.width();m_surfaceH=placement.canvas.height();
}
float FerryToolbar::selectionAlpha(int id,bool selected)
{
    Uint32 now=SDL_GetTicks();auto& fade=m_selectionFades[id];
    float current=fade.from+(fade.to-fade.from)*FerryToolbarLayout::easeOut((now-fade.tick)/150.0f);
    if(fade.to!=(selected?1.0f:0.0f)){fade.from=current;fade.to=selected?1.0f:0.0f;fade.tick=now;}
    return fade.from+(fade.to-fade.from)*FerryToolbarLayout::easeOut((now-fade.tick)/150.0f);
}
QImage FerryToolbar::debugSnapshot() const
{
    if(!m_surface)return QImage();
    return QImage((uchar*)m_surface->pixels,m_surface->w,m_surface->h,m_surface->pitch,QImage::Format_ARGB32).copy();
}
QJsonObject FerryToolbar::debugMetrics()
{
    return {{"dpiScale",m_dpr},{"barX",m_canvasX+m_barX},{"barY",m_canvasY+m_barY},{"barWidth",m_barW},{"barHeight",m_barH},
        {"panelX",m_canvasX+m_panelRect.x},{"panelY",m_canvasY+m_panelRect.y},{"panelWidth",m_panelRect.w},{"panelHeight",m_panelRect.h},
        {"boundsX",screenBounds().x()},{"boundsY",screenBounds().y()},{"boundsWidth",screenBounds().width()},{"boundsHeight",screenBounds().height()}};
}
void FerryToolbar::debugMotion(unsigned elapsed)
{
    m_pressedId=BtnPrivacy;m_pressTick=SDL_GetTicks()-elapsed;m_revealTick=SDL_GetTicks()-elapsed;render();
}

void FerryToolbar::render()
{
    if (m_window == nullptr) {
        return;
    }

    QHash<int, bool> hoverById;
    for (const Item& item : m_items) {
        if (item.hover) {
            hoverById.insert((int)item.id, true);
        }
    }

    layout();
    placeLayout();

    for (Item& item : m_items) {
        if (hoverById.value((int)item.id, false)) {
            item.hover = true;
        }
    }

    // Tooltip for the disabled (on-host) audio button.
    m_tooltipVisible = false;
    if (Item* a = itemById(BtnAudio)) {
        if (a->hover && a->disabled) {
            m_tooltipVisible = true;
            m_tooltipText = QStringLiteral("声音留在对方电脑，在高性能远控面板里改");
            int tw = textWidth((TTF_Font*)m_font, m_tooltipText);
            int padX = S(8);
            int padY = S(6);
            int w = tw + padX * 2;
            int h = m_fontSize + padY * 2;
            int tx = a->rect.x + (a->rect.w - w) / 2;
            if (tx < 0) tx = 0;
            if (tx + w > m_surfaceW) tx = m_surfaceW - w;
            int ty = m_barH + S(6);
            m_tooltipRect = { tx, ty, w, h };
            if (m_surfaceH < ty + h) m_surfaceH = ty + h;
        }
    }

    SDL_SetWindowSize(m_window, m_surfaceW, m_surfaceH);

    if (m_surface == nullptr || m_surface->w != m_surfaceW || m_surface->h != m_surfaceH) {
        if (m_surface != nullptr) {
            SDL_FreeSurface(m_surface);
        }
        m_surface = SDL_CreateRGBSurfaceWithFormat(0, m_surfaceW, m_surfaceH, 32, SDL_PIXELFORMAT_ARGB8888);
    }
    if (m_surface == nullptr) {
        return;
    }

    // Clear to fully transparent.
    SDL_FillRect(m_surface, nullptr, SDL_MapRGBA(m_surface->format, 0, 0, 0, 0));

    // Capsule background + subtle border (anti-aliased, translucent).
    const int capsuleRadius = qMin(m_barW, m_barH) / 2;
    fillRoundedRectAA(m_surface, m_barX, m_barY, m_barX+m_barW, m_barY+m_barH, capsuleRadius,
                      C_BAR_BG.r, C_BAR_BG.g, C_BAR_BG.b, m_expanded?C_BAR_BG.a:(m_handleHover?180:92));
    strokeRoundedRectAA(m_surface, m_barX, m_barY, m_barX+m_barW, m_barY+m_barH, capsuleRadius, 1.0f,
                        C_BAR_BORDER.r, C_BAR_BORDER.g, C_BAR_BORDER.b, C_BAR_BORDER.a);

    // Quality panel card.
    if (m_qualityOpen && m_panelRect.w > 0 && m_panelRect.h > 0) {
        fillRoundedRectAA(m_surface, m_panelRect.x, m_panelRect.y,
                          m_panelRect.x + m_panelRect.w, m_panelRect.y + m_panelRect.h,
                          S(12), C_PANEL_BG.r, C_PANEL_BG.g, C_PANEL_BG.b, C_PANEL_BG.a);
        strokeRoundedRectAA(m_surface, m_panelRect.x, m_panelRect.y,
                            m_panelRect.x + m_panelRect.w, m_panelRect.y + m_panelRect.h,
                            S(12), 1.0f,
                            C_PANEL_BORDER.r, C_PANEL_BORDER.g, C_PANEL_BORDER.b, C_PANEL_BORDER.a);
        for (const auto& label : m_panelLabels) {
            blitText(m_surface, label.first, C_MUTED, m_panelRect.x + S(12), label.second - m_fontSize / 2);
        }
    }

    // Divider lines.
    for (const SDL_Rect& sr : m_separators) {
        SDL_FillRect(m_surface, &sr, SDL_MapRGBA(m_surface->format, C_MUTED.r, C_MUTED.g, C_MUTED.b, 90));
    }

    // Buttons: highlight + icon (main bar) or chip (panel).
    for (const Item& item : m_items) {
        if (!item.visible) {
            continue;
        }

        const bool isChip = (item.id == BtnResRemote || item.id == BtnResLocal ||
                             item.id == BtnRes720 || item.id == BtnRes1080 ||
                             item.id == BtnRes1440 || item.id == BtnRes4K ||
                             item.id == BtnFps30 || item.id == BtnFps60 || item.id == BtnFps120 ||
                             item.id == BtnRateAuto || item.id == BtnRate10 ||
                             item.id == BtnRate20 || item.id == BtnRate40 || item.id == BtnRate80 ||
                             item.id == BtnCodecH264 || item.id == BtnCodecHevc || item.id == BtnCodecHevc444 ||
                             item.id == BtnApply || item.id == BtnCancel);

        if (isChip) {
            // Panel chip: solid highlight block when selected.
            SDL_Color textColor = C_ICON;
            if (item.disabled) {
                textColor = C_MUTED;
            }
            else if (item.selected) {
                fillRoundedRectAA(m_surface, item.rect.x, item.rect.y,
                                  item.rect.x + item.rect.w, item.rect.y + item.rect.h,
                                  S(8), C_ACCENT.r, C_ACCENT.g, C_ACCENT.b, C_ACCENT.a);
                textColor = C_ICON;
            }
            else if (item.hover) {
                fillRoundedRectAA(m_surface, item.rect.x, item.rect.y,
                                  item.rect.x + item.rect.w, item.rect.y + item.rect.h,
                                  S(8), C_HOVER.r, C_HOVER.g, C_HOVER.b, C_HOVER.a);
            }
            else if (item.id == BtnApply) {
                fillRoundedRectAA(m_surface, item.rect.x, item.rect.y,
                                  item.rect.x + item.rect.w, item.rect.y + item.rect.h,
                                  S(8), C_ACCENT.r, C_ACCENT.g, C_ACCENT.b, C_ACCENT.a);
            }
            else {
                fillRoundedRectAA(m_surface, item.rect.x, item.rect.y,
                                  item.rect.x + item.rect.w, item.rect.y + item.rect.h,
                                  S(8), C_CHIP_BG.r, C_CHIP_BG.g, C_CHIP_BG.b, C_CHIP_BG.a);
            }

            int tw = textWidth((TTF_Font*)m_font, item.label);
            int tx = item.rect.x + (item.rect.w - tw) / 2;
            int ty = item.rect.y + (item.rect.h - m_fontSize - 2) / 2;
            blitText(m_surface, item.label, textColor, tx, ty);
        }
        else {
            SDL_Rect r=item.rect;
            float scale=(m_pressedId==(int)item.id)?FerryToolbarLayout::pressScale(SDL_GetTicks()-m_pressTick):1.0f;
            int nw=qRound(r.w*scale),nh=qRound(r.h*scale);r.x+=(r.w-nw)/2;r.y+=(r.h-nh)/2;r.w=nw;r.h=nh;
            if(!m_expanded){
                int gw=isHorizontal()?S(32):S(3),gh=isHorizontal()?S(3):S(32);
                fillRoundedRectAA(m_surface,r.x+(r.w-gw)/2,r.y+(r.h-gh)/2,r.x+(r.w+gw)/2,r.y+(r.h+gh)/2,S(2),241,246,253,m_handleHover?255:190);
            }else{
                float selected=selectionAlpha((int)item.id,item.selected);
                if(selected>0.001f)fillRoundedRectAA(m_surface,r.x+S(3),r.y+S(3),r.x+r.w-S(3),r.y+r.h-S(3),qMin(r.w,r.h)/2,110,177,240,(Uint8)(selected*66));
                if(item.hover&&!item.disabled)fillRoundedRectAA(m_surface,r.x+S(3),r.y+S(3),r.x+r.w-S(3),r.y+r.h-S(3),qMin(r.w,r.h)/2,255,255,255,24);
                blitIcon(m_surface,item.id,r);
            }
        }
    }

    // Transfer progress bar adjacent to the transfer button.
    m_showProgress = false;
    if (m_transferActive ||
        (m_transferDoneTick != 0 && (SDL_GetTicks() - m_transferDoneTick) < TOOLBAR_DONE_FLASH_MS)) {
        if (Item* transfer = itemById(BtnTransfer)) {
            m_showProgress = true;
            m_progressVertical = !isHorizontal();
            const bool flashDone = !m_transferActive;

            if (m_progressVertical) {
                m_progressTrack = { transfer->rect.x + (transfer->rect.w - S(TOOLBAR_PROGRESS_THICK)) / 2,
                                    transfer->rect.y + transfer->rect.h + S(4),
                                    S(TOOLBAR_PROGRESS_THICK), S(TOOLBAR_PROGRESS_H) };
            }
            else {
                m_progressTrack = { transfer->rect.x + transfer->rect.w + S(4),
                                    transfer->rect.y + (transfer->rect.h - S(TOOLBAR_PROGRESS_THICK)) / 2,
                                    S(TOOLBAR_PROGRESS_W), S(TOOLBAR_PROGRESS_THICK) };
            }

            fillRoundedRectAA(m_surface, m_progressTrack.x, m_progressTrack.y,
                              m_progressTrack.x + m_progressTrack.w, m_progressTrack.y + m_progressTrack.h,
                              S(TOOLBAR_PROGRESS_THICK) / 2,
                              C_TRACK.r, C_TRACK.g, C_TRACK.b, C_TRACK.a);

            int percent = flashDone ? 100 : m_transferPercent;
            if (m_progressVertical) {
                int filled = m_progressTrack.h * percent / 100;
                SDL_Rect fill = { m_progressTrack.x,
                                  m_progressTrack.y + (m_progressTrack.h - filled),
                                  m_progressTrack.w, filled };
                m_progressFill = fill;
                fillRoundedRectAA(m_surface, fill.x, fill.y, fill.x + fill.w, fill.y + fill.h,
                                  S(TOOLBAR_PROGRESS_THICK) / 2,
                                  C_FILL.r, C_FILL.g, C_FILL.b, C_FILL.a);
            }
            else {
                int filled = m_progressTrack.w * percent / 100;
                SDL_Rect fill = { m_progressTrack.x, m_progressTrack.y, filled, m_progressTrack.h };
                m_progressFill = fill;
                fillRoundedRectAA(m_surface, fill.x, fill.y, fill.x + fill.w, fill.y + fill.h,
                                  S(TOOLBAR_PROGRESS_THICK) / 2,
                                  C_FILL.r, C_FILL.g, C_FILL.b, C_FILL.a);
            }

            if (!flashDone) {
                QString pct = QString::number(m_transferPercent) + "%";
                int tw = textWidth((TTF_Font*)m_font, pct);
                if (m_progressVertical) {
                    blitText(m_surface, pct, C_ICON, m_progressTrack.x + (m_progressTrack.w - tw) / 2,
                             m_progressTrack.y - m_fontSize - S(4));
                }
                else {
                    blitText(m_surface, pct, C_ICON, m_progressTrack.x + (m_progressTrack.w - tw) / 2,
                             m_progressTrack.y + m_progressTrack.h + S(2));
                }
            }
        }
    }

    // Tooltip.
    if (m_tooltipVisible && m_tooltipRect.w > 0) {
        fillRoundedRectAA(m_surface, m_tooltipRect.x, m_tooltipRect.y,
                          m_tooltipRect.x + m_tooltipRect.w, m_tooltipRect.y + m_tooltipRect.h,
                          S(6), C_TOOLTIP_BG.r, C_TOOLTIP_BG.g, C_TOOLTIP_BG.b, C_TOOLTIP_BG.a);
        strokeRoundedRectAA(m_surface, m_tooltipRect.x, m_tooltipRect.y,
                            m_tooltipRect.x + m_tooltipRect.w, m_tooltipRect.y + m_tooltipRect.h,
                            S(6), 1.0f, C_PANEL_BORDER.r, C_PANEL_BORDER.g, C_PANEL_BORDER.b, C_PANEL_BORDER.a);
        blitText(m_surface, m_tooltipText, C_ICON,
                 m_tooltipRect.x + S(8), m_tooltipRect.y + (m_tooltipRect.h - m_fontSize) / 2);
    }

    // Transient status line.
    if (!m_status.isEmpty() && (SDL_GetTicks() - m_statusTick) < TOOLBAR_STATUS_MS) {
        int sx = S(TOOLBAR_MARGIN) + S(8);
        int sy = S(TOOLBAR_MARGIN) + S(TOOLBAR_CELL) + S(4);
        blitText(m_surface, m_status, C_MUTED, sx, sy);
    }
    else {
        m_status.clear();
    }

    present();

    follow();
}
void FerryToolbar::present()
{
    if(m_offscreen)return;
#ifdef Q_OS_WIN32
    if (m_layered && m_hwnd != nullptr && m_surface != nullptr && m_surfaceW > 0 && m_surfaceH > 0) {
        const int w = m_surface->w;
        const int h = m_surface->h;
        QByteArray bits(w * h * 4, 0);
        Uint32* src = (Uint32*)m_surface->pixels;
        const int pitch = m_surface->pitch;
        uchar* dst = (uchar*)bits.data();
        for (int y = 0; y < h; y++) {
            Uint32* row = (Uint32*)((Uint8*)src + y * pitch);
            uchar* drow = dst + y * w * 4;
            for (int x = 0; x < w; x++) {
                Uint32 px = row[x];
                Uint8 a = (px >> 24) & 0xFF;
                Uint8 r = (px >> 16) & 0xFF;
                Uint8 g = (px >> 8) & 0xFF;
                Uint8 b = px & 0xFF;
                drow[x * 4 + 0] = (b * a) / 255;
                drow[x * 4 + 1] = (g * a) / 255;
                drow[x * 4 + 2] = (r * a) / 255;
                drow[x * 4 + 3] = a;
            }
        }

        BITMAPINFO bi;
        ZeroMemory(&bi, sizeof(bi));
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = w;
        bi.bmiHeader.biHeight = -h; // top-down DIB
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;

        HDC screenDC = GetDC(nullptr);
        HDC memDC = CreateCompatibleDC(screenDC);
        void* pvBits = nullptr;
        HBITMAP hbm = CreateDIBSection(screenDC, &bi, DIB_RGB_COLORS, &pvBits, nullptr, 0);
        if (hbm != nullptr && pvBits != nullptr) {
            memcpy(pvBits, bits.constData(), (size_t)w * h * 4);
            HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, hbm);

            int wx, wy;
            SDL_GetWindowPosition(m_window, &wx, &wy);
            POINT ptDst = { wx, wy };
            SIZE size = { w, h };
            POINT ptSrc = { 0, 0 };
            BLENDFUNCTION blend;
            blend.BlendOp = AC_SRC_OVER;
            blend.BlendFlags = 0;
            blend.SourceConstantAlpha = (BYTE)(255*(m_expanded&&m_revealTick?FerryToolbarLayout::easeOut((SDL_GetTicks()-m_revealTick)/200.0f):1.0f));
            blend.AlphaFormat = AC_SRC_ALPHA;

            UpdateLayeredWindow((HWND)m_hwnd, screenDC, &ptDst, &size, memDC, &ptSrc, 0, &blend, ULW_ALPHA);

            SelectObject(memDC, oldBmp);
            DeleteObject(hbm);
        }
        DeleteDC(memDC);
        ReleaseDC(nullptr, screenDC);
        return;
    }
#endif
    // Fallback (non-Windows): copy the offscreen surface to the window surface.
    if (m_window != nullptr && m_surface != nullptr) {
        SDL_Surface* ws = SDL_GetWindowSurface(m_window);
        if (ws != nullptr) {
            SDL_BlitSurface(m_surface, nullptr, ws, nullptr);
            SDL_UpdateWindowSurface(m_window);
        }
    }
}

void FerryToolbar::computeClientRect(int& cx, int& cy, int& cw, int& ch)
{
    cx = 0;
    cy = 0;
    cw = 0;
    ch = 0;

    if (m_streamWindow == nullptr) {
        return;
    }

    SDL_GetWindowPosition(m_streamWindow, &cx, &cy);
    SDL_GetWindowSize(m_streamWindow, &cw, &ch);

#ifdef Q_OS_WIN32
    if (m_streamHwnd != nullptr) {
        RECT rc;
        if (GetClientRect((HWND)m_streamHwnd, &rc)) {
            POINT pt;
            pt.x = rc.left;
            pt.y = rc.top;
            if (ClientToScreen((HWND)m_streamHwnd, &pt)) {
                cx = pt.x;
                cy = pt.y;
                cw = rc.right - rc.left;
                ch = rc.bottom - rc.top;
            }
        }
    }
#endif
}

void FerryToolbar::reposition(int cx, int cy, int cw, int ch)
{
    if (m_window == nullptr || m_streamWindow == nullptr) {
        return;
    }

    (void)cx;(void)cy;(void)cw;(void)ch;
    if(placementBounds()!=m_layoutBounds){render();return;}
    int tx=m_canvasX,ty=m_canvasY;
    if(m_expanded&&m_revealTick){float t=FerryToolbarLayout::easeOut((SDL_GetTicks()-m_revealTick)/200.0f);if(m_dockEdge==DockTop)ty-=qRound(S(10)*(1-t));}

    if (tx != m_lastTx || ty != m_lastTy) {
        SDL_SetWindowPosition(m_window, tx, ty);
        m_lastTx = tx;
        m_lastTy = ty;
        // The layered snapshot is positioned independently of the HWND, so
        // re-present it at the new position whenever the window moves.
        present();
    }
}

void FerryToolbar::follow()
{
    if (m_window == nullptr || m_streamWindow == nullptr) {
        return;
    }
    if (m_dragging) {
        return;
    }
    int cx, cy, cw, ch;
    computeClientRect(cx, cy, cw, ch);
    reposition(cx, cy, cw, ch);
}

void FerryToolbar::followTo(int wx, int wy, int ww)
{
    (void)wx;
    (void)wy;
    if (m_window == nullptr || m_streamWindow == nullptr) {
        return;
    }
    if (m_dragging) {
        return;
    }
    int cx, cy, cw, ch;
    computeClientRect(cx, cy, cw, ch);
    if (cw <= 0 || ch <= 0) {
        cw = ww;
    }
    reposition(cx, cy, cw, ch);
}

void FerryToolbar::dockToNearestEdge()
{
    int cx, cy, cw, ch;
    computeClientRect(cx, cy, cw, ch);
    if (cw <= 0 || ch <= 0) {
        return;
    }

    int wx, wy;
    SDL_GetWindowPosition(m_window, &wx, &wy);
    int tx = wx + m_barX + m_barW / 2;
    int ty = wy + m_barY + m_barH / 2;

    int dTop = ty - cy;
    int dBottom = (cy + ch) - ty;
    int dLeft = tx - cx;
    int dRight = (cx + cw) - tx;
    int dMin = qMin(qMin(dTop, dBottom), qMin(dLeft, dRight));

    if (dMin == dTop) m_dockEdge = DockTop;
    else if (dMin == dBottom) m_dockEdge = DockBottom;
    else if (dMin == dLeft) m_dockEdge = DockLeft;
    else m_dockEdge = DockRight;

    render();
    reposition(cx, cy, cw, ch);
}

#ifdef Q_OS_WIN32
void SDLCALL FerryToolbar::winMessageHook(void* userdata, void* hWnd,
                                          unsigned int message, Uint64 wParam, Sint64 lParam)
{
    (void)wParam;
    FerryToolbar* self = static_cast<FerryToolbar*>(userdata);
    if (self == nullptr || hWnd == nullptr || self->m_streamHwnd == nullptr) {
        return;
    }
    if (hWnd != self->m_streamHwnd) {
        return;
    }

    if (message == WM_WINDOWPOSCHANGED) {
        const WINDOWPOS* wp = reinterpret_cast<const WINDOWPOS*>(static_cast<LPARAM>(lParam));
        self->followTo(wp->x, wp->y, wp->cx);
    }
    else if (message == WM_MOVING) {
        const RECT* r = reinterpret_cast<const RECT*>(static_cast<LPARAM>(lParam));
        self->followTo(r->left, r->top, r->right - r->left);
    }
}
#endif

void FerryToolbar::tick()
{
    if (m_window == nullptr || m_streamWindow == nullptr) {
        return;
    }

    FerryBridge::readMessages();

    if (m_transferActive && (SDL_GetTicks() - m_lastTransferTick) > TOOLBAR_TRANSFER_TIMEOUT) {
        m_transferActive = false;
        m_transferPercent = 0;
        render();
    }

    int mouseX, mouseY;
    Uint32 buttons = SDL_GetGlobalMouseState(&mouseX, &mouseY);

    int wx, wy, ww, wh;
    SDL_GetWindowPosition(m_window, &wx, &wy);
    SDL_GetWindowSize(m_window, &ww, &wh);

    int cx, cy, cw, ch;
    computeClientRect(cx, cy, cw, ch);

    Uint32 now = SDL_GetTicks();

    if (m_dragging) {
        if (!(buttons & SDL_BUTTON(SDL_BUTTON_LEFT))) {
            m_dragging = false;
            SDL_CaptureMouse(SDL_FALSE);
            dockToNearestEdge();
            return;
        }
        int nx = mouseX - m_dragGrabX;
        int ny = mouseY - m_dragGrabY;
        if (nx != m_lastTx || ny != m_lastTy) {
            SDL_SetWindowPosition(m_window, nx, ny);
            m_lastTx = nx;
            m_lastTy = ny;
            present(); // keep the layered snapshot following the pointer
        }
        return;
    }

    bool overToolbar = (mouseX >= wx && mouseX < wx + ww && mouseY >= wy && mouseY < wy + wh);

    bool overRevealZone = false;
    if (m_fullscreen && !m_expanded) {
        const int zone = S(14);
        switch (m_dockEdge) {
        case DockTop:    overRevealZone = (mouseY <= cy + zone); break;
        case DockBottom: overRevealZone = (mouseY >= cy + ch - zone); break;
        case DockLeft:   overRevealZone = (mouseX <= cx + zone); break;
        case DockRight:  overRevealZone = (mouseX >= cx + cw - zone); break;
        }
    }

    bool nearHandle=!m_expanded && mouseX>=wx-S(16)&&mouseX<wx+ww+S(16)&&mouseY>=wy-S(12)&&mouseY<wy+wh+S(12);
    if(nearHandle!=m_handleHover){m_handleHover=nearHandle;render();}
    bool shouldExpand = overToolbar || overRevealZone;

    if (shouldExpand) {
        m_lastActivityTick = now;
        if (!m_expanded) {
            m_expanded = true;
            m_revealTick=now;
            render();
            follow();
        }
    }
    else {
        Uint32 hideMs = m_fullscreen ? TOOLBAR_FS_HIDE_MS : TOOLBAR_WIN_HIDE_MS;
        if (m_expanded && (now - m_lastActivityTick) > hideMs) {
            m_expanded = false;
            render();
            follow();
        }
    }

    bool animating=(m_pressTick && now-m_pressTick<120)||(m_revealTick&&now-m_revealTick<200);
    for(auto it=m_selectionFades.cbegin();it!=m_selectionFades.cend();++it)animating|=(now-it.value().tick<150);
    if(animating)render();
    follow();
}

bool FerryToolbar::handleMouseButton(const SDL_MouseButtonEvent* ev)
{
    if (m_window == nullptr) {
        return false;
    }

    if (ev->type == SDL_MOUSEBUTTONDOWN && ev->button == SDL_BUTTON_LEFT) {
        Item* item = itemAt(ev->x, ev->y);
        if (item != nullptr && !item->disabled) {
            item->hover = true;
            m_pressedId=(int)item->id;m_pressTick=SDL_GetTicks();
            if (item->id == BtnDrag) {
                int gx, gy;
                SDL_GetGlobalMouseState(&gx, &gy);
                int wx, wy;
                SDL_GetWindowPosition(m_window, &wx, &wy);
                m_dragging = true;
                m_dragGrabX = gx - wx;
                m_dragGrabY = gy - wy;
                SDL_CaptureMouse(SDL_TRUE);
            }
            else { render(); }
        }
    }
    else if (ev->type == SDL_MOUSEBUTTONUP && ev->button == SDL_BUTTON_LEFT) {
        if (m_dragging) {
            m_dragging = false;
            SDL_CaptureMouse(SDL_FALSE);
            dockToNearestEdge();
        }else{
            Item* hit=itemAt(ev->x,ev->y);
            if(hit&&!hit->disabled&&(int)hit->id==m_pressedId)onButton(hit->id);
        }
    }

    return true;
}

bool FerryToolbar::handleMouseMotion(const SDL_MouseMotionEvent* ev)
{
    if (m_window == nullptr) {
        return false;
    }

    if (m_dragging) {
        return true;
    }

    Item* hit = itemAt(ev->x, ev->y);
    bool changed = false;
    for (Item& item : m_items) {
        bool hov = (&item == hit) && !item.disabled;
        if (item.hover != hov) changed = true;
        item.hover = hov;
    }
    if (changed) {
        render();
    }
    return true;
}

void FerryToolbar::onButton(ButtonId id)
{
    switch (id) {
    case BtnDrag:
        break;

    case BtnFullscreen:
        if (m_session != nullptr) {
            m_session->toggleFullscreen();
        }
        break;

    case BtnPrivacy:
        m_privacy = !m_privacy;
        FerryBridge::sendBlank(m_privacy);
        render();
        break;

    case BtnQuality:
        m_qualityOpen = !m_qualityOpen;
        m_pendingResKey = m_resKey;
        m_pendingFps = m_fps;
        m_pendingBitrateMbps = m_bitrateMbps;
        m_pendingBitrateAuto = m_bitrateAuto;
        m_pendingCodec = m_codec;
        render();
        break;

    case BtnAudio:
        // Disabled while the sound stays on the host (audioState onHost=true).
        // Otherwise toggle local mute only — no reconnect.
        if (m_audioOnHost) {
            break;
        }
        m_audioMutedLocal = !m_audioMutedLocal;
        if (m_session != nullptr) {
            m_session->setAudioMuted(m_audioMutedLocal);
        }
        render();
        break;

    case BtnTransfer:
        break;

    case BtnEnd:
        FerryBridge::sendBlank(false);
        FerryBridge::sendQuit();
        {
            SDL_Event event;
            event.type = SDL_QUIT;
            event.quit.timestamp = SDL_GetTicks();
            SDL_PushEvent(&event);
        }
        break;

    case BtnResRemote:  m_pendingResKey = "remote"; render(); break;
    case BtnResLocal:   m_pendingResKey = "local"; render(); break;
    case BtnRes720:     m_pendingResKey = "720"; render(); break;
    case BtnRes1080:    m_pendingResKey = "1080"; render(); break;
    case BtnRes1440:    m_pendingResKey = "1440"; render(); break;
    case BtnRes4K:      m_pendingResKey = "4K"; render(); break;

    case BtnFps30:  m_pendingFps = 30; render(); break;
    case BtnFps60:  m_pendingFps = 60; render(); break;
    case BtnFps120: m_pendingFps = 120; render(); break;

    case BtnRateAuto:
        m_pendingBitrateAuto = true;
        render();
        break;
    case BtnRate10: m_pendingBitrateAuto = false; m_pendingBitrateMbps = 10; render(); break;
    case BtnRate20: m_pendingBitrateAuto = false; m_pendingBitrateMbps = 20; render(); break;
    case BtnRate40: m_pendingBitrateAuto = false; m_pendingBitrateMbps = 40; render(); break;
    case BtnRate80: m_pendingBitrateAuto = false; m_pendingBitrateMbps = 80; render(); break;

    case BtnCodecH264:
        m_pendingCodec = "h264";
        render();
        break;
    case BtnCodecHevc:
        if (!m_hevcAvailable) break;
        m_pendingCodec = "hevc";
        render();
        break;
    case BtnCodecHevc444:
        if (!m_hevc444Available) break;
        m_pendingCodec = "hevc444";
        render();
        break;

    case BtnApply:
        m_resKey = m_pendingResKey;
        m_fps = m_pendingFps;
        m_bitrateMbps = m_pendingBitrateMbps;
        m_bitrateAuto = m_pendingBitrateAuto;
        m_codec = m_pendingCodec;
        m_qualityOpen = false;
        render();
        sendReconnectCurrent();
        break;

    case BtnCancel:
        m_qualityOpen = false;
        m_pendingResKey = m_resKey;
        m_pendingFps = m_fps;
        m_pendingBitrateMbps = m_bitrateMbps;
        m_pendingBitrateAuto = m_bitrateAuto;
        m_pendingCodec = m_codec;
        render();
        break;

    default:
        break;
    }
}

void FerryToolbar::sendReconnectCurrent()
{
    FerryBridge::sendReconnect(m_resKey, m_fps, m_bitrateMbps, m_bitrateAuto, m_codec);
}
