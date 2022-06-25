#pragma once

#include "Engine/Camera.hpp"
#include "Engine/Render/PathTracingRenderer.hpp"
#include "Vulkan/Resources/ImageHelpers.hpp"

class ProbeRenderer
        : Camera
        , PathTracingRenderer
{
public:
    ProbeRenderer(const Scene* scene_, const Environment* environment_);

    Texture CaptureProbe(const glm::vec3& position);

private:
    void SetupRenderTargetsDescriptorSet(
            const ImageHelpers::CubeFacesViews& probeFacesViews);
};
