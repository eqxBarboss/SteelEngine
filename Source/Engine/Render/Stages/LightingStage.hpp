#pragma once

#include "Engine/Render/RenderHelpers.hpp"
#include "Engine/Render/Vulkan/Resources/DescriptorProvider.hpp"

class Scene;
class ComputePipeline;

class LightingStage
{
public:
    LightingStage(const std::vector<vk::ImageView>& gBufferImageViews_);

    ~LightingStage();

    void RegisterScene(const Scene* scene_);

    void RemoveScene();

    void Execute(vk::CommandBuffer commandBuffer, uint32_t imageIndex) const;

    void Resize(const std::vector<vk::ImageView>& gBufferImageViews_);

    void ReloadShaders();

private:
    const Scene* scene = nullptr;

    CameraData cameraData;

    std::vector<vk::ImageView> gBufferImageViews;

    std::unique_ptr<ComputePipeline> pipeline;

    std::unique_ptr<FrameDescriptorProvider> descriptorProvider;
};
