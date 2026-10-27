#pragma once
#include "math.hpp"
#include <vulkan/vulkan.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <cstddef>
#include <filesystem>
#include <map>
#include <string>
#include <vector>
namespace mega {
struct Color {
  float r = 1, g = 1, b = 1, a = 1;
};
struct Vertex {
  Vec3 p;
  Color color;
  float u = 0, v = 0;
  Vec3 normal{0, 1, 0};
  Vec3 material{-1, .75f, 0};
  float normalScale = 1;
};
struct Instance {
  Vec3 position{};
  float padding = 0;
  Vec3 scale{1, 1, 1};
  float reserved = 0;
  Color tint{};
};
static_assert(sizeof(Instance) == 48);
struct Rect {
  float x = 0, y = 0, w = 0, h = 0;
};
struct Batch {
  std::uint32_t first = 0, count = 0;
  Rect clip{};
};
// std430 storage layouts shared with skin.comp. Vertex itself is 16 packed floats.
struct alignas(16) GpuSkinSource {
  Vertex vertex;
  std::array<std::uint32_t, 8> joints{};
  std::array<float, 8> weights{};
  std::array<float, 4> garment{-1, 0, 0, 0};
};
struct alignas(16) GpuSkinBone {
  Mat4 pose, normal;
};
struct alignas(16) GpuSkinJob {
  Mat4 transform = identity(), rotation = identity();
  Color shirt{}, trousers{}, skin{};
  std::array<std::uint32_t, 4> range{}, options{};
};
static_assert(sizeof(Vertex) == 64 && offsetof(Vertex, color) == 12 &&
              offsetof(Vertex, normalScale) == 60);
static_assert(sizeof(GpuSkinSource) == 144 && offsetof(GpuSkinSource, garment) == 128);
static_assert(sizeof(GpuSkinBone) == 128 && sizeof(GpuSkinJob) == 208);
struct SkinRequest {
  std::string mesh;
  std::uint32_t first = 0, count = 0;
  GpuSkinJob job;
  std::vector<GpuSkinBone> bones;
};
class Renderer {
public:
  GLFWwindow *window = nullptr;
  std::string deviceName;
  bool software = false;
  int width = 1440, height = 900;
  Renderer(GLFWwindow *, const std::filesystem::path &shaders,
           const std::vector<unsigned char> &atlas, int atlasSize);
  ~Renderer();
  Renderer(const Renderer &) = delete;
  Renderer &operator=(const Renderer &) = delete;
  void draw(const std::vector<Vertex> &world, const std::vector<Vertex> &ui,
            const std::vector<Batch> &, const Mat4 &camera, Vec3 eye,
            const std::filesystem::path &capture = {});
  void addSkinMesh(const std::string &, const std::vector<GpuSkinSource> &);
  void setSkinRequests(std::vector<SkinRequest> requests) { skinRequests = std::move(requests); }
  std::size_t skinnedVertexCount() const;
  std::size_t skinVertexBudget = 4000000;
  bool canSkinVertices(std::size_t count) const;
  std::vector<Vertex> testSkinVertices(std::vector<SkinRequest>, std::size_t count);
  void setInstances(std::vector<Instance> instances);
  void setEmergencyLights(std::vector<std::array<float, 8>> lights) {
    emergencyLights = std::move(lights);
  }
  std::size_t instanceCount() const { return pendingInstances.size(); }
  std::vector<float> pressure(const std::vector<std::array<float, 4>> &needs);
  void updateFont(const std::vector<unsigned char> &, int size);
  void uploadMaterials(const std::array<std::vector<unsigned char>, 3> &textures, int size);

private:
  std::map<std::string, std::array<std::uint32_t, 3>> skinMeshes;
  std::vector<GpuSkinSource> skinSources;
  std::vector<SkinRequest> skinRequests;
  std::vector<GpuSkinJob> skinJobs;
  bool skinSourcesDirty = true;
  VkDescriptorSetLayout skinDescriptorLayout{};
  VkDescriptorPool skinPool{};
  VkDescriptorSet skinDescriptor{};
  VkPipelineLayout skinLayout{};
  VkPipeline skinPipeline{};
  VkBuffer skinSourceBuffer{}, skinBoneBuffer{}, skinJobBuffer{};
  VkDeviceMemory skinSourceMemory{}, skinBoneMemory{}, skinJobMemory{};
  std::size_t skinSourceBytes = 0, skinBoneBytes = 0, skinJobBytes = 0;
  void setupSkinning();
  void prepareSkins(std::size_t worldCount);
  void dispatchSkins(bool readback = false);
  std::vector<std::array<float, 8>> emergencyLights;
  VkInstance instance{};
  VkSurfaceKHR surface{};
  VkPhysicalDevice physical{};
  VkDevice device{};
  std::uint32_t family{};
  VkQueue queue{};
  VkSwapchainKHR swapchain{};
  VkFormat colorFormat{};
  VkExtent2D extent{};
  std::vector<VkImage> images;
  std::vector<VkImageView> views;
  std::vector<VkFramebuffer> framebuffers;
  VkImage depthImage{}, atlasImage{}, multisampleImage{};
  VkDeviceMemory multisampleMemory{};
  VkImageView multisampleView{};
  VkSampleCountFlagBits sceneSamples = VK_SAMPLE_COUNT_1_BIT;
  VkDeviceMemory depthMemory{}, atlasMemory{};
  VkImageView depthView{}, atlasView{};
  std::array<VkImage, 3> materialImages{};
  std::array<VkDeviceMemory, 3> materialMemory{};
  std::array<VkImageView, 3> materialViews{};
  VkSampler sampler{}, materialSampler{};
  VkRenderPass renderPass{}, shadowPass{};
  VkImage shadowImage{};
  VkDeviceMemory shadowMemory{}, lightMemory{};
  VkImageView shadowView{};
  VkSampler shadowSampler{};
  VkFramebuffer shadowFramebuffer{};
  VkBuffer lightBuffer{};
  VkPipeline shadowPipeline{};
  int shadowSize = 2048;
  VkPipelineLayout layout{};
  VkPipeline scenePipeline{}, uiPipeline{};
  VkDescriptorSetLayout descriptorLayout{};
  VkDescriptorPool descriptorPool{};
  VkDescriptorSet descriptor{};
  VkCommandPool pool{};
  VkCommandBuffer command{};
  VkSemaphore acquired{}, finished{};
  VkFence fence{};
  VkBuffer instanceBuffer{}, instanceCube{};
  VkDeviceMemory instanceMemory{}, cubeMemory{};
  std::size_t instanceBytes = 0;
  std::vector<Instance> pendingInstances;
  bool instancesDirty = true;
  bool anisotropy = false;
  void uploadInstances();
  VkBuffer vertices{};
  VkDeviceMemory vertexMemory{};
  std::size_t vertexBytes = 0;
  std::filesystem::path shaderPath;
  std::uint32_t memoryType(std::uint32_t, VkMemoryPropertyFlags);
  void buffer(VkDeviceSize, VkBufferUsageFlags, VkMemoryPropertyFlags, VkBuffer &,
              VkDeviceMemory &);
  void image(int, int, VkFormat, VkImageUsageFlags, VkImage &, VkDeviceMemory &, VkImageView &,
             VkImageAspectFlags, int levels = 1,
             VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT);
  void swap();
  void destroySwap();
  void pipelines();
  void setupShadows();
  VkShaderModule shader(const std::string &);
  void uploadAtlas(const std::vector<unsigned char> &, int);
  void begin();
  void submit();
};
void box(std::vector<Vertex> &, Vec3 centre, Vec3 size, Color color);
void cylinder(std::vector<Vertex> &, Vec3 from, Vec3 to, float radius, Color, int segments = 8);
void sphere(std::vector<Vertex> &, Vec3 centre, Vec3 radius, Color, int rings = 6, int sectors = 8);
} // namespace mega
