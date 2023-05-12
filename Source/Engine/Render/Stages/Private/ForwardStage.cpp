#include "Engine/Render/Stages/ForwardStage.hpp"

#include "Engine/Engine.hpp"
#include "Engine/Render/RenderContext.hpp"
#include "Engine/Render/Stages/GBufferStage.hpp"
#include "Engine/Render/Vulkan/Pipelines/GraphicsPipeline.hpp"
#include "Engine/Render/Vulkan/RenderPass.hpp"
#include "Engine/Render/Vulkan/VulkanContext.hpp"
#include "Engine/Scene/Components.hpp"
#include "Engine/Scene/GlobalIllumination.hpp"
#include "Engine/Scene/Environment.hpp"

namespace Details
{
    static const std::vector<uint16_t> kEnvironmentIndices{
        0, 3, 1,
        0, 2, 3,
        4, 2, 0,
        4, 6, 2,
        5, 6, 4,
        5, 7, 6,
        1, 7, 5,
        1, 3, 7,
        5, 0, 1,
        5, 4, 0,
        7, 3, 2,
        7, 2, 6
    };

    static const uint32_t kEnvironmentIndexCount = static_cast<uint32_t>(Details::kEnvironmentIndices.size());

    static std::unique_ptr<RenderPass> CreateRenderPass()
    {
        const std::vector<RenderPass::AttachmentDescription> attachments{
            RenderPass::AttachmentDescription{
                RenderPass::AttachmentUsage::eColor,
                VulkanContext::swapchain->GetFormat(),
                vk::AttachmentLoadOp::eLoad,
                vk::AttachmentStoreOp::eStore,
                vk::ImageLayout::eGeneral,
                vk::ImageLayout::eColorAttachmentOptimal,
                vk::ImageLayout::eColorAttachmentOptimal
            },
            RenderPass::AttachmentDescription{
                RenderPass::AttachmentUsage::eDepth,
                GBufferStage::kDepthFormat,
                vk::AttachmentLoadOp::eLoad,
                vk::AttachmentStoreOp::eDontCare,
                vk::ImageLayout::eShaderReadOnlyOptimal,
                vk::ImageLayout::eDepthStencilAttachmentOptimal,
                vk::ImageLayout::eDepthStencilAttachmentOptimal
            }
        };

        const RenderPass::Description description{
            vk::PipelineBindPoint::eGraphics,
            vk::SampleCountFlagBits::e1, attachments
        };

        const std::vector<PipelineBarrier> previousDependencies{
            PipelineBarrier{
                SyncScope::kComputeShaderWrite,
                SyncScope::kColorAttachmentWrite
            },
            PipelineBarrier{
                SyncScope::kDepthStencilAttachmentWrite,
                SyncScope::kDepthStencilAttachmentRead
            },
        };

        const PipelineBarrier followingDependency{
            SyncScope::kColorAttachmentWrite,
            SyncScope::kColorAttachmentWrite
        };

        std::unique_ptr<RenderPass> renderPass = RenderPass::Create(description,
                RenderPass::Dependencies{ previousDependencies, { followingDependency } });

        return renderPass;
    }

    static std::vector<vk::Framebuffer> CreateFramebuffers(const RenderPass& renderPass, vk::ImageView depthImageView)
    {
        const vk::Device device = VulkanContext::device->Get();

        const vk::Extent2D& extent = VulkanContext::swapchain->GetExtent();

        const std::vector<vk::ImageView>& swapchainImageViews = VulkanContext::swapchain->GetImageViews();

        return VulkanHelpers::CreateFramebuffers(device, renderPass.Get(),
                extent, { swapchainImageViews }, { depthImageView });
    }

    static CameraData CreateCameraData()
    {
        const uint32_t bufferCount = VulkanContext::swapchain->GetImageCount();

        constexpr vk::DeviceSize bufferSize = sizeof(glm::mat4);

        return RenderHelpers::CreateCameraData(bufferCount, bufferSize);
    }

    static bool CreateMaterialPipelinePred(MaterialFlags materialFlags)
    {
        if constexpr (Config::kForceForward)
        {
            return true;
        }
        else
        {
            return static_cast<bool>(materialFlags & MaterialFlagBits::eAlphaBlend);
        }
    }

    static std::unique_ptr<GraphicsPipeline> CreateMaterialPipeline(const RenderPass& renderPass,
            const MaterialFlags& materialFlags, const Scene& scene)
    {
        const auto& materialComponent = scene.ctx().get<MaterialStorageComponent>();

        const bool lightVolumeEnabled = scene.ctx().contains<LightVolumeComponent>();

        ShaderDefines defines = MaterialHelpers::BuildShaderDefines(materialFlags);

        defines.emplace("LIGHT_COUNT", static_cast<uint32_t>(scene.view<LightComponent>().size()));
        defines.emplace("MATERIAL_COUNT", static_cast<uint32_t>(materialComponent.materials.size()));
        defines.emplace("RAY_TRACING_ENABLED", static_cast<uint32_t>(Config::kRayTracingEnabled));
        defines.emplace("LIGHT_VOLUME_ENABLED", static_cast<uint32_t>(lightVolumeEnabled));

        const std::vector<ShaderModule> shaderModules{
            VulkanContext::shaderManager->CreateShaderModule(
                    Filepath("~/Shaders/Hybrid/Forward.vert"),
                    vk::ShaderStageFlagBits::eVertex, defines),
            VulkanContext::shaderManager->CreateShaderModule(
                    Filepath("~/Shaders/Hybrid/Forward.frag"),
                    vk::ShaderStageFlagBits::eFragment, defines)
        };

        const vk::CullModeFlagBits cullMode = materialFlags & MaterialFlagBits::eDoubleSided
                ? vk::CullModeFlagBits::eNone : vk::CullModeFlagBits::eBack;

        const std::vector<vk::PushConstantRange> pushConstantRanges{
            vk::PushConstantRange(vk::ShaderStageFlagBits::eVertex, 0, sizeof(glm::mat4)),
            vk::PushConstantRange(vk::ShaderStageFlagBits::eFragment, sizeof(glm::mat4),
                    sizeof(glm::vec3) + sizeof(uint32_t))
        };

        const GraphicsPipeline::Description description{
            vk::PrimitiveTopology::eTriangleList,
            vk::PolygonMode::eFill,
            cullMode,
            vk::FrontFace::eCounterClockwise,
            vk::SampleCountFlagBits::e1,
            vk::CompareOp::eLess,
            shaderModules,
            Primitive::kVertexInputs,
            { BlendMode::eAlphaBlend }
        };

        std::unique_ptr<GraphicsPipeline> pipeline = GraphicsPipeline::Create(renderPass.Get(), description);

        for (const auto& shaderModule : shaderModules)
        {
            VulkanContext::shaderManager->DestroyShaderModule(shaderModule);
        }

        return pipeline;
    }

    static std::unique_ptr<GraphicsPipeline> CreateEnvironmentPipeline(const RenderPass& renderPass)
    {
        constexpr int32_t reverseDepth = static_cast<int32_t>(Config::kReverseDepth);

        const std::vector<ShaderModule> shaderModules{
            VulkanContext::shaderManager->CreateShaderModule(
                    Filepath("~/Shaders/Hybrid/Environment.vert"),
                    vk::ShaderStageFlagBits::eVertex,
                    { std::make_pair("REVERSE_DEPTH", reverseDepth) }),
            VulkanContext::shaderManager->CreateShaderModule(
                    Filepath("~/Shaders/Hybrid/Environment.frag"),
                    vk::ShaderStageFlagBits::eFragment)
        };

        const GraphicsPipeline::Description description{
            vk::PrimitiveTopology::eTriangleList,
            vk::PolygonMode::eFill,
            vk::CullModeFlagBits::eFront,
            vk::FrontFace::eCounterClockwise,
            vk::SampleCountFlagBits::e1,
            vk::CompareOp::eLessOrEqual,
            shaderModules,
            {},
            { BlendMode::eDisabled }
        };

        std::unique_ptr<GraphicsPipeline> pipeline = GraphicsPipeline::Create(renderPass.Get(), description);

        for (const auto& shaderModule : shaderModules)
        {
            VulkanContext::shaderManager->DestroyShaderModule(shaderModule);
        }

        return pipeline;
    }

    void UpdateMaterialDescriptors(const FrameDescriptorProvider& descriptorProvider,
            const Scene& scene, const CameraData& cameraData)
    {
        const auto& renderComponent = scene.ctx().get<RenderStorageComponent>();
        const auto& textureComponent = scene.ctx().get<TextureStorageComponent>();

        DescriptorSetData globalDescriptorSetData{
            DescriptorHelpers::GetData(renderComponent.lightBuffer),
            DescriptorHelpers::GetData(renderComponent.materialBuffer),
            DescriptorHelpers::GetData(textureComponent.textures),
        };

        RenderHelpers::AppendEnvironmentDescriptorData(scene, globalDescriptorSetData);
        RenderHelpers::AppendLightVolumeDescriptorData(scene, globalDescriptorSetData);
        RenderHelpers::AppendRayTracingDescriptorData(scene, globalDescriptorSetData);

        descriptorProvider.UpdateGlobalDescriptorSet(globalDescriptorSetData);

        for (uint32_t i = 0; i < descriptorProvider.GetSliceCount(); ++i)
        {
            const DescriptorSetData frameDescriptorSetData{
                DescriptorHelpers::GetData(cameraData.buffers[i])
            };

            descriptorProvider.UpdateFrameDescriptorSet(i, frameDescriptorSetData);
        }
    }

    void UpdateEnvironmentDescriptors(const FrameDescriptorProvider& descriptorProvider,
            const Scene& scene, const CameraData& cameraData)
    {
        const auto& environmentComponent = scene.ctx().get<EnvironmentComponent>();

        const DescriptorSetData globalDescriptorSetData{
            DescriptorHelpers::GetData(RenderContext::defaultSampler, environmentComponent.cubemapTexture.view)
        };

        descriptorProvider.UpdateGlobalDescriptorSet(globalDescriptorSetData);

        for (uint32_t i = 0; i < descriptorProvider.GetSliceCount(); ++i)
        {
            const DescriptorSetData frameDescriptorSetData{
                DescriptorHelpers::GetData(cameraData.buffers[i])
            };

            descriptorProvider.UpdateFrameDescriptorSet(i, frameDescriptorSetData);
        }
    }

    static std::vector<vk::ClearValue> GetClearValues()
    {
        return { VulkanHelpers::kDefaultClearColorValue, VulkanHelpers::kDefaultClearDepthStencilValue };
    }
}

ForwardStage::ForwardStage(vk::ImageView depthImageView)
{
    renderPass = Details::CreateRenderPass();
    framebuffers = Details::CreateFramebuffers(*renderPass, depthImageView);

    defaultCameraData = Details::CreateCameraData();
    environmentCameraData = Details::CreateCameraData();
}

ForwardStage::~ForwardStage()
{
    RemoveScene();

    for (const auto& buffer : defaultCameraData.buffers)
    {
        VulkanContext::bufferManager->DestroyBuffer(buffer);
    }

    for (const auto& buffer : environmentCameraData.buffers)
    {
        VulkanContext::bufferManager->DestroyBuffer(buffer);
    }

    for (const auto& framebuffer : framebuffers)
    {
        VulkanContext::device->Get().destroyFramebuffer(framebuffer);
    }
}

void ForwardStage::RegisterScene(const Scene* scene_)
{
    RemoveScene();

    scene = scene_;

    environmentData = CreateEnvironmentData();

    materialPipelines = RenderHelpers::CreateMaterialPipelines(*scene, *renderPass,
            &Details::CreateMaterialPipelinePred, &Details::CreateMaterialPipeline);
    environmentPipeline = Details::CreateEnvironmentPipeline(*renderPass);

    if (!materialPipelines.empty())
    {
        materialDescriptorProvider = std::make_unique<FrameDescriptorProvider>(
                materialPipelines.front().pipeline->GetDescriptorSetLayouts());

        Details::UpdateMaterialDescriptors(*materialDescriptorProvider, *scene, defaultCameraData);
    }

    environmentDescriptorProvider = std::make_unique<FrameDescriptorProvider>(
            environmentPipeline->GetDescriptorSetLayouts());

    Details::UpdateEnvironmentDescriptors(*environmentDescriptorProvider, *scene, environmentCameraData);
}

void ForwardStage::RemoveScene()
{
    if (!scene)
    {
        return;
    }

    materialDescriptorProvider.reset();
    environmentDescriptorProvider.reset();

    materialPipelines.clear();
    environmentPipeline.reset();

    VulkanContext::bufferManager->DestroyBuffer(environmentData.indexBuffer);

    scene = nullptr;
}

void ForwardStage::Execute(vk::CommandBuffer commandBuffer, uint32_t imageIndex) const
{
    const auto& cameraComponent = scene->ctx().get<CameraComponent>();

    const glm::mat4& view = cameraComponent.viewMatrix;
    const glm::mat4& proj = cameraComponent.projMatrix;

    const glm::mat4 defaultViewProj = proj * view;
    BufferHelpers::UpdateBuffer(commandBuffer, defaultCameraData.buffers[imageIndex],
            GetByteView(defaultViewProj), SyncScope::kWaitForNone, SyncScope::kVertexUniformRead);

    const glm::mat4 environmentViewProj = proj * glm::mat4(glm::mat3(view));
    BufferHelpers::UpdateBuffer(commandBuffer, environmentCameraData.buffers[imageIndex],
            GetByteView(environmentViewProj), SyncScope::kWaitForNone, SyncScope::kVertexUniformRead);

    const vk::Rect2D renderArea = RenderHelpers::GetSwapchainRenderArea();
    const std::vector<vk::ClearValue> clearValues = Details::GetClearValues();

    const vk::RenderPassBeginInfo beginInfo(
            renderPass->Get(), framebuffers[imageIndex],
            renderArea, clearValues);

    commandBuffer.beginRenderPass(beginInfo, vk::SubpassContents::eInline);

    DrawEnvironment(commandBuffer, imageIndex);

    DrawScene(commandBuffer, imageIndex);

    commandBuffer.endRenderPass();
}

void ForwardStage::Resize(vk::ImageView depthImageView)
{
    for (const auto& framebuffer : framebuffers)
    {
        VulkanContext::device->Get().destroyFramebuffer(framebuffer);
    }

    renderPass = Details::CreateRenderPass();
    framebuffers = Details::CreateFramebuffers(*renderPass, depthImageView);

    materialPipelines = RenderHelpers::CreateMaterialPipelines(*scene, *renderPass,
            &Details::CreateMaterialPipelinePred, &Details::CreateMaterialPipeline);
    environmentPipeline = Details::CreateEnvironmentPipeline(*renderPass);

    if (!materialPipelines.empty())
    {
        materialDescriptorProvider = std::make_unique<FrameDescriptorProvider>(
                materialPipelines.front().pipeline->GetDescriptorSetLayouts());

        Details::UpdateMaterialDescriptors(*materialDescriptorProvider, *scene, defaultCameraData);
    }

    environmentDescriptorProvider = std::make_unique<FrameDescriptorProvider>(
            environmentPipeline->GetDescriptorSetLayouts());

    Details::UpdateEnvironmentDescriptors(*environmentDescriptorProvider, *scene, environmentCameraData);
}

void ForwardStage::ReloadShaders()
{
    materialPipelines = RenderHelpers::CreateMaterialPipelines(*scene, *renderPass,
            &Details::CreateMaterialPipelinePred, &Details::CreateMaterialPipeline);

    environmentPipeline = Details::CreateEnvironmentPipeline(*renderPass);

    if (!materialPipelines.empty())
    {
        materialDescriptorProvider = std::make_unique<FrameDescriptorProvider>(
                materialPipelines.front().pipeline->GetDescriptorSetLayouts());

        Details::UpdateMaterialDescriptors(*materialDescriptorProvider, *scene, defaultCameraData);
    }

    environmentDescriptorProvider = std::make_unique<FrameDescriptorProvider>(
            environmentPipeline->GetDescriptorSetLayouts());

    Details::UpdateEnvironmentDescriptors(*environmentDescriptorProvider, *scene, environmentCameraData);
}

ForwardStage::EnvironmentData ForwardStage::CreateEnvironmentData()
{
    const vk::Buffer indexBuffer = BufferHelpers::CreateBufferWithData(
            vk::BufferUsageFlagBits::eIndexBuffer, GetByteView(Details::kEnvironmentIndices));

    return EnvironmentData{ indexBuffer };
}

void ForwardStage::DrawScene(vk::CommandBuffer commandBuffer, uint32_t imageIndex) const
{
    const auto& cameraComponent = scene->ctx().get<CameraComponent>();

    const glm::vec3& cameraPosition = cameraComponent.location.position;

    const auto sceneRenderView = scene->view<TransformComponent, RenderComponent>();

    const auto& materialComponent = scene->ctx().get<MaterialStorageComponent>();
    const auto& geometryComponent = scene->ctx().get<GeometryStorageComponent>();

    for (const auto& [materialFlags, pipeline] : materialPipelines)
    {
        pipeline->Bind(commandBuffer);

        pipeline->BindDescriptorSets(commandBuffer, 0, materialDescriptorProvider->GetDescriptorSlice(imageIndex));

        pipeline->PushConstant(commandBuffer, "cameraPosition", cameraPosition);

        for (auto&& [entity, tc, rc] : sceneRenderView.each())
        {
            for (const auto& ro : rc.renderObjects)
            {
                if (materialComponent.materials[ro.material].flags == materialFlags)
                {
                    pipeline->PushConstant(commandBuffer, "transform", tc.worldTransform.GetMatrix());

                    pipeline->PushConstant(commandBuffer, "materialIndex", ro.material);

                    const Primitive& primitive = geometryComponent.primitives[ro.primitive];

                    PrimitiveHelpers::DrawPrimitive(commandBuffer, primitive);
                }
            }
        }
    }
}

void ForwardStage::DrawEnvironment(vk::CommandBuffer commandBuffer, uint32_t imageIndex) const
{
    const vk::Rect2D renderArea = RenderHelpers::GetSwapchainRenderArea();
    const vk::Viewport viewport = RenderHelpers::GetSwapchainViewport();

    commandBuffer.setViewport(0, { viewport });
    commandBuffer.setScissor(0, { renderArea });

    environmentPipeline->Bind(commandBuffer);

    commandBuffer.bindIndexBuffer(environmentData.indexBuffer, 0, vk::IndexType::eUint16);

    environmentPipeline->BindDescriptorSets(commandBuffer,
            0, environmentDescriptorProvider->GetDescriptorSlice(imageIndex));

    commandBuffer.drawIndexed(Details::kEnvironmentIndexCount, 1, 0, 0, 0);
}
