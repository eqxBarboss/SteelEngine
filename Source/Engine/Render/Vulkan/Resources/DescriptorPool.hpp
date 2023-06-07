#pragma once

#include "Engine/Render/Vulkan/Resources/DescriptorHelpers.hpp"

// TODO rename to DescriptorManager, implement DescriptorSetLayouts cache
class DescriptorPool
{
public:
    static std::unique_ptr<DescriptorPool> Create(uint32_t maxSetCount,
            const std::vector<vk::DescriptorPoolSize>& poolSizes);

    ~DescriptorPool();

    vk::DescriptorSetLayout CreateDescriptorSetLayout(const DescriptorSetDescription& description) const;

    void DestroyDescriptorSetLayout(vk::DescriptorSetLayout layout);

    std::vector<vk::DescriptorSet> AllocateDescriptorSets(const std::vector<vk::DescriptorSetLayout>& layouts) const;

    std::vector<vk::DescriptorSet> AllocateDescriptorSets(const std::vector<vk::DescriptorSetLayout>& layouts,
            const std::vector<uint32_t>& descriptorCounts) const;

    void FreeDescriptorSets(const std::vector<vk::DescriptorSet>& sets) const;

    // TODO remove
    void UpdateDescriptorSet(vk::DescriptorSet set, const DescriptorSetData& data, uint32_t bindingOffset) const;

    void UpdateDescriptorSet(const std::vector<vk::WriteDescriptorSet>& writes);

private:
    vk::DescriptorPool descriptorPool;

    DescriptorPool(vk::DescriptorPool descriptorPool_);
};
