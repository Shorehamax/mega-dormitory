#pragma once
#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace mega {
struct RuntimePaths {
  std::filesystem::path assets, shaders, saves;
  bool installed = false;
  static bool complete(const std::filesystem::path &root) {
    if (!std::filesystem::is_regular_file(root / "assets/locales/en.json") ||
        !std::filesystem::is_regular_file(root / "assets/fonts/NotoSansCJK-Regular.ttc") ||
        !std::filesystem::is_directory(root / "assets/models"))
      return false;
    for (const char *file :
         {"scene.vert", "scene.frag", "shadow.vert", "shadow.frag", "pressure.comp", "skin.comp"})
      if (!std::filesystem::is_regular_file(root / "shaders" / (std::string(file) + ".spv")))
        return false;
    return true;
  }
  static std::filesystem::path userData() {
    for (const char *variable : {"XDG_DATA_HOME", "LOCALAPPDATA"})
      if (const char *value = std::getenv(variable);
          value && *value && std::filesystem::path(value).is_absolute())
        return std::filesystem::path(value) / "mega-dormitory";
    if (const char *value = std::getenv("HOME");
        value && *value && std::filesystem::path(value).is_absolute())
      return std::filesystem::path(value) / ".local/share/mega-dormitory";
    throw std::runtime_error("Cannot locate your save directory. Set XDG_DATA_HOME.");
  }
  static RuntimePaths discover(const std::filesystem::path &executable,
                               const std::filesystem::path &sourceAssets,
                               const std::filesystem::path &sourceShaders,
                               const std::filesystem::path &sourceProject) {
    std::filesystem::path root;
    if (const char *value = std::getenv("MEGA_DATA_DIR"); value && *value) {
      root = value;
      if (!complete(root))
        throw std::runtime_error("MEGA_DATA_DIR is missing game assets or compiled shaders.");
    } else {
      root = std::filesystem::absolute(executable).parent_path();
      if (!complete(root)) {
        if (std::filesystem::exists(root / "runtime-layout.json") ||
            std::filesystem::exists(root / "assets") ||
            std::filesystem::exists(root / "share/mega-dormitory")) {
          if (complete(root / "share/mega-dormitory"))
            root /= "share/mega-dormitory";
          else
            throw std::runtime_error(
                "Game installation is incomplete. Restore the assets and shaders folders.");
        } else
          return {sourceAssets, sourceShaders, sourceProject / "userdata", false};
      }
    }
    return {root / "assets", root / "shaders", userData(), true};
  }
};
inline std::filesystem::path runningExecutable(const char *argv0) {
#ifdef __linux__
  std::error_code error;
  auto path = std::filesystem::read_symlink("/proc/self/exe", error);
  if (!error)
    return path;
#endif
  return std::filesystem::absolute(argv0);
}
} // namespace mega
