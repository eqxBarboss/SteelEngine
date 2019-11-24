#pragma once

#include <vulkan/vulkan.hpp>

#include <vector>

class VulkanInstance
{
public:
    VulkanInstance() = delete;
    VulkanInstance(const VulkanInstance&) = delete;
    VulkanInstance(std::vector<const char*> requiredExtensions, bool validationEnabled);

    vk::Instance Get() const { return instance.get(); }

private:
    void SetupDebugReportCallback();

    vk::UniqueInstance instance;
    vk::UniqueDebugReportCallbackEXT debugReportCallback;
};
