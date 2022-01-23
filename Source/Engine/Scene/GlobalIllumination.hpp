#pragma once

#include "Engine/Render/Vulkan/Resources/TextureHelpers.hpp"

class Scene;
class ScenePT;
class Environment;

struct LightVolume
{
    vk::Buffer positionsBuffer;
    vk::Buffer tetrahedralBuffer;
    vk::Buffer coefficientsBuffer;
    std::vector<glm::vec3> positions;
};

class GlobalIllumination
{
public:
    GlobalIllumination();
    ~GlobalIllumination();

    LightVolume GenerateLightVolume(Scene* scene, ScenePT* scenePT, Environment* environment) const;

private:
    vk::DescriptorSetLayout probeLayout;
    vk::DescriptorSetLayout coefficientsLayout;

    std::unique_ptr<ComputePipeline> lightVolumePipeline;
};
