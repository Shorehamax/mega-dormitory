#include "mega/camera.hpp"
#include "mega/ui.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace mega;
static void require(bool ok, const char *message) {
  if (!ok)
    throw std::runtime_error(message);
}
int main() {
  try {
    auto target = Vec3{2, 4, 6};
    require(length(observerEye(target, 0, 0, 5) - Vec3{2, 4, 11}) < .0001f,
            "Eye-level observer retained the old hidden height bias");
    require(length(observerEye(target, 1.5707963f, 0, 5) - Vec3{7, 4, 6}) < .0001f,
            "Observer yaw is not relative to its target");
    require(std::abs(length(observerEye(target, .7f, -.4f, 2.5f) - target) - 2.5f) < .0001f,
            "Close orbit does not preserve zoom distance");
    int floor = 10;
    float offset = 2.9f;
    moveObserverHeight(floor, offset, .2f, 2000);
    require(floor == 11 && std::abs(offset - .1f) < .0001f,
            "Upward observation jumped at a floor boundary");
    moveObserverHeight(floor, offset, -.2f, 2000);
    require(floor == 10 && std::abs(offset - 2.9f) < .0001f,
            "Downward observation jumped at a floor boundary");
    moveObserverHeight(floor, offset, -1000, 20);
    require(floor == 0 && offset == 0, "Observer moved beneath its floor bounds");
    moveObserverHeight(floor, offset, 1000, 20);
    require(floor == 19 && offset == 0, "Observer moved past the highest floor");
    Locale locale(std::filesystem::path(MEGA_ASSETS) / "locales");
    Font font(std::filesystem::path(MEGA_ASSETS) / "fonts/NotoSansCJK-Regular.ttc",
              locale.charset());
    auto before = font.glyphs.at('A');
    font.dirty = false;
    const std::string name = "Élodie — 龍龜";
    require(font.width(name, 20) > 60 && font.dirty,
            "new CJK save-name glyphs were not rasterized");
    auto glyph = font.glyphs.at(0x9f8d);
    require(glyph.w > 0 && glyph.u0 != font.glyphs.at('?').u0,
            "supported CJK character uses fallback");
    require(before.u0 == font.glyphs.at('A').u0 && before.v0 == font.glyphs.at('A').v0,
            "adding names moved existing glyphs");
    font.dirty = false;
    font.width(name, 20);
    require(!font.dirty, "unchanged names repeatedly rebuild the GPU atlas");
    UI ui(font);
    ui.begin(1000, 720, 30, 30, true, .016f);
    std::string input = "Long world name with many letters to test cursor visibility";
    ui.input("name", {20, 20, 160, 34}, input);
    ui.end();
    require(ui.typing(), "click did not focus save-name input");
    ui.begin(1000, 720, 30, 30, false, .016f);
    ui.charEvent(0x9f8d);
    ui.input("name", {20, 20, 160, 34}, input);
    ui.end();
    require(input.ends_with("龍"), "Unicode input lost a character");
    // Geometry can extend left while the batch scissor contains it in the field.
    bool clipped = false;
    for (auto &batch : ui.batches)
      clipped |= batch.clip.x >= 20 && batch.clip.w <= 160 && batch.count > 0;
    require(clipped, "long-name text escaped its field clip");
    ui.begin(1000, 720, 30, 30, false, .016f);
    ui.keyEvent(GLFW_KEY_BACKSPACE);
    ui.input("name", {20, 20, 160, 34}, input);
    ui.end();
    require(input.ends_with("visibility"), "backspace split a UTF-8 character");
    std::cout << "Multilingual text, clipped input and continuous observer camera passed\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
