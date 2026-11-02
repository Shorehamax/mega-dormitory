#include "mega/audio.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>
#ifdef __linux__
#include <dlfcn.h>
#endif
namespace mega {
namespace {
constexpr float pi = 3.14159265359f;
float bounded(float value, float low, float high, float fallback = 0) {
  return std::isfinite(value) ? std::clamp(value, low, high) : fallback;
}
float duration(Sound sound) {
  switch (sound) {
  case Sound::Click:
    return .055f;
  case Sound::Error:
    return .24f;
  case Sound::Pickup:
    return .15f;
  case Sound::Hit:
    return .18f;
  case Sound::Shot:
    return .36f;
  default:
    return 0;
  }
}
float random(std::uint32_t &state) {
  state ^= state << 13;
  state ^= state >> 17;
  state ^= state << 5;
  return float(state & 65535) / 32767.5f - 1;
}
} // namespace
void AudioMixer::reset() {
  voices.clear();
  worldGain = 0;
  worldPlaying = false;
}
void AudioMixer::configure(AudioSettings s) {
  s.master = bounded(s.master, 0, 1);
  s.effects = bounded(s.effects, 0, 1);
  s.ambience = bounded(s.ambience, 0, 1);
  settings = s;
}
void AudioMixer::play(Sound sound, float gain, float pan) {
  if (duration(sound) <= 0)
    return;
  if (voices.size() >= maxVoices) {
    auto oldest =
        std::find_if(voices.begin(), voices.end(), [](const Voice &v) { return !v.loop; });
    if (oldest == voices.end())
      return;
    voices.erase(oldest);
  }
  Voice v;
  v.sound = sound;
  v.gain = v.targetGain = bounded(gain, 0, 1);
  pan = bounded(pan, -1, 1);
  v.left = std::sqrt((1 - pan) * .5f);
  v.right = std::sqrt((1 + pan) * .5f);
  v.noise = std::uint32_t(voices.size() + 1) * 747796405u + 2891336453u;
  voices.push_back(v);
}
void AudioMixer::scene(const AudioScene &scene) {
  worldPlaying = scene.playing;
  std::vector<std::uint64_t> keep;
  Vec3 right = length(scene.right) > .01f ? normalized(scene.right) : Vec3{1, 0, 0};
  for (const auto &source : scene.sources) {
    if (keep.size() >= 32 || duration(source.sound) > 0 || !std::isfinite(source.position.x) ||
        !std::isfinite(source.position.y) || !std::isfinite(source.position.z))
      continue;
    Vec3 delta = source.position - scene.listener;
    float distance = length(delta);
    if (!std::isfinite(distance) || distance > 36)
      continue;
    if (std::find(keep.begin(), keep.end(), source.id) != keep.end())
      continue;
    keep.push_back(source.id);
    auto found = std::find_if(voices.begin(), voices.end(),
                              [&](const Voice &v) { return v.loop && v.id == source.id; });
    if (found == voices.end()) {
      if (voices.size() >= maxVoices)
        continue;
      Voice v;
      v.id = source.id;
      v.sound = source.sound;
      v.loop = true;
      v.noise = std::uint32_t(source.id * 747796405u + 2891336453u) | 1u;
      voices.push_back(v);
      found = voices.end() - 1;
    }
    float pan = distance > .01f ? std::clamp(dot(delta * (1 / distance), right), -1.f, 1.f) : 0;
    found->left = std::sqrt((1 - pan) * .5f);
    found->right = std::sqrt((1 + pan) * .5f);
    found->targetGain = bounded(source.gain, 0, 1) / (1 + distance * distance * .055f);
    found->rate = bounded(source.rate, .5f, 8, 2);
  }
  for (auto &v : voices)
    if (v.loop && std::find(keep.begin(), keep.end(), v.id) == keep.end())
      v.targetGain = 0;
}
void AudioMixer::mix(std::span<float> stereo) {
  std::fill(stereo.begin(), stereo.end(), 0);
  for (std::size_t sample = 0; sample + 1 < stereo.size(); sample += 2) {
    worldGain += (float(worldPlaying) - worldGain) * .002f;
    float left = 0, right = 0;
    for (auto &v : voices) {
      double t = double(v.age) / sampleRate;
      if (!v.loop && t >= duration(v.sound)) {
        ++v.age;
        continue;
      }
      float noise = random(v.noise);
      v.filter += .08f * (noise - v.filter);
      float signal = 0;
      if (v.loop) {
        float pulse = std::exp(-float(std::fmod(t * v.rate + double(v.id % 11) / 11, 1)) * 28);
        switch (v.sound) {
        case Sound::Step:
          signal = pulse *
                   (v.filter * .7f + std::sin(float(std::fmod(t * 85, 1) * 2 * pi)) * .16f) * .22f;
          break;
        case Sound::Ladder:
          signal = pulse * (noise * .12f + std::sin(float(std::fmod(t * 740, 1) * 2 * pi)) * .06f);
          break;
        case Sound::Machine:
          signal = v.filter * .026f + std::sin(float(std::fmod(t * 90, 1) * 2 * pi)) * .022f +
                   std::sin(float(std::fmod(t * 121, 1) * 2 * pi)) * .01f;
          break;
        case Sound::Water:
          signal = (noise - v.filter) * .024f *
                   (.85f + .15f * std::sin(float(std::fmod(t * 11, 2 * pi))));
          break;
        case Sound::Fire:
          signal = (v.filter * .12f + noise * .02f) *
                   (1 + .3f * std::sin(float(std::fmod(t * 17, 2 * pi))));
          break;
        case Sound::Motor:
          signal = std::sin(float(std::fmod(t * (45 + v.rate * 12), 1) * 2 * pi)) * .032f +
                   v.filter * .024f;
          break;
        default:
          signal = v.filter * .018f + std::sin(float(std::fmod(t * 57, 1) * 2 * pi)) * .006f;
          break;
        }
        bool step = v.sound == Sound::Step || v.sound == Sound::Ladder;
        signal *= worldGain * (step ? settings.effects : settings.ambience);
      } else {
        float end = duration(v.sound), envelope = std::max(0.f, 1 - float(t) / end);
        switch (v.sound) {
        case Sound::Click:
          signal = std::sin(float(t * 2 * pi * 1050)) * std::exp(-float(t) * 95) * .12f;
          break;
        case Sound::Error:
          signal = std::sin(float(t * 2 * pi * (t < .12 ? 220 : 165))) * envelope * .08f;
          break;
        case Sound::Pickup:
          signal = (noise * .06f + std::sin(float(t * 2 * pi * (900 - 1200 * t))) * .08f) *
                   envelope * envelope;
          break;
        case Sound::Hit:
          signal = (v.filter * .75f + std::sin(float(t * 2 * pi * 75)) * .35f) * envelope *
                   envelope * .24f;
          break;
        case Sound::Shot:
          signal =
              (noise * .45f + std::sin(float(t * 2 * pi * 90)) * .25f) * std::exp(-float(t) * 24);
          break;
        default:
          break;
        }
        float attack = std::min(1.f, float(t) * 1200);
        signal *= attack * settings.effects;
      }
      if (v.loop)
        v.gain += (v.targetGain - v.gain) * .004f;
      signal *= v.gain;
      left += signal * v.left;
      right += signal * v.right;
      ++v.age;
    }
    float gain = settings.muted ? 0 : settings.master;
    stereo[sample] = std::tanh(left * gain);
    stereo[sample + 1] = std::tanh(right * gain);
  }
  std::erase_if(voices, [](const Voice &v) {
    return v.loop ? (v.targetGain == 0 && v.gain < .00001f)
                  : double(v.age) / sampleRate >= duration(v.sound);
  });
}
struct AudioEngine::Impl {
  std::mutex mutex;
  AudioMixer mixer;
  std::atomic<bool> running = false, ready = false;
  std::atomic<std::uint64_t> frames = 0;
  std::thread worker;
#ifdef __linux__
  void *library = nullptr, *pcm = nullptr;
  int (*open)(void **, const char *, int, int) = nullptr;
  int (*params)(void *, int, int, unsigned, unsigned, int, unsigned) = nullptr;
  long (*write)(void *, const void *, unsigned long) = nullptr;
  int (*recover)(void *, int, int) = nullptr;
  int (*drop)(void *) = nullptr;
  int (*close)(void *) = nullptr;
#endif
};
AudioEngine::AudioEngine() : impl(std::make_unique<Impl>()) {}
AudioEngine::~AudioEngine() { stop(); }
bool AudioEngine::start(const std::string &requested) {
  stop();
#ifdef __linux__
  auto &s = *impl;
  s.library = dlopen("libasound.so.2", RTLD_NOW | RTLD_LOCAL);
  if (!s.library)
    return false;
  auto symbol = [&](auto &function, const char *name) {
    void *address = dlsym(s.library, name);
    static_assert(sizeof(function) == sizeof(address));
    std::memcpy(&function, &address, sizeof(address));
    return function != nullptr;
  };
  if (!symbol(s.open, "snd_pcm_open") || !symbol(s.params, "snd_pcm_set_params") ||
      !symbol(s.write, "snd_pcm_writei") || !symbol(s.recover, "snd_pcm_recover") ||
      !symbol(s.drop, "snd_pcm_drop") || !symbol(s.close, "snd_pcm_close")) {
    stop();
    return false;
  }
  const char *env = std::getenv("MEGA_AUDIO_DEVICE");
  std::string device = requested.empty() ? (env && *env ? env : "default") : requested;
  // ABI values from ALSA's public pcm.h: playback=0, nonblocking=1, S16_LE=2, interleaved=3.
  if (s.open(&s.pcm, device.c_str(), 0, 1) < 0 ||
      s.params(s.pcm, 2, 3, 2, AudioMixer::sampleRate, 1, 50000) < 0) {
    stop();
    return false;
  }
  s.frames = 0;
  s.ready = true;
  s.running = true;
  s.worker = std::thread([&s] {
    std::array<float, 960> mixed{};
    std::array<std::int16_t, 960> output{};
    while (s.running) {
      {
        std::lock_guard lock(s.mutex);
        s.mixer.mix(mixed);
      }
      for (std::size_t i = 0; i < output.size(); ++i)
        output[i] = std::int16_t(std::clamp(mixed[i], -1.f, 1.f) * 32760);
      unsigned long offset = 0;
      while (offset < 480 && s.running) {
        long wrote = s.write(s.pcm, output.data() + offset * 2, 480 - offset);
        if (wrote > 0) {
          offset += static_cast<unsigned long>(wrote);
          s.frames += wrote;
        } else if (wrote == -11 || wrote == 0)
          std::this_thread::sleep_for(std::chrono::milliseconds(2));
        else if (wrote != -32 || s.recover(s.pcm, int(wrote), 1) < 0) {
          s.ready = false;
          s.running = false;
        }
      }
      // Null/test devices do not pace writes; also bounds busy-looping after a recovery.
      std::this_thread::sleep_for(std::chrono::milliseconds(8));
    }
    s.ready = false;
  });
  return true;
#else
  (void)requested;
  return false;
#endif
}
void AudioEngine::stop() {
  impl->running = false;
  if (impl->worker.joinable())
    impl->worker.join();
#ifdef __linux__
  if (impl->pcm) {
    impl->drop(impl->pcm);
    impl->close(impl->pcm);
    impl->pcm = nullptr;
  }
  if (impl->library) {
    dlclose(impl->library);
    impl->library = nullptr;
  }
#endif
  impl->ready = false;
  {
    std::lock_guard lock(impl->mutex);
    impl->mixer.reset();
  }
}
bool AudioEngine::available() const { return impl->ready; }
void AudioEngine::reset() {
  std::lock_guard lock(impl->mutex);
  impl->mixer.reset();
}
std::uint64_t AudioEngine::framesWritten() const { return impl->frames; }
void AudioEngine::configure(AudioSettings s) {
  std::lock_guard lock(impl->mutex);
  impl->mixer.configure(s);
}
void AudioEngine::scene(const AudioScene &s) {
  std::lock_guard lock(impl->mutex);
  impl->mixer.scene(s);
}
void AudioEngine::play(Sound sound, float gain, float pan) {
  if (!impl->running)
    return;
  std::lock_guard lock(impl->mutex);
  impl->mixer.play(sound, gain, pan);
}
} // namespace mega
