#pragma once

#include <vulkan/vulkan_core.h>
#include <algorithm>
#include <cstring>
#include <span>
#include <vector>

namespace kvk {
    // Query and retain only supported cross-vendor dynamic-state features.
    // The pNext chain points into this object, so it must stay at the same address until vkCreateDevice.
    struct DynamicStateFeatures {
        VkPhysicalDeviceExtendedDynamicState3FeaturesEXT              extended3                = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_3_FEATURES_EXT };
        VkPhysicalDeviceExtendedDynamicState2FeaturesEXT              extended2                = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_2_FEATURES_EXT };
        VkPhysicalDeviceVertexInputDynamicStateFeaturesEXT            vertexInput              = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_INPUT_DYNAMIC_STATE_FEATURES_EXT };
        VkPhysicalDeviceColorWriteEnableFeaturesEXT                   colorWrite               = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COLOR_WRITE_ENABLE_FEATURES_EXT };
        VkPhysicalDeviceDepthClipEnableFeaturesEXT                    depthClip                = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_CLIP_ENABLE_FEATURES_EXT };
        VkPhysicalDeviceDepthClipControlFeaturesEXT                   depthClipControl         = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_CLIP_CONTROL_FEATURES_EXT };
        VkPhysicalDeviceProvokingVertexFeaturesEXT                    provokingVertex          = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROVOKING_VERTEX_FEATURES_EXT };
        VkPhysicalDeviceTransformFeedbackFeaturesEXT                  transformFeedback        = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TRANSFORM_FEEDBACK_FEATURES_EXT };
        VkPhysicalDeviceBlendOperationAdvancedFeaturesEXT             advancedBlend            = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BLEND_OPERATION_ADVANCED_FEATURES_EXT };
        VkPhysicalDeviceFragmentShadingRateFeaturesKHR                fragmentShadingRate      = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADING_RATE_FEATURES_KHR };
        VkPhysicalDeviceAttachmentFeedbackLoopLayoutFeaturesEXT       feedbackLoopLayout       = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ATTACHMENT_FEEDBACK_LOOP_LAYOUT_FEATURES_EXT };
        VkPhysicalDeviceAttachmentFeedbackLoopDynamicStateFeaturesEXT feedbackLoopDynamicState = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ATTACHMENT_FEEDBACK_LOOP_DYNAMIC_STATE_FEATURES_EXT };
        VkPhysicalDeviceDepthClampControlFeaturesEXT                  depthClampControl        = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_CLAMP_CONTROL_FEATURES_EXT };
        VkPhysicalDeviceDepthBiasControlFeaturesEXT                   depthBiasControl         = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_BIAS_CONTROL_FEATURES_EXT };
        VkPhysicalDevicePrimitiveTopologyListRestartFeaturesEXT       topologyListRestart      = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRIMITIVE_TOPOLOGY_LIST_RESTART_FEATURES_EXT };

        std::vector<const char*>                                      extensions;
        void*                                                         chain;

        DynamicStateFeatures(std::span<const VkExtensionProperties> available,
                             std::span<const char* const>           required,
                             void*                                  tail = nullptr) : extensions(required.begin(), required.end()), chain(tail) {
            auto enable = [&](const char* name) {
                const bool supported = std::any_of(available.begin(), available.end(), [&](const VkExtensionProperties& extension) {
                    return std::strcmp(extension.extensionName, name) == 0;
                });
                if(supported && !hasExtension(name)) {
                    extensions.push_back(name);
                }
                return supported;
            };
            auto append = [&](auto& feature, const char* extension) {
                if(enable(extension)) {
                    feature.pNext = chain;
                    chain         = &feature;
                }
            };

            append(extended3, VK_EXT_EXTENDED_DYNAMIC_STATE_3_EXTENSION_NAME);
            append(extended2, VK_EXT_EXTENDED_DYNAMIC_STATE_2_EXTENSION_NAME);
            append(vertexInput, VK_EXT_VERTEX_INPUT_DYNAMIC_STATE_EXTENSION_NAME);
            append(colorWrite, VK_EXT_COLOR_WRITE_ENABLE_EXTENSION_NAME);
            append(depthClip, VK_EXT_DEPTH_CLIP_ENABLE_EXTENSION_NAME);
            append(depthClipControl, VK_EXT_DEPTH_CLIP_CONTROL_EXTENSION_NAME);
            append(provokingVertex, VK_EXT_PROVOKING_VERTEX_EXTENSION_NAME);
            append(transformFeedback, VK_EXT_TRANSFORM_FEEDBACK_EXTENSION_NAME);
            append(advancedBlend, VK_EXT_BLEND_OPERATION_ADVANCED_EXTENSION_NAME);
            append(fragmentShadingRate, VK_KHR_FRAGMENT_SHADING_RATE_EXTENSION_NAME);
            append(feedbackLoopLayout, VK_EXT_ATTACHMENT_FEEDBACK_LOOP_LAYOUT_EXTENSION_NAME);
            if(hasExtension(VK_EXT_ATTACHMENT_FEEDBACK_LOOP_LAYOUT_EXTENSION_NAME)) {
                append(feedbackLoopDynamicState, VK_EXT_ATTACHMENT_FEEDBACK_LOOP_DYNAMIC_STATE_EXTENSION_NAME);
            }
            append(depthClampControl, VK_EXT_DEPTH_CLAMP_CONTROL_EXTENSION_NAME);
            append(depthBiasControl, VK_EXT_DEPTH_BIAS_CONTROL_EXTENSION_NAME);
            append(topologyListRestart, VK_EXT_PRIMITIVE_TOPOLOGY_LIST_RESTART_EXTENSION_NAME);
            enable(VK_EXT_CONSERVATIVE_RASTERIZATION_EXTENSION_NAME);
            enable(VK_EXT_SAMPLE_LOCATIONS_EXTENSION_NAME);
            enable(VK_EXT_DISCARD_RECTANGLES_EXTENSION_NAME);
        }

        DynamicStateFeatures(const DynamicStateFeatures&)            = delete;
        DynamicStateFeatures& operator=(const DynamicStateFeatures&) = delete;

        bool                  hasExtension(const char* name) const {
            return std::any_of(extensions.begin(), extensions.end(), [&](const char* extension) {
                return std::strcmp(extension, name) == 0;
            });
        }

        // Call after vkGetPhysicalDeviceFeatures2. All other queried dynamic-state bits are retained.
        void filter() {
            if(!hasExtension(VK_EXT_TRANSFORM_FEEDBACK_EXTENSION_NAME)) {
                extended3.extendedDynamicState3RasterizationStream = VK_FALSE;
            }
            if(!hasExtension(VK_EXT_CONSERVATIVE_RASTERIZATION_EXTENSION_NAME)) {
                extended3.extendedDynamicState3ConservativeRasterizationMode    = VK_FALSE;
                extended3.extendedDynamicState3ExtraPrimitiveOverestimationSize = VK_FALSE;
            }
            if(!hasExtension(VK_EXT_DEPTH_CLIP_ENABLE_EXTENSION_NAME)) {
                extended3.extendedDynamicState3DepthClipEnable = VK_FALSE;
            }
            if(!hasExtension(VK_EXT_SAMPLE_LOCATIONS_EXTENSION_NAME)) {
                extended3.extendedDynamicState3SampleLocationsEnable = VK_FALSE;
            }
            if(!hasExtension(VK_EXT_BLEND_OPERATION_ADVANCED_EXTENSION_NAME)) {
                extended3.extendedDynamicState3ColorBlendAdvanced = VK_FALSE;
            }
            if(!hasExtension(VK_EXT_PROVOKING_VERTEX_EXTENSION_NAME)) {
                extended3.extendedDynamicState3ProvokingVertexMode = VK_FALSE;
            }
            if(!hasExtension(VK_EXT_DEPTH_CLIP_CONTROL_EXTENSION_NAME)) {
                extended3.extendedDynamicState3DepthClipNegativeOneToOne = VK_FALSE;
            }
            if(!transformFeedback.transformFeedback) {
                provokingVertex.transformFeedbackPreservesProvokingVertex = VK_FALSE;
            }
            if(!feedbackLoopLayout.attachmentFeedbackLoopLayout) {
                feedbackLoopDynamicState.attachmentFeedbackLoopDynamicState = VK_FALSE;
            }

            // These EXT feature bits control states belonging to vendor-specific extensions.
            extended3.extendedDynamicState3ViewportWScalingEnable           = VK_FALSE;
            extended3.extendedDynamicState3ViewportSwizzle                  = VK_FALSE;
            extended3.extendedDynamicState3CoverageToColorEnable            = VK_FALSE;
            extended3.extendedDynamicState3CoverageToColorLocation          = VK_FALSE;
            extended3.extendedDynamicState3CoverageModulationMode           = VK_FALSE;
            extended3.extendedDynamicState3CoverageModulationTableEnable    = VK_FALSE;
            extended3.extendedDynamicState3CoverageModulationTable          = VK_FALSE;
            extended3.extendedDynamicState3CoverageReductionMode            = VK_FALSE;
            extended3.extendedDynamicState3RepresentativeFragmentTestEnable = VK_FALSE;
            extended3.extendedDynamicState3ShadingRateImageEnable           = VK_FALSE;
        }

        static void enableCoreFeatures(VkPhysicalDeviceFeatures& enabled, const VkPhysicalDeviceFeatures& supported) {
            enabled.geometryShader     = supported.geometryShader;
            enabled.tessellationShader = supported.tessellationShader;
            enabled.depthClamp         = supported.depthClamp;
            enabled.depthBiasClamp     = supported.depthBiasClamp;
            enabled.depthBounds        = supported.depthBounds;
            enabled.wideLines          = supported.wideLines;
            enabled.largePoints        = supported.largePoints;
            enabled.multiViewport      = supported.multiViewport;
            enabled.alphaToOne         = supported.alphaToOne;
            enabled.logicOp            = supported.logicOp;
        }

        static void enableVulkan14Features(VkPhysicalDeviceVulkan14Features& enabled, const VkPhysicalDeviceVulkan14Features& supported) {
            // These are core in Vulkan 1.4; do not also chain their promoted extension structs.
            enabled.rectangularLines                       = supported.rectangularLines;
            enabled.bresenhamLines                         = supported.bresenhamLines;
            enabled.smoothLines                            = supported.smoothLines;
            enabled.stippledRectangularLines               = supported.stippledRectangularLines;
            enabled.stippledBresenhamLines                 = supported.stippledBresenhamLines;
            enabled.stippledSmoothLines                    = supported.stippledSmoothLines;
            enabled.vertexAttributeInstanceRateDivisor     = supported.vertexAttributeInstanceRateDivisor;
            enabled.vertexAttributeInstanceRateZeroDivisor = supported.vertexAttributeInstanceRateZeroDivisor;
        }
    };
}
