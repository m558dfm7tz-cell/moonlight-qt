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
#include <QtGlobal>

#define TOOLBAR_FONT_SIZE 13
#define TOOLBAR_MARGIN 6
#define TOOLBAR_GAP 4
#define TOOLBAR_CELL 32
#define TOOLBAR_STATUS_MS 6000
#define TOOLBAR_FS_HIDE_MS 1500
#define TOOLBAR_WIN_HIDE_MS 250
#define TOOLBAR_TRANSFER_TIMEOUT 15000
#define TOOLBAR_DONE_FLASH_MS 1000
#define TOOLBAR_PROGRESS_W 48
#define TOOLBAR_PROGRESS_H 40
#define TOOLBAR_PROGRESS_THICK 6

// Ferry dark-glass palette (mirrors the WPF settings / main window).
static SDL_Color C_BAR_BG   = { 16, 27, 38, 255 };
static SDL_Color C_HOVER    = { 35, 71, 108, 255 };
static SDL_Color C_ACCENT   = { 37, 102, 179, 255 };
static SDL_Color C_DANGER   = { 186, 62, 52, 255 };
static SDL_Color C_ICON     = { 241, 246, 253, 255 };
static SDL_Color C_MUTED    = { 149, 168, 185, 255 };
static SDL_Color C_TRACK    = { 60, 70, 84, 255 };
static SDL_Color C_FILL     = { 56, 200, 255, 255 };

FerryToolbar::FerryToolbar(Session* session)
    : m_session(session),
      m_streamWindow(nullptr),
      m_window(nullptr),
      m_windowId(0),
      m_font(nullptr),
      m_fontSize(TOOLBAR_FONT_SIZE),
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
      m_lastTx(-1),
      m_lastTy(-1)
{
    m_progressTrack = { 0, 0, 0, 0 };
    m_progressFill = { 0, 0, 0, 0 };
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
                                TOOLBAR_CELL + TOOLBAR_MARGIN * 2,
                                TOOLBAR_CELL + TOOLBAR_MARGIN * 2,
                                SDL_WINDOW_BORDERLESS | SDL_WINDOW_ALWAYS_ON_TOP | SDL_WINDOW_SKIP_TASKBAR);
    if (m_window == nullptr) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Ferry toolbar window creation failed: %s", SDL_GetError());
        return false;
    }
    m_windowId = SDL_GetWindowID(m_window);

    // Ferry dark-glass style: semi-transparent, rounded corners. Opacity is a
    // whole-window alpha; the rounded shape is applied in setShape() once the
    // surface is sized.
    SDL_SetWindowOpacity(m_window, 0.92f);

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

    m_lastActivityTick = SDL_GetTicks();

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
    // Reset the reveal timer so the bar doesn't immediately collapse after a
    // fullscreen toggle.
    m_lastActivityTick = SDL_GetTicks();
    render();
}

void FerryToolbar::setFocused(bool focused)
{
    // Visibility no longer follows focus; it follows the pointer position.
    // We still record the value so the session's existing calls remain
    // harmless, and force a collapse when the stream window loses focus.
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
        // Immediately remove the progress bar and "sending" state.
        m_transferActive = false;
        m_transferPercent = 0;
        m_lastTransferTick = 0;
        if (state == QLatin1String("done")) {
            // Flash a brief completion hint (<= 1s), then revert.
            m_transferDoneTick = SDL_GetTicks();
        }
    }
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
    m_showProgress = false;
    m_progressVertical = !isHorizontal();

    const int M = TOOLBAR_MARGIN;
    const int C = TOOLBAR_CELL;
    const int G = TOOLBAR_GAP;
    const bool horiz = isHorizontal();

    if (!m_expanded) {
        // Collapsed: just the drag handle.
        Item it;
        it.id = BtnDrag;
        it.label = QString();
        it.visible = true;
        it.selected = false;
        it.hover = false;
        it.danger = false;
        it.rect = { M, M, C, C };
        m_items.append(it);
        m_surfaceW = C + 2 * M;
        m_surfaceH = C + 2 * M;
        return;
    }

    int x = M;
    int y = M;

    QVector<QPair<ButtonId, bool>> mainButtons; // (id, danger)
    mainButtons.append(qMakePair(BtnDrag, false));
    mainButtons.append(qMakePair(BtnFullscreen, false));
    mainButtons.append(qMakePair(BtnPrivacy, false));
    mainButtons.append(qMakePair(BtnQuality, false));
    mainButtons.append(qMakePair(BtnTransfer, false));
    mainButtons.append(qMakePair(BtnEnd, true));

    for (const auto& mb : mainButtons) {
        Item it;
        it.id = mb.first;
        it.label = (mb.first == BtnQuality) ? qualityLabel() : QString();
        it.visible = true;
        it.selected = false;
        it.hover = false;
        it.danger = mb.second;
        it.rect = { x, y, C, C };
        m_items.append(it);

        if (horiz) x += C + G;
        else y += C + G;

        // Reserve inline space for the transfer progress bar.
        if (mb.first == BtnTransfer && m_transferActive) {
            if (horiz) x += TOOLBAR_PROGRESS_W + G;
            else y += TOOLBAR_PROGRESS_H + G;
        }
    }

    int mainW = horiz ? (x - G + M) : (C + 2 * M);
    int mainH = horiz ? (C + 2 * M) : (y - G + M);

    m_surfaceW = mainW;
    m_surfaceH = mainH;

    if (m_qualityOpen) {
        // Chip panel appended below the main bar (horizontal layout for the
        // chips, regardless of dock orientation).
        const int rowH = 26;
        const int titleW = 44;
        int px = M + 8;
        int py = M + C + 6;
        int cursor = py;

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
                int w = textWidth(font, label) + 12;
                Item it;
                it.id = chip.first;
                it.label = label;
                it.visible = true;
                it.selected = false;
                it.hover = false;
                it.danger = false;
                it.rect = { cx, cursor, w, rowH };
                m_items.append(it);
                cx += w + G;
            }
            if (cx > m_surfaceW) m_surfaceW = cx + M;
            cursor += rowH + 4;
        }

        Item apply;
        apply.id = BtnApply;
        apply.label = QStringLiteral("应用并重连");
        apply.visible = true;
        apply.selected = false;
        apply.hover = false;
        apply.danger = false;
        int ax = px + titleW + 6;
        apply.rect = { ax, cursor, textWidth(font, apply.label) + 12, rowH };
        m_items.append(apply);

        Item cancel;
        cancel.id = BtnCancel;
        cancel.label = QStringLiteral("收起");
        cancel.visible = true;
        cancel.selected = false;
        cancel.hover = false;
        cancel.danger = false;
        cancel.rect = { apply.rect.x + apply.rect.w + G, cursor, textWidth(font, cancel.label) + 12, rowH };
        m_items.append(cancel);

        int endX = cancel.rect.x + cancel.rect.w + M;
        int endY = cancel.rect.y + cancel.rect.h + M;
        if (endX > m_surfaceW) m_surfaceW = endX;
        if (endY > m_surfaceH) m_surfaceH = endY;
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
    }
}

void FerryToolbar::putPixel(SDL_Surface* dst, int x, int y, SDL_Color c)
{
    if (dst == nullptr || x < 0 || y < 0 || x >= dst->w || y >= dst->h) {
        return;
    }
    SDL_Rect r = { x, y, 1, 1 };
    SDL_FillRect(dst, &r, SDL_MapRGBA(dst->format, c.r, c.g, c.b, c.a));
}

void FerryToolbar::drawLine(SDL_Surface* dst, int x0, int y0, int x1, int y1, SDL_Color c)
{
    int dx = qAbs(x1 - x0);
    int sx = x0 < x1 ? 1 : -1;
    int dy = -qAbs(y1 - y0);
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        putPixel(dst, x0, y0, c);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void FerryToolbar::fillCircle(SDL_Surface* dst, int cx, int cy, int r, SDL_Color c)
{
    for (int y = -r; y <= r; y++) {
        for (int x = -r; x <= r; x++) {
            if (x * x + y * y <= r * r) {
                putPixel(dst, cx + x, cy + y, c);
            }
        }
    }
}

void FerryToolbar::fillRoundedRect(SDL_Surface* dst, const SDL_Rect& r, int radius, SDL_Color c)
{
    int rr = qMin(radius, qMin(r.w, r.h) / 2);
    if (rr <= 0) {
        SDL_FillRect(dst, &r, SDL_MapRGBA(dst->format, c.r, c.g, c.b, c.a));
        return;
    }
    for (int y = 0; y < r.h; y++) {
        for (int x = 0; x < r.w; x++) {
            int dx = 0, dy = 0;
            if (x < rr) dx = rr - x;
            else if (x >= r.w - rr) dx = x - (r.w - rr - 1);
            if (y < rr) dy = rr - y;
            else if (y >= r.h - rr) dy = y - (r.h - rr - 1);
            if (dx * dx + dy * dy <= rr * rr) {
                putPixel(dst, r.x + x, r.y + y, c);
            }
        }
    }
}

void FerryToolbar::drawIcon(SDL_Surface* dst, ButtonId id, const SDL_Rect& r, SDL_Color c)
{
    int cx = r.x + r.w / 2;
    int cy = r.y + r.h / 2;
    const int s = 8; // half-size of the icon

    switch (id) {
    case BtnDrag:
        // 6-dot grip: 3 columns x 2 rows.
        for (int col = 0; col < 3; col++) {
            for (int row = 0; row < 2; row++) {
                fillCircle(dst, cx - 6 + col * 6, cy - 3 + row * 6, 1, c);
            }
        }
        break;

    case BtnFullscreen:
        // Four corner brackets (outward-facing).
        drawLine(dst, cx - s, cy - s + 3, cx - s, cy - s, c);
        drawLine(dst, cx - s, cy - s, cx - s + 3, cy - s, c);
        drawLine(dst, cx + s - 3, cy - s, cx + s, cy - s, c);
        drawLine(dst, cx + s, cy - s, cx + s, cy - s + 3, c);
        drawLine(dst, cx - s, cy + s - 3, cx - s, cy + s, c);
        drawLine(dst, cx - s, cy + s, cx - s + 3, cy + s, c);
        drawLine(dst, cx + s - 3, cy + s, cx + s, cy + s, c);
        drawLine(dst, cx + s, cy + s, cx + s, cy + s - 3, c);
        break;

    case BtnPrivacy:
        // Eye outline (lens shape) with a diagonal slash through it.
        drawLine(dst, cx - s, cy, cx, cy - s, c);
        drawLine(dst, cx, cy - s, cx + s, cy, c);
        drawLine(dst, cx + s, cy, cx, cy + s, c);
        drawLine(dst, cx, cy + s, cx - s, cy, c);
        drawLine(dst, cx - s, cy + s, cx + s, cy - s, c);
        break;

    case BtnTransfer:
        // Up arrow above a base tray.
        drawLine(dst, cx, cy - s, cx, cy + 2, c);
        drawLine(dst, cx - 4, cy - 4, cx, cy - s, c);
        drawLine(dst, cx, cy - s, cx + 4, cy - 4, c);
        drawLine(dst, cx - 6, cy + s, cx + 6, cy + s, c);
        break;

    case BtnEnd:
        // Cross.
        drawLine(dst, cx - s, cy - s, cx + s, cy + s, c);
        drawLine(dst, cx + s, cy - s, cx - s, cy + s, c);
        break;

    default:
        break;
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

    // Buttons (icons + hover highlight + quality "HD" text).
    for (const Item& item : m_items) {
        if (!item.visible) {
            continue;
        }

        // Subtle rounded highlight on hover (or selected chip).
        if (item.hover || item.selected) {
            SDL_Color bg = item.danger ? C_DANGER : (item.selected ? C_ACCENT : C_HOVER);
            SDL_Rect hi = { item.rect.x + 1, item.rect.y + 1, item.rect.w - 2, item.rect.h - 2 };
            fillRoundedRect(surface, hi, 6, bg);
        }

        if (item.id == BtnQuality) {
            int tw = textWidth((TTF_Font*)m_font, item.label);
            int tx = item.rect.x + (item.rect.w - tw) / 2;
            int ty = item.rect.y + (item.rect.h - m_fontSize - 2) / 2;
            blitText(surface, item.label, C_ICON, tx, ty);
        }
        else if (item.id != BtnResRemote && item.id != BtnResLocal &&
                 item.id != BtnRes720 && item.id != BtnRes1080 &&
                 item.id != BtnRes1440 && item.id != BtnRes4K &&
                 item.id != BtnFps30 && item.id != BtnFps60 && item.id != BtnFps120 &&
                 item.id != BtnRateAuto && item.id != BtnRate10 && item.id != BtnRate20 &&
                 item.id != BtnRate40 && item.id != BtnRate80 &&
                 item.id != BtnApply && item.id != BtnCancel) {
            drawIcon(surface, item.id, item.rect, C_ICON);
        }
        else {
            // Text chip (quality panel).
            int tw = textWidth((TTF_Font*)m_font, item.label);
            int tx = item.rect.x + (item.rect.w - tw) / 2;
            int ty = item.rect.y + (item.rect.h - m_fontSize - 2) / 2;
            blitText(surface, item.label, C_ICON, tx, ty);
        }
    }

    // Transfer progress bar, adjacent to the transfer button.
    m_showProgress = false;
    if (m_transferActive || (SDL_GetTicks() - m_transferDoneTick) < TOOLBAR_DONE_FLASH_MS) {
        if (Item* transfer = itemById(BtnTransfer)) {
            m_showProgress = true;
            m_progressVertical = !isHorizontal();
            const bool flashDone = !m_transferActive;

            if (m_progressVertical) {
                // Vertical bar below the transfer icon (only when expanded and docked left/right).
                m_progressTrack = { transfer->rect.x + (transfer->rect.w - TOOLBAR_PROGRESS_THICK) / 2,
                                    transfer->rect.y + transfer->rect.h + 4,
                                    TOOLBAR_PROGRESS_THICK, TOOLBAR_PROGRESS_H };
            }
            else {
                m_progressTrack = { transfer->rect.x + transfer->rect.w + 4,
                                    transfer->rect.y + (transfer->rect.h - TOOLBAR_PROGRESS_THICK) / 2,
                                    TOOLBAR_PROGRESS_W, TOOLBAR_PROGRESS_THICK };
            }

            SDL_FillRect(surface, &m_progressTrack, SDL_MapRGBA(surface->format, C_TRACK.r, C_TRACK.g, C_TRACK.b, C_TRACK.a));

            int percent = flashDone ? 100 : m_transferPercent;
            if (m_progressVertical) {
                int filled = m_progressTrack.h * percent / 100;
                SDL_Rect fill = { m_progressTrack.x,
                                  m_progressTrack.y + (m_progressTrack.h - filled),
                                  m_progressTrack.w, filled };
                m_progressFill = fill;
                SDL_FillRect(surface, &fill, SDL_MapRGBA(surface->format, C_FILL.r, C_FILL.g, C_FILL.b, C_FILL.a));
            }
            else {
                int filled = m_progressTrack.w * percent / 100;
                SDL_Rect fill = { m_progressTrack.x, m_progressTrack.y, filled, m_progressTrack.h };
                m_progressFill = fill;
                SDL_FillRect(surface, &fill, SDL_MapRGBA(surface->format, C_FILL.r, C_FILL.g, C_FILL.b, C_FILL.a));
            }

            if (!flashDone) {
                QString pct = QString::number(m_transferPercent) + "%";
                int tw = textWidth((TTF_Font*)m_font, pct);
                if (m_progressVertical) {
                    blitText(surface, pct, C_ICON, m_progressTrack.x + (m_progressTrack.w - tw) / 2,
                             m_progressTrack.y - m_fontSize - 4);
                }
                else {
                    blitText(surface, pct, C_ICON, m_progressTrack.x + (m_progressTrack.w - tw) / 2,
                             m_progressTrack.y + m_progressTrack.h + 2);
                }
            }
        }
    }

    // Status line (informational, e.g. "正在发送文件…" has been replaced by
    // the progress bar, so this is only used for transient messages).
    if (!m_status.isEmpty() && (SDL_GetTicks() - m_statusTick) < TOOLBAR_STATUS_MS) {
        int sx = TOOLBAR_MARGIN + 8;
        int sy = TOOLBAR_MARGIN + TOOLBAR_CELL + 4;
        blitText(surface, m_status, C_MUTED, sx, sy);
    }
    else {
        m_status.clear();
    }

    SDL_UpdateWindowSurface(m_window);

    setShape();

    // Re-dock immediately after a resize (e.g. the progress bar or quality
    // panel changed the surface size) so the bar doesn't sit at a stale spot
    // until the next tick().
    follow();
}

void FerryToolbar::setShape()
{
    if (m_window == nullptr || m_surfaceW <= 0 || m_surfaceH <= 0) {
        return;
    }

    const int radius = 10;
    SDL_Surface* shape = SDL_CreateRGBSurfaceWithFormat(0, m_surfaceW, m_surfaceH, 32, SDL_PIXELFORMAT_ARGB8888);
    if (shape == nullptr) {
        return;
    }
    SDL_FillRect(shape, nullptr, SDL_MapRGBA(shape->format, 0, 0, 0, 0));

    Uint32 opaque = SDL_MapRGBA(shape->format, 255, 255, 255, 255);
    int r = qMin(radius, qMin(m_surfaceW, m_surfaceH) / 2);
    for (int y = 0; y < m_surfaceH; y++) {
        for (int x = 0; x < m_surfaceW; x++) {
            int dx = 0, dy = 0;
            if (x < r) dx = r - x;
            else if (x >= m_surfaceW - r) dx = x - (m_surfaceW - r - 1);
            if (y < r) dy = r - y;
            else if (y >= m_surfaceH - r) dy = y - (m_surfaceH - r - 1);
            if (dx * dx + dy * dy <= r * r) {
                ((Uint32*)shape->pixels)[y * (shape->pitch / 4) + x] = opaque;
            }
        }
    }

    SDL_SetWindowShape(m_window, shape, nullptr);
    SDL_FreeSurface(shape);
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
    // Keep the toolbar inside the stream window's CLIENT area (the video
    // region). In windowed mode wx/wy include the title bar and frame, so
    // measure the client rect directly instead of guessing the decoration size.
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

    const int margin = 2;
    int tx = cx;
    int ty = cy;

    switch (m_dockEdge) {
    case DockTop:
        tx = cx + (cw - m_surfaceW) / 2;
        ty = cy + margin;
        break;
    case DockBottom:
        tx = cx + (cw - m_surfaceW) / 2;
        ty = cy + ch - m_surfaceH - margin;
        break;
    case DockLeft:
        tx = cx + margin;
        ty = cy + (ch - m_surfaceH) / 2;
        break;
    case DockRight:
        tx = cx + cw - m_surfaceW - margin;
        ty = cy + (ch - m_surfaceH) / 2;
        break;
    }

    // Clamp inside the client area.
    if (tx < cx) tx = cx;
    if (ty < cy) ty = cy;
    if (tx + m_surfaceW > cx + cw) tx = cx + cw - m_surfaceW;
    if (ty + m_surfaceH > cy + ch) ty = cy + ch - m_surfaceH;
    if (tx < cx) tx = cx;
    if (ty < cy) ty = cy;

    if (tx != m_lastTx || ty != m_lastTy) {
        SDL_SetWindowPosition(m_window, tx, ty);
        m_lastTx = tx;
        m_lastTy = ty;
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
    // The hook gives us the outer frame rect; prefer the client rect above,
    // but fall back to the passed values if the client rect is unavailable.
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
    int tx = wx + m_surfaceW / 2;
    int ty = wy + m_surfaceH / 2;

    // Distance to each edge.
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

    // Let Ferry push transferProgress updates into the toolbar.
    FerryBridge::readMessages();

    // Timeout for a stuck "sending" state.
    if (m_transferActive && (SDL_GetTicks() - m_lastTransferTick) > TOOLBAR_TRANSFER_TIMEOUT) {
        m_transferActive = false;
        m_transferPercent = 0;
        render();
    }

    int mouseX, mouseY;
    Uint32 buttons = SDL_GetGlobalMouseState(&mouseX, &mouseY);

    // Toolbar window rect (global).
    int wx, wy, ww, wh;
    SDL_GetWindowPosition(m_window, &wx, &wy);
    SDL_GetWindowSize(m_window, &ww, &wh);

    // Stream client rect (global).
    int cx, cy, cw, ch;
    computeClientRect(cx, cy, cw, ch);

    Uint32 now = SDL_GetTicks();

    // Dragging: move the toolbar with the pointer. We also detect the release
    // here from the global button state, so the drag ends cleanly even if the
    // pointer has moved off the toolbar window (where the button-up would not
    // be delivered).
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
        }
        return;
    }

    // Determine whether the pointer is over the toolbar (expanded rect) or,
    // when collapsed, over the handle.
    bool overToolbar = (mouseX >= wx && mouseX < wx + ww && mouseY >= wy && mouseY < wy + wh);

    // In fullscreen, also treat the middle of the docked edge as a reveal zone
    // so the bar can be summoned even if the handle is not hit exactly.
    bool overRevealZone = false;
    if (m_fullscreen && !m_expanded) {
        const int zone = 14;
        switch (m_dockEdge) {
        case DockTop:    overRevealZone = (mouseY <= cy + zone); break;
        case DockBottom: overRevealZone = (mouseY >= cy + ch - zone); break;
        case DockLeft:   overRevealZone = (mouseX <= cx + zone); break;
        case DockRight:  overRevealZone = (mouseX >= cx + cw - zone); break;
        }
    }

    bool shouldExpand = overToolbar || overRevealZone;

    if (shouldExpand) {
        m_lastActivityTick = now;
        if (!m_expanded) {
            m_expanded = true;
            render();
            follow();
        }
    }
    else {
        // Collapse after a short delay.
        Uint32 hideMs = m_fullscreen ? TOOLBAR_FS_HIDE_MS : TOOLBAR_WIN_HIDE_MS;
        if (m_expanded && (now - m_lastActivityTick) > hideMs) {
            m_expanded = false;
            render();
            follow();
        }
    }

    follow();
}

bool FerryToolbar::handleMouseButton(const SDL_MouseButtonEvent* ev)
{
    if (m_window == nullptr) {
        return false;
    }

    if (ev->type == SDL_MOUSEBUTTONDOWN && ev->button == SDL_BUTTON_LEFT) {
        Item* item = itemAt(ev->x, ev->y);
        if (item != nullptr) {
            item->hover = true;
            if (item->id == BtnDrag) {
                // Begin dragging the toolbar.
                int gx, gy;
                SDL_GetGlobalMouseState(&gx, &gy);
                int wx, wy;
                SDL_GetWindowPosition(m_window, &wx, &wy);
                m_dragging = true;
                m_dragGrabX = gx - wx;
                m_dragGrabY = gy - wy;
                SDL_CaptureMouse(SDL_TRUE);
            }
            else {
                onButton(item->id);
            }
        }
    }
    else if (ev->type == SDL_MOUSEBUTTONUP && ev->button == SDL_BUTTON_LEFT) {
        if (m_dragging) {
            m_dragging = false;
            SDL_CaptureMouse(SDL_FALSE);
            dockToNearestEdge();
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

    if (m_dragging) {
        // Reposition is handled in tick() using the global pointer position.
        return true;
    }

    Item* hit = itemAt(ev->x, ev->y);
    bool changed = false;
    for (Item& item : m_items) {
        bool hov = (&item == hit);
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
        // Dragging is handled in handleMouseButton.
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
        render();
        break;

    case BtnTransfer:
        // The transfer button itself has no action; files are dropped onto the
        // stream window. Progress is driven by Ferry's transferProgress.
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
        render();
        FerryBridge::sendReconnect(m_resKey, m_fps, m_bitrateMbps, m_bitrateAuto);
        break;

    case BtnCancel:
        m_qualityOpen = false;
        m_pendingResKey = m_resKey;
        m_pendingFps = m_fps;
        m_pendingBitrateMbps = m_bitrateMbps;
        m_pendingBitrateAuto = m_bitrateAuto;
        render();
        break;

    default:
        break;
    }
}
