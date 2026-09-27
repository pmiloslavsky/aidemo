#include "runtime.h"

#include <cmrc/cmrc.hpp>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <system_error>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

CMRC_DECLARE(fractals_assets);

namespace fs = std::filesystem;

namespace runtime {
namespace {

fs::path exeDir() {
#ifdef _WIN32
  std::wstring buf(32768, L'\0');
  DWORD n = GetModuleFileNameW(nullptr, buf.data(), (DWORD)buf.size());
  buf.resize(n);
  return fs::path(buf).parent_path();
#else
  // Inside an AppImage the exe lives on a read-only mount; use the AppImage's folder
  if (const char *appimage = std::getenv("APPIMAGE")) return fs::path(appimage).parent_path();
  std::error_code ec;
  fs::path exe = fs::read_symlink("/proc/self/exe", ec);
  return ec ? fs::current_path() : exe.parent_path();
#endif
}

fs::path userDataDir() {
#ifdef _WIN32
  if (const wchar_t *local = _wgetenv(L"LOCALAPPDATA")) return fs::path(local) / "Fractals";
  return fs::temp_directory_path() / "Fractals";
#else
  if (const char *xdg = std::getenv("XDG_DATA_HOME"); xdg && *xdg) return fs::path(xdg) / "fractals";
  if (const char *home = std::getenv("HOME")) return fs::path(home) / ".local" / "share" / "fractals";
  return fs::temp_directory_path() / "fractals";
#endif
}

// Creates the folder and checks that a file can actually be written there.
bool writable(const fs::path &dir) {
  std::error_code ec;
  fs::create_directories(dir, ec);
  if (ec) return false;
  fs::path probe = dir / ".write_test";
  {
    std::ofstream f(probe);
    if (!(f << "x")) return false;
  }
  fs::remove(probe, ec);
  return true;
}

// Copies every character to two stream buffers (either may be null).
class TeeBuf : public std::streambuf {
 public:
  TeeBuf(std::streambuf *a, std::streambuf *b) : a_(a), b_(b) {}

 protected:
  int overflow(int c) override {
    if (c == EOF) return 0;
    if (a_) a_->sputc((char)c);
    if (b_) b_->sputc((char)c);
    return c;
  }
  int sync() override {
    if (a_) a_->pubsync();
    if (b_) b_->pubsync();
    return 0;
  }

 private:
  std::streambuf *a_;
  std::streambuf *b_;
};

}  // namespace

void openConsole() {
#ifdef _WIN32
  if (!AttachConsole(ATTACH_PARENT_PROCESS)) AllocConsole();
  FILE *f = nullptr;
  freopen_s(&f, "CONOUT$", "w", stdout);
  freopen_s(&f, "CONOUT$", "w", stderr);
#endif
}

fs::path setupDataDir() {
  fs::path data = exeDir() / "FractalsData";
  if (!writable(data)) data = userDataDir();
  std::error_code ec;
  for (const char *sub : {"themes", "keys", "screenshots", "escape_images"})
    fs::create_directories(data / sub, ec);
  return data;
}

void startLogging(const fs::path &logFile, bool console) {
#ifndef _WIN32
  console = true;  // a Linux binary always has somewhere to print to
#endif
  static std::ofstream log;
  log.open(logFile, std::ios::out | std::ios::trunc);
  std::streambuf *file = log ? log.rdbuf() : nullptr;
  static TeeBuf out(file, console ? std::cout.rdbuf() : nullptr);
  static TeeBuf err(file, console ? std::cerr.rdbuf() : nullptr);
  std::cout.rdbuf(&out);
  std::cerr.rdbuf(&err);
  if (!log) std::cerr << "Can't write log file " << logFile.string() << std::endl;
}

void extractDefaultAssets(const fs::path &data) {
  auto embedded = cmrc::fractals_assets::get_filesystem();
  for (const std::string dir : {"themes", "escape_images"}) {
    for (auto &&entry : embedded.iterate_directory(dir)) {
      if (!entry.is_file()) continue;
      fs::path out = data / dir / entry.filename();
      std::error_code ec;
      if (fs::exists(out, ec)) continue;
      cmrc::file file = embedded.open(dir + "/" + entry.filename());
      std::ofstream f(out, std::ios::binary);
      f.write(file.begin(), (std::streamsize)file.size());
      std::cout << (f ? "Extracted " : "Failed to extract ") << out.string() << std::endl;
    }
  }
}

}  // namespace runtime
