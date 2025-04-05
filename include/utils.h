#pragma once
#include <vulkan/vulkan.h>
#include <cstdint>
#include <span>
#include <functional>

#include "common.h"

namespace kvk {

	/*=====================================
	  Struct fillers
	  =====================================*/
	VkImageSubresourceRange imageSubresourceRange(VkImageAspectFlags aspectMask);

	VkImageCreateInfo imageCreateInfo(VkPhysicalDevice physicalDevice,
									  VkFormat format,
									  VkImageUsageFlags usageFlags,
									  VkExtent3D extent,
                                      std::uint32_t arrayLayerCount = 1);

	VkImageViewCreateInfo imageViewCreateInfo(VkFormat format,
											  VkImage image,
											  VkImageAspectFlags aspectFlags,
                                              bool isCubemap = false);

	VkDescriptorSetLayoutBinding descriptorSetLayoutBinding(std::uint32_t binding,
															VkDescriptorType type);

	/*=====================================
	  Constructors
	  =====================================*/
	ReturnCode createDescriptorSetLayout(VkDescriptorSetLayout& setLayout,
										 VkDevice device,
										 VkShaderStageFlags shaderFlags,
										 std::span<VkDescriptorSetLayoutBinding> bindings,
										 VkDescriptorSetLayoutCreateFlags flags = 0);

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
						 VkImageLayout newLayout);

	void blitImageToImage(VkCommandBuffer cmd,
						  VkImage src,
						  VkImage dst,
						  VkExtent2D srcExtent,
						  VkExtent2D dstExtent,
                          bool srcIsDepth = false,
                          bool dstIsDepth = false);
}
