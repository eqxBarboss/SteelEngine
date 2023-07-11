#include "Engine/Engine.hpp"

#include "Engine/Config.hpp"
#include "Engine/Filesystem/Filesystem.hpp"
#include "Engine/Systems/CameraSystem.hpp"
#include "Engine/Render/FrameLoop.hpp"
#include "Engine/Render/RenderContext.hpp"
#include "Engine/Render/SceneRenderer.hpp"
#include "Engine/Render/UIRenderer.hpp"
#include "Engine/Render/Vulkan/VulkanContext.hpp"
#include "Engine/Systems/TransformSystem.hpp"

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
}

Timer Engine::timer;

bool Engine::drawingSuspended = false;

std::unique_ptr<Window> Engine::window;
std::unique_ptr<FrameLoop> Engine::frameLoop;

std::unique_ptr<Scene> Engine::scene;

std::unique_ptr<SceneRenderer> Engine::sceneRenderer;
std::unique_ptr<UIRenderer> Engine::uiRenderer;

std::vector<std::unique_ptr<System>> Engine::systems;
std::map<EventType, std::vector<EventHandler>> Engine::eventMap;

void Engine::Create()
{
    EASY_FUNCTION()

    window = std::make_unique<Window>(Config::kExtent, Config::kWindowMode);

    VulkanContext::Create(*window);
    RenderContext::Create();

    AddEventHandler<vk::Extent2D>(EventType::eResize, &Engine::HandleResizeEvent);
    AddEventHandler<KeyInput>(EventType::eKeyInput, &Engine::HandleKeyInputEvent);
    AddEventHandler<MouseInput>(EventType::eMouseInput, &Engine::HandleMouseInputEvent);

    frameLoop = std::make_unique<FrameLoop>();

    sceneRenderer = std::make_unique<SceneRenderer>();
    uiRenderer = std::make_unique<UIRenderer>(*window);

    AddSystem<TransformSystem>();
    AddSystem<CameraSystem>();

    OpenScene();
}

void Engine::Run()
{
    while (!window->ShouldClose())
    {
        EASY_BLOCK("Engine::Frame")

        window->PollEvents();

        timer.Tick();

        if (scene)
        {
            for (const auto& system : systems)
            {
                system->Process(*scene, timer.GetDeltaSeconds());
            }
        }

        if (drawingSuspended)
        {
            continue;
        }

        frameLoop->Draw([](vk::CommandBuffer commandBuffer, uint32_t imageIndex)
            {
                sceneRenderer->Render(commandBuffer, imageIndex);
                uiRenderer->Render(commandBuffer, imageIndex);
            });
    }
}

void Engine::Destroy()
{
    VulkanContext::device->WaitIdle();

    systems.clear();

    uiRenderer.reset();
    sceneRenderer.reset();

    scene.reset();
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

    drawingSuspended = extent.width == 0 || extent.height == 0;

    if (!drawingSuspended)
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
        case Key::eO:
            OpenScene();
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

void Engine::OpenScene()
{
    EASY_FUNCTION()

    VulkanContext::device->WaitIdle();

    sceneRenderer->RemoveScene();

    scene = std::make_unique<Scene>(Details::GetScenePath());

    for (const auto& system : systems)
    {
        system->Process(*scene, 0.0f);
    }

    sceneRenderer->RegisterScene(scene.get());
}
