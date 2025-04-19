#include "common.h"
#include "utils.h"
#include "vulkan/vulkan_core.h"

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
										 VkDescriptorSetLayoutCreateFlags flags) {
        KVK_PROFILE();
		for(auto& binding : bindings) {
			binding.stageFlags |= shaderFlags;
		}

		VkDescriptorSetLayoutCreateInfo createInfo = {
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
			.flags = flags,
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
        KVK_PROFILE();
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
        KVK_PROFILE();
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

	void transitionImageMip(VkCommandBuffer cmd,
                            VkImage image,
                            std::uint32_t baseMipLevel,
                            std::uint32_t levelCount,
                            VkImageLayout currentLayout,
                            VkImageLayout newLayout,
                            VkPipelineStageFlags2 srcStageMask,
                            VkAccessFlags2 srcAccessMask,
                            VkPipelineStageFlags2 dstStageMask,
                            VkAccessFlags2 dstAccessMask) {
        KVK_PROFILE();
        VkImageMemoryBarrier2 imageBarrier = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = srcStageMask,
            .srcAccessMask = srcAccessMask,
            .dstStageMask = dstStageMask,                                   
            .dstAccessMask = dstAccessMask,
            .oldLayout = currentLayout,
            .newLayout = newLayout,
        };


        VkImageAspectFlags aspectMask;
        if(newLayout == VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL) {
            aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        } else if(newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
            aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
        } else {
            aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        }

        imageBarrier.subresourceRange = imageSubresourceRange(aspectMask, baseMipLevel, levelCount);
        imageBarrier.image = image;

        VkDependencyInfo depInfo {};
        depInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        depInfo.pNext = nullptr;

        depInfo.imageMemoryBarrierCount = 1;
        depInfo.pImageMemoryBarriers = &imageBarrier;

        vkCmdPipelineBarrier2(cmd, &depInfo);
    }

    void transitionImage(VkCommandBuffer cmd,
                         VkImage image,
                         VkImageLayout currentLayout,
                         VkImageLayout newLayout,
                         VkPipelineStageFlags2 srcStageMask,
                         VkAccessFlags2 srcAccessMask,
                         VkPipelineStageFlags2 dstStageMask,
                         VkAccessFlags2 dstAccessMask) {
        transitionImageMip(cmd,
                           image,
                           0,
                           VK_REMAINING_MIP_LEVELS,
                           currentLayout,
                           newLayout,
                           srcStageMask,
                           srcAccessMask,
                           dstStageMask,
                           dstAccessMask);
    }


	VkImageCreateInfo imageCreateInfo(VkPhysicalDevice physicalDevice,
									  VkFormat format,
									  VkImageUsageFlags usageFlags,
									  VkExtent3D extent,
                                      std::uint32_t arrayLayerCount,
                                      std::uint32_t mipLevels) {
        KVK_PROFILE();
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

	void blitImageToImage(VkCommandBuffer cmd,
						  VkImage src,
						  VkImage dst,
						  VkExtent2D srcExtent,
						  VkExtent2D dstExtent,
                          VkImageAspectFlags aspect,
                          std::uint32_t srcMipLevel,
                          std::uint32_t dstMipLevel) {
        KVK_PROFILE();
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
        KVK_PROFILE();
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

        queueMutex.lock();
		retval = vkQueueSubmit(queue,
							   1,
							   &submitInfo,
							   fence);
		if(retval != VK_SUCCESS) {
			logError("Queue submit failed");
            queueMutex.unlock();
			return retval;
        }
        queueMutex.unlock();

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
}
