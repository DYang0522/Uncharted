// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <boost/container/small_vector.hpp>

#include <cstdio>
#include <cstdlib>

#include "shader_recompiler/info.h"
#include "video_core/renderer_vulkan/vk_compute_pipeline.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"

namespace Vulkan {

ComputePipeline::ComputePipeline(const Instance& instance, Scheduler& scheduler,
                                 DescriptorHeap& desc_heap, const Shader::Profile& profile,
                                 vk::PipelineCache pipeline_cache, ComputePipelineKey compute_key_,
                                 const Shader::Info& info_, vk::ShaderModule module,
                                 SerializationSupport& sdata, bool preloading /*=false*/)
    : Pipeline{instance, scheduler, desc_heap, profile, pipeline_cache, true},
      compute_key{compute_key_} {
    auto& info = stages[int(Shader::SwStage::Compute)];
    info = &info_;
    const auto debug_str = GetDebugString();

    // gow3 1 Nov 2026 (#328): or the device's own size where the driver could vary it (Intel).
    const u32 subgroup_size = instance.IsSubgroupSize64Supported() ? 64 : instance.FixedComputeSubgroupSize();
    const vk::PipelineShaderStageRequiredSubgroupSizeCreateInfo subgroup_size_ci = {
        .requiredSubgroupSize = subgroup_size,
    };
    const vk::PipelineShaderStageCreateInfo shader_ci = {
        .pNext = subgroup_size ? &subgroup_size_ci : nullptr,
        .stage = vk::ShaderStageFlagBits::eCompute,
        .module = module,
        .pName = "main",
    };

    u32 binding{};
    boost::container::small_vector<vk::DescriptorSetLayoutBinding, 32> bindings;
    for (const auto& buffer : info->buffers) {
        // During deserialization, we don't have access to the UD to fetch sharp data. To address
        // this properly we need to track shaprs or portion of them in `sdata`, but since we're
        // interested only in "is storage" flag (which is not even effective atm), we can take a
        // shortcut there.
        const auto sharp = preloading ? AmdGpu::Buffer{} : buffer.GetSharp(*info);
        bindings.push_back({
            .binding = binding++,
            .descriptorType = vk::DescriptorType::eStorageBuffer,
            .descriptorCount = 1,
            .stageFlags = vk::ShaderStageFlagBits::eCompute,
        });
    }
    for (const auto& image : info->images) {
        const u32 num_bindings = image.NumBindings(*info);
        bindings.push_back({
            .binding = binding,
            .descriptorType = image.is_written ? vk::DescriptorType::eStorageImage
                                               : vk::DescriptorType::eSampledImage,
            .descriptorCount = num_bindings,
            .stageFlags = vk::ShaderStageFlagBits::eCompute,
        });
        binding += num_bindings;
    }
    for (const auto& sampler : info->samplers) {
        bindings.push_back({
            .binding = binding++,
            .descriptorType = vk::DescriptorType::eSampler,
            .descriptorCount = 1,
            .stageFlags = vk::ShaderStageFlagBits::eCompute,
        });
    }

    const vk::PushConstantRange push_constants = {
        .stageFlags = vk::ShaderStageFlagBits::eCompute,
        .offset = 0,
        .size = sizeof(Shader::PushData),
    };

    uses_push_descriptors = binding < instance.MaxPushDescriptors();
    const auto flags = uses_push_descriptors
                           ? vk::DescriptorSetLayoutCreateFlagBits::ePushDescriptorKHR
                           : vk::DescriptorSetLayoutCreateFlagBits{};
    const vk::DescriptorSetLayoutCreateInfo desc_layout_ci = {
        .flags = flags,
        .bindingCount = static_cast<u32>(bindings.size()),
        .pBindings = bindings.data(),
    };
    const auto device = instance.GetDevice();
    auto [descriptor_set_result, descriptor_set] =
        device.createDescriptorSetLayoutUnique(desc_layout_ci);
    ASSERT_MSG(descriptor_set_result == vk::Result::eSuccess,
               "Failed to create compute descriptor set layout: {}",
               vk::to_string(descriptor_set_result));
    desc_layout = std::move(descriptor_set);

    const vk::DescriptorSetLayout set_layout = *desc_layout;
    const vk::PipelineLayoutCreateInfo layout_info = {
        .setLayoutCount = 1U,
        .pSetLayouts = &set_layout,
        .pushConstantRangeCount = 1U,
        .pPushConstantRanges = &push_constants,
    };
    auto [layout_result, layout] = instance.GetDevice().createPipelineLayoutUnique(layout_info);
    ASSERT_MSG(layout_result == vk::Result::eSuccess,
               "Failed to create compute pipeline layout: {}", vk::to_string(layout_result));
    pipeline_layout = std::move(layout);
    SetObjectName(device, *pipeline_layout, "Compute PipelineLayout {}", debug_str);

    const vk::ComputePipelineCreateInfo compute_pipeline_ci = {
        .stage = shader_ci,
        .layout = *pipeline_layout,
    };
    // GOW3 diagnostic: one compact record for runtime pipeline creation only. Skip archive
    // warm-up pipelines so tracing a long play session does not flood the normal game log.
    const char* trace_compute_env = std::getenv("GOW3_TRACE_COMPUTE_PIPELINE");
    const bool trace_compute_pipeline =
        trace_compute_env && trace_compute_env[0] == '1' && !preloading;
    if (trace_compute_pipeline) {
        // Encode all image descriptor types/counts on the same line: S = storage, T = sampled.
        char image_layout[512]{};
        std::size_t image_layout_size = 0;
        for (std::size_t i = 0; i < info->images.size(); ++i) {
            const auto& image = info->images[i];
            const u32 count = image.NumBindings(*info);
            const std::size_t remaining = sizeof(image_layout) - image_layout_size;
            const int written = std::snprintf(
                image_layout + image_layout_size, remaining, "%s%c%u",
                i == 0 ? "" : ",", image.is_written ? 'S' : 'T', count);
            if (written < 0 || static_cast<std::size_t>(written) >= remaining) {
                image_layout[sizeof(image_layout) - 1] = '\0';
                break;
            }
            image_layout_size += static_cast<std::size_t>(written);
        }

        std::fprintf(stderr,
                     "[GOW3_COMPUTE_PIPELINE] shader=0x%016llx key=0x%zx "
                     "pgm_base=0x%016llx hw=%u sw=%u buffers=%zu images=%zu samplers=%zu "
                     "bindings=%u push=%u image_layout=[%s]\n",
                     static_cast<unsigned long long>(info->pgm_hash), compute_key.value,
                     static_cast<unsigned long long>(info->pgm_base),
                     static_cast<unsigned>(info->hw_stage),
                     static_cast<unsigned>(info->sw_stage), info->buffers.size(),
                     info->images.size(), info->samplers.size(), binding,
                     static_cast<unsigned>(uses_push_descriptors), image_layout);
        std::fflush(stderr);
    }

    auto [pipeline_result, pipe] =
        instance.GetDevice().createComputePipelineUnique(pipeline_cache, compute_pipeline_ci);
    ASSERT_MSG(pipeline_result == vk::Result::eSuccess, "Failed to create compute pipeline: {}",
               vk::to_string(pipeline_result));
    pipeline = std::move(pipe);
    SetObjectName(device, *pipeline, "Compute Pipeline {}", debug_str);
}

ComputePipeline::~ComputePipeline() = default;

} // namespace Vulkan
