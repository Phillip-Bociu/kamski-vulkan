#pragma once
#include <vulkan/vulkan.h>

namespace kvk {
	/*=====================================
	  Struct fillers
	  =====================================*/
	VkImageSubresourceRange imageSubresourceRange(VkImageAspectFlags aspectMask);
	VkImageCreateInfo imageCreateInfo(VkFormat format,
									  VkImageUsageFlags usageFlags,
									  VkExtent3D extent);
	
	VkImageViewCreateInfo imageViewCreateInfo(VkFormat format,
											  VkImage image,
											  VkImageAspectFlags aspectFlags);



	/*=====================================
	  Commands
	  =====================================*/
	void transitionImage(VkCommandBuffer cmd,
						 VkImage image,
						 VkImageLayout currentLayout,
						 VkImageLayout newLayout);

	void blitImageToImage(VkCommandBuffer cmd,
						  VkImage src,
						  VkImage dst,
						  VkExtent2D srcExtent,
						  VkExtent2D dstExtent);
}
