#include "mega/assets.hpp"
#define CGLTF_IMPLEMENTATION
#include "cgltf.h"
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include <algorithm>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>

namespace mega {
namespace {
std::filesystem::path localFile(const std::filesystem::path &folder, const char *uri) {
  if (!uri)
    throw std::runtime_error("Missing asset URI");
  std::filesystem::path relative(uri);
  if (relative.is_absolute() || std::string(uri).find(':') != std::string::npos)
    throw std::runtime_error("Only local asset references are supported");
  auto root = std::filesystem::weakly_canonical(folder);
  auto path = std::filesystem::weakly_canonical(root / relative);
  auto rel = path.lexically_relative(root);
  if (rel.empty() || *rel.begin() == "..")
    throw std::runtime_error("Asset reference escapes its directory");
  return path;
}
Vec3 point(const float *m, Vec3 p) {
  return {m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12],
          m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13],
          m[2] * p.x + m[6] * p.y + m[10] * p.z + m[14]};
}
Vec3 normal(const float *m, Vec3 n) {
  // Inverse-transpose handles non-uniform scale in glTF node transforms.
  Vec3 a{m[0], m[1], m[2]}, b{m[4], m[5], m[6]}, c{m[8], m[9], m[10]};
  float determinant = dot(a, cross(b, c));
  if (std::abs(determinant) < 1e-8f)
    throw std::runtime_error("Singular mesh transform");
  return normalized((cross(b, c) * n.x + cross(c, a) * n.y + cross(a, b) * n.z) *
                    (1 / determinant));
}
void bounds(MeshAsset &mesh) {
  if (mesh.vertices.empty())
    throw std::runtime_error("Asset contains no triangles");
  mesh.minimum = mesh.maximum = mesh.vertices.front().p;
  for (auto &v : mesh.vertices) {
    if (!std::isfinite(v.p.x) || !std::isfinite(v.p.y) || !std::isfinite(v.p.z))
      throw std::runtime_error("Non-finite asset position");
    mesh.minimum = {std::min(mesh.minimum.x, v.p.x), std::min(mesh.minimum.y, v.p.y),
                    std::min(mesh.minimum.z, v.p.z)};
    mesh.maximum = {std::max(mesh.maximum.x, v.p.x), std::max(mesh.maximum.y, v.p.y),
                    std::max(mesh.maximum.z, v.p.z)};
  }
}
MeshAsset readObj(const std::filesystem::path &path) {
  std::ifstream input(path);
  if (!input)
    throw std::runtime_error("Cannot open OBJ");
  MeshAsset mesh;
  std::vector<Vec3> positions, normals, uvs;
  std::string line;
  auto index = [](int value, std::size_t count) {
    auto i = value > 0 ? value - 1 : int(count) + value;
    if (value == 0 || i < 0 || std::size_t(i) >= count)
      throw std::runtime_error("OBJ index out of range");
    return i;
  };
  while (std::getline(input, line)) {
    std::istringstream stream(line);
    std::string tag;
    stream >> tag;
    if (tag == "v" || tag == "vn" || tag == "vt") {
      Vec3 p;
      stream >> p.x >> p.y;
      if (tag != "vt")
        stream >> p.z;
      (tag == "v" ? positions : tag == "vn" ? normals : uvs).push_back(p);
    } else if (tag == "f") {
      std::vector<Vertex> face;
      std::string token;
      while (stream >> token) {
        Vertex v;
        std::istringstream part(token);
        std::string field;
        std::getline(part, field, '/');
        v.p = positions.at(index(std::stoi(field), positions.size()));
        if (std::getline(part, field, '/') && !field.empty()) {
          auto uv = uvs.at(index(std::stoi(field), uvs.size()));
          v.u = uv.x;
          v.v = 1 - uv.y;
        }
        if (std::getline(part, field, '/') && !field.empty())
          v.normal = normalized(normals.at(index(std::stoi(field), normals.size())));
        else
          v.normal = {};
        face.push_back(v);
        if (face.size() > 4096)
          throw std::runtime_error("OBJ face too large");
      }
      for (std::size_t i = 1; i + 1 < face.size(); ++i) {
        Vertex triangle[] = {face[0], face[i], face[i + 1]};
        Vec3 n = normalized(cross(triangle[1].p - triangle[0].p, triangle[2].p - triangle[0].p));
        for (auto &v : triangle) {
          if (length(v.normal) < .01f)
            v.normal = n;
          mesh.vertices.push_back(v);
        }
      }
    }
    if (mesh.vertices.size() > 6000000 || positions.size() > 2000000)
      throw std::runtime_error("Mesh exceeds import limit");
  }
  bounds(mesh);
  return mesh;
}
} // namespace
AssetLibrary::AssetLibrary() {
  for (auto &pixels : textures)
    pixels.resize(std::size_t(atlasSize) * atlasSize * 4);
}
void AssetLibrary::load(const std::string &name, const std::filesystem::path &file) {
  if (std::filesystem::file_size(file) > 128 * 1024 * 1024)
    throw std::runtime_error("Mesh file exceeds import limit");
  if (file.extension() == ".obj") {
    meshes[name] = readObj(file);
    return;
  }
  if (file.extension() != ".gltf" && file.extension() != ".glb")
    throw std::runtime_error("Unsupported mesh format; use glTF, GLB or OBJ");
  cgltf_options options{};
  cgltf_data *raw = nullptr;
  if (cgltf_parse_file(&options, file.c_str(), &raw) != cgltf_result_success)
    throw std::runtime_error("Invalid glTF: " + file.string());
  std::unique_ptr<cgltf_data, decltype(&cgltf_free)> data(raw, cgltf_free);
  if (data->nodes_count > 4096 || data->skins_count > 64)
    throw std::runtime_error("Skeleton exceeds import limit");
  if (data->extensions_required_count)
    throw std::runtime_error("Required glTF extensions are not supported by this static importer");
  for (std::size_t i = 0; i < data->buffers_count; ++i)
    if (data->buffers[i].uri) {
      std::string uri = data->buffers[i].uri;
      if (!uri.starts_with("data:")) {
        auto path = localFile(file.parent_path(), uri.c_str());
        if (std::filesystem::file_size(path) > 256 * 1024 * 1024)
          throw std::runtime_error("Buffer exceeds import limit");
      }
      if (data->buffers[i].size > 256 * 1024 * 1024)
        throw std::runtime_error("Buffer exceeds import limit");
    }
  if (cgltf_load_buffers(&options, data.get(), file.c_str()) != cgltf_result_success ||
      cgltf_validate(data.get()) != cgltf_result_success)
    throw std::runtime_error("Invalid glTF buffers: " + file.string());
  if (materialCount + int(data->materials_count) > columns * columns)
    throw std::runtime_error("Material atlas capacity exceeded");
  int firstMaterial = materialCount;
  for (std::size_t i = 0; i < data->materials_count; ++i) {
    auto &m = data->materials[i];
    int tile = materialCount++;
    cgltf_texture *maps[] = {m.pbr_metallic_roughness.base_color_texture.texture,
                             m.normal_texture.texture,
                             m.pbr_metallic_roughness.metallic_roughness_texture.texture};
    for (int channel = 0; channel < 3; ++channel) {
      int width = 1, height = 1, components = 4;
      unsigned char *decoded = nullptr;
      if (maps[channel] && maps[channel]->image) {
        auto *im = maps[channel]->image;
        if (im->buffer_view) {
          auto *view = im->buffer_view;
          auto *bytes = static_cast<unsigned char *>(view->buffer->data) + view->offset;
          decoded = stbi_load_from_memory(bytes, int(view->size), &width, &height, &components, 4);
        } else if (im->uri) {
          auto path = localFile(file.parent_path(), im->uri);
          if (std::filesystem::file_size(path) > 64 * 1024 * 1024)
            throw std::runtime_error("Texture exceeds import limit");
          decoded = stbi_load(path.c_str(), &width, &height, &components, 4);
        }
        if (!decoded)
          throw std::runtime_error("Texture decode failed: " + file.string());
      }
      std::unique_ptr<unsigned char, decltype(&stbi_image_free)> pixels(decoded, stbi_image_free);
      if (width <= 0 || height <= 0 || width > 16384 || height > 16384)
        throw std::runtime_error("Invalid texture dimensions");
      unsigned char fallback[4] = {255, 255, 255, 255};
      if (channel == 1) {
        fallback[0] = 128;
        fallback[1] = 128;
        fallback[2] = 255;
      }
      if (channel == 2) {
        fallback[0] = 255;
        fallback[1] = 255;
        fallback[2] = 255;
      }
      for (int y = 0; y < tileSize; ++y)
        for (int x = 0; x < tileSize; ++x) {
          auto *source = decoded ? decoded + (std::size_t(y) * height / tileSize * width +
                                              std::size_t(x) * width / tileSize) *
                                                 4
                                 : fallback;
          std::size_t dest = (std::size_t((tile / columns) * tileSize + y) * atlasSize +
                              (tile % columns) * tileSize + x) *
                             4;
          std::copy(source, source + 4, textures[channel].begin() + dest);
        }
    }
  }
  MeshAsset mesh;
  if (data->skins_count) {
    for (std::size_t i = 0; i < data->nodes_count; ++i) {
      auto &node = data->nodes[i];
      RigNode rig;
      rig.name = node.name ? node.name : "";
      rig.parent = node.parent ? int(node.parent - data->nodes) : -1;
      cgltf_node_transform_local(&node, rig.local.data());
      cgltf_node_transform_world(&node, rig.bindWorld.data());
      mesh.rig.push_back(rig);
    }
    for (std::size_t i = 0; i < data->skins_count; ++i) {
      const auto &source = data->skins[i];
      if (!source.joints_count || source.joints_count > 512)
        throw std::runtime_error("Invalid joint count");
      SkinBinding skin;
      for (std::size_t j = 0; j < source.joints_count; ++j) {
        skin.nodes.push_back(int(source.joints[j] - data->nodes));
        Mat4 inverse = identity();
        if (source.inverse_bind_matrices &&
            !cgltf_accessor_read_float(source.inverse_bind_matrices, j, inverse.data(), 16))
          throw std::runtime_error("Invalid inverse bind matrix");
        for (float value : inverse)
          if (!std::isfinite(value))
            throw std::runtime_error("Non-finite inverse bind matrix");
        skin.inverseBind.push_back(inverse);
      }
      mesh.skins.push_back(std::move(skin));
    }
  }
  std::function<void(cgltf_node *)> visit = [&](cgltf_node *node) {
    float transform[16];
    cgltf_node_transform_world(node, transform);
    std::size_t partStart = mesh.vertices.size();
    if (node->mesh)
      for (std::size_t pi = 0; pi < node->mesh->primitives_count; ++pi) {
        auto &primitive = node->mesh->primitives[pi];
        if (primitive.type != cgltf_primitive_type_triangles ||
            primitive.has_draco_mesh_compression)
          throw std::runtime_error("Unsupported compressed or non-triangle primitive");
        cgltf_accessor *position = nullptr, *normals = nullptr, *uv = nullptr;
        cgltf_accessor *joints[2]{}, *weights[2]{};
        for (std::size_t a = 0; a < primitive.attributes_count; ++a) {
          auto &attr = primitive.attributes[a];
          if (attr.type == cgltf_attribute_type_position)
            position = attr.data;
          else if (attr.type == cgltf_attribute_type_normal)
            normals = attr.data;
          else if (attr.type == cgltf_attribute_type_texcoord && attr.index == 0)
            uv = attr.data;
          else if (attr.type == cgltf_attribute_type_joints && attr.index < 2)
            joints[attr.index] = attr.data;
          else if (attr.type == cgltf_attribute_type_weights && attr.index < 2)
            weights[attr.index] = attr.data;
        }
        if (!position)
          throw std::runtime_error("Missing vertex positions");
        std::size_t count = primitive.indices ? primitive.indices->count : position->count;
        if (count % 3 || mesh.vertices.size() + count > 6000000)
          throw std::runtime_error("Invalid mesh index count");
        auto *mat = primitive.material;
        float rough = 1, metal = 0;
        Color factor{};
        int tile = -1;
        if (mat) {
          auto &pbr = mat->pbr_metallic_roughness;
          factor = {pbr.base_color_factor[0], pbr.base_color_factor[1], pbr.base_color_factor[2],
                    pbr.base_color_factor[3]};
          rough = pbr.roughness_factor;
          metal = pbr.metallic_factor;
          tile = firstMaterial + int(mat - data->materials);
        }
        std::size_t begin = mesh.vertices.size();
        for (std::size_t ii = 0; ii < count; ++ii) {
          std::size_t index =
              primitive.indices ? cgltf_accessor_read_index(primitive.indices, ii) : ii;
          float p[3]{}, n[3]{}, t[2]{};
          if (!cgltf_accessor_read_float(position, index, p, 3))
            throw std::runtime_error("Invalid position accessor");
          if (normals && !cgltf_accessor_read_float(normals, index, n, 3))
            throw std::runtime_error("Invalid normal accessor");
          if (uv && !cgltf_accessor_read_float(uv, index, t, 2))
            throw std::runtime_error("Invalid UV accessor");
          Vertex v;
          SkinVertex influence;
          if (node->skin) {
            influence.skin = int(node->skin - data->skins);
            if (!joints[0] || !weights[0])
              throw std::runtime_error("Skinned primitive lacks joint weights");
            float total = 0;
            for (int set = 0; set < 2; ++set) {
              if (!joints[set] && !weights[set])
                continue;
              if (!joints[set] || !weights[set] ||
                  !cgltf_accessor_read_uint(joints[set], index, influence.joints.data() + set * 4,
                                            4) ||
                  !cgltf_accessor_read_float(weights[set], index,
                                             influence.weights.data() + set * 4, 4))
                throw std::runtime_error("Invalid skin influence accessor");
              for (int k = set * 4; k < set * 4 + 4; ++k) {
                float weight = influence.weights[k];
                if (!std::isfinite(weight) || weight < 0 ||
                    influence.joints[k] >= node->skin->joints_count)
                  throw std::runtime_error("Invalid skin joint or weight");
                total += weight;
              }
            }
            if (total < 1e-6f || !std::isfinite(total))
              throw std::runtime_error("Empty skin weights");
            for (float &weight : influence.weights)
              weight /= total;
            v.p = {p[0], p[1], p[2]};
            v.normal = normals ? normalized(Vec3{n[0], n[1], n[2]}) : Vec3{};
          } else {
            v.p = point(transform, {p[0], p[1], p[2]});
            v.normal = normals ? normal(transform, {n[0], n[1], n[2]}) : Vec3{};
          }
          if (!mesh.rig.empty())
            mesh.influences.push_back(influence);
          v.color = factor;
          v.u = t[0];
          v.v = t[1];
          v.material = {float(tile), rough, metal};
          v.normalScale = mat ? mat->normal_texture.scale : 1;
          mesh.vertices.push_back(v);
        }
        if (!normals)
          for (std::size_t i = begin; i < mesh.vertices.size(); i += 3) {
            auto &a = mesh.vertices[i];
            auto &b = mesh.vertices[i + 1];
            auto &c = mesh.vertices[i + 2];
            a.normal = b.normal = c.normal = normalized(cross(b.p - a.p, c.p - a.p));
          }
      }
    if (mesh.vertices.size() > partStart)
      mesh.parts.push_back({node->name ? node->name : "mesh", partStart,
                            mesh.vertices.size() - partStart, point(transform, {})});
    for (std::size_t i = 0; i < node->children_count; ++i)
      visit(node->children[i]);
  };
  if (data->scene)
    for (std::size_t n = 0; n < data->scene->nodes_count; ++n)
      visit(data->scene->nodes[n]);
  else
    for (std::size_t n = 0; n < data->nodes_count; ++n)
      if (!data->nodes[n].parent)
        visit(&data->nodes[n]);
  bounds(mesh);
  std::cout << "Asset " << name << ": " << mesh.vertices.size() / 3 << " triangles\n";
  meshes[name] = std::move(mesh);
}
void AssetLibrary::loadDormitory(const std::filesystem::path &root) {
  for (auto name : {"old_bed_frame", "plastic_monobloc_chair_01", "painted_wooden_table",
                    "metal_trash_can", "painted_wooden_cabinet_02", "wheelchair_01"})
    load(name, root / name / (std::string(name) + "_1k.gltf"));
  load("concrete_floor_02", root.parent_path() / "surfaces/concrete_floor_02/material.gltf");
  for (auto name : {"sedan", "van", "ambulance"})
    load(name, root.parent_path() / "vehicles/kenney" / (std::string(name) + ".glb"));
  for (auto gender : {"Male", "Female"}) {
    auto name = std::string("resident_") + gender;
    load(name, root.parent_path() / "characters/quaternius" /
                   (std::string("Superhero_") + gender + "_FullBody.gltf"));
    prepareCharacter(name);
    makeCharacterLod(name, .045f);
  }
}
void AssetLibrary::append(std::vector<Vertex> &out, const std::string &name, Vec3 position,
                          float scale, float yaw, Color tint, float opening) const {
  const auto &mesh = meshes.at(name);
  float c = std::cos(yaw), s = std::sin(yaw);
  Vec3 center{(mesh.minimum.x + mesh.maximum.x) * .5f, mesh.minimum.y,
              (mesh.minimum.z + mesh.maximum.z) * .5f};
  std::size_t index = 0;
  for (auto v : mesh.vertices) {
    for (const auto &part : mesh.parts)
      if (index >= part.first && index < part.first + part.count) {
        if (part.name.find("door") != std::string::npos && opening > 0) {
          float angle = opening * 1.45f, co = std::cos(angle), si = std::sin(angle);
          Vec3 p = v.p - part.pivot, n = v.normal;
          v.p = part.pivot + Vec3{p.x * co + p.z * si, p.y, -p.x * si + p.z * co};
          v.normal = {n.x * co + n.z * si, n.y, -n.x * si + n.z * co};
        } else if (part.name.find("drawer") != std::string::npos)
          v.p.z += opening * .24f;
        if (part.name.find("wheel") == std::string::npos) {
          v.color.r *= tint.r;
          v.color.g *= tint.g;
          v.color.b *= tint.b;
        }
        break;
      }
    ++index;
    Vec3 p = (v.p - center) * scale, n = v.normal;
    v.p = position + Vec3{p.x * c + p.z * s, p.y, -p.x * s + p.z * c};
    v.normal = {n.x * c + n.z * s, n.y, -n.x * s + n.z * c};
    out.push_back(v);
  }
}
} // namespace mega
