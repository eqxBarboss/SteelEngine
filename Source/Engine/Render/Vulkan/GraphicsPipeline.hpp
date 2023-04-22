#pragma once

#include "Engine/Render/Vulkan/Shaders/ShaderManager.hpp"
#include "Engine/Render/Vulkan/VulkanHelpers.hpp"

struct VertexInput
{
    VertexFormat format;
    uint32_t stride;
    vk::VertexInputRate inputRate;
};

enum class BlendMode
{
    eDisabled,
    eAlphaBlend,
};

class GraphicsPipeline
{
public:
    struct Description
    {
        vk::PrimitiveTopology topology;
        vk::PolygonMode polygonMode;
        vk::CullModeFlagBits cullMode;
        vk::FrontFace frontFace;
        vk::SampleCountFlagBits sampleCount;
        std::optional<vk::CompareOp> depthTest;
        std::vector<ShaderModule> shaderModules;
        std::vector<VertexInput> vertexInputs;
        std::vector<BlendMode> blendModes;
        std::vector<vk::DescriptorSetLayout> layouts; // retrieve from shader reflection
        std::vector<vk::PushConstantRange> pushConstantRanges;
    };

    static std::unique_ptr<GraphicsPipeline> Create(
            vk::RenderPass renderPass, const Description& description);

    ~GraphicsPipeline();

    vk::Pipeline Get() const { return pipeline; }

    vk::PipelineLayout GetLayout() const { return layout; }

private:
    vk::Pipeline pipeline;

    vk::PipelineLayout layout;

    GraphicsPipeline(vk::Pipeline pipeline_, vk::PipelineLayout layout_);
};
