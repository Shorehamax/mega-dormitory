#pragma once
#include "renderer.hpp"
#include <map>
#include <string>
#include <unordered_map>
namespace mega {
std::vector<std::uint32_t> decodeUTF8(const std::string &);
class Locale {
  std::map<std::string, std::map<std::string, std::string>> tables;

public:
  std::string language = "en";
  explicit Locale(const std::filesystem::path &);
  std::string operator()(const std::string &key) const;
  std::string charset() const;
};
struct Glyph {
  float u0 = 0, v0 = 0, u1 = 0, v1 = 0, x = 0, y = 0, w = 0, h = 0, advance = 0;
};
class Font {
public:
  int atlasSize = 2048;
  std::vector<unsigned char> atlas;
  std::unordered_map<std::uint32_t, Glyph> glyphs;
  Font(const std::filesystem::path &, const std::string &characters);
  float width(const std::string &, float size) const;
};
class UI {
  Font &font;
  Rect currentClip{};
  float mx = 0, my = 0;
  bool clicked = false;
  std::map<std::string, float> hover;
  std::string activeInput;
  std::vector<unsigned int> characters;
  bool backspace = false;
  bool enter = false;

public:
  std::vector<Vertex> vertices;
  std::vector<Batch> batches;
  int width = 1440, height = 900;
  float dt = .016f;
  bool enabled = true;
  double scroll = 0;
  explicit UI(Font &f) : font(f) {}
  void begin(int, int, double, double, bool, float);
  void clip(Rect);
  void end();
  void fill(Rect, Color, float radius = 0);
  void line(float, float, float, float, Color, float thickness = 1);
  void text(float x, float y, const std::string &, float size, Color);
  float textWidth(const std::string &s, float size) const { return font.width(s, size); }
  float wrap(float, float, float, const std::string &, float, Color);
  bool button(const std::string &id, Rect, const std::string &, bool primary = false,
              bool selected = false);
  bool input(const std::string &id, Rect, std::string &, bool numeric = false);
  bool hit(Rect r) const {
    return mx >= currentClip.x && mx <= currentClip.x + currentClip.w && my >= currentClip.y &&
           my <= currentClip.y + currentClip.h && mx >= r.x && mx <= r.x + r.w && my >= r.y &&
           my <= r.y + r.h;
  }
  void charEvent(unsigned int c) { characters.push_back(c); }
  void keyEvent(int key) {
    if (key == GLFW_KEY_ESCAPE)
      activeInput.clear();
    if (key == GLFW_KEY_BACKSPACE)
      backspace = true;
    if (key == GLFW_KEY_ENTER)
      enter = true;
  }
  bool typing() const { return !activeInput.empty(); }
  void blur() { activeInput.clear(); }
};
Color hex(unsigned value, float alpha = 1);
} // namespace mega
