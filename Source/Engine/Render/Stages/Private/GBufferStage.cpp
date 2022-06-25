#include "Engine/Render/Stages/GBufferStage.hpp"

#include "Engine/Render/Vulkan/GraphicsPipeline.hpp"
#include "Engine/Render/Vulkan/RenderPass.hpp"
#include "Engine/Render/Vulkan/VulkanContext.hpp"
#include "Engine/Render/Vulkan/VulkanHelpers.hpp"
#include "Engine/Render/Vulkan/Resources/ImageHelpers.hpp"
#include "Engine/Scene/Components.hpp"

namespace Details
{
    static std::unique_ptr<RenderPass> CreateRenderPass()
    {
        std::vector<RenderPass::AttachmentDescription> attachments(GBufferStage::kFormats.size());

        for (size_t i = 0; i < attachments.size(); ++i)
        {
            if (ImageHelpers::IsDepthFormat(GBufferStage::kFormats[i]))
            {
                attachments[i] = RenderPass::AttachmentDescription{
                    RenderPass::AttachmentUsage::eDepth,
                    GBufferStage::kFormats[i],
                    vk::AttachmentLoadOp::eClear,
                    vk::AttachmentStoreOp::eStore,
                    vk::ImageLayout::eDepthStencilAttachmentOptimal,
                    vk::ImageLayout::eDepthStencilAttachmentOptimal,
                    vk::ImageLayout::eShaderReadOnlyOptimal
                };
            }
            else
            {
                attachments[i] = RenderPass::AttachmentDescription{
                    RenderPass::AttachmentUsage::eColor,
                    GBufferStage::kFormats[i],
                    vk::AttachmentLoadOp::eClear,
                    vk::AttachmentStoreOp::eStore,
                    vk::ImageLayout::eGeneral,
                    vk::ImageLayout::eColorAttachmentOptimal,
                    vk::ImageLayout::eGeneral
                };
            }
        }

        const RenderPass::Description description{
            vk::PipelineBindPoint::eGraphics,
            vk::SampleCountFlagBits::e1,
            attachments
        };

        const std::vector<PipelineBarrier> followingDependencies{
            PipelineBarrier{
                SyncScope::kColorAttachmentWrite | SyncScope::kDepthStencilAttachmentWrite,
                SyncScope::kComputeShaderRead
            }
        };

        std::unique_ptr<RenderPass> renderPass = RenderPass::Create(description,
                RenderPass::Dependencies{ {}, followingDependencies });

        return renderPass;
    }

    static vk::Framebuffer CreateFramebuffer(const RenderPass& renderPass,
            const std::vector<vk::ImageView>& imageViews)
    {
        const vk::Device device = VulkanContext::device->Get();

        const vk::Extent2D& extent = VulkanContext::swapchain->GetExtent();

        return VulkanHelpers::CreateFramebuffers(device, renderPass.Get(), extent, {}, imageViews).front();
    }

    static std::unique_ptr<GraphicsPipeline> CreatePipeline(const RenderPass& renderPass,
            const std::vector<vk::DescriptorSetLayout>& descriptorSetLayouts,
            const MaterialFlags& materialFlags)
    {
        const ShaderDefines defines = MaterialHelpers::BuildShaderDefines(materialFlags);

        const std::vector<ShaderModule> shaderModules{
            VulkanContext::shaderManager->CreateShaderModule(
                    vk::ShaderStageFlagBits::eVertex,
                    Filepath("~/Shaders/Hybrid/GBuffer.vert"), defines),
            VulkanContext::shaderManager->CreateShaderModule(
                    vk::ShaderStageFlagBits::eFragment,
                    Filepath("~/Shaders/Hybrid/GBuffer.frag"), defines)
        };

        const vk::CullModeFlagBits cullMode = materialFlags & MaterialFlagBits::eAlphaTest
            ? vk::CullModeFlagBits::eNone : vk::CullModeFlagBits::eBack;

        const VertexDescription vertexDescription{
            Primitive::Vertex::kFormat,
            0, vk::VertexInputRate::eVertex
        };

        const std::vector<BlendMode> blendModes = Repeat(BlendMode::eDisabled, GBufferStage::kFormats.size() - 1);

        const std::vector<vk::PushConstantRange> pushConstantRanges{
            vk::PushConstantRange(vk::ShaderStageFlagBits::eVertex, 0, sizeof(glm::mat4)),
            vk::PushConstantRange(vk::ShaderStageFlagBits::eFragment, sizeof(glm::mat4), sizeof(glm::vec3) + sizeof(uint32_t))
        };

        const GraphicsPipeline::Description description{
            vk::PrimitiveTopology::eTriangleList,
            vk::PolygonMode::eFill,
            cullMode,
            vk::FrontFace::eCounterClockwise,
            vk::SampleCountFlagBits::e1,
            vk::CompareOp::eLess,
            shaderModules,
            { vertexDescription },
            blendModes,
            descriptorSetLayouts,
            pushConstantRanges
        };

        std::unique_ptr<GraphicsPipeline> pipeline = GraphicsPipeline::Create(renderPass.Get(), description);

        for (const auto& shaderModule : shaderModules)
        {
            VulkanContext::shaderManager->DestroyShaderModule(shaderModule);
        }

        return pipeline;
    }

    static std::vector<vk::ClearValue> GetClearValues()
    {
        std::vector<vk::ClearValue> clearValues(GBufferStage::kFormats.size());

        for (size_t i = 0; i < clearValues.size(); ++i)
        {
            if (ImageHelpers::IsDepthFormat(GBufferStage::kFormats[i]))
            {
                clearValues[i] = VulkanHelpers::kDefaultClearDepthStencilValue;
            }
            else
            {
                clearValues[i] = VulkanHelpers::kDefaultClearColorValue;
            }
        }

        return clearValues;
    }
}

GBufferStage::GBufferStage(const Scene* scene_, const Camera* camera_, const std::vector<vk::ImageView>& imageViews)
    : scene(scene_)
    , camera(camera_)
{
    renderPass = Details::CreateRenderPass();
    framebuffer = Details::CreateFramebuffer(*renderPass, imageViews);

    SetupCameraData();
    SetupMaterialsData();
    SetupPipelines();
}

GBufferStage::~GBufferStage()
{
    DescriptorHelpers::DestroyMultiDescriptorSet(cameraData.descriptorSet);
    for (const auto& buffer : cameraData.buffers)
    {
        VulkanContext::bufferManager->DestroyBuffer(buffer);
    }

    VulkanContext::device->Get().destroyFramebuffer(framebuffer);
}

void GBufferStage::Execute(vk::CommandBuffer commandBuffer, uint32_t imageIndex) const
{
    const glm::mat4 viewProj = camera->GetProjectionMatrix() * camera->GetViewMatrix();

    BufferHelpers::UpdateBuffer(commandBuffer, cameraData.buffers[imageIndex],
            ByteView(viewProj), SyncScope::kWaitForNone, SyncScope::kVertexUniformRead);

    const glm::vec3& cameraPosition = camera->GetLocation().position;

    const vk::Rect2D renderArea = RenderHelpers::GetSwapchainRenderArea();
    const vk::Viewport viewport = RenderHelpers::GetSwapchainViewport();
    const std::vector<vk::ClearValue> clearValues = Details::GetClearValues();

    const vk::RenderPassBeginInfo beginInfo(
            renderPass->Get(), framebuffer,
            renderArea, clearValues);

    commandBuffer.beginRenderPass(beginInfo, vk::SubpassContents::eInline);

    const auto sceneView = scene->view<TransformComponent, RenderComponent>();

    for (const auto& [materialFlags, pipeline] : pipelines)
    {
        commandBuffer.setViewport(0, { viewport });
        commandBuffer.setScissor(0, { renderArea });

        commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline->Get());

        commandBuffer.pushConstants<glm::vec3>(pipeline->GetLayout(),
                vk::ShaderStageFlagBits::eFragment, sizeof(glm::mat4), { cameraPosition });

        const std::vector<vk::DescriptorSet> descriptorSets{
            cameraData.descriptorSet.values[imageIndex],
            materialDescriptorSet.value
        };

        commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                pipeline->GetLayout(), 0, descriptorSets, {});

        for (auto&& [entity, tc, rc] : sceneView.each())
        {
            for (const auto& ro : rc.renderObjects)
            {
                if (scene->materials[ro.material].flags == materialFlags)
                {
                    const Primitive& primitive = scene->primitives[ro.primitive];

                    commandBuffer.bindIndexBuffer(primitive.indexBuffer, 0, primitive.indexType);
                    commandBuffer.bindVertexBuffers(0, { primitive.vertexBuffer }, { 0 });

                    commandBuffer.pushConstants<glm::mat4>(pipeline->GetLayout(),
                        vk::ShaderStageFlagBits::eVertex, 0, { tc.worldTransform });

                    commandBuffer.pushConstants<uint32_t>(pipeline->GetLayout(),
                        vk::ShaderStageFlagBits::eFragment, sizeof(glm::mat4) + sizeof(glm::vec3), { ro.material });

                    commandBuffer.drawIndexed(primitive.indexCount, 1, 0, 0, 0);
                }
            }
        }
    }

    commandBuffer.endRenderPass();
}

void GBufferStage::Resize(const std::vector<vk::ImageView>& imageViews)
{
    VulkanContext::device->Get().destroyFramebuffer(framebuffer);

    framebuffer = Details::CreateFramebuffer(*renderPass, imageViews);
}

void GBufferStage::ReloadShaders()
{
    SetupPipelines();
}

void GBufferStage::SetupCameraData()
{
    const uint32_t bufferCount = VulkanContext::swapchain->GetImageCount();

    constexpr vk::DeviceSize bufferSize = sizeof(glm::mat4);

    constexpr vk::ShaderStageFlags shaderStages = vk::ShaderStageFlagBits::eVertex;

    cameraData = RenderHelpers::CreateCameraData(bufferCount, bufferSize, shaderStages);
}

void GBufferStage::SetupMaterialsData()
{
    const uint32_t textureCount = static_cast<uint32_t>(scene->textures.size());

    const DescriptorSetDescription descriptorSetDescription{
        DescriptorDescription{
            textureCount,
            vk::DescriptorType::eCombinedImageSampler,
            vk::ShaderStageFlagBits::eFragment,
            vk::DescriptorBindingFlagBits::eVariableDescriptorCount
        },
        DescriptorDescription{
            1, vk::DescriptorType::eUniformBuffer,
            vk::ShaderStageFlagBits::eFragment,
            vk::DescriptorBindingFlags()
        }
    };


    const DescriptorSetData descriptorSetData{
        DescriptorHelpers::GetData(scene->textures),
        DescriptorHelpers::GetData(scene->materialBuffer)
    };

    materialDescriptorSet = DescriptorHelpers::CreateDescriptorSet(
            descriptorSetDescription, descriptorSetData);
}

void GBufferStage::SetupPipelines()
{
    pipelines.clear();

    const std::vector<vk::DescriptorSetLayout> scenePipelineLayouts{
        cameraData.descriptorSet.layout,
        materialDescriptorSet.layout
    };

    for (const auto& material : scene->materials)
    {
        const auto pred = [&material](const MaterialPipeline& materialPipeline)
            {
                return materialPipeline.materialFlags == material.flags;
            };

        const auto it = std::ranges::find_if(pipelines, pred);

        if (it == pipelines.end())
        {
            std::unique_ptr<GraphicsPipeline> pipeline = Details::CreatePipeline(
                    *renderPass, scenePipelineLayouts, material.flags);

            pipelines.emplace_back(material.flags, std::move(pipeline));
        }
    }
}
