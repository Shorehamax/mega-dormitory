#include "mega/assets.hpp"
#include <functional>
#include <iostream>
#include <stdexcept>

namespace mega {
namespace {
Vec3 point(const Mat4 &m, Vec3 p) {
  return {m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12],
          m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13],
          m[2] * p.x + m[6] * p.y + m[10] * p.z + m[14]};
}
Vec3 vector(const Mat4 &m, Vec3 p) {
  return {m[0] * p.x + m[4] * p.y + m[8] * p.z, m[1] * p.x + m[5] * p.y + m[9] * p.z,
          m[2] * p.x + m[6] * p.y + m[10] * p.z};
}
Mat4 normalMatrix(const Mat4 &m) {
  Vec3 a{m[0], m[1], m[2]}, b{m[4], m[5], m[6]}, c{m[8], m[9], m[10]};
  float det = dot(a, cross(b, c));
  if (std::abs(det) < 1e-8f)
    throw std::runtime_error("Singular skin matrix");
  auto x = cross(b, c) * (1 / det), y = cross(c, a) * (1 / det), z = cross(a, b) * (1 / det);
  return {x.x, x.y, x.z, 0, y.x, y.y, y.z, 0, z.x, z.y, z.z, 0, 0, 0, 0, 1};
}
Mat4 rotation(Vec3 e) {
  float cx = std::cos(e.x), sx = std::sin(e.x), cy = std::cos(e.y), sy = std::sin(e.y),
        cz = std::cos(e.z), sz = std::sin(e.z);
  Mat4 x{1, 0, 0, 0, 0, cx, sx, 0, 0, -sx, cx, 0, 0, 0, 0, 1};
  Mat4 y{cy, 0, -sy, 0, 0, 1, 0, 0, sy, 0, cy, 0, 0, 0, 0, 1};
  Mat4 z{cz, sz, 0, 0, -sz, cz, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
  return mul(y, mul(x, z));
}
struct PoseDescription {
  std::map<std::string, Vec3> rotations;
  float lower = 0, lying = 0;
};
PoseDescription describe(CharacterPose pose, float phase, float motion) {
  PoseDescription d;
  float stride = std::sin(phase), breathe = std::sin(phase * .28f) * .018f;
  d.rotations["spine_02"] = {breathe, 0, 0};
  d.rotations["Head"] = {0, std::sin(phase * .12f) * .055f, 0};
  for (int side : {-1, 1}) {
    std::string suffix = side == 1 ? "_l" : "_r";
    Vec3 arm{0, 0, -side * 1.4f}, elbow{-.12f, 0, 0}, thigh{}, calf{};
    if (pose == CharacterPose::Walk || pose == CharacterPose::Carry) {
      arm.x = side * stride * .38f * motion;
      thigh.x = -side * stride * .53f * motion;
      calf.x = std::max(0.f, side * stride) * .7f * motion;
    }
    if (pose == CharacterPose::Sit || pose == CharacterPose::Eat) {
      thigh.x = -1.55f;
      calf.x = 1.5f;
      arm.x = -.3f;
      elbow.x = -.95f;
      d.lower = -.48f;
      if (pose == CharacterPose::Eat && side == -1) {
        float bite = .5f + .5f * std::sin(phase * .55f);
        arm.x = -.4f - bite * .45f;
        elbow.x = -.95f - bite * .85f;
      }
    }
    if (pose == CharacterPose::Sleep) {
      arm.z = -side * 1.48f;
      elbow.x = -.1f;
      d.lying = 1;
    }
    if (pose == CharacterPose::Climb) {
      arm = {-2.35f - side * stride * .26f, 0, -side * .75f};
      elbow.x = -.45f;
      thigh.x = -.8f - side * stride * .4f;
      calf.x = 1.18f;
    }
    if (pose == CharacterPose::Carry) {
      arm = {-.65f, 0, -side * 1.12f};
      elbow.x = -1.1f;
    }
    if (pose == CharacterPose::Work) {
      arm = {-.6f - std::sin(phase + side) * .15f, 0, -side * 1.18f};
      elbow.x = -.85f;
    }
    if (pose == CharacterPose::Fight) {
      float punch = side == -1 ? std::max(0.f, std::sin(phase)) : 0;
      arm = {-1.25f - punch * .6f, 0, -side * 1.1f};
      elbow.x = -.9f + punch * .85f;
      thigh.x = -.1f;
      calf.x = .17f;
    }
    if (pose == CharacterPose::Surrender) {
      arm = {-.1f, 0, side * .3f};
      elbow.x = -1.4f;
    }
    d.rotations["upperarm" + suffix] = arm;
    d.rotations["lowerarm" + suffix] = elbow;
    d.rotations["thigh" + suffix] = thigh;
    d.rotations["calf" + suffix] = calf;
    // Curl the fingers slightly instead of retaining the source's spread T-pose hands.
    for (auto finger : {"index_02", "middle_02", "ring_02", "pinky_02"})
      d.rotations[std::string(finger) + suffix] = {-.18f, 0, 0};
  }
  return d;
}
} // namespace
std::vector<Mat4> AssetLibrary::pose(const std::string &name,
                                     const std::map<std::string, Vec3> &rotations) const {
  const auto &mesh = meshes.at(name);
  std::vector<Mat4> out(mesh.rig.size());
  std::vector<unsigned char> state(mesh.rig.size());
  std::function<void(int)> visit = [&](int i) {
    if (i < 0 || std::size_t(i) >= mesh.rig.size())
      throw std::runtime_error("Invalid rig parent");
    if (state[i] == 2)
      return;
    if (state[i] == 1)
      throw std::runtime_error("Cyclic skeleton");
    state[i] = 1;
    const auto &node = mesh.rig[i];
    if (node.parent >= 0) {
      visit(node.parent);
      out[i] = mul(out[node.parent], node.local);
    } else
      out[i] = node.local;
    if (auto it = rotations.find(node.name); it != rotations.end()) {
      Vec3 pivot{out[i][12], out[i][13], out[i][14]};
      out[i] = mul(rotation(it->second), out[i]);
      out[i][12] = pivot.x;
      out[i][13] = pivot.y;
      out[i][14] = pivot.z;
    }
    state[i] = 2;
  };
  for (std::size_t i = 0; i < mesh.rig.size(); ++i)
    visit(int(i));
  return out;
}
void AssetLibrary::prepareCharacter(const std::string &name) {
  auto &mesh = meshes.at(name);
  std::vector<bool> body(mesh.vertices.size());
  for (const auto &part : mesh.parts)
    if (part.name.find("SuperHero") != std::string::npos ||
        part.name.find("Superhero") != std::string::npos)
      for (std::size_t i = part.first; i < part.first + part.count; ++i)
        body[i] = true;
  mesh.garments.assign(mesh.vertices.size(), Vec3{-1, 0, 0});
  for (std::size_t i = 0; i < mesh.vertices.size(); ++i)
    if (body[i]) {
      auto p = mesh.vertices[i].p;
      const auto &influence = mesh.influences[i];
      float hands = 0, arms = 0;
      for (int k = 0; k < 8; ++k)
        if (influence.weights[k] > 0) {
          const auto &bone = mesh.rig[mesh.skins[influence.skin].nodes[influence.joints[k]]].name;
          if (bone.starts_with("hand") || bone.starts_with("thumb") || bone.starts_with("index") ||
              bone.starts_with("middle") || bone.starts_with("ring") || bone.starts_with("pinky"))
            hands += influence.weights[k];
          if (bone.starts_with("upperarm") || bone.starts_with("lowerarm"))
            arms += influence.weights[k];
        }
      auto smooth = [](float low, float high, float x) {
        float t = std::clamp((x - low) / (high - low), 0.f, 1.f);
        return t * t * (3 - 2 * t);
      };
      float sleeve = 1 - smooth(.25f, .8f, hands);
      float cloth = sleeve * std::max(1 - smooth(1.51f, 1.56f, p.y), smooth(.1f, .8f, arms));
      float waist = smooth(.91f, .97f, p.y), shoe = 1 - smooth(.12f, .17f, p.y);
      mesh.garments[i] = {cloth, waist, shoe};
    }
}
void AssetLibrary::makeCharacterLod(const std::string &name, float cellSize) {
  if (!std::isfinite(cellSize) || cellSize <= 0)
    throw std::runtime_error("Invalid character LOD cell");
  const auto &source = meshes.at(name);
  if (source.influences.size() != source.vertices.size() ||
      source.garments.size() != source.vertices.size())
    throw std::runtime_error("Prepare the skinned character before building its LOD");
  struct Cluster {
    Vertex vertex;
    Vec3 position{}, normal{}, garment{};
    float u = 0, v = 0;
    unsigned count = 0;
    int skin = -1;
    std::map<unsigned, float> weights;
  };
  using Key = std::array<int, 7>;
  std::map<Key, unsigned> cells;
  std::vector<Cluster> clusters;
  std::vector<unsigned> remap;
  for (std::size_t i = 0; i < source.vertices.size(); ++i) {
    const auto &v = source.vertices[i];
    const auto &influence = source.influences[i];
    Key key{int(std::floor(v.p.x / cellSize)),
            int(std::floor(v.p.y / cellSize)),
            int(std::floor(v.p.z / cellSize)),
            int(v.material.x),
            int(std::floor(v.u * 8)),
            int(std::floor(v.v * 8)),
            influence.skin};
    auto [it, added] = cells.try_emplace(key, unsigned(clusters.size()));
    if (added) {
      clusters.emplace_back();
      clusters.back().vertex = v;
      clusters.back().skin = influence.skin;
    }
    auto &c = clusters[it->second];
    ++c.count;
    c.position = c.position + v.p;
    c.normal = c.normal + v.normal;
    c.u += v.u;
    c.v += v.v;
    c.garment = c.garment + source.garments[i];
    for (int k = 0; k < 8; ++k)
      if (influence.weights[k] > 0)
        c.weights[influence.joints[k]] += influence.weights[k];
    remap.push_back(it->second);
  }
  std::vector<SkinVertex> influences;
  for (auto &c : clusters) {
    float reciprocal = 1.f / c.count;
    c.vertex.p = c.position * reciprocal;
    c.vertex.normal = normalized(c.normal);
    c.vertex.u = c.u * reciprocal;
    c.vertex.v = c.v * reciprocal;
    c.garment = c.garment * reciprocal;
    std::vector<std::pair<float, unsigned>> sorted;
    for (auto [joint, weight] : c.weights)
      sorted.push_back({weight, joint});
    std::sort(sorted.rbegin(), sorted.rend());
    SkinVertex influence;
    influence.skin = c.skin;
    float total = 0;
    for (std::size_t k = 0; k < std::min<std::size_t>(8, sorted.size()); ++k) {
      influence.joints[k] = sorted[k].second;
      influence.weights[k] = sorted[k].first;
      total += sorted[k].first;
    }
    if (total > 0)
      for (float &weight : influence.weights)
        weight /= total;
    influences.push_back(influence);
  }
  MeshAsset lod;
  lod.minimum = source.minimum;
  lod.maximum = source.maximum;
  lod.rig = source.rig;
  lod.skins = source.skins;
  for (std::size_t i = 0; i < remap.size(); i += 3) {
    auto a = remap[i], b = remap[i + 1], c = remap[i + 2];
    if (a == b || b == c || a == c ||
        length(cross(clusters[b].vertex.p - clusters[a].vertex.p,
                     clusters[c].vertex.p - clusters[a].vertex.p)) < 1e-8f)
      continue;
    for (unsigned id : {a, b, c}) {
      lod.vertices.push_back(clusters[id].vertex);
      lod.influences.push_back(influences[id]);
      lod.garments.push_back(clusters[id].garment);
    }
  }
  if (lod.vertices.empty())
    throw std::runtime_error("Character LOD contains no triangles");
  std::cout << "Character LOD " << name << ": " << lod.vertices.size() / 3 << " triangles\n";
  meshes[name + "_lod"] = std::move(lod);
}

void AssetLibrary::appendSkinned(std::vector<Vertex> &out, const std::string &name,
                                 const std::vector<Mat4> &bones, Vec3 position, float scale,
                                 float yaw, const CharacterStyle *style) const {
  const auto &mesh = meshes.at(name);
  if (mesh.rig.empty() || bones.size() != mesh.rig.size() ||
      mesh.influences.size() != mesh.vertices.size())
    throw std::runtime_error("Invalid skeleton instance");
  std::vector<std::vector<Mat4>> palettes(mesh.skins.size()), normals(mesh.skins.size());
  for (std::size_t s = 0; s < mesh.skins.size(); ++s)
    for (std::size_t j = 0; j < mesh.skins[s].nodes.size(); ++j) {
      auto m = mul(bones.at(mesh.skins[s].nodes[j]), mesh.skins[s].inverseBind[j]);
      palettes[s].push_back(m);
      normals[s].push_back(normalMatrix(m));
    }
  std::vector<Vertex> hair;
  int headSkin = -1, headJoint = -1;
  if (style) {
    for (std::size_t s = 0; s < mesh.skins.size(); ++s)
      for (std::size_t j = 0; j < mesh.skins[s].nodes.size(); ++j)
        if (mesh.rig[mesh.skins[s].nodes[j]].name == "Head") {
          headSkin = int(s);
          headJoint = int(j);
        }
    if (headSkin >= 0) {
      auto cap = [&](int row, int col) {
        float phi = float(col) * 6.2831853f / 16;
        float theta = float(row) / 6 * (std::sin(phi) > .3f ? 1.55f : 1.96f);
        Vec3 n{std::sin(theta) * std::cos(phi), std::cos(theta), std::sin(theta) * std::sin(phi)};
        Vertex v;
        v.p = {n.x * .112f, mesh.maximum.y - .105f + n.y * .128f, -.015f + n.z * .107f};
        v.normal = normalized(Vec3{n.x / .112f, n.y / .128f, n.z / .107f});
        v.color = {.065f, .055f, .05f, 1};
        v.material = {-1, .85f, 0};
        return v;
      };
      for (int row = 0; row < 6; ++row)
        for (int col = 0; col < 16; ++col) {
          auto a = cap(row, col), b = cap(row + 1, col), c = cap(row + 1, col + 1),
               d = cap(row, col + 1);
          hair.insert(hair.end(), {a, b, c, a, c, d});
        }
    }
  }
  float co = std::cos(yaw), si = std::sin(yaw);
  for (std::size_t i = 0; i < mesh.vertices.size(); ++i) {
    auto v = mesh.vertices[i];
    const auto &influence = mesh.influences[i];
    Vec3 p = v.p, n = v.normal;
    if (style && i < mesh.garments.size() && mesh.garments[i].x >= 0) {
      float cloth = mesh.garments[i].x, waist = mesh.garments[i].y, shoe = mesh.garments[i].z;
      v.color = {style->trousers.r * (1 - waist) + style->shirt.r * waist,
                 style->trousers.g * (1 - waist) + style->shirt.g * waist,
                 style->trousers.b * (1 - waist) + style->shirt.b * waist, cloth};
      if (style->pattern == 1 && p.y > 1 && p.y < 1.45f && std::abs(p.x) < .035f && p.z > 0)
        v.color = {style->trousers.r, style->trousers.g, style->trousers.b, cloth};
      if (style->pattern == 2 && p.y > 1.02f && p.y < 1.16f && std::abs(p.x) < .18f && p.z > .08f)
        v.color = {style->shirt.r * .7f, style->shirt.g * .7f, style->shirt.b * .7f, cloth};
      v.color.r = v.color.r * (1 - shoe) + .055f * shoe;
      v.color.g = v.color.g * (1 - shoe) + .065f * shoe;
      v.color.b = v.color.b * (1 - shoe) + .075f * shoe;
      // The negative normal-scale marks an opaque skin/clothing blend, using colour alpha
      // as the fabric mask. Every vertex retains the same imported material and UVs.
      v.normalScale = -1;
    } else if (style) {
      v.color.r *= style->skin.r;
      v.color.g *= style->skin.g;
      v.color.b *= style->skin.b;
    }
    if (influence.skin >= 0) {
      Vec3 skinned{}, normal{};
      for (int k = 0; k < 8; ++k)
        if (influence.weights[k] > 0) {
          unsigned joint = influence.joints[k];
          float weight = influence.weights[k];
          skinned = skinned + point(palettes[influence.skin][joint], p) * weight;
          normal = normal + vector(normals[influence.skin][joint], n) * weight;
        }
      p = skinned;
      n = normalized(normal);
    }
    p = p * scale;
    v.p = position + Vec3{p.x * co + p.z * si, p.y, -p.x * si + p.z * co};
    v.normal = {n.x * co + n.z * si, n.y, -n.x * si + n.z * co};
    out.push_back(v);
  }
  for (auto v : hair) {
    auto p = point(palettes[headSkin][headJoint], v.p) * scale;
    auto n = normalized(vector(normals[headSkin][headJoint], v.normal));
    v.p = position + Vec3{p.x * co + p.z * si, p.y, -p.x * si + p.z * co};
    v.normal = {n.x * co + n.z * si, n.y, -n.x * si + n.z * co};
    out.push_back(v);
  }
}
void AssetLibrary::appendCharacter(std::vector<Vertex> &out, const std::string &name, Vec3 position,
                                   float yaw, CharacterPose previous, CharacterPose current,
                                   float blend, float phase, float locomotion,
                                   const CharacterStyle &style) const {
  auto a = describe(previous, phase, locomotion), b = describe(current, phase, locomotion);
  blend = std::clamp(blend, 0.f, 1.f);
  blend = blend * blend * (3 - 2 * blend);
  for (auto &[bone, r] : b.rotations)
    r = a.rotations[bone] * (1 - blend) + r * blend;
  auto bones = pose(name, b.rotations);
  float lying = a.lying * (1 - blend) + b.lying * blend;
  float lower = a.lower * (1 - blend) + b.lower * blend;
  auto root = rotation({-1.5707963f * lying, 0, 0});
  auto bedTurn = rotation({0, 3.1415927f * lying, 0});
  float scale = style.height / (meshes.at(name).maximum.y - meshes.at(name).minimum.y);
  std::size_t first = out.size();
  appendSkinned(out, name, bones, {}, scale, 0, &style);
  float co = std::cos(yaw), si = std::sin(yaw);
  for (std::size_t i = first; i < out.size(); ++i) {
    auto &v = out[i];
    Vec3 p = vector(root, v.p) + Vec3{0, lower + .65f * lying, .9f * lying};
    Vec3 n = vector(root, v.normal);
    p = vector(bedTurn, p);
    n = vector(bedTurn, n);
    // Source faces +Z; gameplay characters face -Z at yaw zero.
    v.p = position + Vec3{-p.x * co + p.z * si, p.y, -p.x * si - p.z * co};
    v.normal = {-n.x * co + n.z * si, n.y, -n.x * si - n.z * co};
  }
}
} // namespace mega
