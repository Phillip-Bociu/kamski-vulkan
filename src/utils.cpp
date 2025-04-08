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


	VkImageSubresourceRange imageSubresourceRange(VkImageAspectFlags aspectMask) {
		VkImageSubresourceRange retval = {
			.aspectMask = aspectMask,
			.baseMipLevel = 0,
			.levelCount = VK_REMAINING_MIP_LEVELS,
			.baseArrayLayer = 0,
			.layerCount = VK_REMAINING_ARRAY_LAYERS,
		};
		return retval;
	}

	void transitionImage(VkCommandBuffer cmd,
						 VkImage image,
						 VkImageLayout currentLayout,
						 VkImageLayout newLayout) {
		VkImageMemoryBarrier2 imageBarrier {.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
		imageBarrier.pNext = nullptr;

		imageBarrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
		imageBarrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
		imageBarrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
		imageBarrier.dstAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT;

		imageBarrier.oldLayout = currentLayout;
		imageBarrier.newLayout = newLayout;

		VkImageAspectFlags aspectMask;
		if(newLayout == VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL) {
		    aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
		} else if(newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
		    aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
		} else {
		    aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		}

		imageBarrier.subresourceRange = imageSubresourceRange(aspectMask);
		imageBarrier.image = image;

		VkDependencyInfo depInfo {};
		depInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
		depInfo.pNext = nullptr;

		depInfo.imageMemoryBarrierCount = 1;
		depInfo.pImageMemoryBarriers = &imageBarrier;

		vkCmdPipelineBarrier2(cmd, &depInfo);
	}

	VkImageCreateInfo imageCreateInfo(VkPhysicalDevice physicalDevice,
									  VkFormat format,
									  VkImageUsageFlags usageFlags,
									  VkExtent3D extent,
                                      std::uint32_t arrayLayerCount) {
        const VkImageType imageType = (extent.depth == 1 ? VK_IMAGE_TYPE_2D : VK_IMAGE_TYPE_3D);

		VkPhysicalDeviceImageFormatInfo2 imageFormatInfo = {
			.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2,
			.format = format,
			.type = imageType,
			.tiling = VK_IMAGE_TILING_OPTIMAL,
			.usage = usageFlags,
		};

		return {
			.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
			.pNext = nullptr,
            .flags = arrayLayerCount == 6 ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0u,
			.imageType = imageType,
			.format = format,
			.extent = extent,
			.mipLevels = 1,
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
                                              std::uint32_t baseArrayLayer) {
		return {
			.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
			.pNext = nullptr,
			.image = image,
			.viewType = isCubemap ? VK_IMAGE_VIEW_TYPE_CUBE : VK_IMAGE_VIEW_TYPE_2D,
			.format = format,
			.subresourceRange = {
				.aspectMask = aspectFlags,
				.baseMipLevel = 0,
				.levelCount = 1,
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
                          bool srcIsDepth,
                          bool dstIsDepth) {
  		VkImageBlit2 blitRegion = {
 			.sType = VK_STRUCTURE_TYPE_IMAGE_BLIT_2,

 			.srcSubresource = {
				.aspectMask = static_cast<VkImageAspectFlags>(srcIsDepth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT),
				.mipLevel = 0,
				.baseArrayLayer = 0,
				.layerCount = 1,
 			},

 			.dstSubresource =  {
				.aspectMask = static_cast<VkImageAspectFlags>(dstIsDepth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT),
				.mipLevel = 0,
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

		const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_TRANSFER_BIT;

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
