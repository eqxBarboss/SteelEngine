#pragma once

#include "Engine/Scene/Material.hpp"

class DescriptorProvider;
class GraphicsPipeline;

enum class PipelineStage
{
    eGBuffer,
    eForward,
};

class PipelineCache
{
public:
    PipelineCache(PipelineStage stage_, vk::RenderPass pass_);

    const GraphicsPipeline& GetPipeline(MaterialFlags flags);

    DescriptorProvider& GetDescriptorProvider() const;

    void ReloadPipelines();

private:
    PipelineStage stage;
    vk::RenderPass pass;

    std::map<MaterialFlags, std::unique_ptr<GraphicsPipeline>> pipelines;

    std::vector<vk::DescriptorSetLayout> descriptorSetLayouts;

    std::unique_ptr<DescriptorProvider> descriptorProvider;
};
