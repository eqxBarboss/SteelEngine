#pragma once

class Scene;
class ComputePipeline;

struct LightVolume
{
    vk::Buffer positionsBuffer;
    vk::Buffer tetrahedralBuffer;
    vk::Buffer coefficientsBuffer;
    std::vector<glm::vec3> positions;
    std::vector<uint32_t> edgeIndices;
};

class GlobalIllumination
{
public:
    GlobalIllumination();
    ~GlobalIllumination();

    LightVolume GenerateLightVolume(const Scene* scene) const;

private:
    vk::DescriptorSetLayout probeLayout;
    vk::DescriptorSetLayout coefficientsLayout;

    std::unique_ptr<ComputePipeline> lightVolumePipeline;
};
