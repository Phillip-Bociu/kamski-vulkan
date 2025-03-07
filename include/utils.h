#pragma once
#include <vulkan/vulkan.h>

namespace kvk {
	VkImageSubresourceRange imageSubresourceRange(VkImageAspectFlags aspectMask);
	void transitionImage(VkCommandBuffer cmd,
						 VkImage image,
						 VkImageLayout currentLayout,
						 VkImageLayout newLayout);

	VkImageCreateInfo imageCreateInfo(VkFormat format,
									  VkImageUsageFlags usageFlags,
									  VkExtent3D extent);
	
}
