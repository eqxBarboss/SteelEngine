#pragma once

#include "Engine/Render/Vulkan/Device.hpp"
#include "Engine/Render/Vulkan/Resources/Buffer.hpp"
#include "Engine/Render/Vulkan/Resources/MemoryManager.hpp"
#include "Engine/Render/Vulkan/Resources/ResourcesHelpers.hpp"

#include "Utils/Flags.hpp"

enum class BufferCreateFlagBits
{
    eCpuMemory,
    eStagingBuffer,
};

using BufferCreateFlags = Flags<BufferCreateFlagBits>;

OVERLOAD_LOGIC_OPERATORS(BufferCreateFlags, BufferCreateFlagBits)

class BufferManager
        : protected SharedStagingBufferProvider
{
public:
    BufferManager(std::shared_ptr<Device> device_, std::shared_ptr<MemoryManager> memoryManager_);
    ~BufferManager();

    BufferHandle CreateBuffer(const BufferDescription &description, BufferCreateFlags bufferCreateFlags);

    template <class T>
    BufferHandle CreateBuffer(const BufferDescription &description, BufferCreateFlags bufferCreateFlags,
            std::vector<T> initialData);

    void UpdateBuffer(BufferHandle handle, vk::CommandBuffer commandBuffer);

    void DestroyBuffer(BufferHandle handle);

private:
    std::shared_ptr<Device> device;
    std::shared_ptr<MemoryManager> memoryManager;

    std::map<BufferHandle, vk::Buffer> buffers;
};

template <class T>
BufferHandle BufferManager::CreateBuffer(const BufferDescription &description,
        BufferCreateFlags bufferCreateFlags, std::vector<T> initialData)
{
    const vk::DeviceSize initialDataSize = initialData.size() * sizeof(T);
    Assert(initialDataSize <= description.size);

    const BufferHandle handle = CreateBuffer(description, bufferCreateFlags);

    if (!(bufferCreateFlags & BufferCreateFlagBits::eCpuMemory))
    {
        handle->cpuData = reinterpret_cast<uint8_t *>(initialData.data());
    }
    else
    {
        auto [data, size] = handle->AccessCpuData<T>();
        std::copy(initialData.begin(), initialData.end(), data);
    }

    if (!(bufferCreateFlags & BufferCreateFlagBits::eStagingBuffer)
        && !(description.memoryProperties & vk::MemoryPropertyFlagBits::eHostVisible))
    {
        UpdateSharedStagingBuffer(GetRef(device), GetRef(memoryManager), initialDataSize);

        buffers[handle] = sharedStagingBuffer.buffer;
    }

    device->ExecuteOneTimeCommands([this, &handle](vk::CommandBuffer commandBuffer)
        {
            UpdateBuffer(handle, commandBuffer);
        });

    if (!(bufferCreateFlags & BufferCreateFlagBits::eCpuMemory))
    {
        handle->cpuData = nullptr;
    }

    if (!(bufferCreateFlags & BufferCreateFlagBits::eStagingBuffer)
        && !(description.memoryProperties & vk::MemoryPropertyFlagBits::eHostVisible))
    {
        buffers[handle] = nullptr;
    }

    return handle;
}
