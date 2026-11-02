#include "mega/preferences.hpp"
#include "json.hpp"
#include <cmath>
#include <fstream>
namespace mega {
void Preferences::normalize() {
  auto number = [](float v, float lo, float hi, float fallback) {
    return std::isfinite(v) ? std::clamp(v, lo, hi) : fallback;
  };
  audio.master = number(audio.master, 0, 1, .7f);
  audio.effects = number(audio.effects, 0, 1, .8f);
  audio.ambience = number(audio.ambience, 0, 1, .6f);
  sensitivity = number(sensitivity, .0006f, .006f, .0025f);
  fov = number(fov, 55, 100, 72);
  if (language != "en" && language != "fr" && language != "zh")
    language = "en";
  if (visibleFloors != 32 && visibleFloors != 96 && visibleFloors != 192)
    visibleFloors = 96;
}
Preferences Preferences::load(const std::filesystem::path &path) {
  Preferences p;
  if (!std::filesystem::exists(path))
    return p;
  if (std::filesystem::file_size(path) > 65536)
    throw std::runtime_error("preferences.error.load");
  try {
    std::ifstream stream(path);
    auto j = nlohmann::json::parse(stream);
    if (!j.is_object() || j.value("version", 1) != 1)
      throw std::runtime_error("version");
    p.language = j.value("language", p.language);
    p.visibleFloors = j.value("visibleFloors", p.visibleFloors);
    p.gpuSkinning = j.value("gpuSkinning", true);
    p.cutaway = j.value("cutaway", true);
    p.sensitivity = j.value("sensitivity", p.sensitivity);
    p.fov = j.value("fov", p.fov);
    if (j.contains("audio")) {
      const auto &a = j.at("audio");
      if (!a.is_object())
        throw std::runtime_error("audio");
      p.audio.master = a.value("master", p.audio.master);
      p.audio.effects = a.value("effects", p.audio.effects);
      p.audio.ambience = a.value("ambience", p.audio.ambience);
      p.audio.muted = a.value("muted", false);
      p.audio.muteUnfocused = a.value("muteUnfocused", true);
    }
  } catch (const std::exception &) {
    throw std::runtime_error("preferences.error.load");
  }
  p.normalize();
  return p;
}
void Preferences::save(const std::filesystem::path &path) const {
  auto p = *this;
  p.normalize();
  nlohmann::json j = {{"version", 1},
                      {"language", p.language},
                      {"visibleFloors", p.visibleFloors},
                      {"gpuSkinning", p.gpuSkinning},
                      {"cutaway", p.cutaway},
                      {"sensitivity", p.sensitivity},
                      {"fov", p.fov},
                      {"audio",
                       {{"master", p.audio.master},
                        {"effects", p.audio.effects},
                        {"ambience", p.audio.ambience},
                        {"muted", p.audio.muted},
                        {"muteUnfocused", p.audio.muteUnfocused}}}};
  if (!path.parent_path().empty())
    std::filesystem::create_directories(path.parent_path());
  auto temporary = path;
  temporary += ".tmp";
  {
    std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
    out << j.dump(2) << '\n';
    out.flush();
    if (!out)
      throw std::runtime_error("preferences.error.save");
  }
  try {
    if (std::filesystem::is_regular_file(path)) {
      auto backup = path;
      backup += ".bak";
      std::filesystem::copy_file(path, backup, std::filesystem::copy_options::overwrite_existing);
    }
    std::filesystem::rename(temporary, path);
  } catch (const std::exception &) {
    std::filesystem::remove(temporary);
    throw std::runtime_error("preferences.error.save");
  }
}
} // namespace mega
