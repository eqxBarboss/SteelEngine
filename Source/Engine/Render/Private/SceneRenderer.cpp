#include "Engine/Render/SceneRenderer.hpp"

#include "Engine/Config.hpp"
#include "Engine/Engine.hpp"
#include "Engine/Scene/SceneHelpers.hpp"
#include "Engine/Scene/Components/Components.hpp"
#include "Engine/Scene/Components/TransformComponent.hpp"
#include "Engine/Scene/Components/EnvironmentComponent.hpp"
#include "Engine/Render/HybridRenderer.hpp"
#include "Engine/Render/PathTracingRenderer.hpp"
#include "Engine/Render/Vulkan/VulkanContext.hpp"
#include "Engine/Render/Vulkan/Resources/BufferHelpers.hpp"

#include "Shaders/Common/Common.h"

namespace Details
{
    static void EmplaceDefaultCamera(Scene& scene)
    {
        const entt::entity entity = scene.create();

        auto& cc = scene.emplace<CameraComponent>(entity);

        cc.location = Config::DefaultCamera::kLocation;
        cc.projection = Config::DefaultCamera::kProjection;

        cc.viewMatrix = CameraHelpers::ComputeViewMatrix(cc.location);
        cc.projMatrix = CameraHelpers::ComputeProjMatrix(cc.projection);

        scene.ctx().emplace<CameraComponent&>(cc);
    }

    static void EmplaceDefaultEnvironment(Scene& scene)
    {
        const entt::entity entity = scene.create();

        auto& ec = scene.emplace<EnvironmentComponent>(entity);

        ec = EnvironmentHelpers::LoadEnvironment(Config::kDefaultPanoramaPath);

        scene.ctx().emplace<EnvironmentComponent>(ec);
    }

    static RenderSceneComponent CreateRenderSceneComponent()
    {
        RenderSceneComponent renderComponent;

        renderComponent.lightBuffer = BufferHelpers::CreateEmptyBuffer(
                vk::BufferUsageFlagBits::eUniformBuffer, sizeof(gpu::Light) * MAX_LIGHT_COUNT);

        renderComponent.materialBuffer = BufferHelpers::CreateEmptyBuffer(
                vk::BufferUsageFlagBits::eUniformBuffer, sizeof(gpu::Material) * MAX_MATERIAL_COUNT);

        renderComponent.frameBuffers.resize(VulkanContext::swapchain->GetImageCount());

        for (auto& frameBuffer : renderComponent.frameBuffers)
        {
            frameBuffer = BufferHelpers::CreateEmptyBuffer(
                    vk::BufferUsageFlagBits::eUniformBuffer, sizeof(gpu::Frame));
        }

        renderComponent.updateLightBuffer = false;
        renderComponent.updateMaterialBuffer = false;

        return renderComponent;
    }

    static void UpdateLightBuffer(vk::CommandBuffer commandBuffer, const Scene& scene)
    {
        const auto sceneLightsView = scene.view<TransformComponent, LightComponent>();

        std::vector<gpu::Light> lights;
        lights.reserve(sceneLightsView.size_hint());

        for (auto&& [entity, tc, lc] : sceneLightsView.each())
        {
            gpu::Light light{};

            if (lc.type == LightType::eDirectional)
            {
                const glm::vec3 direction = tc.GetWorldTransform().GetAxis(Axis::eX);

                light.location = glm::vec4(-direction, 0.0f);
            }
            else if (lc.type == LightType::ePoint)
            {
                const glm::vec3 position = tc.GetWorldTransform().GetTranslation();

                light.location = glm::vec4(position, 1.0f);
            }

            light.color = glm::vec4(lc.color, 0.0f);

            lights.push_back(light);
        }

        if (!lights.empty())
        {
            const auto& renderComponent = scene.ctx().get<RenderSceneComponent>();

            BufferHelpers::UpdateBuffer(commandBuffer, renderComponent.lightBuffer, GetByteView(lights),
                    SyncScope::kWaitForNone, SyncScope::kUniformRead);
        }
    }

    static void UpdateMaterialBuffer(vk::CommandBuffer commandBuffer, const Scene& scene)
    {
        const auto& materialComponent = scene.ctx().get<MaterialStorageComponent>();

        std::vector<gpu::Material> materials;
        materials.reserve(materialComponent.materials.size());

        for (const Material& material : materialComponent.materials)
        {
            materials.push_back(material.data);
        }

        if (!materials.empty())
        {
            const auto& renderComponent = scene.ctx().get<RenderSceneComponent>();

            BufferHelpers::UpdateBuffer(commandBuffer, renderComponent.materialBuffer, GetByteView(materials),
                    SyncScope::kWaitForNone, SyncScope::kUniformRead);
        }
    }

    static void UpdateFrameBuffer(vk::CommandBuffer commandBuffer, const Scene& scene, uint32_t imageIndex)
    {
        const auto& renderComponent = scene.ctx().get<RenderSceneComponent>();
        const auto& cameraComponent = scene.ctx().get<CameraComponent>();

        const glm::mat4 viewProjMatrix = cameraComponent.projMatrix * cameraComponent.viewMatrix;

        const glm::mat4 inverseViewMatrix = glm::inverse(cameraComponent.viewMatrix);
        const glm::mat4 inverseProjMatrix = glm::inverse(cameraComponent.projMatrix);

        const gpu::Frame frameData{
            cameraComponent.viewMatrix,
            cameraComponent.projMatrix,
            viewProjMatrix,
            inverseViewMatrix,
            inverseProjMatrix,
            inverseViewMatrix * inverseProjMatrix,
            cameraComponent.location.position,
            cameraComponent.projection.zNear,
            cameraComponent.projection.zFar,
            Timer::GetGlobalSeconds(),
            {}
        };

        BufferHelpers::UpdateBuffer(commandBuffer, renderComponent.frameBuffers[imageIndex],
                GetByteView(frameData), SyncScope::kWaitForNone, SyncScope::kUniformRead);
    }

    static RayTracingSceneComponent CreateRayTracingSceneComponent(const Scene& scene)
    {
        TlasInstances tlasInstances;

        for (auto&& [entity, tc, rc] : scene.view<TransformComponent, RenderComponent>().each())
        {
            for (const auto& ro : rc.renderObjects)
            {
                tlasInstances.push_back(SceneHelpers::GetTlasInstance(scene, tc, ro));
            }
        }

        AccelerationStructureManager& accelerationStructureManager = *VulkanContext::accelerationStructureManager;

        return RayTracingSceneComponent{ accelerationStructureManager.CreateTlas(tlasInstances) };
    }

    static void BuildTlas(vk::CommandBuffer commandBuffer, const Scene& scene)
    {
        TlasInstances tlasInstances;

        for (auto&& [entity, tc, rc] : scene.view<TransformComponent, RenderComponent>().each())
        {
            for (const auto& ro : rc.renderObjects)
            {
                tlasInstances.push_back(SceneHelpers::GetTlasInstance(scene, tc, ro));
            }
        }

        if (!tlasInstances.empty())
        {
            const auto& rayTracingComponent = scene.ctx().get<RayTracingSceneComponent>();

            VulkanContext::accelerationStructureManager->BuildTlas(commandBuffer, rayTracingComponent.tlas,
                    tlasInstances);
        }
    }
}

SceneRenderer::SceneRenderer()
{
    hybridRenderer = std::make_unique<HybridRenderer>();

    if constexpr (Config::kRayTracingEnabled)
    {
        pathTracingRenderer = std::make_unique<PathTracingRenderer>();
    }

    renderSceneComponent = Details::CreateRenderSceneComponent();

    Engine::AddEventHandler<vk::Extent2D>(EventType::eResize,
            MakeFunction(this, &SceneRenderer::HandleResizeEvent));

    Engine::AddEventHandler<KeyInput>(EventType::eKeyInput,
            MakeFunction(this, &SceneRenderer::HandleKeyInputEvent));
}

SceneRenderer::~SceneRenderer()
{
    RemoveScene();

    if (renderSceneComponent.lightBuffer)
    {
        VulkanContext::bufferManager->DestroyBuffer(renderSceneComponent.lightBuffer);
    }

    VulkanContext::bufferManager->DestroyBuffer(renderSceneComponent.materialBuffer);

    for (const auto frameBuffer : renderSceneComponent.frameBuffers)
    {
        VulkanContext::bufferManager->DestroyBuffer(frameBuffer);
    }
}

void SceneRenderer::RegisterScene(Scene* scene_)
{
    EASY_FUNCTION()

    RemoveScene();

    scene = scene_;

    if (!scene->ctx().contains<CameraComponent&>())
    {
        Details::EmplaceDefaultCamera(*scene);
    }

    if (!scene->ctx().contains<EnvironmentComponent&>())
    {
        Details::EmplaceDefaultEnvironment(*scene);
    }

    scene->ctx().emplace<RenderSceneComponent&>(renderSceneComponent);

    renderSceneComponent.updateLightBuffer = true;
    renderSceneComponent.updateMaterialBuffer = true;

    if constexpr (Config::kRayTracingEnabled)
    {
        scene->ctx().emplace<RayTracingSceneComponent>(Details::CreateRayTracingSceneComponent(*scene));
    }

    hybridRenderer->RegisterScene(scene);
    if (pathTracingRenderer)
    {
        pathTracingRenderer->RegisterScene(scene);
    }
}

void SceneRenderer::RemoveScene()
{
    if (!scene)
    {
        return;
    }

    hybridRenderer->RemoveScene();
    if (pathTracingRenderer)
    {
        pathTracingRenderer->RemoveScene();
    }

    scene->ctx().erase<RenderSceneComponent>();

    if constexpr (Config::kRayTracingEnabled)
    {
        const auto& rayTracingComponent = scene->ctx().get<RayTracingSceneComponent>();

        VulkanContext::accelerationStructureManager->DestroyAccelerationStructure(rayTracingComponent.tlas);

        scene->ctx().erase<RayTracingSceneComponent>();
    }

    scene = nullptr;
}

void SceneRenderer::Render(vk::CommandBuffer commandBuffer, uint32_t imageIndex)
{
    Details::UpdateFrameBuffer(commandBuffer, *scene, imageIndex);

    if (renderSceneComponent.updateLightBuffer)
    {
        Details::UpdateLightBuffer(commandBuffer, *scene);

        renderSceneComponent.updateLightBuffer = false;
    }

    if (renderSceneComponent.updateMaterialBuffer)
    {
        Details::UpdateMaterialBuffer(commandBuffer, *scene);

        renderSceneComponent.updateMaterialBuffer = false;
    }

    if constexpr (Config::kRayTracingEnabled)
    {
        Details::BuildTlas(commandBuffer, *scene);
    }

    if (renderMode == RenderMode::ePathTracing && pathTracingRenderer)
    {
        pathTracingRenderer->Render(commandBuffer, imageIndex);
    }
    else
    {
        hybridRenderer->Render(commandBuffer, imageIndex);
    }
}

void SceneRenderer::HandleResizeEvent(const vk::Extent2D& extent) const
{
    if (extent.width == 0 || extent.height == 0)
    {
        return;
    }

    hybridRenderer->Resize(extent);

    if (pathTracingRenderer)
    {
        pathTracingRenderer->Resize(extent);
    }
}

void SceneRenderer::HandleKeyInputEvent(const KeyInput& keyInput)
{
    if (keyInput.action == KeyAction::ePress)
    {
        switch (keyInput.key)
        {
        case Key::eT:
            ToggleRenderMode();
            break;
        default:
            break;
        }
    }
}

void SceneRenderer::ToggleRenderMode()
{
    uint32_t i = static_cast<const uint32_t>(renderMode);

    i = (i + 1) % kRenderModeCount;

    renderMode = static_cast<RenderMode>(i);
}