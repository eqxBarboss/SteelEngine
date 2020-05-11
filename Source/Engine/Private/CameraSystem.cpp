#include "Engine/CameraSystem.hpp"

#include "Engine/EngineHelpers.hpp"

namespace SCameraSystem
{
    constexpr float kSensitivityReduction = 0.001f;
    constexpr float kPitchLimitRad = glm::radians(89.0f);

    const std::map<CameraSystem::MovementAxis, glm::vec3> kMovementAxisDirections{
        { CameraSystem::MovementAxis::eForward, Direction::kForward },
        { CameraSystem::MovementAxis::eLeft, Direction::kLeft },
        { CameraSystem::MovementAxis::eUp, Direction::kUp },
    };

    glm::quat GetOrientationQuat(const glm::vec2 yawPitch)
    {
        const glm::quat yawQuat = glm::angleAxis(yawPitch.x, Direction::kDown);
        const glm::quat pitchQuat = glm::angleAxis(yawPitch.y, Direction::kRight);

        return glm::normalize(yawQuat * pitchQuat);
    }
}

CameraSystem::CameraSystem(Camera *camera_, const Parameters &parameters_,
        const MovementKeyBindings &movementKeyBindings_,
        const SpeedKeyBindings &speedKeyBindings_)
    : camera(camera_)
    , parameters(parameters_)
    , movementKeyBindings(movementKeyBindings_)
    , speedKeyBindings(speedKeyBindings_)
{
    const glm::vec3 direction = glm::normalize(camera->GetDescription().direction);
    const glm::vec2 projection(direction.x, direction.z);

    state.yawPitch.x = glm::atan(direction.x, -direction.z);
    state.yawPitch.y = std::atan2(direction.y, glm::length(projection));;
}

void CameraSystem::Process(float deltaSeconds, EngineState &engineState)
{
    engineState.cameraUpdated = state.rotated || CameraMoved();

    const glm::vec3 movementDirection = SCameraSystem::GetOrientationQuat(state.yawPitch) * GetMovementDirection();

    const float speed = parameters.baseSpeed * std::powf(parameters.speedMultiplier, state.speedIndex);
    const float distance = speed * deltaSeconds;

    camera->SetPosition(camera->GetDescription().position + movementDirection * distance);

    state.rotated = false;
}

void CameraSystem::OnResize(const vk::Extent2D &extent)
{
    if (extent.width != 0 && extent.height != 0)
    {
        camera->SetAspect(extent.width / static_cast<float>(extent.height));
    }
}

void CameraSystem::OnKeyInput(Key key, KeyAction action, ModifierFlags)
{
    switch (action)
    {
    case KeyAction::ePress:
        OnKeyPress(key);
        break;
    case KeyAction::eRelease:
        OnKeyRelease(key);
        break;
    case KeyAction::eRepeat:
        break;
    }
}

void CameraSystem::OnMouseMove(const glm::vec2 &position)
{
    if (lastMousePosition.has_value())
    {
        glm::vec2 delta = position - lastMousePosition.value();
        delta.y = -delta.y;

        state.yawPitch += delta * parameters.sensitivity * SCameraSystem::kSensitivityReduction;
        state.yawPitch.y = std::clamp(state.yawPitch.y, -SCameraSystem::kPitchLimitRad, SCameraSystem::kPitchLimitRad);

        const glm::vec3 direction = SCameraSystem::GetOrientationQuat(state.yawPitch) * Direction::kForward;
        camera->SetDirection(glm::normalize(direction));
    }

    lastMousePosition = position;

    state.rotated = true;
}

void CameraSystem::OnKeyPress(Key key)
{
    if (const auto it = std::find(speedKeyBindings.begin(), speedKeyBindings.end(), key);it != speedKeyBindings.end())
    {
        state.speedIndex = static_cast<float>(std::distance(speedKeyBindings.begin(), it));
        return;
    }

    const auto pred = [&key](const MovementKeyBindings::value_type &entry)
        {
            return entry.second.first == key || entry.second.second == key;
        };

    const auto it = std::find_if(movementKeyBindings.begin(), movementKeyBindings.end(), pred);

    if (it != movementKeyBindings.end())
    {
        const auto &[axis, keys] = *it;

        MovementValue &value = state.movement.at(axis);
        if (value == MovementValue::eNone)
        {
            value = key == keys.first ? MovementValue::ePositive : MovementValue::eNegative;
        }
        else
        {
            value = key == keys.first ? MovementValue::eWeakNegative : MovementValue::eWeakPositive;
        }
    }
}

void CameraSystem::OnKeyRelease(Key key)
{
    const auto pred = [&key](const MovementKeyBindings::value_type &value)
        {
            return value.second.first == key || value.second.second == key;
        };

    const auto it = std::find_if(movementKeyBindings.begin(), movementKeyBindings.end(), pred);

    if (it != movementKeyBindings.end())
    {
        const auto &[axis, keys] = *it;

        MovementValue &value = state.movement.at(axis);
        if (value == MovementValue::ePositive || value == MovementValue::eNegative)
        {
            value = MovementValue::eNone;
        }
        else
        {
            value = key == keys.first ? MovementValue::eNegative : MovementValue::ePositive;
        }
    }
}

bool CameraSystem::CameraMoved() const
{
    return std::any_of(state.movement.begin(), state.movement.end(), [](const auto &entry)
        {
            return entry.second != MovementValue::eNone;
        });
}

glm::vec3 CameraSystem::GetMovementDirection() const
{
    glm::vec3 movementDirection(0.0f);

    for (const auto &[axis, value] : state.movement)
    {
        switch (value)
        {
        case MovementValue::ePositive:
        case MovementValue::eWeakPositive:
            movementDirection += SCameraSystem::kMovementAxisDirections.at(axis);
            break;
        case MovementValue::eWeakNegative:
        case MovementValue::eNegative:
            movementDirection -= SCameraSystem::kMovementAxisDirections.at(axis);
            break;
        case MovementValue::eNone:
            break;
        }
    }

    if (movementDirection != glm::vec3(0.0f))
    {
        movementDirection = glm::normalize(movementDirection);
    }

    return movementDirection;
}
