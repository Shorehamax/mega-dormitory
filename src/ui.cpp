#include "mega/ui.hpp"
#include "json.hpp"
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
#include <cmath>
#include <fstream>
#include <set>
namespace mega {
Color hex(unsigned v, float a) {
  return {float((v >> 16) & 255) / 255, float((v >> 8) & 255) / 255, float(v & 255) / 255, a};
}
std::vector<std::uint32_t> decodeUTF8(const std::string &s) {
  std::vector<std::uint32_t> out;
  for (std::size_t i = 0; i < s.size();) {
    unsigned char c = s[i++];
    std::uint32_t cp = c;
    int n = 0;
    if (c >= 0xf0) {
      cp = c & 7;
      n = 3;
    } else if (c >= 0xe0) {
      cp = c & 15;
      n = 2;
    } else if (c >= 0xc0) {
      cp = c & 31;
      n = 1;
    } else if (c >= 0x80) {
      out.push_back('?');
      continue;
    }
    bool valid = i + n <= s.size();
    for (int j = 0; j < n && i < s.size(); ++j) {
      unsigned char next = s[i++];
      if ((next & 0xc0) != 0x80)
        valid = false;
      cp = (cp << 6) | (next & 63);
    }
    out.push_back(valid ? cp : '?');
  }
  return out;
}
static std::string encode(unsigned cp) {
  std::string s;
  if (cp < 0x80)
    s += char(cp);
  else if (cp < 0x800) {
    s += char(0xc0 | (cp >> 6));
    s += char(0x80 | (cp & 63));
  } else if (cp < 0x10000) {
    s += char(0xe0 | (cp >> 12));
    s += char(0x80 | ((cp >> 6) & 63));
    s += char(0x80 | (cp & 63));
  } else {
    s += char(0xf0 | (cp >> 18));
    s += char(0x80 | ((cp >> 12) & 63));
    s += char(0x80 | ((cp >> 6) & 63));
    s += char(0x80 | (cp & 63));
  }
  return s;
}
Locale::Locale(const std::filesystem::path &folder) {
  for (auto lang : {"en", "fr", "zh"}) {
    std::ifstream f(folder / (std::string(lang) + ".json"));
    if (!f)
      throw std::runtime_error("Missing localization catalogue");
    nlohmann::json j;
    f >> j;
    tables[lang] = j.get<std::map<std::string, std::string>>();
  }
  for (auto &[k, v] : tables["en"])
    if (!tables["fr"].contains(k) || !tables["zh"].contains(k))
      throw std::runtime_error("Incomplete language catalogue: " + k);
}
std::string Locale::operator()(const std::string &key) const {
  auto table = tables.find(language);
  if (table == tables.end())
    table = tables.find("en");
  auto it = table->second.find(key);
  return it == table->second.end() ? key : it->second;
}
std::string Locale::charset() const {
  std::string s = "陈李王张刘马丁杜布瓦洛朗贝尔纳莫罗史密斯布朗威尔逊泰勒罗伊明雨桐浩然思雅"
                  "子轩若曦俊杰安然宇航诗涵辰逸宁志远亦菲博文丽0123456789¥€→←↑↓×−+·…";
  for (auto &[lang, table] : tables)
    for (auto &[key, value] : table)
      s += value;
  return s;
}
Font::Font(const std::filesystem::path &file, const std::string &characters) {
  std::ifstream f(file, std::ios::binary);
  if (!f)
    throw std::runtime_error("Missing font");
  source.assign(std::istreambuf_iterator<char>(f), {});
  atlas.assign(atlasSize * atlasSize, 0);
  for (int y = 0; y < 3; ++y)
    for (int x = 0; x < 3; ++x)
      atlas[y * atlasSize + x] = 255;
  std::string initial;
  for (unsigned c = 32; c < 591; ++c)
    initial += encode(c);
  ensure(initial + characters);
}
void Font::ensure(const std::string &characters) {
  auto codepoints = decodeUTF8(characters);
  bool missing = false;
  for (auto cp : codepoints)
    missing |= !glyphs.contains(cp);
  if (!missing)
    return;
  stbtt_fontinfo info;
  if (!stbtt_InitFont(&info, source.data(), stbtt_GetFontOffsetForIndex(source.data(), 2)))
    throw std::runtime_error("Cannot decode CJK font");
  float scale = stbtt_ScaleForMappingEmToPixels(&info, 40);
  for (auto cp : codepoints) {
    if (glyphs.contains(cp))
      continue;
    // Unsupported code points use the fallback without repeatedly re-rasterizing.
    if (!stbtt_FindGlyphIndex(&info, cp) && cp != '?') {
      glyphs[cp] = glyphs.at('?');
      continue;
    }
    int w, h, xoff, yoff, advance, lsb;
    auto *bitmap = stbtt_GetCodepointBitmap(&info, 0, scale, cp, &w, &h, &xoff, &yoff);
    stbtt_GetCodepointHMetrics(&info, cp, &advance, &lsb);
    if (cursorX + w + 2 >= atlasSize) {
      cursorX = 4;
      cursorY += rowHeight + 3;
      rowHeight = 0;
    }
    if (cursorY + h + 2 >= atlasSize) {
      stbtt_FreeBitmap(bitmap, nullptr);
      glyphs[cp] = glyphs.at('?');
      continue;
    }
    for (int yy = 0; yy < h; ++yy)
      for (int xx = 0; xx < w; ++xx)
        atlas[(cursorY + yy) * atlasSize + cursorX + xx] = bitmap[yy * w + xx];
    Glyph g;
    g.u0 = float(cursorX) / atlasSize;
    g.v0 = float(cursorY) / atlasSize;
    g.u1 = float(cursorX + w) / atlasSize;
    g.v1 = float(cursorY + h) / atlasSize;
    g.x = float(xoff);
    g.y = float(yoff);
    g.w = float(w);
    g.h = float(h);
    g.advance = advance * scale;
    glyphs[cp] = g;
    cursorX += w + 3;
    rowHeight = std::max(rowHeight, h);
    stbtt_FreeBitmap(bitmap, nullptr);
    dirty = true;
  }
}
float Font::width(const std::string &s, float size) {
  ensure(s);
  float n = 0;
  for (auto c : decodeUTF8(s)) {
    auto it = glyphs.find(c);
    if (it == glyphs.end())
      it = glyphs.find('?');
    n += it->second.advance * size / 40;
  }
  return n;
}
void UI::begin(int w, int h, double x, double y, bool click, float delta) {
  width = w;
  height = h;
  mx = float(x);
  my = float(y);
  clicked = click;
  dt = delta;
  enabled = true;
  vertices.clear();
  batches.clear();
  clip({0, 0, float(w), float(h)});
}
void UI::clip(Rect r) {
  currentClip = r;
  if (!batches.empty())
    batches.back().count = std::uint32_t(vertices.size()) - batches.back().first;
  batches.push_back({std::uint32_t(vertices.size()), 0, r});
}
void UI::end() {
  if (!batches.empty())
    batches.back().count = std::uint32_t(vertices.size()) - batches.back().first;
  characters.clear();
  backspace = enter = false;
  scroll = 0;
}
void UI::fill(Rect r, Color c, float radius) {
  float u = .5f / font.atlasSize;
  auto vertex = [&](float x, float y) { return Vertex{{x, y, 0}, c, u, u, {0, 0, 1}}; };
  if (radius <= 0) {
    Vertex p[] = {vertex(r.x, r.y), vertex(r.x + r.w, r.y), vertex(r.x + r.w, r.y + r.h),
                  vertex(r.x, r.y + r.h)};
    for (int i : {0, 1, 2, 0, 2, 3})
      vertices.push_back(p[i]);
    return;
  }
  radius = std::min(radius, std::min(r.w, r.h) / 2);
  std::vector<Vertex> edge;
  for (int corner = 0; corner < 4; ++corner) {
    float cx = r.x + (corner == 0 || corner == 3 ? radius : r.w - radius),
          cy = r.y + (corner < 2 ? radius : r.h - radius);
    for (int s = 0; s <= 6; ++s) {
      float a = 3.14159265f + (corner * 1.5707963f) + s * 1.5707963f / 6;
      edge.push_back(vertex(cx + std::cos(a) * radius, cy + std::sin(a) * radius));
    }
  }
  auto centre = vertex(r.x + r.w / 2, r.y + r.h / 2);
  for (std::size_t i = 0; i < edge.size(); ++i) {
    vertices.push_back(centre);
    vertices.push_back(edge[i]);
    vertices.push_back(edge[(i + 1) % edge.size()]);
  }
}
void UI::line(float x, float y, float x2, float y2, Color c, float t) {
  float dx = x2 - x, dy = y2 - y, len = std::sqrt(dx * dx + dy * dy);
  if (len < .001)
    return;
  float nx = -dy / len * t * .5f, ny = dx / len * t * .5f, u = .5f / font.atlasSize;
  Vertex p[] = {{{x + nx, y + ny, 0}, c, u, u, {}},
                {{x2 + nx, y2 + ny, 0}, c, u, u, {}},
                {{x2 - nx, y2 - ny, 0}, c, u, u, {}},
                {{x - nx, y - ny, 0}, c, u, u, {}}};
  for (int i : {0, 1, 2, 0, 2, 3})
    vertices.push_back(p[i]);
}
void UI::text(float x, float y, const std::string &s, float size, Color c) {
  font.ensure(s);
  float scale = size / 40;
  for (auto cp : decodeUTF8(s)) {
    auto it = font.glyphs.find(cp);
    if (it == font.glyphs.end())
      it = font.glyphs.find('?');
    auto &g = it->second;
    float left = x + g.x * scale, top = y + size * .82f + g.y * scale;
    Vertex p[] = {{{left, top, 0}, c, g.u0, g.v0, {}},
                  {{left + g.w * scale, top, 0}, c, g.u1, g.v0, {}},
                  {{left + g.w * scale, top + g.h * scale, 0}, c, g.u1, g.v1, {}},
                  {{left, top + g.h * scale, 0}, c, g.u0, g.v1, {}}};
    for (int i : {0, 1, 2, 0, 2, 3})
      vertices.push_back(p[i]);
    x += g.advance * scale;
  }
}
float UI::wrap(float x, float y, float max, const std::string &s, float size, Color c) {
  std::string line;
  for (auto cp : decodeUTF8(s)) {
    if (cp == '\n') {
      text(x, y, line, size, c);
      y += size * 1.5f;
      line.clear();
      continue;
    }
    std::string ch = encode(cp);
    if (font.width(line + ch, size) > max && !line.empty()) {
      auto space = line.find_last_of(' ');
      if (space != std::string::npos && space > 0) {
        text(x, y, line.substr(0, space), size, c);
        line = line.substr(space + 1);
      } else {
        text(x, y, line, size, c);
        line.clear();
      }
      y += size * 1.5f;
    }
    if (cp != ' ' || !line.empty())
      line += ch;
  }
  if (!line.empty()) {
    text(x, y, line, size, c);
    y += size * 1.5f;
  }
  return y;
}
bool UI::button(const std::string &id, Rect r, const std::string &label, bool primary,
                bool selected) {
  bool over = enabled && hit(r);
  float &h = hover[id];
  h += (float(over) - h) * std::min(1.f, dt * 16);
  Color bg = primary ? hex(0xa5cbb9) : selected ? hex(0x32423e) : hex(0x20262f);
  bg.r += h * .04f;
  bg.g += h * .04f;
  bg.b += h * .04f;
  fill(r, bg, 6);
  if (selected || over) {
    line(r.x + 6, r.y + r.h - 1, r.x + r.w - 6, r.y + r.h - 1, hex(0xa5cbb9, .6f));
  }
  float size = 15;
  if (font.width(label, size) > r.w - 16)
    size = std::max(10.f, 15 * (r.w - 16) / font.width(label, 15));
  text(r.x + (r.w - font.width(label, size)) / 2, r.y + (r.h - size) / 2, label, size,
       primary ? hex(0x162226) : hex(0xd9e1df, enabled ? 1 : .45f));
  if (over && clicked) {
    clicked = false;
    activeInput.clear();
    return true;
  }
  return false;
}
bool UI::input(const std::string &id, Rect r, std::string &value, bool numeric) {
  fill(r, hex(activeInput == id ? 0x22383d : 0x132126), 5);
  if (enabled && hit(r) && clicked) {
    activeInput = id;
    clicked = false;
  }
  bool changed = false;
  if (activeInput == id && enabled) {
    if (backspace && !value.empty()) {
      std::size_t start = value.size() - 1;
      while (start > 0 && (static_cast<unsigned char>(value[start]) & 0xc0) == 0x80)
        --start;
      value.erase(start);
      changed = true;
      backspace = false;
    }
    for (auto c : characters)
      if ((!numeric || (c >= '0' && c <= '9') || c == '.' || (c == '-' && value.empty())) &&
          value.size() < 512) {
        value += encode(c);
        changed = true;
      }
    characters.clear();
    if (enter) {
      activeInput.clear();
      enter = false;
    }
    line(r.x + 4, r.y + r.h - 1, r.x + r.w - 4, r.y + r.h - 1, hex(0xa5cbb9));
  }
  Rect previous = currentClip;
  float left = std::max(r.x, previous.x), top = std::max(r.y, previous.y);
  clip({left, top, std::max(0.f, std::min(r.x + r.w, previous.x + previous.w) - left),
        std::max(0.f, std::min(r.y + r.h, previous.y + previous.h) - top)});
  auto displayed = value + (activeInput == id ? "_" : "");
  float shift = activeInput == id ? std::min(0.f, r.w - 24 - font.width(displayed, 17)) : 0;
  text(r.x + 12 + shift, r.y + (r.h - 17) / 2, displayed, 17, hex(0xe2e9e6));
  clip(previous);
  return changed;
}
} // namespace mega
