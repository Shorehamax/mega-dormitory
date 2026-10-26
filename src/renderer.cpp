#include "mega/renderer.hpp"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
namespace mega {
struct SceneLighting {
  Mat4 sun;
  std::array<std::array<float, 4>, 16> fire{}, smoke{};
  std::array<float, 4> counts{};
};
static_assert(sizeof(SceneLighting) == sizeof(Mat4) + 33 * 16);

static void check(VkResult r, const char *operation) {
  if (r != VK_SUCCESS)
    throw std::runtime_error(std::string(operation) + ": Vulkan " + std::to_string(r));
}
std::uint32_t Renderer::memoryType(std::uint32_t bits, VkMemoryPropertyFlags flags) {
  VkPhysicalDeviceMemoryProperties p;
  vkGetPhysicalDeviceMemoryProperties(physical, &p);
  for (std::uint32_t i = 0; i < p.memoryTypeCount; ++i)
    if ((bits & (1u << i)) && (p.memoryTypes[i].propertyFlags & flags) == flags)
      return i;
  throw std::runtime_error("No compatible Vulkan memory");
}
void Renderer::buffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags flags,
                      VkBuffer &out, VkDeviceMemory &mem) {
  VkBufferCreateInfo b{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
  b.size = size;
  b.usage = usage;
  b.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  check(vkCreateBuffer(device, &b, nullptr, &out), "buffer");
  VkMemoryRequirements req;
  vkGetBufferMemoryRequirements(device, out, &req);
  VkMemoryAllocateInfo a{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  a.allocationSize = req.size;
  a.memoryTypeIndex = memoryType(req.memoryTypeBits, flags);
  check(vkAllocateMemory(device, &a, nullptr, &mem), "buffer memory");
  check(vkBindBufferMemory(device, out, mem, 0), "bind buffer");
}
void Renderer::image(int w, int h, VkFormat format, VkImageUsageFlags usage, VkImage &im,
                     VkDeviceMemory &mem, VkImageView &view, VkImageAspectFlags aspect, int levels,
                     VkSampleCountFlagBits samples) {
  VkImageCreateInfo i{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
  i.imageType = VK_IMAGE_TYPE_2D;
  i.format = format;
  i.extent = {std::uint32_t(w), std::uint32_t(h), 1};
  i.mipLevels = levels;
  i.arrayLayers = 1;
  i.samples = samples;
  i.tiling = VK_IMAGE_TILING_OPTIMAL;
  i.usage = usage;
  i.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  check(vkCreateImage(device, &i, nullptr, &im), "image");
  VkMemoryRequirements req;
  vkGetImageMemoryRequirements(device, im, &req);
  VkMemoryAllocateInfo a{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  a.allocationSize = req.size;
  a.memoryTypeIndex = memoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  check(vkAllocateMemory(device, &a, nullptr, &mem), "image memory");
  check(vkBindImageMemory(device, im, mem, 0), "bind image");
  VkImageViewCreateInfo v{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
  v.image = im;
  v.viewType = VK_IMAGE_VIEW_TYPE_2D;
  v.format = format;
  v.subresourceRange = {aspect, 0, std::uint32_t(levels), 0, 1};
  check(vkCreateImageView(device, &v, nullptr, &view), "image view");
}
void Renderer::begin() {
  check(vkResetCommandBuffer(command, 0), "reset command");
  VkCommandBufferBeginInfo b{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  b.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  check(vkBeginCommandBuffer(command, &b), "begin command");
}
void Renderer::submit() {
  check(vkEndCommandBuffer(command), "end command");
  VkSubmitInfo s{VK_STRUCTURE_TYPE_SUBMIT_INFO};
  s.commandBufferCount = 1;
  s.pCommandBuffers = &command;
  check(vkQueueSubmit(queue, 1, &s, VK_NULL_HANDLE), "submit");
  check(vkQueueWaitIdle(queue), "queue idle");
}
Renderer::Renderer(GLFWwindow *w, const std::filesystem::path &path,
                   const std::vector<unsigned char> &atlas, int size)
    : window(w), shaderPath(path) {
  std::uint32_t count;
  auto ext = glfwGetRequiredInstanceExtensions(&count);
  if (!ext)
    throw std::runtime_error("GLFW Vulkan surface unavailable");
  VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
  app.pApplicationName = "MEGA DORMITORY";
  app.apiVersion = VK_API_VERSION_1_3;
  VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
  info.pApplicationInfo = &app;
  info.enabledExtensionCount = count;
  info.ppEnabledExtensionNames = ext;
  check(vkCreateInstance(&info, nullptr, &instance), "instance");
  check(glfwCreateWindowSurface(instance, window, nullptr, &surface), "surface");
  check(vkEnumeratePhysicalDevices(instance, &count, nullptr), "enumerate devices");
  std::vector<VkPhysicalDevice> devices(count);
  check(vkEnumeratePhysicalDevices(instance, &count, devices.data()), "enumerate devices");
  int best = -1;
  for (auto d : devices) {
    VkPhysicalDeviceProperties p;
    vkGetPhysicalDeviceProperties(d, &p);
    std::uint32_t n;
    vkGetPhysicalDeviceQueueFamilyProperties(d, &n, nullptr);
    std::vector<VkQueueFamilyProperties> q(n);
    vkGetPhysicalDeviceQueueFamilyProperties(d, &n, q.data());
    for (std::uint32_t j = 0; j < n; ++j) {
      VkBool32 present = 0;
      vkGetPhysicalDeviceSurfaceSupportKHR(d, j, surface, &present);
      if (present && (q[j].queueFlags & VK_QUEUE_GRAPHICS_BIT) &&
          (q[j].queueFlags & VK_QUEUE_COMPUTE_BIT)) {
        int score = p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU     ? 100
                    : p.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 50
                                                                             : 10;
        if (score > best) {
          physical = d;
          family = j;
          best = score;
          deviceName = p.deviceName;
          software = p.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU;
        }
      }
    }
  }
  if (!physical)
    throw std::runtime_error("No Vulkan graphics/compute device; install a "
                             "Vulkan driver or Mesa software Vulkan");
  float priority = 1;
  VkDeviceQueueCreateInfo qi{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
  qi.queueFamilyIndex = family;
  qi.queueCount = 1;
  qi.pQueuePriorities = &priority;
  const char *swapExt = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
  VkDeviceCreateInfo di{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
  di.queueCreateInfoCount = 1;
  di.pQueueCreateInfos = &qi;
  di.enabledExtensionCount = 1;
  di.ppEnabledExtensionNames = &swapExt;
  VkPhysicalDeviceFeatures supported{}, enabled{};
  vkGetPhysicalDeviceFeatures(physical, &supported);
  anisotropy = supported.samplerAnisotropy;
  enabled.samplerAnisotropy = supported.samplerAnisotropy;
  di.pEnabledFeatures = &enabled;
  check(vkCreateDevice(physical, &di, nullptr, &device), "device");
  vkGetDeviceQueue(device, family, 0, &queue);
  VkCommandPoolCreateInfo pi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
  pi.queueFamilyIndex = family;
  pi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  check(vkCreateCommandPool(device, &pi, nullptr, &pool), "command pool");
  VkCommandBufferAllocateInfo ca{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
  ca.commandPool = pool;
  ca.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  ca.commandBufferCount = 1;
  check(vkAllocateCommandBuffers(device, &ca, &command), "command buffer");
  VkSemaphoreCreateInfo si{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
  check(vkCreateSemaphore(device, &si, nullptr, &acquired), "acquire semaphore");
  check(vkCreateSemaphore(device, &si, nullptr, &finished), "present semaphore");
  VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
  fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;
  check(vkCreateFence(device, &fi, nullptr, &fence), "fence");
  VkDescriptorSetLayoutBinding binding{};
  binding.binding = 0;
  binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  binding.descriptorCount = 1;
  binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
  VkDescriptorSetLayoutCreateInfo dl{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
  VkDescriptorSetLayoutBinding bindings[7];
  for (int i = 0; i < 5; ++i) {
    bindings[i] = binding;
    bindings[i].binding = i;
  }
  bindings[5] = {5, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1,
                 VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
  bindings[6] = {6, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr};
  dl.bindingCount = 7;
  dl.pBindings = bindings;
  check(vkCreateDescriptorSetLayout(device, &dl, nullptr, &descriptorLayout), "descriptor layout");
  VkPushConstantRange range{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 92};
  VkPipelineLayoutCreateInfo li{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  li.setLayoutCount = 1;
  li.pSetLayouts = &descriptorLayout;
  li.pushConstantRangeCount = 1;
  li.pPushConstantRanges = &range;
  check(vkCreatePipelineLayout(device, &li, nullptr, &layout), "pipeline layout");
  setupShadows();
  uploadAtlas(atlas, size);
  swap();
  std::cout << "Vulkan device: " << deviceName << (software ? " (software)" : "") << "; MSAA "
            << int(sceneSamples) << "x\n";
}
void Renderer::uploadAtlas(const std::vector<unsigned char> &data, int size) {
  VkBuffer staging;
  VkDeviceMemory mem;
  buffer(data.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, staging, mem);
  void *p;
  check(vkMapMemory(device, mem, 0, data.size(), 0, &p), "map font");
  std::memcpy(p, data.data(), data.size());
  vkUnmapMemory(device, mem);
  image(size, size, VK_FORMAT_R8_UNORM,
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, atlasImage, atlasMemory,
        atlasView, VK_IMAGE_ASPECT_COLOR_BIT);
  begin();
  VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
  b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  b.image = atlasImage;
  b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                       0, 0, nullptr, 0, nullptr, 1, &b);
  VkBufferImageCopy c{};
  c.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  c.imageExtent = {std::uint32_t(size), std::uint32_t(size), 1};
  vkCmdCopyBufferToImage(command, staging, atlasImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &c);
  b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
  vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT,
                       VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
  submit();
  vkDestroyBuffer(device, staging, nullptr);
  vkFreeMemory(device, mem, nullptr);
  VkSamplerCreateInfo s{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
  s.magFilter = VK_FILTER_LINEAR;
  s.minFilter = VK_FILTER_LINEAR;
  s.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
  s.addressModeU = s.addressModeV = s.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  s.maxLod = 0;
  check(vkCreateSampler(device, &s, nullptr, &sampler), "sampler");
  VkDescriptorPoolSize ps[] = {{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 5},
                               {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1},
                               {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1}};
  VkDescriptorPoolCreateInfo pc{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
  pc.maxSets = 1;
  pc.poolSizeCount = 3;
  pc.pPoolSizes = ps;
  check(vkCreateDescriptorPool(device, &pc, nullptr, &descriptorPool), "descriptor pool");
  VkDescriptorSetAllocateInfo a{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
  a.descriptorPool = descriptorPool;
  a.descriptorSetCount = 1;
  a.pSetLayouts = &descriptorLayout;
  check(vkAllocateDescriptorSets(device, &a, &descriptor), "descriptor set");
  VkDescriptorImageInfo shadowInfo{shadowSampler, shadowView,
                                   VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL};
  VkDescriptorBufferInfo lightInfo{lightBuffer, 0, sizeof(SceneLighting)};
  VkWriteDescriptorSet lighting[2]{};
  lighting[0] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
  lighting[0].dstSet = descriptor;
  lighting[0].dstBinding = 4;
  lighting[0].descriptorCount = 1;
  lighting[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  lighting[0].pImageInfo = &shadowInfo;
  lighting[1] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
  lighting[1].dstSet = descriptor;
  lighting[1].dstBinding = 5;
  lighting[1].descriptorCount = 1;
  lighting[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  lighting[1].pBufferInfo = &lightInfo;
  vkUpdateDescriptorSets(device, 2, lighting, 0, nullptr);
  VkDescriptorImageInfo ii{sampler, atlasView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
  VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
  write.dstSet = descriptor;
  write.dstBinding = 0;
  write.descriptorCount = 1;
  write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  write.pImageInfo = &ii;
  vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
}
void Renderer::uploadMaterials(const std::array<std::vector<unsigned char>, 3> &textures,
                               int size) {
  VkSamplerCreateInfo samplerInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
  samplerInfo.magFilter = samplerInfo.minFilter = VK_FILTER_LINEAR;
  samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
  samplerInfo.addressModeU = samplerInfo.addressModeV = samplerInfo.addressModeW =
      VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  samplerInfo.maxLod = 6; // Keep mip footprints inside each atlas tile.
  samplerInfo.anisotropyEnable = anisotropy;
  VkPhysicalDeviceProperties properties{};
  vkGetPhysicalDeviceProperties(physical, &properties);
  samplerInfo.maxAnisotropy =
      anisotropy ? std::min(8.f, properties.limits.maxSamplerAnisotropy) : 1.f;
  check(vkCreateSampler(device, &samplerInfo, nullptr, &materialSampler), "material sampler");
  for (int i = 0; i < 3; ++i) {
    const auto &data = textures[i];
    if (data.size() != std::size_t(size) * size * 4)
      throw std::runtime_error("Invalid material atlas dimensions");
    VkFormat format = i == 0 ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
    VkFormatProperties properties;
    vkGetPhysicalDeviceFormatProperties(physical, format, &properties);
    VkFormatFeatureFlags required = VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT |
                                    VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT;
    int levels = (properties.optimalTilingFeatures & required) == required
                     ? std::min(11, int(std::log2(size)) + 1)
                     : 1;
    VkBuffer staging;
    VkDeviceMemory memory;
    buffer(data.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, staging,
           memory);
    void *p;
    check(vkMapMemory(device, memory, 0, data.size(), 0, &p), "map material");
    std::memcpy(p, data.data(), data.size());
    vkUnmapMemory(device, memory);
    image(size, size, format,
          VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
              VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
          materialImages[i], materialMemory[i], materialViews[i], VK_IMAGE_ASPECT_COLOR_BIT,
          levels);
    begin();
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = materialImages[i];
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, std::uint32_t(levels), 0, 1};
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);
    VkBufferImageCopy copy{};
    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy.imageExtent = {std::uint32_t(size), std::uint32_t(size), 1};
    vkCmdCopyBufferToImage(command, staging, materialImages[i],
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
    int dimension = size;
    for (int level = 1; level < levels; ++level) {
      barrier.subresourceRange.baseMipLevel = level - 1;
      barrier.subresourceRange.levelCount = 1;
      barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
      barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
      barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
      barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
      vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                           0, 0, nullptr, 0, nullptr, 1, &barrier);
      VkImageBlit blit{};
      blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, std::uint32_t(level - 1), 0, 1};
      blit.srcOffsets[1] = {dimension, dimension, 1};
      int next = std::max(1, dimension / 2);
      blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, std::uint32_t(level), 0, 1};
      blit.dstOffsets[1] = {next, next, 1};
      vkCmdBlitImage(command, materialImages[i], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                     materialImages[i], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit,
                     VK_FILTER_LINEAR);
      barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
      barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
      barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
      barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
      vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT,
                           VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1,
                           &barrier);
      dimension = next;
    }
    barrier.subresourceRange.baseMipLevel = levels - 1;
    barrier.subresourceRange.levelCount = 1;
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1,
                         &barrier);
    submit();
    vkDestroyBuffer(device, staging, nullptr);
    vkFreeMemory(device, memory, nullptr);
    VkDescriptorImageInfo imageInfo{materialSampler, materialViews[i],
                                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = descriptor;
    write.dstBinding = i + 1;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &imageInfo;
    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
  }
}
VkShaderModule Renderer::shader(const std::string &name) {
  std::ifstream f(shaderPath / name, std::ios::binary | std::ios::ate);
  if (!f)
    throw std::runtime_error("Missing shader: " + name);
  auto size = f.tellg();
  if (size <= 0 || size % 4 != 0)
    throw std::runtime_error("Invalid SPIR-V");
  std::vector<std::uint32_t> code(static_cast<std::size_t>(size) / 4);
  f.seekg(0);
  f.read(reinterpret_cast<char *>(code.data()), size);
  VkShaderModuleCreateInfo c{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
  c.codeSize = static_cast<std::size_t>(size);
  c.pCode = code.data();
  VkShaderModule m;
  check(vkCreateShaderModule(device, &c, nullptr, &m), "shader module");
  return m;
}
void Renderer::setupShadows() {
  shadowSize = software ? 512 : 2048;
  image(shadowSize, shadowSize, VK_FORMAT_D32_SFLOAT,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, shadowImage,
        shadowMemory, shadowView, VK_IMAGE_ASPECT_DEPTH_BIT);
  buffer(sizeof(SceneLighting), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, lightBuffer,
         lightMemory);
  VkSamplerCreateInfo samplerInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
  samplerInfo.magFilter = samplerInfo.minFilter = VK_FILTER_NEAREST;
  samplerInfo.addressModeU = samplerInfo.addressModeV = samplerInfo.addressModeW =
      VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
  samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
  samplerInfo.compareEnable = VK_TRUE;
  samplerInfo.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
  check(vkCreateSampler(device, &samplerInfo, nullptr, &shadowSampler), "shadow sampler");
  VkAttachmentDescription attachment{};
  attachment.format = VK_FORMAT_D32_SFLOAT;
  attachment.samples = VK_SAMPLE_COUNT_1_BIT;
  attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  attachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
  VkAttachmentReference depth{0, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
  VkSubpassDescription subpass{};
  subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpass.pDepthStencilAttachment = &depth;
  VkSubpassDependency dependencies[2]{};
  dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
  dependencies[0].dstSubpass = 0;
  dependencies[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
  dependencies[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
  dependencies[0].dstStageMask =
      VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
  dependencies[0].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
  dependencies[1].srcSubpass = 0;
  dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
  dependencies[1].srcStageMask =
      VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
  dependencies[1].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
  dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
  dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
  VkRenderPassCreateInfo passInfo{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
  passInfo.attachmentCount = 1;
  passInfo.pAttachments = &attachment;
  passInfo.subpassCount = 1;
  passInfo.pSubpasses = &subpass;
  passInfo.dependencyCount = 2;
  passInfo.pDependencies = dependencies;
  check(vkCreateRenderPass(device, &passInfo, nullptr, &shadowPass), "shadow render pass");
  VkFramebufferCreateInfo framebufferInfo{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
  framebufferInfo.renderPass = shadowPass;
  framebufferInfo.attachmentCount = 1;
  framebufferInfo.pAttachments = &shadowView;
  framebufferInfo.width = framebufferInfo.height = shadowSize;
  framebufferInfo.layers = 1;
  check(vkCreateFramebuffer(device, &framebufferInfo, nullptr, &shadowFramebuffer),
        "shadow framebuffer");
}
void Renderer::pipelines() {
  auto vert = shader("scene.vert.spv"), frag = shader("scene.frag.spv");
  VkPipelineShaderStageCreateInfo stages[2]{};
  stages[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
  stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  stages[0].module = vert;
  stages[0].pName = "main";
  stages[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
  stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  stages[1].module = frag;
  stages[1].pName = "main";
  VkVertexInputBindingDescription binding{0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX};
  VkVertexInputAttributeDescription attrs[] = {
      {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, p)},
      {1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Vertex, color)},
      {2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, u)},
      {3, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal)},
      {4, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Vertex, material)}};
  VkPipelineVertexInputStateCreateInfo vi{
      VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
  vi.vertexBindingDescriptionCount = 1;
  vi.pVertexBindingDescriptions = &binding;
  vi.vertexAttributeDescriptionCount = 5;
  vi.pVertexAttributeDescriptions = attrs;
  VkPipelineInputAssemblyStateCreateInfo ia{
      VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
  ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
  vp.viewportCount = 1;
  vp.scissorCount = 1;
  VkPipelineRasterizationStateCreateInfo rs{
      VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
  rs.polygonMode = VK_POLYGON_MODE_FILL;
  rs.cullMode = VK_CULL_MODE_NONE;
  rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  rs.lineWidth = 1;
  VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
  ms.rasterizationSamples = sceneSamples;
  VkPipelineDepthStencilStateCreateInfo ds{
      VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
  ds.depthTestEnable = VK_TRUE;
  ds.depthWriteEnable = VK_TRUE;
  ds.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
  VkPipelineColorBlendAttachmentState ba{};
  ba.colorWriteMask = 15;
  ba.blendEnable = VK_TRUE;
  ba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
  ba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  ba.colorBlendOp = VK_BLEND_OP_ADD;
  ba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
  ba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  ba.alphaBlendOp = VK_BLEND_OP_ADD;
  VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
  cb.attachmentCount = 1;
  cb.pAttachments = &ba;
  VkDynamicState dynamic[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
  VkPipelineDynamicStateCreateInfo dy{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
  dy.dynamicStateCount = 2;
  dy.pDynamicStates = dynamic;
  VkGraphicsPipelineCreateInfo p{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
  p.stageCount = 2;
  p.pStages = stages;
  p.pVertexInputState = &vi;
  p.pInputAssemblyState = &ia;
  p.pViewportState = &vp;
  p.pRasterizationState = &rs;
  p.pMultisampleState = &ms;
  p.pDepthStencilState = &ds;
  p.pColorBlendState = &cb;
  p.pDynamicState = &dy;
  p.layout = layout;
  p.renderPass = renderPass;
  check(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &p, nullptr, &scenePipeline),
        "scene pipeline");
  ds.depthTestEnable = VK_FALSE;
  ds.depthWriteEnable = VK_FALSE;
  check(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &p, nullptr, &uiPipeline),
        "UI pipeline");
  auto shadowVert = shader("shadow.vert.spv"), shadowFrag = shader("shadow.frag.spv");
  stages[0].module = shadowVert;
  stages[1].module = shadowFrag;
  p.renderPass = shadowPass;
  ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
  cb.attachmentCount = 0;
  cb.pAttachments = nullptr;
  ds.depthTestEnable = ds.depthWriteEnable = VK_TRUE;
  rs.depthBiasEnable = VK_TRUE;
  rs.depthBiasConstantFactor = 1.25f;
  rs.depthBiasSlopeFactor = 1.75f;
  check(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &p, nullptr, &shadowPipeline),
        "shadow pipeline");
  vkDestroyShaderModule(device, shadowVert, nullptr);
  vkDestroyShaderModule(device, shadowFrag, nullptr);
  vkDestroyShaderModule(device, vert, nullptr);
  vkDestroyShaderModule(device, frag, nullptr);
}
void Renderer::swap() {
  glfwGetFramebufferSize(window, &width, &height);
  if (width <= 0 || height <= 0)
    return;
  VkSurfaceCapabilitiesKHR caps;
  check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical, surface, &caps),
        "surface capabilities");
  std::uint32_t n;
  vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &n, nullptr);
  std::vector<VkSurfaceFormatKHR> formats(n);
  vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &n, formats.data());
  if (formats.empty())
    throw std::runtime_error("No surface formats");
  auto fmt = formats[0];
  for (auto &f : formats)
    if (f.format == VK_FORMAT_B8G8R8A8_SRGB) {
      fmt = f;
      break;
    }
  colorFormat = fmt.format;
  extent = caps.currentExtent.width == std::numeric_limits<std::uint32_t>::max()
               ? VkExtent2D{std::clamp(std::uint32_t(width), caps.minImageExtent.width,
                                       caps.maxImageExtent.width),
                            std::clamp(std::uint32_t(height), caps.minImageExtent.height,
                                       caps.maxImageExtent.height)}
               : caps.currentExtent;
  VkSwapchainCreateInfoKHR c{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
  c.surface = surface;
  c.minImageCount =
      std::min(caps.minImageCount + 1, caps.maxImageCount ? caps.maxImageCount : 100u);
  c.imageFormat = colorFormat;
  c.imageColorSpace = fmt.colorSpace;
  c.imageExtent = extent;
  c.imageArrayLayers = 1;
  c.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
  c.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
  c.preTransform = caps.currentTransform;
  c.compositeAlpha = (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR)
                         ? VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR
                         : VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
  c.presentMode = VK_PRESENT_MODE_FIFO_KHR;
  c.clipped = VK_TRUE;
  check(vkCreateSwapchainKHR(device, &c, nullptr, &swapchain), "swapchain");
  vkGetSwapchainImagesKHR(device, swapchain, &n, nullptr);
  images.resize(n);
  vkGetSwapchainImagesKHR(device, swapchain, &n, images.data());
  views.resize(n);
  for (std::size_t j = 0; j < n; ++j) {
    VkImageViewCreateInfo v{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    v.image = images[j];
    v.viewType = VK_IMAGE_VIEW_TYPE_2D;
    v.format = colorFormat;
    v.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    check(vkCreateImageView(device, &v, nullptr, &views[j]), "swapchain view");
  }
  VkImageFormatProperties colorSupport{}, depthSupport{};
  auto colorResult = vkGetPhysicalDeviceImageFormatProperties(
      physical, colorFormat, VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL,
      VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, 0, &colorSupport);
  auto depthResult = vkGetPhysicalDeviceImageFormatProperties(
      physical, VK_FORMAT_D32_SFLOAT, VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL,
      VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, 0, &depthSupport);
  sceneSamples = VK_SAMPLE_COUNT_1_BIT;
  if (!software && colorResult == VK_SUCCESS && depthResult == VK_SUCCESS) {
    auto supported = colorSupport.sampleCounts & depthSupport.sampleCounts;
    if (supported & VK_SAMPLE_COUNT_4_BIT)
      sceneSamples = VK_SAMPLE_COUNT_4_BIT;
    else if (supported & VK_SAMPLE_COUNT_2_BIT)
      sceneSamples = VK_SAMPLE_COUNT_2_BIT;
  }
  bool multisampled = sceneSamples != VK_SAMPLE_COUNT_1_BIT;
  image(extent.width, extent.height, VK_FORMAT_D32_SFLOAT,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, depthImage, depthMemory, depthView,
        VK_IMAGE_ASPECT_DEPTH_BIT, 1, sceneSamples);
  if (multisampled)
    image(extent.width, extent.height, colorFormat, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
          multisampleImage, multisampleMemory, multisampleView, VK_IMAGE_ASPECT_COLOR_BIT, 1,
          sceneSamples);
  VkAttachmentDescription attachments[3]{};
  attachments[0].format = colorFormat;
  attachments[0].samples = sceneSamples;
  attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  attachments[0].storeOp =
      multisampled ? VK_ATTACHMENT_STORE_OP_DONT_CARE : VK_ATTACHMENT_STORE_OP_STORE;
  attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  attachments[0].finalLayout =
      multisampled ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
  attachments[1].format = VK_FORMAT_D32_SFLOAT;
  attachments[1].samples = sceneSamples;
  attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
  attachments[2].format = colorFormat;
  attachments[2].samples = VK_SAMPLE_COUNT_1_BIT;
  attachments[2].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  attachments[2].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  attachments[2].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  attachments[2].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
  VkAttachmentReference color{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
      depth{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL},
      resolve{2, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
  VkSubpassDescription sub{};
  sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  sub.colorAttachmentCount = 1;
  sub.pColorAttachments = &color;
  sub.pDepthStencilAttachment = &depth;
  sub.pResolveAttachments = multisampled ? &resolve : nullptr;
  VkSubpassDependency dep{};
  dep.srcSubpass = VK_SUBPASS_EXTERNAL;
  dep.dstSubpass = 0;
  dep.srcStageMask =
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
  dep.dstStageMask = dep.srcStageMask;
  dep.dstAccessMask =
      VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
  VkRenderPassCreateInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
  rp.attachmentCount = multisampled ? 3 : 2;
  rp.pAttachments = attachments;
  rp.subpassCount = 1;
  rp.pSubpasses = &sub;
  rp.dependencyCount = 1;
  rp.pDependencies = &dep;
  check(vkCreateRenderPass(device, &rp, nullptr, &renderPass), "render pass");
  framebuffers.resize(n);
  for (std::size_t j = 0; j < n; ++j) {
    VkImageView a[] = {multisampled ? multisampleView : views[j], depthView, views[j]};
    VkFramebufferCreateInfo f{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    f.renderPass = renderPass;
    f.attachmentCount = multisampled ? 3 : 2;
    f.pAttachments = a;
    f.width = extent.width;
    f.height = extent.height;
    f.layers = 1;
    check(vkCreateFramebuffer(device, &f, nullptr, &framebuffers[j]), "framebuffer");
  }
  pipelines();
}
void Renderer::destroySwap() {
  if (!device)
    return;
  vkDeviceWaitIdle(device);
  for (auto f : framebuffers)
    vkDestroyFramebuffer(device, f, nullptr);
  framebuffers.clear();
  if (shadowPipeline)
    vkDestroyPipeline(device, shadowPipeline, nullptr);
  shadowPipeline = VK_NULL_HANDLE;
  if (scenePipeline)
    vkDestroyPipeline(device, scenePipeline, nullptr);
  if (uiPipeline)
    vkDestroyPipeline(device, uiPipeline, nullptr);
  if (renderPass)
    vkDestroyRenderPass(device, renderPass, nullptr);
  for (auto v : views)
    vkDestroyImageView(device, v, nullptr);
  views.clear();
  if (multisampleView)
    vkDestroyImageView(device, multisampleView, nullptr);
  if (multisampleImage)
    vkDestroyImage(device, multisampleImage, nullptr);
  if (multisampleMemory)
    vkFreeMemory(device, multisampleMemory, nullptr);
  multisampleView = VK_NULL_HANDLE;
  multisampleImage = VK_NULL_HANDLE;
  multisampleMemory = VK_NULL_HANDLE;
  if (depthView)
    vkDestroyImageView(device, depthView, nullptr);
  if (depthImage)
    vkDestroyImage(device, depthImage, nullptr);
  if (depthMemory)
    vkFreeMemory(device, depthMemory, nullptr);
  if (swapchain)
    vkDestroySwapchainKHR(device, swapchain, nullptr);
  scenePipeline = uiPipeline = VK_NULL_HANDLE;
  renderPass = VK_NULL_HANDLE;
  depthView = VK_NULL_HANDLE;
  depthImage = VK_NULL_HANDLE;
  depthMemory = VK_NULL_HANDLE;
  swapchain = VK_NULL_HANDLE;
}
void Renderer::setInstances(std::vector<Instance> instances) {
  pendingInstances = std::move(instances);
  instancesDirty = true;
}
void Renderer::uploadInstances() {
  if (!instancesDirty)
    return;
  if (!instanceCube) {
    std::vector<Vertex> cube;
    box(cube, {}, {1, 1, 1}, {1, 1, 1, 1});
    buffer(cube.size() * sizeof(Vertex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, instanceCube,
           cubeMemory);
    void *mapped;
    check(vkMapMemory(device, cubeMemory, 0, cube.size() * sizeof(Vertex), 0, &mapped),
          "map instance cube");
    std::memcpy(mapped, cube.data(), cube.size() * sizeof(Vertex));
    vkUnmapMemory(device, cubeMemory);
  }
  std::size_t bytes = std::max(sizeof(Instance), pendingInstances.size() * sizeof(Instance));
  if (bytes > instanceBytes) {
    if (instanceBuffer)
      vkDestroyBuffer(device, instanceBuffer, nullptr);
    if (instanceMemory)
      vkFreeMemory(device, instanceMemory, nullptr);
    instanceBytes = bytes * 2;
    buffer(instanceBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
           instanceBuffer, instanceMemory);
    VkDescriptorBufferInfo info{instanceBuffer, 0, instanceBytes};
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = descriptor;
    write.dstBinding = 6;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    write.pBufferInfo = &info;
    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
  }
  void *mapped;
  check(vkMapMemory(device, instanceMemory, 0, bytes, 0, &mapped), "map instances");
  if (!pendingInstances.empty())
    std::memcpy(mapped, pendingInstances.data(), pendingInstances.size() * sizeof(Instance));
  else
    std::memset(mapped, 0, bytes);
  vkUnmapMemory(device, instanceMemory);
  instancesDirty = false;
}
void Renderer::draw(const std::vector<Vertex> &world, const std::vector<Vertex> &ui,
                    const std::vector<Batch> &batches, const Mat4 &camera, Vec3 eye,
                    const std::filesystem::path &capture) {
  int w, h;
  glfwGetFramebufferSize(window, &w, &h);
  if (w <= 0 || h <= 0)
    return;
  if (w != width || h != height) {
    destroySwap();
    swap();
  }
  check(vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX), "frame fence");
  uploadInstances();
  std::uint32_t index;
  VkResult result =
      vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, acquired, VK_NULL_HANDLE, &index);
  if (result == VK_ERROR_OUT_OF_DATE_KHR) {
    destroySwap();
    swap();
    return;
  }
  if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
    check(result, "acquire image");
  auto bytes = (world.size() + ui.size()) * sizeof(Vertex);
  if (bytes > vertexBytes) {
    if (vertices)
      vkDestroyBuffer(device, vertices, nullptr);
    if (vertexMemory)
      vkFreeMemory(device, vertexMemory, nullptr);
    vertexBytes = std::max<std::size_t>(bytes * 2, 1024 * 1024);
    buffer(vertexBytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, vertices,
           vertexMemory);
  }
  if (bytes) {
    void *p;
    check(vkMapMemory(device, vertexMemory, 0, bytes, 0, &p), "map vertices");
    std::memcpy(p, world.data(), world.size() * sizeof(Vertex));
    std::memcpy(static_cast<char *>(p) + world.size() * sizeof(Vertex), ui.data(),
                ui.size() * sizeof(Vertex));
    vkUnmapMemory(device, vertexMemory);
  }
  Vec3 forward = normalized(Vec3{-camera[2], -camera[6], -camera[10]});
  Vec3 focus = eye;
  (void)forward;
  Vec3 light = normalized(Vec3{-.35f, .85f, .3f});
  Mat4 view = lookAt(focus + light * 100, focus);
  // Quantize in light space so subpixel camera motion does not shimmer on steel rails.
  float texel = 128.f / shadowSize;
  view[12] = std::round(view[12] / texel) * texel;
  view[13] = std::round(view[13] / texel) * texel;
  Mat4 lightMvp = mul(orthographic(64, 1, 220), view);
  void *lightData;
  SceneLighting lighting{};
  lighting.sun = lightMvp;
  lighting.counts[0] =
      float(std::min(emergencyLights.size(), software ? std::size_t(4) : std::size_t(16)));
  for (int n = 0; n < int(lighting.counts[0]); ++n)
    for (int axis = 0; axis < 4; ++axis) {
      lighting.fire[n][axis] = emergencyLights[n][axis];
      lighting.smoke[n][axis] = emergencyLights[n][axis + 4];
    }
  check(vkMapMemory(device, lightMemory, 0, sizeof(SceneLighting), 0, &lightData),
        "map scene lighting");
  std::memcpy(lightData, &lighting, sizeof(SceneLighting));
  vkUnmapMemory(device, lightMemory);
  begin();
  VkClearValue shadowClear{};
  shadowClear.depthStencil = {1, 0};
  VkRenderPassBeginInfo shadowBegin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
  shadowBegin.renderPass = shadowPass;
  shadowBegin.framebuffer = shadowFramebuffer;
  shadowBegin.renderArea.extent = {std::uint32_t(shadowSize), std::uint32_t(shadowSize)};
  shadowBegin.clearValueCount = 1;
  shadowBegin.pClearValues = &shadowClear;
  vkCmdBeginRenderPass(command, &shadowBegin, VK_SUBPASS_CONTENTS_INLINE);
  VkViewport shadowViewport{0, 0, float(shadowSize), float(shadowSize), 0, 1};
  VkRect2D shadowScissor{{0, 0}, shadowBegin.renderArea.extent};
  vkCmdSetViewport(command, 0, 1, &shadowViewport);
  vkCmdSetScissor(command, 0, 1, &shadowScissor);
  VkDeviceSize shadowOffset = 0;
  vkCmdBindVertexBuffers(command, 0, 1, &vertices, &shadowOffset);
  vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &descriptor, 0,
                          nullptr);
  vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowPipeline);
  if (!world.empty())
    vkCmdDraw(command, world.size(), 1, 0, 0);
  vkCmdEndRenderPass(command);
  VkClearValue clear[2]{};
  clear[0].color = {{.018f, .029f, .037f, 1}};
  clear[1].depthStencil = {1, 0};
  VkRenderPassBeginInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
  rp.renderPass = renderPass;
  rp.framebuffer = framebuffers[index];
  rp.renderArea.extent = extent;
  rp.clearValueCount = 2;
  rp.pClearValues = clear;
  vkCmdBeginRenderPass(command, &rp, VK_SUBPASS_CONTENTS_INLINE);
  VkViewport vp{0, 0, float(extent.width), float(extent.height), 0, 1};
  VkRect2D scissor{{0, 0}, extent};
  vkCmdSetViewport(command, 0, 1, &vp);
  vkCmdSetScissor(command, 0, 1, &scissor);
  VkDeviceSize offset = 0;
  vkCmdBindVertexBuffers(command, 0, 1, &vertices, &offset);
  vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &descriptor, 0,
                          nullptr);
  struct Push {
    Mat4 mvp;
    float ui;
    float padding[3];
    Vec3 eye;
  } push{camera, 0, {0, 0, 0}, eye};
  vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, scenePipeline);
  vkCmdPushConstants(command, layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                     sizeof(push), &push);
  if (!world.empty())
    vkCmdDraw(command, world.size(), 1, 0, 0);
  if (!pendingInstances.empty()) {
    push.ui = -1;
    vkCmdPushConstants(command, layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(push), &push);
    vkCmdBindVertexBuffers(command, 0, 1, &instanceCube, &offset);
    vkCmdDraw(command, 36, std::uint32_t(pendingInstances.size()), 0, 0);
    vkCmdBindVertexBuffers(command, 0, 1, &vertices, &offset);
  }
  push.mvp = {2.f / extent.width, 0, 0, 0, 0, 2.f / extent.height, 0, 0, 0, 0, 1, 0, -1, -1, 0, 1};
  push.ui = 1;
  vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, uiPipeline);
  vkCmdPushConstants(command, layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                     sizeof(push), &push);
  for (auto &b : batches) {
    int x = std::clamp(int(b.clip.x), 0, int(extent.width)),
        y = std::clamp(int(b.clip.y), 0, int(extent.height));
    int right = std::clamp(int(b.clip.x + b.clip.w), x, int(extent.width)),
        bottom = std::clamp(int(b.clip.y + b.clip.h), y, int(extent.height));
    VkRect2D clip{{x, y}, {std::uint32_t(right - x), std::uint32_t(bottom - y)}};
    vkCmdSetScissor(command, 0, 1, &clip);
    if (clip.extent.width && clip.extent.height)
      vkCmdDraw(command, b.count, 1, world.size() + b.first, 0);
  }
  vkCmdEndRenderPass(command);
  VkBuffer readback{};
  VkDeviceMemory readMemory{};
  if (!capture.empty()) {
    buffer(extent.width * extent.height * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, readback,
           readMemory);
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    b.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    b.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    b.image = images[index];
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
    VkBufferImageCopy c{};
    c.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    c.imageExtent = {extent.width, extent.height, 1};
    vkCmdCopyImageToBuffer(command, images[index], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback,
                           1, &c);
    b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    b.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    b.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    b.dstAccessMask = 0;
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
  }
  check(vkEndCommandBuffer(command), "end frame");
  check(vkResetFences(device, 1, &fence), "reset frame fence");
  VkPipelineStageFlags wait = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  VkSubmitInfo s{VK_STRUCTURE_TYPE_SUBMIT_INFO};
  s.waitSemaphoreCount = 1;
  s.pWaitSemaphores = &acquired;
  s.pWaitDstStageMask = &wait;
  s.commandBufferCount = 1;
  s.pCommandBuffers = &command;
  s.signalSemaphoreCount = 1;
  s.pSignalSemaphores = &finished;
  check(vkQueueSubmit(queue, 1, &s, fence), "submit frame");
  VkPresentInfoKHR p{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
  p.waitSemaphoreCount = 1;
  p.pWaitSemaphores = &finished;
  p.swapchainCount = 1;
  p.pSwapchains = &swapchain;
  p.pImageIndices = &index;
  result = vkQueuePresentKHR(queue, &p);
  if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR && result != VK_ERROR_OUT_OF_DATE_KHR)
    check(result, "present");
  check(vkQueueWaitIdle(queue), "present idle");
  if (readback) {
    void *data;
    check(vkMapMemory(device, readMemory, 0, VK_WHOLE_SIZE, 0, &data), "read screenshot");
    std::ofstream file(capture, std::ios::binary);
    file << "P6\n" << extent.width << " " << extent.height << "\n255\n";
    auto *pixels = static_cast<unsigned char *>(data);
    bool bgra = colorFormat == VK_FORMAT_B8G8R8A8_SRGB || colorFormat == VK_FORMAT_B8G8R8A8_UNORM;
    for (std::size_t i = 0; i < extent.width * extent.height; ++i) {
      char rgb[] = {char(pixels[i * 4 + (bgra ? 2 : 0)]), char(pixels[i * 4 + 1]),
                    char(pixels[i * 4 + (bgra ? 0 : 2)])};
      file.write(rgb, 3);
    }
    vkUnmapMemory(device, readMemory);
    vkDestroyBuffer(device, readback, nullptr);
    vkFreeMemory(device, readMemory, nullptr);
  }
}
std::vector<float> Renderer::pressure(const std::vector<std::array<float, 4>> &input) {
  if (input.empty())
    return {};
  std::vector<float> out(input.size());
  if (software) {
    for (std::size_t i = 0; i < input.size(); ++i) {
      auto n = input[i];
      out[i] =
          std::clamp(n[0] * .3f + n[1] * .35f + (1 - n[2]) * .15f + (1 - n[3]) * .2f, 0.f, 1.f);
    }
    return out;
  }
  vkDeviceWaitIdle(device);
  VkBuffer inBuf, outBuf;
  VkDeviceMemory inMem, outMem;
  buffer(input.size() * 16, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, inBuf, inMem);
  buffer(input.size() * 4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, outBuf,
         outMem);
  void *p;
  check(vkMapMemory(device, inMem, 0, VK_WHOLE_SIZE, 0, &p), "map compute input");
  std::memcpy(p, input.data(), input.size() * 16);
  vkUnmapMemory(device, inMem);
  VkDescriptorSetLayoutBinding b[2] = {
      {0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
      {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}};
  VkDescriptorSetLayoutCreateInfo dc{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
  dc.bindingCount = 2;
  dc.pBindings = b;
  VkDescriptorSetLayout dl;
  check(vkCreateDescriptorSetLayout(device, &dc, nullptr, &dl), "compute descriptors");
  VkPushConstantRange range{VK_SHADER_STAGE_COMPUTE_BIT, 0, 4};
  VkPipelineLayoutCreateInfo lc{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  lc.setLayoutCount = 1;
  lc.pSetLayouts = &dl;
  lc.pushConstantRangeCount = 1;
  lc.pPushConstantRanges = &range;
  VkPipelineLayout pl;
  check(vkCreatePipelineLayout(device, &lc, nullptr, &pl), "compute layout");
  VkDescriptorPoolSize ps{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2};
  VkDescriptorPoolCreateInfo pc{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
  pc.maxSets = 1;
  pc.poolSizeCount = 1;
  pc.pPoolSizes = &ps;
  VkDescriptorPool dp;
  check(vkCreateDescriptorPool(device, &pc, nullptr, &dp), "compute pool");
  VkDescriptorSetAllocateInfo da{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
  da.descriptorPool = dp;
  da.descriptorSetCount = 1;
  da.pSetLayouts = &dl;
  VkDescriptorSet ds;
  check(vkAllocateDescriptorSets(device, &da, &ds), "compute set");
  VkDescriptorBufferInfo bi[] = {{inBuf, 0, VK_WHOLE_SIZE}, {outBuf, 0, VK_WHOLE_SIZE}};
  VkWriteDescriptorSet writes[2]{};
  for (int i = 0; i < 2; ++i) {
    writes[i] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    writes[i].dstSet = ds;
    writes[i].dstBinding = i;
    writes[i].descriptorCount = 1;
    writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[i].pBufferInfo = &bi[i];
  }
  vkUpdateDescriptorSets(device, 2, writes, 0, nullptr);
  auto module = shader("pressure.comp.spv");
  VkComputePipelineCreateInfo ci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
  ci.layout = pl;
  ci.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
  ci.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
  ci.stage.module = module;
  ci.stage.pName = "main";
  VkPipeline pipeline;
  check(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &ci, nullptr, &pipeline),
        "compute pipeline");
  begin();
  vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
  vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE, pl, 0, 1, &ds, 0, nullptr);
  std::uint32_t n = input.size();
  vkCmdPushConstants(command, pl, VK_SHADER_STAGE_COMPUTE_BIT, 0, 4, &n);
  vkCmdDispatch(command, (n + 255) / 256, 1, 1);
  VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
  barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
  barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
  vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0,
                       1, &barrier, 0, nullptr, 0, nullptr);
  submit();
  check(vkMapMemory(device, outMem, 0, VK_WHOLE_SIZE, 0, &p), "map compute output");
  std::memcpy(out.data(), p, out.size() * 4);
  vkUnmapMemory(device, outMem);
  vkDestroyPipeline(device, pipeline, nullptr);
  vkDestroyShaderModule(device, module, nullptr);
  vkDestroyPipelineLayout(device, pl, nullptr);
  vkDestroyDescriptorPool(device, dp, nullptr);
  vkDestroyDescriptorSetLayout(device, dl, nullptr);
  vkDestroyBuffer(device, inBuf, nullptr);
  vkDestroyBuffer(device, outBuf, nullptr);
  vkFreeMemory(device, inMem, nullptr);
  vkFreeMemory(device, outMem, nullptr);
  return out;
}
Renderer::~Renderer() {
  if (!device)
    return;
  vkDeviceWaitIdle(device);
  destroySwap();
  if (vertices)
    vkDestroyBuffer(device, vertices, nullptr);
  if (vertexMemory)
    vkFreeMemory(device, vertexMemory, nullptr);
  if (instanceBuffer)
    vkDestroyBuffer(device, instanceBuffer, nullptr);
  if (instanceMemory)
    vkFreeMemory(device, instanceMemory, nullptr);
  if (instanceCube)
    vkDestroyBuffer(device, instanceCube, nullptr);
  if (cubeMemory)
    vkFreeMemory(device, cubeMemory, nullptr);
  vkDestroyDescriptorPool(device, descriptorPool, nullptr);
  vkDestroyFramebuffer(device, shadowFramebuffer, nullptr);
  vkDestroyRenderPass(device, shadowPass, nullptr);
  vkDestroySampler(device, shadowSampler, nullptr);
  vkDestroyImageView(device, shadowView, nullptr);
  vkDestroyImage(device, shadowImage, nullptr);
  vkFreeMemory(device, shadowMemory, nullptr);
  vkDestroyBuffer(device, lightBuffer, nullptr);
  vkFreeMemory(device, lightMemory, nullptr);
  for (int i = 0; i < 3; ++i) {
    if (materialViews[i])
      vkDestroyImageView(device, materialViews[i], nullptr);
    if (materialImages[i])
      vkDestroyImage(device, materialImages[i], nullptr);
    if (materialMemory[i])
      vkFreeMemory(device, materialMemory[i], nullptr);
  }
  if (materialSampler)
    vkDestroySampler(device, materialSampler, nullptr);
  vkDestroySampler(device, sampler, nullptr);
  vkDestroyImageView(device, atlasView, nullptr);
  vkDestroyImage(device, atlasImage, nullptr);
  vkFreeMemory(device, atlasMemory, nullptr);
  vkDestroyPipelineLayout(device, layout, nullptr);
  vkDestroyDescriptorSetLayout(device, descriptorLayout, nullptr);
  vkDestroySemaphore(device, acquired, nullptr);
  vkDestroySemaphore(device, finished, nullptr);
  vkDestroyFence(device, fence, nullptr);
  vkDestroyCommandPool(device, pool, nullptr);
  vkDestroyDevice(device, nullptr);
  vkDestroySurfaceKHR(instance, surface, nullptr);
  vkDestroyInstance(instance, nullptr);
}
void box(std::vector<Vertex> &v, Vec3 c, Vec3 s, Color col) {
  s = s * .5f;
  Vec3 p[8] = {{c.x - s.x, c.y - s.y, c.z - s.z}, {c.x + s.x, c.y - s.y, c.z - s.z},
               {c.x + s.x, c.y + s.y, c.z - s.z}, {c.x - s.x, c.y + s.y, c.z - s.z},
               {c.x - s.x, c.y - s.y, c.z + s.z}, {c.x + s.x, c.y - s.y, c.z + s.z},
               {c.x + s.x, c.y + s.y, c.z + s.z}, {c.x - s.x, c.y + s.y, c.z + s.z}};
  int faces[6][4] = {{0, 3, 2, 1}, {4, 5, 6, 7}, {0, 4, 7, 3},
                     {1, 2, 6, 5}, {0, 1, 5, 4}, {3, 7, 6, 2}};
  for (auto &f : faces) {
    Vec3 n = normalized(cross(p[f[1]] - p[f[0]], p[f[2]] - p[f[0]]));
    for (int i : {0, 1, 2, 0, 2, 3})
      v.push_back({p[f[i]], col, 0, 0, n});
  }
}
void cylinder(std::vector<Vertex> &v, Vec3 a, Vec3 b, float radius, Color c, int segments) {
  Vec3 axis = normalized(b - a),
       u = normalized(cross(axis, std::abs(axis.y) > .9f ? Vec3{1, 0, 0} : Vec3{0, 1, 0})),
       w = cross(axis, u);
  for (int i = 0; i < segments; ++i) {
    float t = float(i) * 6.2831853f / segments, t2 = float(i + 1) * 6.2831853f / segments;
    Vec3 n = u * std::cos(t) + w * std::sin(t), m = u * std::cos(t2) + w * std::sin(t2);
    Vertex p[] = {{a + n * radius, c, 0, 0, n},
                  {b + n * radius, c, 0, 0, n},
                  {b + m * radius, c, 0, 0, m},
                  {a + m * radius, c, 0, 0, m}};
    for (int k : {0, 1, 2, 0, 2, 3})
      v.push_back(p[k]);
  }
}
void sphere(std::vector<Vertex> &v, Vec3 centre, Vec3 radius, Color c, int rings, int sectors) {
  for (int r = 0; r < rings; ++r)
    for (int s = 0; s < sectors; ++s) {
      Vertex p[4];
      for (int k = 0; k < 4; ++k) {
        float lat = -1.5707963f + 3.1415926f * (r + (k >= 2 ? 1 : 0)) / rings,
              lon = 6.2831853f * (s + ((k == 1 || k == 2) ? 1 : 0)) / sectors;
        Vec3 n = {std::cos(lat) * std::cos(lon), std::sin(lat), std::cos(lat) * std::sin(lon)};
        p[k] = {{centre.x + n.x * radius.x, centre.y + n.y * radius.y, centre.z + n.z * radius.z},
                c,
                0,
                0,
                n};
      }
      for (int k : {0, 1, 2, 0, 2, 3})
        v.push_back(p[k]);
    }
}
} // namespace mega
