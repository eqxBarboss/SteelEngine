#include "Engine/Engine.hpp"

#include "Engine/Config.hpp"
#include "Engine/Filesystem/Filesystem.hpp"
#include "Engine/Scene/Environment.hpp"
#include "Engine/Systems/CameraSystem.hpp"
#include "Engine/Systems/UIRenderSystem.hpp"
#include "Engine/Render/PathTracingRenderer.hpp"
#include "Engine/Render/HybridRenderer.hpp"
#include "Engine/Render/FrameLoop.hpp"
#include "Engine/Render/RenderContext.hpp"
#include "Engine/Render/Vulkan/VulkanContext.hpp"

namespace Details
{
    static Filepath GetScenePath()
    {
        if constexpr (Config::kUseDefaultAssets)
        {
            return Config::kDefaultScenePath;
        }
        else
        {
            const DialogDescription dialogDescription{
                "Select Scene File", Filepath("~/"),
                { "glTF Files", "*.gltf" }
            };

            const std::optional<Filepath> scenePath = Filesystem::ShowOpenDialog(dialogDescription);

            return scenePath.value_or(Config::kDefaultScenePath);
        }
    }

    static Filepath GetEnvironmentPath()
    {
        if constexpr (Config::kUseDefaultAssets)
        {
            return Config::kDefaultEnvironmentPath;
        }
        else
        {
            const DialogDescription dialogDescription{
                "Select Environment File", Filepath("~/"),
                { "Image Files", "*.hdr *.png" }
            };

            const std::optional<Filepath> environmentPath = Filesystem::ShowOpenDialog(dialogDescription);

            return environmentPath.value_or(Config::kDefaultEnvironmentPath);
        }
    }

    static std::string GetCameraPositionText(const Camera& camera)
    {
        const Camera::Location& cameraLocation = camera.GetLocation();

        const glm::vec3 cameraPosition = cameraLocation.position;

        return Format("Camera position: %.2f %.2f %.2f", cameraPosition.x, cameraPosition.y, cameraPosition.z);
    }

    static std::string GetCameraDirectionText(const Camera& camera)
    {
        const Camera::Location& cameraLocation = camera.GetLocation();

        const glm::vec3 cameraDirection = glm::normalize(cameraLocation.target - cameraLocation.position);

        return Format("Camera direction: %.2f %.2f %.2f", cameraDirection.x, cameraDirection.y, cameraDirection.z);
    }

    static std::string GetLightDirectionText(const Environment& environment)
    {
        const glm::vec4& lightDirection = environment.GetDirectLight().direction;

        return Format("Light direction: %.2f %.2f %.2f", lightDirection.x, lightDirection.y, lightDirection.z);
    }

    static std::string GetLightColorText(const Environment& environment)
    {
        const glm::vec4& lightColor = environment.GetDirectLight().color;

        return Format("Light color: %.2f %.2f %.2f", lightColor.r, lightColor.g, lightColor.b);
    }
}

Timer Engine::timer;
Engine::State Engine::state;

std::unique_ptr<Window> Engine::window;
std::unique_ptr<FrameLoop> Engine::frameLoop;

std::unique_ptr<Environment> Engine::environment;
std::unique_ptr<Camera> Engine::camera;

std::unique_ptr<Scene2> Engine::scene2;

std::unique_ptr<HybridRenderer> Engine::hybridRenderer;
std::unique_ptr<PathTracingRenderer> Engine::pathTracingRenderer;

std::vector<std::unique_ptr<System>> Engine::systems;
std::map<EventType, std::vector<EventHandler>> Engine::eventMap;

void Engine::Create()
{
    window = std::make_unique<Window>(Config::kExtent, Config::kWindowMode);

    VulkanContext::Create(*window);
    RenderContext::Create();

    AddEventHandler<vk::Extent2D>(EventType::eResize, &Engine::HandleResizeEvent);
    AddEventHandler<KeyInput>(EventType::eKeyInput, &Engine::HandleKeyInputEvent);
    AddEventHandler<MouseInput>(EventType::eMouseInput, &Engine::HandleMouseInputEvent);

    frameLoop = std::make_unique<FrameLoop>();

    environment = std::make_unique<Environment>(Details::GetEnvironmentPath());
    camera = std::make_unique<Camera>(Config::DefaultCamera::kLocation, Config::DefaultCamera::kDescription);

    scene2 = std::make_unique<Scene2>(Details::GetScenePath());

    if constexpr (Config::kRayTracingEnabled)
    {
        scene2->GenerateTlas();
    }

    hybridRenderer = std::make_unique<HybridRenderer>(scene2.get(), camera.get(), environment.get());
    //pathTracingRenderer = std::make_unique<PathTracingRenderer>(scenePT.get(), camera.get(), environment.get());

    AddSystem<CameraSystem>(camera.get());
    AddSystem<UIRenderSystem>(*window);

    GetSystem<UIRenderSystem>()->BindText([] { return Details::GetCameraPositionText(*camera); });
    GetSystem<UIRenderSystem>()->BindText([] { return Details::GetCameraDirectionText(*camera); });
    GetSystem<UIRenderSystem>()->BindText([] { return Details::GetLightDirectionText(*environment); });
    GetSystem<UIRenderSystem>()->BindText([] { return Details::GetLightColorText(*environment); });
}

void Engine::Run()
{
    while (!window->ShouldClose())
    {
        window->PollEvents();

        for (const auto& system : systems)
        {
            system->Process(timer.GetDeltaSeconds());
        }

        if (state.drawingSuspended)
        {
            continue;
        }

        frameLoop->Draw([](vk::CommandBuffer commandBuffer, uint32_t imageIndex)
            {
                /*if (state.renderMode == RenderMode::ePathTracing)
                {
                    pathTracingRenderer->Render(commandBuffer, imageIndex);
                }
                else
                {
                    hybridRenderer->Render(commandBuffer, imageIndex);
                }*/

                hybridRenderer->Render(commandBuffer, imageIndex);

                GetSystem<UIRenderSystem>()->Render(commandBuffer, imageIndex);
            });
    }
}

void Engine::Destroy()
{
    VulkanContext::device->WaitIdle();

    systems.clear();

    hybridRenderer.reset();
    pathTracingRenderer.reset();
    camera.reset();
    environment.reset();
    frameLoop.reset();
    window.reset();

    RenderContext::Destroy();

    VulkanContext::Destroy();
}

void Engine::TriggerEvent(EventType type)
{
    for (const auto& handler : eventMap[type])
    {
        handler(std::any());
    }
}

void Engine::AddEventHandler(EventType type, std::function<void()> handler)
{
    std::vector<EventHandler>& eventHandlers = eventMap[type];
    eventHandlers.emplace_back([handler](std::any)
        {
            handler();
        });
}

void Engine::HandleResizeEvent(const vk::Extent2D& extent)
{
    VulkanContext::device->WaitIdle();

    state.drawingSuspended = extent.width == 0 || extent.height == 0;

    if (!state.drawingSuspended)
    {
        const Swapchain::Description swapchainDescription{
            extent, Config::kVSyncEnabled
        };

        VulkanContext::swapchain->Recreate(swapchainDescription);
    }
}

void Engine::HandleKeyInputEvent(const KeyInput& keyInput)
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

void Engine::HandleMouseInputEvent(const MouseInput& mouseInput)
{
    if (mouseInput.button == Config::DefaultCamera::kControlMouseButton)
    {
        switch (mouseInput.action)
        {
        case MouseButtonAction::ePress:
            window->SetCursorMode(Window::CursorMode::eDisabled);
            break;
        case MouseButtonAction::eRelease:
            window->SetCursorMode(Window::CursorMode::eEnabled);
            break;
        default:
            break;
        }
    }
}

void Engine::ToggleRenderMode()
{
    uint32_t i = static_cast<const uint32_t>(state.renderMode);

    i = (i + 1) % kRenderModeCount;

    state.renderMode = static_cast<RenderMode>(i);
}
