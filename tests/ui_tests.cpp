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
    // A viewed resident stays at the centre of the uncovered screen, even at close zoom.
    for (float width : {1000.f, 1200.f, 1920.f})
      for (float distance : {2.5f, 44.f, 200.f})
        for (float slide : {0.f, .5f, 1.f}) {
          Vec3 target{3, 6, -2};
          auto eye = observerEye(target, .7f, .2f, distance);
          auto matrix = mul(observerProjection(width, 720, 460 * slide), lookAt(eye, target));
          float clipX =
              matrix[0] * target.x + matrix[4] * target.y + matrix[8] * target.z + matrix[12];
          float clipW =
              matrix[3] * target.x + matrix[7] * target.y + matrix[11] * target.z + matrix[15];
          float pixelX = (clipX / clipW + 1) * width * .5f;
          require(std::abs(pixelX - (width - 460 * slide) * .5f) < .005f,
                  "Opening the sidebar turned the camera away from its target");
        }
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
    // Dense contact can alternate the body's final millimetres without rotating the view.
    for (float rate : {30.f, 60.f, 144.f}) {
      FirstPersonFollow follow;
      Vec3 origin{50, 0, 50};
      follow.update(origin, 1 / rate, true);
      float largest = 0;
      for (int n = 0; n < int(rate * 4); ++n) {
        Vec3 contact = origin + Vec3{n % 2 ? .035f : -.035f, 0, 0};
        auto eye = follow.update(contact, 1 / rate, true);
        largest = std::max(largest, std::abs(eye.x - origin.x));
        require(eye.y == contact.y && length(eye - contact) <= .1001f,
                "Contact filtering left the real body footprint");
      }
      require(largest < .012f, "Crowd contact still shakes the first-person camera");
      for (int n = 0; n < int(rate * 2); ++n) {
        Vec3 walking = origin + Vec3{7.2f * n / rate, 0, 0};
        auto eye = follow.update(walking, 1 / rate, true);
        require(length(eye - walking) <= .1001f, "Camera filtering added excessive running lag");
      }
      Vec3 falling{55, -7.25f, 51};
      require(length(follow.update(falling, 1 / rate, false) - falling) < .0001f,
              "Camera smoothing hid a real fall");
      Vec3 changed{150, 300, -150};
      require(length(follow.update(changed, 1 / rate, true) - changed) < .0001f,
              "Camera interpolated across a resident change or teleport");
      follow.reset();
      require(length(follow.update(origin, 1 / rate, true) - origin) < .0001f,
              "Camera reset retained another body's offset");
    }
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
    for (const auto &language : {"en", "fr", "zh"}) {
      locale.language = language;
      auto hint = locale("hud.controls") + " · " + name;
      for (float width : {220.f, 420.f, 720.f}) {
        ui.begin(1000, 720, 30, 30, false, .016f);
        float measured = ui.wrappedHeight(width, hint, 14);
        float end = ui.wrap(20, 50, width, hint, 14, hex(0xffffff));
        require(measured > 0 && std::abs(end - 50 - measured) < .01f,
                "Multilingual prompt background does not match rendered wrapping");
        ui.end();
      }
    }
    ui.begin(1000, 720, 30, 30, true, .016f);
    ui.enabled = false;
    require(!ui.button("disabled", {20, 20, 100, 34}, "Disabled") && ui.activations == 0,
            "Disabled button emitted an activation");
    ui.enabled = true;
    ui.clip({100, 100, 100, 100});
    require(!ui.button("clipped", {20, 20, 100, 34}, "Clipped") && ui.activations == 0,
            "Hidden button emitted an activation");
    ui.clip({0, 0, 1000, 720});
    require(ui.button("active", {20, 20, 100, 34}, "Active") && ui.activations == 1,
            "Enabled button failed to emit exactly one activation");
    require(!ui.button("overlap", {20, 20, 100, 34}, "Overlap") && ui.activations == 1,
            "A consumed click emitted a second activation");
    ui.end();
    ui.begin(1000, 720, 30, 30, true, .016f);
    require(ui.activations == 0, "Button feedback carried into another frame");
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
    std::cout << "Multilingual text, clipped input, crowd-contact first-person camera and "
                 "continuous observer camera passed\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
