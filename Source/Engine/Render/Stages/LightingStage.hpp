#pragma once

#include "Engine/Render/RenderHelpers.hpp"
#include "Engine/Render/Vulkan/DescriptorHelpers.hpp"

class ScenePT;
class Scene;
class Camera;
class Environment;
class ComputePipeline;
struct LightVolume;

class LightingStage
{
public:
    LightingStage(Scene* scene_, Camera* camera_, Environment* environment_,
            LightVolume* lightVolume_, const std::vector<vk::ImageView>& gBufferImageViews);

    ~LightingStage();

    void Execute(vk::CommandBuffer commandBuffer, uint32_t imageIndex) const;

    void Resize(const std::vector<vk::ImageView>& gBufferImageViews);

    void ReloadShaders();

private:
    struct LightingData
    {
        vk::Buffer directLightBuffer;
        DescriptorSet descriptorSet;
    };

    Scene* scene = nullptr;
    Camera* camera = nullptr;
    Environment* environment = nullptr;
    LightVolume* lightVolume = nullptr;

    DescriptorSet gBufferDescriptorSet;
    MultiDescriptorSet swapchainDescriptorSet;

    CameraData cameraData;
    LightingData lightingData;

    std::unique_ptr<ComputePipeline> pipeline;

    void SetupCameraData();
    void SetupLightingData();

    void SetupPipeline();
};
