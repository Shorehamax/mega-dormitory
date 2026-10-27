#include "json.hpp"
#include "mega/assets.hpp"
#include <cstring>
#include <fstream>
#include <iostream>
using namespace mega;
using nlohmann::json;
static void require(bool b, const char *message) {
  if (!b)
    throw std::runtime_error(message);
}
int main() {
  try {
    AssetLibrary assets;
    assets.loadDormitory(std::filesystem::path(MEGA_ASSETS) / "models");
    require(assets.meshes.size() == 14 && assets.materialCount == 18,
            "Missing downloaded assets/materials");
    for (const auto &[name, mesh] : assets.meshes) {
      require(!mesh.vertices.empty() && mesh.vertices.size() % 3 == 0, "Invalid triangles");
      require(length(mesh.maximum - mesh.minimum) > .1f, "Invalid model bounds");
      for (const auto &v : mesh.vertices) {
        require(std::abs(length(v.normal) - 1) < .02f, "Invalid normal transform");
        require(v.material.x >= 0 && v.material.x < assets.materialCount, "Invalid material index");
      }
      std::cout << "PASS CC0 asset " << name << "; bounds " << mesh.minimum.x << ","
                << mesh.minimum.y << "," << mesh.minimum.z << " to " << mesh.maximum.x << ","
                << mesh.maximum.y << "," << mesh.maximum.z << '\n';
    }
    {
      const auto &cabinet = assets.meshes.at("painted_wooden_cabinet_02");
      require(cabinet.parts.size() == 3, "Cabinet lost independent door/drawer parts");
      std::vector<Vertex> closed, opened;
      assets.append(closed, "painted_wooden_cabinet_02", {}, 1, 0, {1, 1, 1, 1}, 0);
      assets.append(opened, "painted_wooden_cabinet_02", {}, 1, 0, {1, 1, 1, 1}, 1);
      int changed = 0;
      for (std::size_t i = 0; i < closed.size(); ++i)
        if (length(closed[i].p - opened[i].p) > .001f)
          ++changed;
      require(changed > 0 && changed < int(closed.size()),
              "Opening moved the whole cabinet or no parts");
      require(assets.meshes.at("sedan").parts.size() == 5, "Vehicle wheel/body separation lost");
      std::cout << "PASS immutable mesh instances and independent cabinet/vehicle parts\n";
    }
    for (auto name :
         {"resident_Male", "resident_Female", "resident_Male_lod", "resident_Female_lod"}) {
      const auto &mesh = assets.meshes.at(name);
      if (std::string(name).ends_with("_lod"))
        require(mesh.vertices.size() <
                    assets.meshes.at(std::string(name).substr(0, std::string(name).size() - 4))
                            .vertices.size() *
                        .7,
                "Character LOD did not reduce geometry");
      require(mesh.skins.size() == 1 && mesh.rig.size() >= 65,
              "Character rig or inverse bind matrices were dropped");
      std::vector<Vertex> rest;
      assets.appendSkinned(rest, name, assets.pose(name), {});
      require(rest.size() == mesh.vertices.size(), "Skinning changed triangle count");
      for (std::size_t i = 0; i < rest.size(); ++i) {
        require(length(rest[i].p - mesh.vertices[i].p) < .001f,
                "Bind pose does not reproduce source geometry");
        require(std::abs(length(rest[i].normal) - 1) < .001f, "Skinned normal is invalid");
      }
      auto gpuSource = assets.skinGeometry(name);
      require(gpuSource.size() == mesh.vertices.size() + 576,
              "GPU geometry lost body or hair triangles");
      CharacterStyle appearance;
      for (auto pose : {CharacterPose::Idle, CharacterPose::Walk, CharacterPose::Sit,
                        CharacterPose::Eat, CharacterPose::Sleep, CharacterPose::Climb,
                        CharacterPose::Carry, CharacterPose::Work, CharacterPose::Fight,
                        CharacterPose::Surrender, CharacterPose::Fall, CharacterPose::Push}) {
        std::vector<Vertex> rendered;
        assets.appendCharacter(rendered, name, {}, 0, pose, pose, 1, 1.2f, 1, appearance);
        auto request = assets.characterSkin(name, 7, {}, 0, pose, pose, 1, 1.2f, 1, appearance);
        require(request.first == 7 && request.count == rendered.size() && !request.bones.empty(),
                "GPU character job lost ranges or palettes");
        for (const auto &vertex : gpuSource)
          for (int joint = 0; joint < 8; ++joint)
            require(vertex.weights[joint] == 0 || vertex.joints[joint] < request.bones.size(),
                    "GPU geometry references an invalid palette joint");
        float minY = 100, maxY = -100;
        for (const auto &vertex : rendered) {
          require(std::isfinite(length(vertex.p)) && length(vertex.p) < 3,
                  "Animated character escaped its local bounds");
          require(std::abs(length(vertex.normal) - 1) < .002f, "Pose normal lost normalization");
          minY = std::min(minY, vertex.p.y);
          maxY = std::max(maxY, vertex.p.y);
        }
        if (pose == CharacterPose::Sleep)
          require(minY > .35f && maxY < 1.05f, "Sleeping character is not lying on its mattress");
      }
      {
        std::vector<Vertex> idle, pushing;
        assets.appendCharacter(idle, name, {}, 0, CharacterPose::Idle, CharacterPose::Idle, 1,
                               1.5707963f, 1, appearance);
        assets.appendCharacter(pushing, name, {}, 0, CharacterPose::Push, CharacterPose::Push, 1,
                               1.5707963f, 1, appearance);
        for (const char *hand : {"hand_l", "hand_r"}) {
          float restZ = 0, pushZ = 0;
          int samples = 0;
          for (std::size_t i = 0; i < mesh.influences.size(); ++i) {
            const auto &influence = mesh.influences[i];
            float weight = 0;
            for (int k = 0; k < 8; ++k)
              if (influence.weights[k] > 0 &&
                  mesh.rig[mesh.skins[influence.skin].nodes[influence.joints[k]]].name == hand)
                weight += influence.weights[k];
            if (weight > .35f) {
              restZ += idle[i].p.z;
              pushZ += pushing[i].p.z;
              ++samples;
            }
          }
          require(samples > 5 && pushZ / samples < restZ / samples - .15f,
                  "A shove did not extend both hands in front of the body");
        }
      }
      std::cout << "PASS bind-pose fidelity, all activity poses and sleeping bounds: " << name
                << '\n';
    }
    {
      // Independent hand-computed two-joint fixture exercises hierarchy and blended weights.
      MeshAsset fixture;
      RigNode rootNode, child;
      rootNode.name = "root";
      rootNode.local[12] = 1;
      rootNode.bindWorld = rootNode.local;
      child.name = "child";
      child.parent = 0;
      child.local[13] = 1;
      child.bindWorld = mul(rootNode.local, child.local);
      fixture.rig = {rootNode, child};
      auto inverseRoot = identity(), inverseChild = identity();
      inverseRoot[12] = -1;
      inverseChild[12] = -1;
      inverseChild[13] = -1;
      fixture.skins.push_back({{0, 1}, {inverseRoot, inverseChild}});
      Vertex a, b, c;
      a.p = {1, 0, 0};
      b.p = {2, 1, 0};
      c.p = {2, 1, 0};
      a.normal = b.normal = c.normal = {0, 0, 1};
      fixture.vertices = {a, b, c};
      SkinVertex rootWeight, childWeight, blendWeight;
      rootWeight.skin = childWeight.skin = blendWeight.skin = 0;
      rootWeight.weights[0] = childWeight.weights[0] = 1;
      childWeight.joints[0] = 1;
      blendWeight.joints[1] = 1;
      blendWeight.weights[0] = blendWeight.weights[1] = .5f;
      fixture.influences = {rootWeight, childWeight, blendWeight};
      assets.meshes["rig_fixture"] = fixture;
      std::vector<Vertex> result;
      assets.appendSkinned(result, "rig_fixture",
                           assets.pose("rig_fixture", {{"child", {0, 0, 1.5707963f}}}), {});
      require(length(result[0].p - Vec3{1, 0, 0}) < .0001f,
              "Child animation moved parent geometry");
      require(length(result[1].p - Vec3{1, 2, 0}) < .0001f,
              "Child rotated around wrong joint pivot");
      require(length(result[2].p - Vec3{1.5f, 1.5f, 0}) < .0001f, "Skin weights were not blended");
      assets.meshes.erase("rig_fixture");
      std::cout << "PASS independent hierarchical joint pivot and weighted skin fixture\n";
    }

    auto root = std::filesystem::temp_directory_path() / "mega-asset-tests";
    std::filesystem::create_directories(root);
    auto obj = root / "triangle.obj";
    {
      std::ofstream f(obj);
      f << "v 0 0 0\nv 1 0 0\nv 0 1 0\nf -3 -2 -1\n";
    }
    assets.load("obj", obj);
    require(assets.meshes.at("obj").vertices.size() == 3, "OBJ negative indices failed");
    std::vector<Vertex> placed;
    assets.append(placed, "obj", {10, 20, 30}, 2, 0);
    require(placed[0].p.x == 9 && placed[0].p.y == 20 && placed[0].p.z == 30,
            "Mesh placement lost pivot or scale");
    std::cout << "PASS OBJ triangulation, negative indices and instance transforms\n";
    float vertices[] = {0, 0, 0, 1, 0, 0, 0, 1, 0};
    json gltf = {
        {"asset", {{"version", "2.0"}}},
        {"buffers", json::array({{{"byteLength", 36}}})},
        {"bufferViews", json::array({{{"buffer", 0}, {"byteLength", 36}}})},
        {"accessors",
         json::array(
             {{{"bufferView", 0}, {"componentType", 5126}, {"count", 3}, {"type", "VEC3"}}})},
        {"meshes",
         json::array({{{"primitives", json::array({{{"attributes", {{"POSITION", 0}}}}})}}})},
        {"nodes", json::array({{{"mesh", 0}}})},
        {"scenes", json::array({{{"nodes", json::array({0})}}})},
        {"scene", 0}};
    auto text = gltf.dump();
    while (text.size() % 4)
      text += ' ';
    auto binary = root / "triangle.glb";
    {
      std::ofstream f(binary, std::ios::binary);
      auto write = [&](std::uint32_t value) { f.write(reinterpret_cast<char *>(&value), 4); };
      write(0x46546c67);
      write(2);
      write(12 + 8 + text.size() + 8 + sizeof(vertices));
      write(text.size());
      write(0x4e4f534a);
      f.write(text.data(), text.size());
      write(sizeof(vertices));
      write(0x004e4942);
      f.write(reinterpret_cast<char *>(vertices), sizeof(vertices));
    }
    assets.load("glb", binary);
    require(assets.meshes.at("glb").vertices.size() == 3, "Embedded GLB failed");
    std::cout << "PASS binary GLB buffers and generated normals\n";
    gltf["buffers"][0]["uri"] = "../outside.bin";
    auto malicious = root / "escape.gltf";
    {
      std::ofstream f(malicious);
      f << gltf;
    }
    bool rejected = false;
    try {
      assets.load("escape", malicious);
    } catch (const std::exception &) {
      rejected = true;
    }
    require(rejected, "Importer accepted traversal");
    std::cout << "PASS imported references cannot escape asset directory\n";
    std::filesystem::remove_all(root);
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
