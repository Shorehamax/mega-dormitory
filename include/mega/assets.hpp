#pragma once
#include "renderer.hpp"
#include <map>
namespace mega {
struct MeshPart {
  std::string name;
  std::size_t first = 0, count = 0;
  Vec3 pivot{};
};
struct RigNode {
  std::string name;
  int parent = -1;
  Mat4 local = identity(), bindWorld = identity();
};
struct SkinBinding {
  std::vector<int> nodes;
  std::vector<Mat4> inverseBind;
};
struct SkinVertex {
  int skin = -1;
  std::array<unsigned, 8> joints{};
  std::array<float, 8> weights{};
};
enum class CharacterPose { Idle, Walk, Sit, Eat, Sleep, Climb, Carry, Work, Fight, Surrender };
struct CharacterStyle {
  Color shirt{.3f, .4f, .5f, 1}, trousers{.16f, .2f, .25f, 1};
  Color skin{1, 1, 1, 1};
  float height = 1.75f;
  int pattern = 0;
};
struct MeshAsset {
  std::vector<Vertex> vertices;
  Vec3 minimum{}, maximum{};
  std::vector<MeshPart> parts;
  std::vector<RigNode> rig;
  std::vector<SkinBinding> skins;
  std::vector<SkinVertex> influences;
  std::vector<Vec3> garments; // fabric mask (-1 for other materials), waist, footwear
};
class AssetLibrary {
public:
  static constexpr int tileSize = 768, columns = 5, atlasSize = tileSize * columns;
  std::array<std::vector<unsigned char>, 3> textures;
  std::map<std::string, MeshAsset> meshes;
  int materialCount = 0;
  AssetLibrary();
  void load(const std::string &name, const std::filesystem::path &file);
  void loadDormitory(const std::filesystem::path &directory);
  void prepareCharacter(const std::string &name);
  void makeCharacterLod(const std::string &name, float cellSize);
  void append(std::vector<Vertex> &out, const std::string &name, Vec3 position, float scale = 1,
              float yaw = 0, Color tint = {1, 1, 1, 1}, float opening = 0) const;
  // Bone offsets are model-space rotations around the animated joint pivot.
  std::vector<Mat4> pose(const std::string &name,
                         const std::map<std::string, Vec3> &rotations = {}) const;
  void appendSkinned(std::vector<Vertex> &out, const std::string &name,
                     const std::vector<Mat4> &bones, Vec3 position, float scale = 1, float yaw = 0,
                     const CharacterStyle *style = nullptr) const;
  void appendCharacter(std::vector<Vertex> &out, const std::string &name, Vec3 position, float yaw,
                       CharacterPose previous, CharacterPose current, float blend, float phase,
                       float locomotion, const CharacterStyle &style) const;
};
} // namespace mega
