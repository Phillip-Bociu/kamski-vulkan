#include "../../../src/KamskiEngine/KamskiTypes.h"
#include "common.h"
#include "utils.h"
#include "vulkan/vulkan_core.h"
#include <mutex>

namespace kvk {
	VkDescriptorSetLayoutBinding descriptorSetLayoutBinding(std::uint32_t binding,
															VkDescriptorType type) {
		return {
			.binding = binding,
			.descriptorType = type,
			.descriptorCount = 1,
		};
	}

	ReturnCode createDescriptorSetLayout(VkDescriptorSetLayout& setLayout,
										 VkDevice device,
										 VkShaderStageFlags shaderFlags,
										 std::span<VkDescriptorSetLayoutBinding> bindings,
                                         const VkDescriptorSetLayoutBindingFlagsCreateInfo* flags) {
        KAMSKI_PROFILE();
		for(auto& binding : bindings) {
			binding.stageFlags |= shaderFlags;
		}

		VkDescriptorSetLayoutCreateInfo createInfo = {
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
			.pNext = flags,
			.bindingCount = std::uint32_t(bindings.size()),
			.pBindings = bindings.data(),
		};

		if(vkCreateDescriptorSetLayout(device,
									   &createInfo,
									   nullptr,
									   &setLayout) != VK_SUCCESS) {
			logError("Could not create descriptor set layout");
			return ReturnCode::UNKNOWN;
		}
		return ReturnCode::OK;
	}

	ReturnCode createDescriptorPool(VkDescriptorPool& pool,
									VkDevice device,
									VkDescriptorPoolSize* sizes,
									const std::uint32_t sizeCount) {
        KAMSKI_PROFILE();
		std::uint32_t maxSets = 0;
		for(std::uint32_t i = 0; i != sizeCount; i++) {
			maxSets += sizes[i].descriptorCount;
		}

		VkDescriptorPoolCreateInfo info = {
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
			.maxSets = maxSets,
			.poolSizeCount = sizeCount,
			.pPoolSizes = sizes,
		};

		if(vkCreateDescriptorPool(device,
								  &info,
								  nullptr,
								  &pool) != VK_SUCCESS) {
			logError("Could not create descriptor pool");
			return ReturnCode::UNKNOWN;
		}
		return ReturnCode::OK;
	}

	ReturnCode allocateDescriptorSet(VkDescriptorSet& set,
									 VkDescriptorPool pool,
									 VkDevice device,
									 VkDescriptorSetLayout layout) {
        KAMSKI_PROFILE();
		VkDescriptorSetAllocateInfo allocInfo = {
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
			.descriptorPool = pool,
			.descriptorSetCount = 1,
			.pSetLayouts = &layout,
		};

		if(vkAllocateDescriptorSets(device,
									&allocInfo,
									&set) != VK_SUCCESS) {
			logError("Could not allocate descriptor sets");
			return ReturnCode::UNKNOWN;
		}
		return ReturnCode::OK;
	}


	VkImageSubresourceRange imageSubresourceRange(VkImageAspectFlags aspectMask,
                                                  std::uint32_t baseMipLevel,
                                                  std::uint32_t levelCount) {
		VkImageSubresourceRange retval = {
			.aspectMask = aspectMask,
			.baseMipLevel = baseMipLevel,
			.levelCount = levelCount,
			.baseArrayLayer = 0,
			.layerCount = VK_REMAINING_ARRAY_LAYERS,
		};
		return retval;
	}

	void transitionImageMip(const VkCommandBuffer cmd,
                            const VkImage image,
                            const std::uint32_t baseMipLevel,
                            const std::uint32_t levelCount,
                            const VkImageLayout currentLayout,
                            const VkImageLayout newLayout,
                            const VkPipelineStageFlags2 srcStageMask,
                            const VkAccessFlags2 srcAccessMask,
                            const VkPipelineStageFlags2 dstStageMask,
                            const VkAccessFlags2 dstAccessMask,
                            const VkImageAspectFlags aspectMask) {
        KAMSKI_PROFILE();
        VkImageMemoryBarrier2 imageBarrier = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = srcStageMask,
            .srcAccessMask = srcAccessMask,
            .dstStageMask = dstStageMask,                                   
            .dstAccessMask = dstAccessMask,
            .oldLayout = currentLayout,
            .newLayout = newLayout,
        };

        imageBarrier.subresourceRange = imageSubresourceRange(aspectMask, baseMipLevel, levelCount);
        imageBarrier.image = image;

        VkDependencyInfo depInfo {};
        depInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        depInfo.pNext = nullptr;

        depInfo.imageMemoryBarrierCount = 1;
        depInfo.pImageMemoryBarriers = &imageBarrier;

        vkCmdPipelineBarrier2(cmd, &depInfo);
    }

    void transitionImage(const VkCommandBuffer cmd,
                         const VkImage image,
                         const VkImageLayout currentLayout,
                         const VkImageLayout newLayout,
                         const VkPipelineStageFlags2 srcStageMask,
                         const VkAccessFlags2 srcAccessMask,
                         const VkPipelineStageFlags2 dstStageMask,
                         const VkAccessFlags2 dstAccessMask,
                         const VkImageAspectFlags aspectMask) {
        transitionImageMip(cmd,
                           image,
                           0,
                           VK_REMAINING_MIP_LEVELS,
                           currentLayout,
                           newLayout,
                           srcStageMask,
                           srcAccessMask,
                           dstStageMask,
                           dstAccessMask,
                           aspectMask);
    }


	VkImageCreateInfo imageCreateInfo(VkPhysicalDevice physicalDevice,
									  VkFormat format,
									  VkImageUsageFlags usageFlags,
									  VkExtent3D extent,
                                      std::uint32_t arrayLayerCount,
                                      std::uint32_t mipLevels) {
        KAMSKI_PROFILE();
        const VkImageType imageType = (extent.depth == 1 ? VK_IMAGE_TYPE_2D : VK_IMAGE_TYPE_3D);
		return {
			.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
			.pNext = nullptr,
            .flags = arrayLayerCount == 6 ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0u,
			.imageType = imageType,
			.format = format,
			.extent = extent,
			.mipLevels = mipLevels,
			.arrayLayers = arrayLayerCount,
			.samples = VK_SAMPLE_COUNT_1_BIT,
			.tiling = VK_IMAGE_TILING_OPTIMAL,
			.usage = usageFlags,
		};
	}

	VkImageViewCreateInfo imageViewCreateInfo(VkFormat format,
											  VkImage image,
											  VkImageAspectFlags aspectFlags,
                                              bool isCubemap,
                                              std::uint32_t baseArrayLayer,
                                              std::uint32_t mipLevelCount) {
		return {
			.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
			.pNext = nullptr,
			.image = image,
			.viewType = isCubemap ? VK_IMAGE_VIEW_TYPE_CUBE : VK_IMAGE_VIEW_TYPE_2D,
			.format = format,
			.subresourceRange = {
				.aspectMask = aspectFlags,
				.baseMipLevel = 0,
				.levelCount = mipLevelCount,
				.baseArrayLayer = baseArrayLayer,
				.layerCount = (isCubemap ? 6u : 1u),
			},
		};
	}

    VkImageViewCreateInfo imageViewCreateInfo2(VkFormat format,
                                               VkImage image,
                                               VkImageAspectFlags aspectFlags,
                                               bool isCubemap,
                                               std::uint32_t layerIndex,
                                               std::uint32_t layerCount,
                                               std::uint32_t mipIndex,
                                               std::uint32_t mipCount) {
        return {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .pNext = nullptr,
            .image = image,
            .viewType = isCubemap ? VK_IMAGE_VIEW_TYPE_CUBE : VK_IMAGE_VIEW_TYPE_2D,
            .format = format,
            .subresourceRange = {
                .aspectMask = aspectFlags,
                .baseMipLevel = mipIndex,
                .levelCount = mipCount,
                .baseArrayLayer = layerIndex,
                .layerCount = layerCount,
            },
        };
    }

	void blitImageToImage(VkCommandBuffer cmd,
						  VkImage src,
						  VkImage dst,
						  VkExtent2D srcExtent,
						  VkExtent2D dstExtent,
                          VkImageAspectFlags aspect,
                          std::uint32_t srcMipLevel,
                          std::uint32_t dstMipLevel) {
        KAMSKI_PROFILE();
  		VkImageBlit2 blitRegion = {
 			.sType = VK_STRUCTURE_TYPE_IMAGE_BLIT_2,

 			.srcSubresource = {
				.aspectMask = aspect,
				.mipLevel = srcMipLevel,
				.baseArrayLayer = 0,
				.layerCount = 1,
 			},

 			.dstSubresource =  {
				.aspectMask = aspect,
				.mipLevel = dstMipLevel,
				.baseArrayLayer = 0,
				.layerCount = 1,
 			},
  		};

  		blitRegion.srcOffsets[1].x = srcExtent.width;
  		blitRegion.srcOffsets[1].y = srcExtent.height;
  		blitRegion.srcOffsets[1].z = 1;

  		blitRegion.dstOffsets[1].x = dstExtent.width;
  		blitRegion.dstOffsets[1].y = dstExtent.height;
  		blitRegion.dstOffsets[1].z = 1;

  		VkBlitImageInfo2 blitInfo = {
 			.sType = VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2,
 			.srcImage = src,
 			.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
 			.dstImage = dst,
 			.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
 			.regionCount = 1,
 			.pRegions = &blitRegion,
 			.filter = VK_FILTER_LINEAR,
  		};
  		vkCmdBlitImage2(cmd, &blitInfo);
	}

	VkResult immediateSubmit(VkCommandBuffer cmd,
							 VkDevice device,
							 VkQueue queue,
                             std::mutex& queueMutex,
							 std::function<void(VkCommandBuffer)>&& function) {
        KAMSKI_PROFILE();
		VkResult retval;
		VkCommandBufferBeginInfo beginInfo = {
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
			.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
		};

		VkFenceCreateInfo fenceCreateInfo = {
			.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
		};

		VkFence fence;
		retval = vkCreateFence(device,
							   &fenceCreateInfo,
							   nullptr,
							   &fence);
		if(retval != VK_SUCCESS) {
			logError("Could not create fence");
			return retval;
		}

		retval = vkBeginCommandBuffer(cmd,
									  &beginInfo);
		if(retval != VK_SUCCESS) {
			logError("Could not start command buffer recording");
			return retval;
		}

		function(cmd);

		retval = vkEndCommandBuffer(cmd);
		if(retval != VK_SUCCESS) {
			logError("Could not end command buffer");
			return retval;
		}

		VkSubmitInfo submitInfo = {
			.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
			.commandBufferCount = 1,
			.pCommandBuffers = &cmd,
		};

        {
            std::lock_guard lck(queueMutex);
            retval = vkQueueSubmit(queue,
                                   1,
                                   &submitInfo,
                                   fence);
            if(retval != VK_SUCCESS) {
                logError("Queue submit failed");
                return retval;
            }
        }

		vkWaitForFences(device,
						1,
						&fence,
						VK_TRUE,
						std::numeric_limits<std::uint64_t>::max());

		vkDestroyFence(device,
					   fence,
					   nullptr);

		return retval;
	}

    std::uint32_t getMipLevels(std::uint32_t width, std::uint32_t height) {
        std::uint32_t retval = 0;
        while(width != 0 && height != 0) {
            retval++;
            width /= 2;
            height /= 2;
        }
        return retval;
    }

}
