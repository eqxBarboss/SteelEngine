#include "Engine/Render/Stages/LightingStage.hpp"

#include "Engine/Render/RenderContext.hpp"
#include "Engine/Render/Stages/GBufferStage.hpp"
#include "Engine/Render/Vulkan/ComputeHelpers.hpp"
#include "Engine/Render/Vulkan/ComputePipeline.hpp"
#include "Engine/Render/Vulkan/VulkanContext.hpp"
#include "Engine/Render/Vulkan/Resources/BufferHelpers.hpp"
#include "Engine/Render/Vulkan/Resources/ImageHelpers.hpp"
#include "Engine/Scene/GlobalIllumination.hpp"
#include "Engine/Scene/Environment.hpp"

namespace Details
{
    static constexpr glm::uvec2 kWorkGroupSize(8, 8);

    static BoundingBox GetBoundingBox(const AABBox& bbox)
    {
        BoundingBox boundingBox;
        boundingBox.min = glm::vec4(bbox.GetMin(), 1.0f);
        boundingBox.max = glm::vec4(bbox.GetMax(), 1.0f);

        return boundingBox;
    }

    static DescriptorSet CreateGBufferDescriptorSet(const std::vector<vk::ImageView>& imageViews)
    {
        const DescriptorDescription storageImageDescriptorDescription{
            1, vk::DescriptorType::eStorageImage,
            vk::ShaderStageFlagBits::eCompute,
            vk::DescriptorBindingFlags()
        };

        const DescriptorDescription sampledImageDescriptorDescription{
            1, vk::DescriptorType::eCombinedImageSampler,
            vk::ShaderStageFlagBits::eCompute,
            vk::DescriptorBindingFlags()
        };

        DescriptorSetDescription descriptorSetDescription(imageViews.size());
        DescriptorSetData descriptorSetData(imageViews.size());

        for (size_t i = 0; i < imageViews.size(); ++i)
        {
            if (ImageHelpers::IsDepthFormat(GBufferStage::kFormats[i]))
            {
                descriptorSetDescription[i] = sampledImageDescriptorDescription;
                descriptorSetData[i] = DescriptorHelpers::GetData(RenderContext::texelSampler, imageViews[i]);
            }
            else
            {
                descriptorSetDescription[i] = storageImageDescriptorDescription;
                descriptorSetData[i] = DescriptorHelpers::GetData(imageViews[i]);
            }
        }

        return DescriptorHelpers::CreateDescriptorSet(descriptorSetDescription, descriptorSetData);
    }

    static MultiDescriptorSet CreateSwapchainDescriptorSet()
    {
        const std::vector<vk::ImageView>& swapchainImageViews = VulkanContext::swapchain->GetImageViews();

        const DescriptorDescription descriptorDescription{
            1, vk::DescriptorType::eStorageImage,
            vk::ShaderStageFlagBits::eCompute,
            vk::DescriptorBindingFlags()
        };

        std::vector<DescriptorSetData> multiDescriptorSetData;
        multiDescriptorSetData.reserve(swapchainImageViews.size());

        for (const auto& swapchainImageView : swapchainImageViews)
        {
            multiDescriptorSetData.push_back({ DescriptorHelpers::GetData(swapchainImageView) });
        }

        return DescriptorHelpers::CreateMultiDescriptorSet({ descriptorDescription }, multiDescriptorSetData);
    }

    static std::unique_ptr<ComputePipeline> CreatePipeline(const Scene& scene,
            const std::vector<vk::DescriptorSetLayout>& descriptorSetLayouts)
    {
        const uint32_t pointLightCount = static_cast<uint32_t>(scene.GetHierarchy().pointLights.size());
        const uint32_t materialCount = static_cast<uint32_t>(scene.GetHierarchy().materials.size());

        const std::tuple specializationValues = std::make_tuple(
                kWorkGroupSize.x, kWorkGroupSize.y, 1, materialCount);

        const ShaderModule shaderModule = VulkanContext::shaderManager->CreateShaderModule(
                vk::ShaderStageFlagBits::eCompute, Filepath("~/Shaders/Hybrid/Lighting.comp"),
                { std::make_pair("POINT_LIGHT_COUNT", pointLightCount) }, specializationValues);

        const vk::PushConstantRange pushConstantRange(
                vk::ShaderStageFlagBits::eCompute, 0, sizeof(glm::vec3));

        const ComputePipeline::Description description{
            shaderModule, descriptorSetLayouts, { pushConstantRange }
        };

        std::unique_ptr<ComputePipeline> pipeline = ComputePipeline::Create(description);

        VulkanContext::shaderManager->DestroyShaderModule(shaderModule);

        return pipeline;
    }
}

LightingStage::LightingStage(Scene* scene_, Camera* camera_, Environment* environment_,
        IrradianceVolume* irradianceVolume_, const std::vector<vk::ImageView>& gBufferImageViews)
    : scene(scene_)
    , camera(camera_)
    , environment(environment_)
    , irradianceVolume(irradianceVolume_)
{
    gBufferDescriptorSet = Details::CreateGBufferDescriptorSet(gBufferImageViews);
    swapchainDescriptorSet = Details::CreateSwapchainDescriptorSet();

    SetupCameraData();
    SetupLightingData();

    SetupPipeline();
}

LightingStage::~LightingStage()
{
    DescriptorHelpers::DestroyDescriptorSet(lightingData.descriptorSet);
    VulkanContext::bufferManager->DestroyBuffer(lightingData.boundingBoxBuffer);
    VulkanContext::bufferManager->DestroyBuffer(lightingData.directLightBuffer);
    for (const auto& texture : irradianceVolume->textures)
    {
        VulkanContext::textureManager->DestroyTexture(texture);
    }

    DescriptorHelpers::DestroyMultiDescriptorSet(cameraData.descriptorSet);
    for (const auto& buffer : cameraData.buffers)
    {
        VulkanContext::bufferManager->DestroyBuffer(buffer);
    }

    DescriptorHelpers::DestroyDescriptorSet(gBufferDescriptorSet);
    DescriptorHelpers::DestroyMultiDescriptorSet(swapchainDescriptorSet);
}

void LightingStage::Execute(vk::CommandBuffer commandBuffer, uint32_t imageIndex) const
{
    const glm::mat4& view = camera->GetViewMatrix();
    const glm::mat4& proj = camera->GetProjectionMatrix();

    const glm::mat4 inverseProjView = glm::inverse(view) * glm::inverse(proj);

    BufferHelpers::UpdateBuffer(commandBuffer, cameraData.buffers[imageIndex],
            ByteView(inverseProjView), SyncScope::kWaitForNone, SyncScope::kComputeShaderRead);

    const vk::Image swapchainImage = VulkanContext::swapchain->GetImages()[imageIndex];
    const vk::Extent2D& extent = VulkanContext::swapchain->GetExtent();
    const glm::vec3& cameraPosition = camera->GetDescription().position;

    const ImageLayoutTransition layoutTransition{
        vk::ImageLayout::ePresentSrcKHR,
        vk::ImageLayout::eGeneral,
        PipelineBarrier{
            SyncScope::kWaitForNone,
            SyncScope::kComputeShaderWrite
        }
    };

    ImageHelpers::TransitImageLayout(commandBuffer, swapchainImage,
            ImageHelpers::kFlatColor, layoutTransition);

    std::vector<vk::DescriptorSet> descriptorSets{
        swapchainDescriptorSet.values[imageIndex],
        gBufferDescriptorSet.value,
        lightingData.descriptorSet.value,
        cameraData.descriptorSet.values[imageIndex],
        scene->GetDescriptorSets().rayTracing.value
    };

    if (scene->GetDescriptorSets().pointLights.has_value())
    {
        descriptorSets.push_back(scene->GetDescriptorSets().pointLights.value().value);
    }

    commandBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, pipeline->Get());

    commandBuffer.pushConstants<glm::vec3>(pipeline->GetLayout(),
            vk::ShaderStageFlagBits::eCompute, 0, { cameraPosition });

    commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eCompute,
            pipeline->GetLayout(), 0, descriptorSets, {});

    const glm::uvec3 groupCount = ComputeHelpers::CalculateWorkGroupCount(extent, Details::kWorkGroupSize);

    commandBuffer.dispatch(groupCount.x, groupCount.y, groupCount.z);
}

void LightingStage::Resize(const std::vector<vk::ImageView>& gBufferImageViews)
{
    DescriptorHelpers::DestroyDescriptorSet(gBufferDescriptorSet);
    DescriptorHelpers::DestroyMultiDescriptorSet(swapchainDescriptorSet);

    gBufferDescriptorSet = Details::CreateGBufferDescriptorSet(gBufferImageViews);
    swapchainDescriptorSet = Details::CreateSwapchainDescriptorSet();

    SetupPipeline();
}

void LightingStage::ReloadShaders()
{
    SetupPipeline();
}

void LightingStage::SetupCameraData()
{
    const size_t bufferCount = VulkanContext::swapchain->GetImages().size();

    constexpr vk::DeviceSize bufferSize = sizeof(glm::mat4);

    constexpr vk::ShaderStageFlags shaderStages = vk::ShaderStageFlagBits::eCompute;

    cameraData = StageHelpers::CreateCameraData(bufferCount, bufferSize, shaderStages);
}

void LightingStage::SetupLightingData()
{
    const ImageBasedLighting& imageBasedLighting = *RenderContext::imageBasedLighting;

    const ImageBasedLighting::Samplers& iblSamplers = imageBasedLighting.GetSamplers();
    const Texture& irradianceTexture = environment->GetIrradianceTexture();
    const Texture& reflectionTexture = environment->GetReflectionTexture();
    const Texture& specularBRDF = imageBasedLighting.GetSpecularBRDF();

    const BoundingBox& boundingBox = Details::GetBoundingBox(irradianceVolume->bbox);
    lightingData.boundingBoxBuffer = BufferHelpers::CreateBufferWithData(
            vk::BufferUsageFlagBits::eUniformBuffer, ByteView(boundingBox));

    const DirectLight& directLight = environment->GetDirectLight();
    lightingData.directLightBuffer = BufferHelpers::CreateBufferWithData(
            vk::BufferUsageFlagBits::eUniformBuffer, ByteView(directLight));

    const DescriptorSetDescription descriptorSetDescription{
        DescriptorDescription{
            1, vk::DescriptorType::eCombinedImageSampler,
            vk::ShaderStageFlagBits::eCompute,
            vk::DescriptorBindingFlags()
        },
        DescriptorDescription{
            1, vk::DescriptorType::eCombinedImageSampler,
            vk::ShaderStageFlagBits::eCompute,
            vk::DescriptorBindingFlags()
        },
        DescriptorDescription{
            1, vk::DescriptorType::eCombinedImageSampler,
            vk::ShaderStageFlagBits::eCompute,
            vk::DescriptorBindingFlags()
        },
        DescriptorDescription{
            static_cast<uint32_t>(irradianceVolume->textures.size()),
            vk::DescriptorType::eCombinedImageSampler,
            vk::ShaderStageFlagBits::eCompute,
            vk::DescriptorBindingFlags()
        },
        DescriptorDescription{
            1, vk::DescriptorType::eUniformBuffer,
            vk::ShaderStageFlagBits::eCompute,
            vk::DescriptorBindingFlags()
        },
        DescriptorDescription{
            1, vk::DescriptorType::eUniformBuffer,
            vk::ShaderStageFlagBits::eCompute,
            vk::DescriptorBindingFlags()
        },
    };

    const vk::Sampler irradianceVolumeSample
            = RenderContext::globalIllumination->GetIrradianceVolumeSampler();

    ImageInfo irradianceVolumeDescriptorInfo;
    for (const auto& texture : irradianceVolume->textures)
    {
        irradianceVolumeDescriptorInfo.emplace_back(irradianceVolumeSample,
                texture.view, vk::ImageLayout::eShaderReadOnlyOptimal);
    }

    const DescriptorSetData descriptorSetData{
        DescriptorHelpers::GetData(iblSamplers.irradiance, irradianceTexture.view),
        DescriptorHelpers::GetData(iblSamplers.reflection, reflectionTexture.view),
        DescriptorHelpers::GetData(iblSamplers.specularBRDF, specularBRDF.view),
        DescriptorData{ vk::DescriptorType::eCombinedImageSampler, irradianceVolumeDescriptorInfo },
        DescriptorHelpers::GetData(lightingData.boundingBoxBuffer),
        DescriptorHelpers::GetData(lightingData.directLightBuffer),
    };

    lightingData.descriptorSet = DescriptorHelpers::CreateDescriptorSet(
            descriptorSetDescription, descriptorSetData);
}

void LightingStage::SetupPipeline()
{
    std::vector<vk::DescriptorSetLayout> descriptorSetLayouts{
        swapchainDescriptorSet.layout,
        gBufferDescriptorSet.layout,
        lightingData.descriptorSet.layout,
        cameraData.descriptorSet.layout,
        scene->GetDescriptorSets().rayTracing.layout
    };

    if (scene->GetDescriptorSets().pointLights.has_value())
    {
        descriptorSetLayouts.push_back(scene->GetDescriptorSets().pointLights.value().layout);
    }

    pipeline = Details::CreatePipeline(*scene, descriptorSetLayouts);
}
