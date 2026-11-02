#include "mega/audio.hpp"
#include "mega/preferences.hpp"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <thread>
using namespace mega;
int passed = 0, failed = 0;
void require(bool b, const char *m) {
  if (!b)
    throw std::runtime_error(m);
}
void test(const char *name, std::function<void()> fn) {
  try {
    fn();
    ++passed;
    std::cout << "PASS " << name << '\n';
  } catch (const std::exception &e) {
    ++failed;
    std::cerr << "FAIL " << name << ": " << e.what() << '\n';
  }
}
double energy(const std::vector<float> &v, int channel = -1) {
  double n = 0;
  for (std::size_t i = channel < 0 ? 0 : channel; i < v.size(); i += channel < 0 ? 1 : 2)
    n += double(v[i]) * v[i];
  return n;
}
std::vector<float> render(AudioMixer &m, int frames = 12000) {
  std::vector<float> v(frames * 2);
  m.mix(v);
  return v;
}
AudioScene room(Vec3 position = {0, 0, 0}, Sound sound = Sound::Machine) {
  AudioScene s;
  s.playing = true;
  s.sources.push_back({1, sound, position, 1, 2});
  return s;
}
void wav(const std::string &path, const std::vector<float> &v) {
  std::ofstream f(path, std::ios::binary);
  auto put = [&](std::uint32_t n, int bytes) {
    for (int i = 0; i < bytes; ++i)
      f.put(char((n >> (8 * i)) & 255));
  };
  f.write("RIFF", 4);
  put(36 + v.size() * 2, 4);
  f.write("WAVEfmt ", 8);
  put(16, 4);
  put(1, 2);
  put(2, 2);
  put(48000, 4);
  put(192000, 4);
  put(4, 2);
  put(16, 2);
  f.write("data", 4);
  put(v.size() * 2, 4);
  for (float s : v)
    put(std::uint16_t(std::int16_t(std::clamp(s, -1.f, 1.f) * 32760)), 2);
  require(bool(f), "Audio capture failed");
}
int main(int argc, char **argv) {
  if (argc > 1 && std::string(argv[1]) == "--device") {
    AudioEngine engine;
    if (!engine.start(argc > 2 ? argv[2] : "null")) {
      std::cout << "Audio backend unavailable; silent operation remains supported\n";
      return 77;
    }
    engine.scene(room());
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    require(engine.available() && engine.framesWritten() >= 480,
            "Audio thread did not write output");
    engine.reset();
    require(engine.available(), "World sound reset closed the device");
    engine.stop();
    require(!engine.available(), "Audio device stayed open");
    require(engine.start("null"), "Audio device could not restart");
    engine.stop();
    std::cout << "PASS nonblocking native output, stop and restart\n";
    return 0;
  }
  test("Silent scene remains exactly silent", [] {
    AudioMixer m;
    require(energy(render(m)) == 0, "Unrequested audio");
  });
  test("One-shot effects expire and do not depend on block size", [] {
    AudioMixer a, b;
    a.play(Sound::Pickup);
    b.play(Sound::Pickup);
    auto whole = render(a, 24000);
    std::vector<float> chunks(48000);
    for (std::size_t i = 0; i < chunks.size(); i += 960)
      b.mix(std::span(chunks).subspan(i, std::min<std::size_t>(960, chunks.size() - i)));
    require(whole == chunks, "Effect duration changes with buffer size");
    require(energy(whole) > 0 && a.voiceCount() == 0 && energy(render(a)) == 0,
            "Effect repeated or failed to finish");
  });
  test("Spatial sources pan toward their actual side", [] {
    AudioMixer a, b;
    a.scene(room({3, 0, 0}));
    b.scene(room({-3, 0, 0}));
    auto right = render(a), left = render(b);
    require(energy(right, 1) > energy(right, 0) * 10 && energy(left, 0) > energy(left, 1) * 10,
            "Stereo direction reversed or centered");
  });
  test("Nearby machinery is louder than distant machinery", [] {
    AudioMixer a, b;
    a.scene(room({1, 0, 0}));
    b.scene(room({15, 0, 0}));
    require(energy(render(a)) > energy(render(b)) * 20, "Distance attenuation missing");
  });
  test("Paused scenes fade their world audio without muting interface effects", [] {
    AudioMixer m;
    auto s = room();
    m.scene(s);
    render(m);
    s.playing = false;
    m.scene(s);
    render(m, 24000);
    require(energy(render(m)) < 1e-12, "Paused machinery kept sounding");
    m.play(Sound::Click);
    require(energy(render(m)) > .00001, "Paused interface feedback muted");
  });
  test("Effects and ambience have independent controls", [] {
    AudioMixer m;
    AudioSettings s;
    s.ambience = 0;
    m.configure(s);
    m.scene(room());
    require(energy(render(m)) == 0, "Ambient bus ignored mute");
    m.play(Sound::Click);
    require(energy(render(m)) > 0, "Effects incorrectly muted with ambience");
    s.effects = 0;
    s.ambience = 1;
    m.configure(s);
    m.scene(room());
    m.play(Sound::Click);
    require(energy(render(m)) > 0, "Ambience incorrectly muted with effects");
  });
  test("Master mute silences both buses and recovers", [] {
    AudioMixer m;
    AudioSettings s;
    s.muted = true;
    m.configure(s);
    m.scene(room());
    m.play(Sound::Shot);
    require(energy(render(m)) == 0, "Master mute leaked audio");
    s.muted = false;
    m.configure(s);
    require(energy(render(m)) > 0, "Unmute did not restore ongoing scene");
  });
  test("Voice caps, stopped sources and nonfinite inputs stay bounded", [] {
    AudioMixer m;
    AudioScene s;
    s.playing = true;
    for (int i = 0; i < 200; ++i)
      s.sources.push_back({std::uint64_t(i), Sound::Machine, {0, 0, 0}, 1, 2});
    m.scene(s);
    for (int i = 0; i < 200; ++i)
      m.play(Sound::Shot);
    auto v = render(m);
    require(m.voiceCount() <= AudioMixer::maxVoices, "Unbounded voices");
    for (float x : v)
      require(std::isfinite(x) && std::abs(x) <= 1, "Invalid/clipped samples");
    m.scene({});
    render(m, 24000);
    require(m.voiceCount() == 0, "Stopped sources never released");
    AudioSettings bad;
    bad.master = std::numeric_limits<float>::quiet_NaN();
    m.configure(bad);
    m.play(Sound::Click, std::numeric_limits<float>::infinity());
    for (float x : render(m))
      require(std::isfinite(x), "Nonfinite volume poisoned stream");
  });
  test("Reset does not replay old events", [] {
    AudioMixer m;
    m.play(Sound::Shot);
    m.scene(room());
    m.reset();
    require(m.voiceCount() == 0 && energy(render(m)) == 0, "Reset replayed historical audio");
  });
  auto root = std::filesystem::temp_directory_path() /
              ("mega-preferences-" +
               std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(root);
  test("Preferences persist independently of saved worlds", [&] {
    auto file = root / "preferences.json";
    Preferences p;
    p.language = "fr";
    p.audio.master = .35f;
    p.audio.muted = true;
    p.visibleFloors = 192;
    p.fov = 90;
    p.sensitivity = .004f;
    p.save(file);
    auto q = Preferences::load(file);
    require(p == q, "Settings changed after restart");
    p.language = "zh";
    p.save(file);
    require(Preferences::load(file).language == "zh" &&
                Preferences::load(file.string() + ".bak").language == "fr",
            "Atomic save or previous backup lost");
  });
  test("Bad settings do not overwrite the file or affect defaults", [&] {
    auto file = root / "broken.json";
    {
      std::ofstream f(file);
      f << "{broken";
    }
    bool refused = false;
    try {
      Preferences::load(file);
    } catch (...) {
      refused = true;
    }
    require(refused, "Malformed settings silently accepted");
    std::ifstream f(file);
    std::string text;
    std::getline(f, text);
    require(text == "{broken", "Reading settings mutated the file");
    require(Preferences::load(root / "missing.json") == Preferences{},
            "Missing settings did not use defaults");
  });
  test("Out-of-range preferences normalize to supported controls", [] {
    Preferences p;
    p.language = "unknown";
    p.visibleFloors = 100000;
    p.audio.master = 20;
    p.audio.effects = -3;
    p.fov = 200;
    p.sensitivity = 0;
    p.normalize();
    require(p.language == "en" && p.visibleFloors == 96 && p.audio.master == 1 &&
                p.audio.effects == 0 && p.fov == 100 && p.sensitivity >= .0006f,
            "Invalid settings escaped normalization");
  });
  std::filesystem::remove_all(root);
  for (int i = 1; i + 1 < argc; ++i)
    if (std::string(argv[i]) == "--wav") {
      AudioMixer m;
      std::vector<float> samples;
      auto append = [&](int count) {
        auto part = render(m, count);
        samples.insert(samples.end(), part.begin(), part.end());
      };
      m.scene(room({-2, 0, 0}));
      append(48000);
      m.play(Sound::Click);
      append(24000);
      m.scene(room({2, 0, 0}, Sound::Step));
      append(96000);
      m.play(Sound::Pickup);
      append(24000);
      m.scene(room({0, 0, 3}, Sound::Water));
      append(48000);
      m.scene({});
      append(24000);
      wav(argv[i + 1], samples);
    }
  std::cout << passed << " passed, " << failed << " failed\n";
  return failed ? 1 : 0;
}
