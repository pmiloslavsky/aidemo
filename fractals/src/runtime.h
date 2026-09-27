#pragma once
// Where the app keeps its files, the embedded default assets, and logging.

#include <filesystem>

namespace runtime {

// With --console on Windows, attach to the parent's console (or open one) so
// output shows up; the exe is a GUI app and has no console by default.
void openConsole();

// Picks the data folder and creates its subfolders:
//   <exe dir>/FractalsData, or the per-user data folder when that isn't writable
//   (%LOCALAPPDATA%\Fractals, $XDG_DATA_HOME/fractals, ~/.local/share/fractals).
std::filesystem::path setupDataDir();

// Sends std::cout and std::cerr to <data>/fractals.log, and also to the
// console when there is one (always on Linux, with --console on Windows).
void startLogging(const std::filesystem::path &logFile, bool console);

// Writes the embedded themes and escape images into the data folder, skipping
// files that already exist so user edits survive.
void extractDefaultAssets(const std::filesystem::path &data);

}  // namespace runtime
