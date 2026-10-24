#pragma once

/*
 * Folder that contains assets/, with a trailing separator.
 * Next to the .exe in a shipped build; while developing, Visual Studio puts the
 * .exe in x64\Debug (or Release), so this walks up to the solution folder.
 */
const char *Asset_Root(void);
