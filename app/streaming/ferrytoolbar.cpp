#include "ferrytoolbar.h"

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

#define TOOLBAR_FONT_SIZE 13
#define TOOLBAR_BAR_HEIGHT 34
#define TOOLBAR_PAD_X 10
#define TOOLBAR_PAD_Y 5
#define TOOLBAR_GAP 4
#define TOOLBAR_MARGIN 6
#define TOOLBAR_STATUS_MS 6000
#define TOOLBAR_FS_HIDE_MS 1500

static SDL_Color C_BAR_BG   = { 20, 28, 40, 255 };
static SDL_Color C_BTN      = { 40, 52, 70, 255 };
static SDL_Color C_BTN_HOV  = { 55, 70, 92, 255 };
static SDL_Color C_ACCENT   = { 37, 102, 179, 255 };
static SDL_Color C_ACCENT_H = { 61, 131, 210, 255 };
static SDL_Color C_DANGER   = { 186, 62, 52, 255 };
static SDL_Color C_DANGER_H = { 228, 106, 96, 255 };
static SDL_Color C_TEXT     = { 241, 246, 253, 255 };
static SDL_Color C_MUTED    = { 149, 168, 185, 255 };

FerryToolbar::FerryToolbar(Session* session)
    : m_session(session),
      m_streamWindow(nullptr),
      m_window(nullptr),
      m_windowId(0),
      m_font(nullptr),
      m_fontSize(TOOLBAR_FONT_SIZE),
      m_barHeight(TOOLBAR_BAR_HEIGHT),
      m_surfaceW(0),
      m_surfaceH(0),
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
      m_visible(false),
      m_lastTopTick(0)
{
#ifdef Q_OS_WIN32
    m_streamHwnd = nullptr;
#endif
}

FerryToolbar::~FerryToolbar()
{
    destroy();
}

static TTF_Font* loadToolbarFont(int size)
{
    // Prefer a CJK-capable system font so the Chinese labels render.
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

    // Fall back to Moonlight's bundled ModeSeven font (Latin only).
    QByteArray fontData = Path::readDataFile("ModeSeven.ttf");
    if (!fontData.isEmpty()) {
        return TTF_OpenFontRW(SDL_RWFromConstMem(fontData.constData(), fontData.size()), 1, size);
    }
    return nullptr;
}

bool FerryToolbar::init(SDL_Window* streamWindow)
{
    m_streamWindow = streamWindow;

    // NB: SDL_ttf is already initialized by the Session's OverlayManager.
    m_font = loadToolbarFont(m_fontSize);
    if (m_font == nullptr) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Ferry toolbar: no font available");
    }

    m_window = SDL_CreateWindow("FerryRemoteToolbar",
                                0, 0,
                                400, TOOLBAR_BAR_HEIGHT + TOOLBAR_MARGIN * 2,
                                SDL_WINDOW_BORDERLESS | SDL_WINDOW_ALWAYS_ON_TOP | SDL_WINDOW_SKIP_TASKBAR);
    if (m_window == nullptr) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Ferry toolbar window creation failed: %s", SDL_GetError());
        return false;
    }
    m_windowId = SDL_GetWindowID(m_window);

#ifdef Q_OS_WIN32
    // Never activate the toolbar on click so keyboard focus stays with the stream window.
    SDL_SysWMinfo info;
    SDL_VERSION(&info.version);
    if (SDL_GetWindowWMInfo(m_window, &info) && info.subsystem == SDL_SYSWM_WINDOWS) {
        LONG_PTR ex = GetWindowLongPtrW(info.info.win.window, GWL_EXSTYLE);
        ex |= WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW;
        SetWindowLongPtrW(info.info.win.window, GWL_EXSTYLE, ex);
    }

    // Obtain the stream window's native handle and install a message hook so
    // the toolbar keeps following while the window is dragged. During a drag
    // Windows runs a modal move loop, so the SDL event loop never reaches
    // tick()/follow(); the hook reparents on WM_WINDOWPOSCHANGED/WM_MOVING.
    m_streamHwnd = nullptr;
    SDL_SysWMinfo streamInfo;
    SDL_VERSION(&streamInfo.version);
    if (SDL_GetWindowWMInfo(m_streamWindow, &streamInfo) && streamInfo.subsystem == SDL_SYSWM_WINDOWS) {
        m_streamHwnd = streamInfo.info.win.window;
    }
    SDL_SetWindowsMessageHook(winMessageHook, this);
#endif

    return true;
}

void FerryToolbar::destroy()
{
#ifdef Q_OS_WIN32
    if (m_streamHwnd != nullptr) {
        SDL_SetWindowsMessageHook(nullptr, nullptr);
        m_streamHwnd = nullptr;
    }
#endif
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
    syncLabels();
    render();
}

void FerryToolbar::setFocused(bool focused)
{
    m_focused = focused;
}

void FerryToolbar::setPrivacy(bool on)
{
    m_privacy = on;
    syncLabels();
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

void FerryToolbar::syncLabels()
{
    if (Item* it = itemById(BtnFullscreen)) {
        it->label = m_fullscreen ? QStringLiteral("退出全屏") : QStringLiteral("全屏");
    }
    if (Item* it = itemById(BtnPrivacy)) {
        it->label = m_privacy ? QStringLiteral("恢复本机显示") : QStringLiteral("黑屏隐私");
    }
    if (Item* it = itemById(BtnQuality)) {
        it->label = m_qualityOpen ? QStringLiteral("清晰度 ▴") : QStringLiteral("清晰度 ▾");
    }
}

static QString resLabel(const QString& key)
{
    if (key == "remote") return QStringLiteral("对方屏幕");
    if (key == "local")  return QStringLiteral("本机屏幕");
    if (key == "720")    return QStringLiteral("720p");
    if (key == "1080")   return QStringLiteral("1080p");
    if (key == "1440")   return QStringLiteral("1440p");
    if (key == "4K")     return QStringLiteral("4K");
    return key;
}

void FerryToolbar::layout()
{
    TTF_Font* font = (TTF_Font*)m_font;

    m_items.clear();

    QString peer = FerryBridge::peerName;
    if (peer.isEmpty()) {
        peer = QStringLiteral("Ferry");
    }
    if (peer.length() > 14) {
        peer = peer.left(14) + QStringLiteral("…");
    }
    int peerNameW = textWidth(font, peer);

    int btnY = TOOLBAR_MARGIN + TOOLBAR_PAD_Y;
    int btnH = m_barHeight - TOOLBAR_PAD_Y * 2;
    int x = TOOLBAR_MARGIN + 6 + peerNameW + 10;

    QVector<QPair<ButtonId, bool>> mainButtons; // (id, danger)
    mainButtons.append(qMakePair(BtnFullscreen, false));
    mainButtons.append(qMakePair(BtnPrivacy, false));
    mainButtons.append(qMakePair(BtnQuality, false));
    mainButtons.append(qMakePair(BtnEnd, true));

    for (const auto& mb : mainButtons) {
        QString label;
        switch (mb.first) {
        case BtnFullscreen: label = m_fullscreen ? QStringLiteral("退出全屏") : QStringLiteral("全屏"); break;
        case BtnPrivacy:    label = m_privacy ? QStringLiteral("恢复本机显示") : QStringLiteral("黑屏隐私"); break;
        case BtnQuality:    label = m_qualityOpen ? QStringLiteral("清晰度 ▴") : QStringLiteral("清晰度 ▾"); break;
        case BtnEnd:        label = QStringLiteral("结束远程"); break;
        default: label = QString(); break;
        }
        int w = textWidth(font, label) + TOOLBAR_PAD_X * 2;
        Item it;
        it.id = mb.first;
        it.label = label;
        it.visible = true;
        it.selected = false;
        it.hover = false;
        it.danger = mb.second;
        it.rect = { x, btnY, w, btnH };
        m_items.append(it);
        x += w + TOOLBAR_GAP;
    }

    int totalW = x + TOOLBAR_MARGIN;
    int totalH = TOOLBAR_MARGIN + m_barHeight + TOOLBAR_MARGIN;

    if (!m_status.isEmpty() && SDL_GetTicks() - m_statusTick < TOOLBAR_STATUS_MS) {
        int sw = textWidth(font, m_status);
        totalH += 22;
        if (sw + 24 > totalW) totalW = sw + 24;
    }
    else {
        m_status.clear();
    }

    if (m_qualityOpen) {
        int px = TOOLBAR_MARGIN + 8;
        int py = TOOLBAR_MARGIN + m_barHeight + 6;
        int rowH = 26;
        int titleW = 44;

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

        for (const ChipRow& row : rows) {
            int cx = px + titleW + 6;
            for (const auto& chip : row.chips) {
                QString label = QString::fromUtf8(chip.second);
                int w = textWidth(font, label) + TOOLBAR_PAD_X * 2;
                Item it;
                it.id = chip.first;
                it.label = label;
                it.visible = true;
                it.selected = false;
                it.hover = false;
                it.danger = false;
                it.rect = { cx, py, w, rowH };
                m_items.append(it);
                cx += w + TOOLBAR_GAP;
            }
            if (cx > totalW) totalW = cx + TOOLBAR_MARGIN;
            py += rowH + 4;
        }

        int ax = px + titleW + 6;
        Item apply;
        apply.id = BtnApply;
        apply.label = QStringLiteral("应用并重连");
        apply.visible = true;
        apply.selected = false;
        apply.hover = false;
        apply.danger = false;
        apply.rect = { ax, py, textWidth(font, apply.label) + TOOLBAR_PAD_X * 2, rowH };
        m_items.append(apply);
        ax += apply.rect.w + TOOLBAR_GAP;

        Item cancel;
        cancel.id = BtnCancel;
        cancel.label = QStringLiteral("收起");
        cancel.visible = true;
        cancel.selected = false;
        cancel.hover = false;
        cancel.danger = false;
        cancel.rect = { ax, py, textWidth(font, cancel.label) + TOOLBAR_PAD_X * 2, rowH };
        m_items.append(cancel);
        ax += cancel.rect.w + TOOLBAR_GAP;
        if (ax > totalW) totalW = ax + TOOLBAR_MARGIN;
        totalH = py + rowH + TOOLBAR_MARGIN;
    }

    m_surfaceW = totalW;
    m_surfaceH = totalH;

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
    }
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

void FerryToolbar::drawButton(SDL_Surface* dst, const Item& item)
{
    if (!item.visible) {
        return;
    }
    SDL_Color bg = item.danger ? (item.hover ? C_DANGER_H : C_DANGER)
                               : (item.selected ? (item.hover ? C_ACCENT_H : C_ACCENT)
                                                : (item.hover ? C_BTN_HOV : C_BTN));
    SDL_FillRect(dst, &item.rect, SDL_MapRGBA(dst->format, bg.r, bg.g, bg.b, bg.a));

    int tw = textWidth((TTF_Font*)m_font, item.label);
    int tx = item.rect.x + (item.rect.w - tw) / 2;
    int ty = item.rect.y + (item.rect.h - m_fontSize - 2) / 2;
    blitText(dst, item.label, C_TEXT, tx, ty);
}

void FerryToolbar::render()
{
    if (m_window == nullptr) {
        return;
    }

    // Preserve hover state across the re-layout below.
    QHash<int, bool> hoverById;
    for (const Item& item : m_items) {
        if (item.hover) {
            hoverById.insert((int)item.id, true);
        }
    }

    layout();

    for (Item& item : m_items) {
        if (hoverById.value((int)item.id, false)) {
            item.hover = true;
        }
    }

    SDL_SetWindowSize(m_window, m_surfaceW, m_surfaceH);
    SDL_Surface* surface = SDL_GetWindowSurface(m_window);
    if (surface == nullptr) {
        return;
    }

    SDL_FillRect(surface, nullptr, SDL_MapRGBA(surface->format, C_BAR_BG.r, C_BAR_BG.g, C_BAR_BG.b, C_BAR_BG.a));

    QString peer = FerryBridge::peerName;
    if (peer.isEmpty()) {
        peer = QStringLiteral("Ferry");
    }
    if (peer.length() > 14) {
        peer = peer.left(14) + QStringLiteral("…");
    }
    int nameY = TOOLBAR_MARGIN + (m_barHeight - m_fontSize - 2) / 2;
    blitText(surface, peer, C_MUTED, TOOLBAR_MARGIN + 6, nameY);

    for (const Item& item : m_items) {
        drawButton(surface, item);
    }

    if (!m_status.isEmpty()) {
        int sy = TOOLBAR_MARGIN + m_barHeight + 4;
        blitText(surface, m_status, C_MUTED, TOOLBAR_MARGIN + 8, sy);
    }

    if (m_qualityOpen) {
        int titleX = TOOLBAR_MARGIN + 8;
        int titleW = 44;
        int rowH = 26;
        int py = TOOLBAR_MARGIN + m_barHeight + 6;
        const char* titles[3] = { "分辨率", "帧率", "码率" };
        for (int i = 0; i < 3; i++) {
            blitText(surface, QString::fromUtf8(titles[i]), C_MUTED, titleX, py + (rowH - m_fontSize - 2) / 2);
            py += rowH + 4;
        }
    }

    SDL_UpdateWindowSurface(m_window);
}

void FerryToolbar::follow()
{
    if (m_window == nullptr || m_streamWindow == nullptr) {
        return;
    }
    if (!m_visible) {
        return;
    }
    int wx, wy, ww, wh;
    SDL_GetWindowPosition(m_streamWindow, &wx, &wy);
    SDL_GetWindowSize(m_streamWindow, &ww, &wh);
    followTo(wx, wy, ww);
}

void FerryToolbar::followTo(int wx, int wy, int ww)
{
    if (m_window == nullptr || m_streamWindow == nullptr) {
        return;
    }
    if (!m_visible) {
        return;
    }
    int tx = wx + (ww - m_surfaceW) / 2;
    if (tx < wx) tx = wx;
    int ty = wy + 2;
    SDL_SetWindowPosition(m_window, tx, ty);
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

    bool show = m_focused;

    if (m_fullscreen && show) {
        int mouseX, mouseY;
        SDL_GetGlobalMouseState(&mouseX, &mouseY);
        int wx, wy;
        SDL_GetWindowPosition(m_streamWindow, &wx, &wy);
        Uint32 now = SDL_GetTicks();
        if (mouseY <= wy + m_barHeight + 8 && mouseX >= wx && mouseX <= wx + m_surfaceW) {
            m_lastTopTick = now;
        }
        show = (now - m_lastTopTick) < TOOLBAR_FS_HIDE_MS;
    }

    if (show != m_visible) {
        m_visible = show;
        if (show) {
            SDL_ShowWindow(m_window);
            follow();
        }
        else {
            SDL_HideWindow(m_window);
        }
    }
    else if (show) {
        follow();
    }
}

bool FerryToolbar::handleMouseButton(const SDL_MouseButtonEvent* ev)
{
    if (m_window == nullptr) {
        return false;
    }
    if (ev->state == SDL_PRESSED && ev->button == SDL_BUTTON_LEFT) {
        Item* item = itemAt(ev->x, ev->y);
        if (item != nullptr) {
            item->hover = true;
            onButton(item->id);
        }
    }
    // Clicks inside the toolbar window are always consumed.
    return true;
}

bool FerryToolbar::handleMouseMotion(const SDL_MouseMotionEvent* ev)
{
    if (m_window == nullptr) {
        return false;
    }
    Item* hit = itemAt(ev->x, ev->y);
    for (Item& item : m_items) {
        item.hover = (&item == hit);
    }
    render();
    return true;
}

void FerryToolbar::onButton(ButtonId id)
{
    switch (id) {
    case BtnFullscreen:
        if (m_session != nullptr) {
            m_session->toggleFullscreen();
        }
        break;

    case BtnPrivacy:
        m_privacy = !m_privacy;
        FerryBridge::sendBlank(m_privacy);
        syncLabels();
        render();
        break;

    case BtnQuality:
        m_qualityOpen = !m_qualityOpen;
        m_pendingResKey = m_resKey;
        m_pendingFps = m_fps;
        m_pendingBitrateMbps = m_bitrateMbps;
        m_pendingBitrateAuto = m_bitrateAuto;
        syncLabels();
        render();
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

    case BtnApply:
        m_resKey = m_pendingResKey;
        m_fps = m_pendingFps;
        m_bitrateMbps = m_pendingBitrateMbps;
        m_bitrateAuto = m_pendingBitrateAuto;
        m_qualityOpen = false;
        syncLabels();
        render();
        FerryBridge::sendReconnect(m_resKey, m_fps, m_bitrateMbps, m_bitrateAuto);
        break;

    case BtnCancel:
        m_qualityOpen = false;
        m_pendingResKey = m_resKey;
        m_pendingFps = m_fps;
        m_pendingBitrateMbps = m_bitrateMbps;
        m_pendingBitrateAuto = m_bitrateAuto;
        syncLabels();
        render();
        break;

    default:
        break;
    }
}
