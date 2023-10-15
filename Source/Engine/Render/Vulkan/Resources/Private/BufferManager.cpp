#include "Engine/Render/Vulkan/Resources/BufferManager.hpp"

#include "Engine/Render/Vulkan/VulkanContext.hpp"

#include "Utils/Assert.hpp"

namespace Details
{
    static vk::BufferCreateInfo GetBufferCreateInfo(const BufferDescription& description)
    {
        const Queues::Description& queuesDescription = VulkanContext::device->GetQueuesDescription();

        const vk::BufferCreateInfo createInfo({}, description.size, description.usage,
                vk::SharingMode::eExclusive, 0, &queuesDescription.graphicsFamilyIndex);

        return createInfo;
    }
}

vk::Buffer BufferManager::CreateBuffer(const BufferDescription& description)
{
    const vk::BufferCreateInfo createInfo = Details::GetBufferCreateInfo(description);

    vk::Buffer buffer;

    if (description.scratchAlignment)
    {
        buffer = VulkanContext::memoryManager->CreateBuffer(createInfo, vk::MemoryPropertyFlagBits::eDeviceLocal,
                VulkanContext::device->GetRayTracingProperties().minScratchOffsetAlignment);
    }
    else
    {
        buffer = VulkanContext::memoryManager->CreateBuffer(createInfo, vk::MemoryPropertyFlagBits::eDeviceLocal);
    }

    vk::Buffer stagingBuffer = nullptr;

    if (description.stagingBuffer)
    {
        stagingBuffer = BufferHelpers::CreateStagingBuffer(description.size);
    }

    buffers.emplace(buffer, BufferEntry{ description, stagingBuffer });

    return buffer;
}

vk::Buffer BufferManager::CreateBufferWithData(vk::BufferUsageFlags usage, const ByteView& data)
{
    const BufferDescription description{
        .size = data.size,
        .usage = usage | vk::BufferUsageFlagBits::eTransferDst,
        .stagingBuffer = true
    };

    const vk::Buffer buffer = CreateBuffer(description);

    VulkanContext::device->ExecuteOneTimeCommands([&](vk::CommandBuffer commandBuffer)
        {
            VulkanContext::bufferManager->UpdateBuffer(commandBuffer, buffer, BufferUpdate{ data });
        });

    return buffer;
}

vk::Buffer BufferManager::CreateEmptyBuffer(vk::BufferUsageFlags usage, vk::DeviceSize size)
{
    const BufferDescription description{
        .size = size,
        .usage = usage | vk::BufferUsageFlagBits::eTransferDst,
        .stagingBuffer = true
    };

    return CreateBuffer(description);
}

const BufferDescription& BufferManager::GetBufferDescription(vk::Buffer buffer) const
{
    return buffers.at(buffer).description;
}

void BufferManager::UpdateBuffer(vk::CommandBuffer commandBuffer,
        vk::Buffer buffer, const BufferUpdate& update) const
{
    const auto& [description, stagingBuffer] = buffers.at(buffer);

    Assert(description.usage & vk::BufferUsageFlagBits::eTransferDst);

    const MemoryBlock memoryBlock = VulkanContext::memoryManager->GetBufferMemoryBlock(stagingBuffer);

    if (update.updater)
    {
        update.updater(VulkanContext::memoryManager->MapMemory(memoryBlock));
    }
    else
    {
        update.data.CopyTo(VulkanContext::memoryManager->MapMemory(memoryBlock));
    }

    VulkanContext::memoryManager->UnmapMemory(memoryBlock);

    BufferHelpers::InsertPipelineBarrier(commandBuffer, buffer,
            PipelineBarrier{ update.waitedScope, SyncScope::kTransferWrite });

    commandBuffer.copyBuffer(stagingBuffer, buffer, { vk::BufferCopy(0, 0, description.size) });

    BufferHelpers::InsertPipelineBarrier(commandBuffer, buffer,
            PipelineBarrier{ SyncScope::kTransferWrite, update.blockedScope });
}

void BufferManager::ReadBuffer(vk::CommandBuffer commandBuffer,
        vk::Buffer buffer, const BufferReader& reader) const
{
    const auto& [description, stagingBuffer] = buffers.at(buffer);

    Assert(description.usage & vk::BufferUsageFlagBits::eTransferSrc);

    const vk::BufferCopy region(0, 0, description.size);

    commandBuffer.copyBuffer(buffer, stagingBuffer, { region });

    const MemoryBlock memoryBlock = VulkanContext::memoryManager->GetBufferMemoryBlock(stagingBuffer);

    reader(VulkanContext::memoryManager->MapMemory(memoryBlock));

    VulkanContext::memoryManager->UnmapMemory(memoryBlock);
}

void BufferManager::DestroyBuffer(vk::Buffer buffer)
{
    const auto& [description, stagingBuffer] = buffers.at(buffer);

    if (stagingBuffer)
    {
        VulkanContext::memoryManager->DestroyBuffer(stagingBuffer);
    }

    VulkanContext::memoryManager->DestroyBuffer(buffer);

    buffers.erase(buffers.find(buffer));
}
