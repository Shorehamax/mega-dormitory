#pragma once
#include "mega/audio.hpp"
#include <filesystem>
#include <string>
namespace mega {
struct Preferences {
  AudioSettings audio;
  std::string language = "en";
  int visibleFloors = 96;
  bool gpuSkinning = true, cutaway = true;
  float sensitivity = .0025f, fov = 72;
  bool operator==(const Preferences &) const = default;
  void normalize();
  static Preferences load(const std::filesystem::path &);
  void save(const std::filesystem::path &) const;
};
} // namespace mega
