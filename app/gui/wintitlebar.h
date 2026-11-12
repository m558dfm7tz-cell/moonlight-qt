#pragma once

struct SDL_Window;
class QWindow;

// Native Windows caption styling (DWM). No-op on other platforms and on
// older Windows builds that reject the attributes.
void applyNativeWindowTitleBar(void* hwnd, bool borderless);
void applyQtWindowTitleBar(QWindow* window);
void applySdlWindowTitleBar(SDL_Window* window);
void installQtWindowTitleBarStyle();
