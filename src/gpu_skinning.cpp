#include "mega/renderer.hpp"
#include <cstring>
#include <limits>
#include <stdexcept>
namespace mega {
namespace {
void skinCheck(VkResult value, const char *operation) {
  if (value != VK_SUCCESS)
    throw std::runtime_error(std::string("Vulkan skinning ") + operation + ": " +
                             std::to_string(value));
}
} // namespace
void Renderer::addSkinMesh(const std::string &name, const std::vector<GpuSkinSource> &source) {
  if (name.empty() || source.empty() || skinMeshes.contains(name) || source.size() > 1000000 ||
      skinSources.size() > 4000000 - source.size())
    throw std::runtime_error("Invalid GPU character geometry");
  std::uint32_t required = 0;
  for (const auto &v : source)
    for (int n = 0; n < 8; ++n)
      if (v.weights[n] > 0) {
        if (v.joints[n] >= 65536)
          throw std::runtime_error("Invalid GPU character joint");
        required = std::max(required, v.joints[n] + 1);
      }
  skinMeshes[name] = {std::uint32_t(skinSources.size()), std::uint32_t(source.size()), required};
  skinSources.insert(skinSources.end(), source.begin(), source.end());
  skinSourcesDirty = true;
}
bool Renderer::canSkinVertices(std::size_t count) const {
  VkPhysicalDeviceProperties properties;
  vkGetPhysicalDeviceProperties(physical, &properties);
  return count <= skinVertexBudget &&
         count <= properties.limits.maxStorageBufferRange / sizeof(Vertex) &&
         skinSources.size() <= properties.limits.maxStorageBufferRange / sizeof(GpuSkinSource);
}
std::size_t Renderer::skinnedVertexCount() const {
  std::size_t count = 0;
  for (const auto &request : skinRequests)
    count += request.count;
  return count;
}
void Renderer::setupSkinning() {
  if (skinPipeline)
    return;
  VkDescriptorSetLayoutBinding bindings[4]{};
  for (std::uint32_t n = 0; n < 4; ++n)
    bindings[n] = {n, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
  VkDescriptorSetLayoutCreateInfo description{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
  description.bindingCount = 4;
  description.pBindings = bindings;
  skinCheck(vkCreateDescriptorSetLayout(device, &description, nullptr, &skinDescriptorLayout),
            "descriptors");
  VkPushConstantRange range{VK_SHADER_STAGE_COMPUTE_BIT, 0, 4};
  VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  layoutInfo.setLayoutCount = 1;
  layoutInfo.pSetLayouts = &skinDescriptorLayout;
  layoutInfo.pushConstantRangeCount = 1;
  layoutInfo.pPushConstantRanges = &range;
  skinCheck(vkCreatePipelineLayout(device, &layoutInfo, nullptr, &skinLayout), "layout");
  VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4};
  VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
  poolInfo.maxSets = 1;
  poolInfo.poolSizeCount = 1;
  poolInfo.pPoolSizes = &size;
  skinCheck(vkCreateDescriptorPool(device, &poolInfo, nullptr, &skinPool), "pool");
  VkDescriptorSetAllocateInfo allocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
  allocate.descriptorPool = skinPool;
  allocate.descriptorSetCount = 1;
  allocate.pSetLayouts = &skinDescriptorLayout;
  skinCheck(vkAllocateDescriptorSets(device, &allocate, &skinDescriptor), "set");
  auto module = shader("skin.comp.spv");
  VkComputePipelineCreateInfo pipeline{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
  pipeline.layout = skinLayout;
  pipeline.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
  pipeline.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
  pipeline.stage.module = module;
  pipeline.stage.pName = "main";
  auto result =
      vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipeline, nullptr, &skinPipeline);
  vkDestroyShaderModule(device, module, nullptr);
  skinCheck(result, "pipeline");
}
void Renderer::prepareSkins(std::size_t worldCount) {
  skinJobs.clear();
  if (skinRequests.empty())
    return;
  if (skinRequests.size() > 256)
    throw std::runtime_error("GPU character budget exceeded");
  VkPhysicalDeviceProperties properties;
  vkGetPhysicalDeviceProperties(physical, &properties);
  if (worldCount > properties.limits.maxStorageBufferRange / sizeof(Vertex))
    throw std::runtime_error("Character storage range exceeded; choose CPU skinning");
  setupSkinning();
  auto upload = [&](const void *data, std::size_t bytes, VkBuffer &object, VkDeviceMemory &memory,
                    std::size_t &capacity) {
    if (!bytes || bytes > properties.limits.maxStorageBufferRange)
      throw std::runtime_error("Invalid GPU skin storage size");
    if (bytes > capacity) {
      if (object)
        vkDestroyBuffer(device, object, nullptr);
      if (memory)
        vkFreeMemory(device, memory, nullptr);
      object = VK_NULL_HANDLE;
      memory = VK_NULL_HANDLE;
      capacity = std::min<std::size_t>(std::max<std::size_t>(4096, bytes * 2),
                                       properties.limits.maxStorageBufferRange);
      buffer(capacity, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
             VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, object,
             memory);
    }
    void *mapped;
    skinCheck(vkMapMemory(device, memory, 0, bytes, 0, &mapped), "map input");
    std::memcpy(mapped, data, bytes);
    vkUnmapMemory(device, memory);
  };
  if (skinSourcesDirty) {
    upload(skinSources.data(), skinSources.size() * sizeof(GpuSkinSource), skinSourceBuffer,
           skinSourceMemory, skinSourceBytes);
    skinSourcesDirty = false;
  }
  std::sort(skinRequests.begin(), skinRequests.end(),
            [](const auto &a, const auto &b) { return a.first < b.first; });
  std::vector<GpuSkinBone> bones;
  std::size_t previous = 0;
  for (const auto &request : skinRequests) {
    auto found = skinMeshes.find(request.mesh);
    if (found == skinMeshes.end() || request.count != found->second[1] ||
        request.bones.size() < found->second[2] || request.bones.empty() ||
        request.first < previous || request.first > worldCount ||
        request.count > worldCount - request.first)
      throw std::runtime_error("Invalid GPU skin dispatch range");
    previous = request.first + request.count;
    auto job = request.job;
    job.range = {found->second[0], request.first, request.count, std::uint32_t(bones.size())};
    bones.insert(bones.end(), request.bones.begin(), request.bones.end());
    skinJobs.push_back(job);
  }
  upload(bones.data(), bones.size() * sizeof(GpuSkinBone), skinBoneBuffer, skinBoneMemory,
         skinBoneBytes);
  upload(skinJobs.data(), skinJobs.size() * sizeof(GpuSkinJob), skinJobBuffer, skinJobMemory,
         skinJobBytes);
  VkDescriptorBufferInfo infos[] = {
      {skinSourceBuffer, 0, skinSources.size() * sizeof(GpuSkinSource)},
      {skinBoneBuffer, 0, bones.size() * sizeof(GpuSkinBone)},
      {skinJobBuffer, 0, skinJobs.size() * sizeof(GpuSkinJob)},
      {vertices, 0, worldCount * sizeof(Vertex)}};
  VkWriteDescriptorSet writes[4]{};
  for (std::uint32_t n = 0; n < 4; ++n) {
    writes[n] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    writes[n].dstSet = skinDescriptor;
    writes[n].dstBinding = n;
    writes[n].descriptorCount = 1;
    writes[n].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[n].pBufferInfo = &infos[n];
  }
  vkUpdateDescriptorSets(device, 4, writes, 0, nullptr);
}
void Renderer::dispatchSkins(bool readback) {
  if (skinJobs.empty())
    return;
  vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, skinPipeline);
  vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE, skinLayout, 0, 1,
                          &skinDescriptor, 0, nullptr);
  for (std::uint32_t n = 0; n < skinJobs.size(); ++n) {
    vkCmdPushConstants(command, skinLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, 4, &n);
    vkCmdDispatch(command, (skinJobs[n].range[2] + 63) / 64, 1, 1);
  }
  VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
  barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
  barrier.dstAccessMask = readback ? VK_ACCESS_HOST_READ_BIT : VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
  vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                       readback ? VK_PIPELINE_STAGE_HOST_BIT : VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
                       0, 1, &barrier, 0, nullptr, 0, nullptr);
}
std::vector<Vertex> Renderer::testSkinVertices(std::vector<SkinRequest> requests,
                                               std::size_t count) {
  if (!count || count > 4000000)
    throw std::runtime_error("Invalid skin test size");
  skinCheck(vkDeviceWaitIdle(device), "wait for readback");
  std::size_t bytes = count * sizeof(Vertex);
  if (bytes > vertexBytes) {
    if (vertices)
      vkDestroyBuffer(device, vertices, nullptr);
    if (vertexMemory)
      vkFreeMemory(device, vertexMemory, nullptr);
    vertexBytes = bytes;
    buffer(vertexBytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, vertices,
           vertexMemory);
  }
  setSkinRequests(std::move(requests));
  prepareSkins(count);
  begin();
  dispatchSkins(true);
  submit();
  std::vector<Vertex> output(count);
  void *mapped;
  skinCheck(vkMapMemory(device, vertexMemory, 0, bytes, 0, &mapped), "map readback");
  std::memcpy(output.data(), mapped, bytes);
  vkUnmapMemory(device, vertexMemory);
  skinRequests.clear();
  skinJobs.clear();
  return output;
}
} // namespace mega
