#pragma once

#include <list>

#include "Engine/Render/Vulkan/VulkanDevice.hpp"
#include "Engine/Render/Vulkan/BufferStructs.hpp"

class BufferPool
{
public:
    static std::unique_ptr<BufferPool> Create(std::shared_ptr<VulkanDevice> device);

    BufferPool(std::shared_ptr<VulkanDevice> aDevice);
    ~BufferPool();

    BufferData CreateBuffer(const BufferProperties &properties);

    template <class T>
    BufferData CreateBuffer(const BufferProperties &properties, std::vector<T> initialData, uint32_t dataOffset = 0);

    void ForceUpdate(const BufferData &aBufferData);

    void Update();

    BufferData Destroy(const BufferData &aBufferData);

private:
    std::shared_ptr<VulkanDevice> device;

    std::list<BufferData> buffers;
};

template <class T>
BufferData BufferPool::CreateBuffer(const BufferProperties &properties, std::vector<T> initialData, uint32_t dataOffset)
{
    BufferData bufferData = CreateBuffer(properties);
    auto [data, count] = bufferData.AccessData<T>();

    Assert(dataOffset + initialData.size() <= count);

    std::copy(initialData.begin(), initialData.end(), data);

    bufferData.MarkForUpdate();

    return bufferData;
}
