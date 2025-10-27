#pragma once
#include <cstdint>
#include <span>
#include <functional>
#include <mutex>

#include "common.h"
#include "vulkan/vulkan_core.h"

namespace kvk {
	/*=====================================
	  Misc.
	  =====================================*/
    std::uint32_t getMipLevels(std::uint32_t width, std::uint32_t height);

	/*=====================================
	  Struct fillers
	  =====================================*/
	VkImageSubresourceRange imageSubresourceRange(VkImageAspectFlags aspectMask,
                                                  std::uint32_t baseMipLevel = 0,
                                                  std::uint32_t levelCount = VK_REMAINING_MIP_LEVELS);

	VkImageCreateInfo imageCreateInfo(VkPhysicalDevice physicalDevice,
									  VkFormat format,
									  VkImageUsageFlags usageFlags,
									  VkExtent3D extent,
                                      std::uint32_t arrayLayerCount = 1,
                                      std::uint32_t mipLevels = 1);

	VkImageViewCreateInfo imageViewCreateInfo(VkFormat format,
											  VkImage image,
											  VkImageAspectFlags aspectFlags,
                                              bool isCubemap = false,
                                              std::uint32_t baseArrayLayer = 0,
                                              std::uint32_t mipLevelCount = 1);

    VkImageViewCreateInfo imageViewCreateInfo2(VkFormat format,
                                               VkImage image,
                                               VkImageAspectFlags aspectFlags,
                                               bool isCubemap = false,
                                               std::uint32_t layerIndex = 0,
                                               std::uint32_t layerCount = VK_REMAINING_ARRAY_LAYERS,
                                               std::uint32_t mipIndex = 0,
                                               std::uint32_t mipCount = VK_REMAINING_MIP_LEVELS);

	VkDescriptorSetLayoutBinding descriptorSetLayoutBinding(std::uint32_t binding,
															VkDescriptorType type);

	/*=====================================
	  Constructors
	  =====================================*/
	ReturnCode createDescriptorSetLayout(VkDescriptorSetLayout& setLayout,
										 VkDevice device,
										 VkShaderStageFlags shaderFlags,
										 std::span<VkDescriptorSetLayoutBinding> bindings,
                                         const VkDescriptorSetLayoutBindingFlagsCreateInfo* flags = nullptr,
                                         bool isPushDescriptor = false);

	ReturnCode createDescriptorPool(VkDescriptorPool& pool,
									VkDevice device,
									VkDescriptorPoolSize* sizes,
									const std::uint32_t sizeCount);

	ReturnCode allocateDescriptorSet(VkDescriptorSet& set,
									 VkDescriptorPool pool,
									 VkDevice device,
									 VkDescriptorSetLayout layout);

	/*=====================================
	  Commands
	  =====================================*/
	VkResult immediateSubmit(VkCommandBuffer cmd,
							 VkDevice device,
							 VkQueue queue,
                             std::mutex& queueMutex,
							 std::function<void(VkCommandBuffer)>&& function);

	void transitionImage(VkCommandBuffer cmd,
						 VkImage image,
						 VkImageLayout currentLayout,
                         VkImageLayout newLayout,
                         VkPipelineStageFlags2 srcStageMask  = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                         VkAccessFlags2 srcAccessMask        = VK_ACCESS_2_MEMORY_WRITE_BIT,
                         VkPipelineStageFlags2 dstStageMask  = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                         VkAccessFlags2 dstAccessMask        = VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT,
                         VkImageAspectFlags aspectMask       = VK_IMAGE_ASPECT_COLOR_BIT);

	void transitionImageMip(VkCommandBuffer cmd,
                            VkImage image,
                            std::uint32_t baseMipLevel,
                            std::uint32_t levelCount,
                            VkImageLayout currentLayout,
                            VkImageLayout newLayout,
                            VkPipelineStageFlags2 srcStageMask  = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                            VkAccessFlags2 srcAccessMask        = VK_ACCESS_2_MEMORY_WRITE_BIT,
                            VkPipelineStageFlags2 dstStageMask  = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                            VkAccessFlags2 dstAccessMask        = VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT,
                            VkImageAspectFlags aspectMask       = VK_IMAGE_ASPECT_COLOR_BIT);

	void blitImageToImage(VkCommandBuffer cmd,
						  VkImage src,
						  VkImage dst,
						  VkExtent2D srcExtent,
						  VkExtent2D dstExtent,
                          VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT,
                          std::uint32_t srcMipLevel = 0,
                          std::uint32_t dstMipLevel = 0);
}
