#pragma once

#include <QString>

// Development-only preview entry point, activated by the FERRY_TOOLBAR_PREVIEW
// environment variable. It renders the real FerryToolbar over a plain
// background and saves screen captures of each state (A expanded / B collapsed /
// C docked / quality panel / transfer progress / privacy / audio states) to PNG
// files. Returns the process exit code. Never used during normal streaming.
int runFerryToolbarPreview(const QString& outDir);
