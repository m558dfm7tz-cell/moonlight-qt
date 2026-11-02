#pragma once
#include <QString>
// Development-only entry, FERRY_TOOLBAR_PREVIEW=<directory>. Uses hidden SDL
// windows and the real toolbar rasterizer; never reads/captures the desktop.
int runFerryToolbarPreview(const QString& outDir);
