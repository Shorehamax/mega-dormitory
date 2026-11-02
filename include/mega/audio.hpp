#pragma once
#include "mega/math.hpp"
#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace mega {
enum class Sound {
  Click,
  Error,
  Pickup,
  Hit,
  Shot,
  Step,
  Ladder,
  Air,
  Machine,
  Water,
  Fire,
  Motor
};
struct AudioSettings {
  float master = .7f, effects = .8f, ambience = .6f;
  bool muted = false, muteUnfocused = true;
  bool operator==(const AudioSettings &) const = default;
};
struct SoundSource {
  std::uint64_t id = 0;
  Sound sound = Sound::Air;
  Vec3 position{};
  float gain = 1, rate = 2;
};
struct AudioScene {
  Vec3 listener{}, right{1, 0, 0};
  bool playing = false;
  std::vector<SoundSource> sources;
};
class AudioMixer {
  struct Voice {
    std::uint64_t id = 0, age = 0;
    Sound sound = Sound::Click;
    float gain = 0, targetGain = 0, left = .7071f, right = .7071f, rate = 2, filter = 0;
    std::uint32_t noise = 1;
    bool loop = false;
  };
  std::vector<Voice> voices;
  AudioSettings settings;
  float worldGain = 0;
  bool worldPlaying = false;

public:
  static constexpr int sampleRate = 48000;
  static constexpr std::size_t maxVoices = 48;
  void reset();
  void configure(AudioSettings);
  void scene(const AudioScene &);
  void play(Sound, float gain = 1, float pan = 0);
  void mix(std::span<float> stereo);
  std::size_t voiceCount() const { return voices.size(); }
};
class AudioEngine {
  struct Impl;
  std::unique_ptr<Impl> impl;

public:
  AudioEngine();
  ~AudioEngine();
  AudioEngine(const AudioEngine &) = delete;
  AudioEngine &operator=(const AudioEngine &) = delete;
  bool start(const std::string &device = "");
  void stop();
  void reset();
  bool available() const;
  std::uint64_t framesWritten() const;
  void configure(AudioSettings);
  void scene(const AudioScene &);
  void play(Sound, float gain = 1, float pan = 0);
};
} // namespace mega
