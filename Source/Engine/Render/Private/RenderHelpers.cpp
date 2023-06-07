#include "Engine/Render/RenderHelpers.hpp"

#include "Engine/Render/RenderContext.hpp"
#include "Engine/Render/Vulkan/VulkanContext.hpp"
#include "Engine/Render/Vulkan/Resources/BufferHelpers.hpp"
#include "Engine/Scene/Environment.hpp"
#include "Engine/Scene/GlobalIllumination.hpp"
#include "Engine/Scene/ImageBasedLighting.hpp"
#include "Engine/Scene/Scene.hpp"
#include "Engine/Scene/StorageComponents.hpp"

CameraData RenderHelpers::CreateCameraData(uint32_t bufferCount, vk::DeviceSize bufferSize)
{
    std::vector<vk::Buffer> buffers(bufferCount);

    for (uint32_t i = 0; i < bufferCount; ++i)
    {
        buffers[i] = BufferHelpers::CreateEmptyBuffer(
                vk::BufferUsageFlagBits::eUniformBuffer, bufferSize);
    }

    return CameraData{ buffers };
}

vk::Rect2D RenderHelpers::GetSwapchainRenderArea()
{
    return vk::Rect2D(vk::Offset2D(), VulkanContext::swapchain->GetExtent());
}

vk::Viewport RenderHelpers::GetSwapchainViewport()
{
    const vk::Extent2D& extent = VulkanContext::swapchain->GetExtent();

    return vk::Viewport(0.0f, 0.0f,
            static_cast<float>(extent.width),
            static_cast<float>(extent.height),
            0.0f, 1.0f);
}

void RenderHelpers::PushEnvironmentDescriptorData(const Scene& scene, DescriptorProvider& descriptorProvider)
{
    const auto& environmentComponent = scene.ctx().get<EnvironmentComponent>();

    const ImageBasedLighting& imageBasedLighting = *RenderContext::imageBasedLighting;

    const ImageBasedLighting::Samplers& iblSamplers = imageBasedLighting.GetSamplers();

    const TextureSampler irradianceMap{ environmentComponent.irradianceTexture.view, iblSamplers.irradiance };
    const TextureSampler reflectionMap{ environmentComponent.reflectionTexture.view, iblSamplers.reflection };
    const TextureSampler specularBRDF{ imageBasedLighting.GetSpecularBRDF().view, iblSamplers.specularBRDF };

    descriptorProvider.PushGlobalData("irradianceMap", irradianceMap);
    descriptorProvider.PushGlobalData("reflectionMap", reflectionMap);
    descriptorProvider.PushGlobalData("specularBRDF", specularBRDF);
}

void RenderHelpers::PushLightVolumeDescriptorData(const Scene& scene, DescriptorProvider& descriptorProvider)
{
    if (scene.ctx().contains<LightVolumeComponent>())
    {
        const auto& lightVolumeComponent = scene.ctx().get<LightVolumeComponent>();

        descriptorProvider.PushGlobalData("positions", lightVolumeComponent.positionsBuffer);
        descriptorProvider.PushGlobalData("tetrahedral", lightVolumeComponent.tetrahedralBuffer);
        descriptorProvider.PushGlobalData("coefficients", lightVolumeComponent.coefficientsBuffer);
    }
}

void RenderHelpers::PushRayTracingDescriptorData(const Scene& scene, DescriptorProvider& descriptorProvider)
{
    if constexpr (Config::kRayTracingEnabled)
    {
        const auto& geometryComponent = scene.ctx().get<GeometryStorageComponent>();
        const auto& renderComponent = scene.ctx().get<RenderStorageComponent>();

        std::vector<vk::Buffer> indexBuffers;
        std::vector<vk::Buffer> texCoordBuffers;

        indexBuffers.reserve(geometryComponent.primitives.size());
        texCoordBuffers.reserve(geometryComponent.primitives.size());

        for (const auto& primitive : geometryComponent.primitives)
        {
            indexBuffers.push_back(primitive.indexBuffer);
            texCoordBuffers.push_back(primitive.texCoordBuffer);
        }

        descriptorProvider.PushGlobalData("tlas", &renderComponent.tlas);
        descriptorProvider.PushGlobalData("indexBuffers", &indexBuffers);
        descriptorProvider.PushGlobalData("texCoordBuffers", &texCoordBuffers);
    }
}

std::vector<MaterialPipeline> RenderHelpers::CreateMaterialPipelines(
        const Scene& scene, const RenderPass& renderPass,
        const CreateMaterialPipelinePred& createPipelinePred,
        const MaterialPipelineCreator& pipelineCreator)
{
    std::vector<MaterialPipeline> pipelines;

    const auto& materialComponent = scene.ctx().get<MaterialStorageComponent>();

    for (const auto& material : materialComponent.materials)
    {
        if (createPipelinePred(material.flags))
        {
            const auto pred = [&material](const MaterialPipeline& materialPipeline)
                {
                    return materialPipeline.materialFlags == material.flags;
                };

            const auto it = std::ranges::find_if(pipelines, pred);

            if (it == pipelines.end())
            {
                pipelines.emplace_back(material.flags, pipelineCreator(
                        renderPass, material.flags, scene));
            }
        }
    }

    return pipelines;
}
