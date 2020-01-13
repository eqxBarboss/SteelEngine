#pragma once

#include <list>

#include "Engine/Render/Vulkan/Device.hpp"

#include "Engine/Render/Vulkan/Resources/BufferData.hpp"
#include "Engine/Render/Vulkan/Resources/TransferManager.hpp"

class BufferManager
{
public:
    static std::unique_ptr<BufferManager> Create(std::shared_ptr<Device> device,
            std::shared_ptr<TransferManager> transferManager);

    BufferManager(std::shared_ptr<Device> aDevice, std::shared_ptr<TransferManager> aTransferManager);
    ~BufferManager();

    BufferData CreateBuffer(const BufferProperties &properties);

    template <class T>
    BufferData CreateBuffer(const BufferProperties &properties, std::vector<T> initialData);

    void ForceUpdate(const BufferData &aBufferData);

    void UpdateBuffers();

    BufferData Destroy(const BufferData &aBufferData);

private:
    std::shared_ptr<Device> device;
    std::shared_ptr<TransferManager> transferManager;

    std::list<BufferData> buffers;
};

template <class T>
BufferData BufferManager::CreateBuffer(const BufferProperties &properties, std::vector<T> initialData)
{
    BufferData bufferData = CreateBuffer(properties);
    auto [data, count] = bufferData.AccessData<T>();

    Assert(initialData.size() <= count);

    std::copy(initialData.begin(), initialData.end(), data);

    bufferData.MarkForUpdate();

    return bufferData;
}