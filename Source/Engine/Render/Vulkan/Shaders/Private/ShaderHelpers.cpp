#include <spirv_reflect.h>

#include "Engine/Render/Vulkan/Shaders/ShaderHelpers.hpp"

#include "Engine/Render/Vulkan/Shaders/ShaderManager.hpp"

namespace Details
{
    vk::DescriptorType GetDescriptorType(SpvReflectDescriptorType descriptorType)
    {
        switch (descriptorType)
        {
        case SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLER:
            return vk::DescriptorType::eSampler;
        case SPV_REFLECT_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
            return vk::DescriptorType::eCombinedImageSampler;
        case SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
            return vk::DescriptorType::eSampledImage;
        case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_IMAGE:
            return vk::DescriptorType::eStorageImage;
        case SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
            return vk::DescriptorType::eUniformTexelBuffer;
        case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
            return vk::DescriptorType::eStorageTexelBuffer;
        case SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
            return vk::DescriptorType::eUniformBuffer;
        case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER:
            return vk::DescriptorType::eStorageBuffer;
        case SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
            return vk::DescriptorType::eUniformBufferDynamic;
        case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
            return vk::DescriptorType::eStorageBufferDynamic;
        case SPV_REFLECT_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
            return vk::DescriptorType::eInputAttachment;
        case SPV_REFLECT_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR:
            return vk::DescriptorType::eAccelerationStructureKHR;
        default:
            Assert(false);
            return {};
        }
    }

    vk::ShaderStageFlagBits GetShaderStage(SpvReflectShaderStageFlagBits shaderStage)
    {
        switch (shaderStage)
        {
        case SPV_REFLECT_SHADER_STAGE_VERTEX_BIT:
            return vk::ShaderStageFlagBits::eVertex;
        case SPV_REFLECT_SHADER_STAGE_TESSELLATION_CONTROL_BIT:
            return vk::ShaderStageFlagBits::eTessellationControl;
        case SPV_REFLECT_SHADER_STAGE_TESSELLATION_EVALUATION_BIT:
            return vk::ShaderStageFlagBits::eTessellationEvaluation;
        case SPV_REFLECT_SHADER_STAGE_GEOMETRY_BIT:
            return vk::ShaderStageFlagBits::eGeometry;
        case SPV_REFLECT_SHADER_STAGE_FRAGMENT_BIT:
            return vk::ShaderStageFlagBits::eFragment;
        case SPV_REFLECT_SHADER_STAGE_COMPUTE_BIT:
            return vk::ShaderStageFlagBits::eCompute;
        case SPV_REFLECT_SHADER_STAGE_TASK_BIT_EXT:
            return vk::ShaderStageFlagBits::eTaskEXT;
        case SPV_REFLECT_SHADER_STAGE_MESH_BIT_EXT:
            return vk::ShaderStageFlagBits::eMeshEXT;
        case SPV_REFLECT_SHADER_STAGE_RAYGEN_BIT_KHR:
            return vk::ShaderStageFlagBits::eRaygenKHR;
        case SPV_REFLECT_SHADER_STAGE_ANY_HIT_BIT_KHR:
            return vk::ShaderStageFlagBits::eAnyHitKHR;
        case SPV_REFLECT_SHADER_STAGE_CLOSEST_HIT_BIT_KHR:
            return vk::ShaderStageFlagBits::eClosestHitKHR;
        case SPV_REFLECT_SHADER_STAGE_MISS_BIT_KHR:
            return vk::ShaderStageFlagBits::eMissKHR;
        case SPV_REFLECT_SHADER_STAGE_INTERSECTION_BIT_KHR:
            return vk::ShaderStageFlagBits::eIntersectionKHR;
        case SPV_REFLECT_SHADER_STAGE_CALLABLE_BIT_KHR:
            return vk::ShaderStageFlagBits::eCallableKHR;
        default:
            Assert(false);
            return {};
        }
    }

    DescriptorDescription BuildDescriptorReflection(const SpvReflectDescriptorBinding& descriptorBinding)
    {
        return DescriptorDescription{
            descriptorBinding.count,
            GetDescriptorType(descriptorBinding.descriptor_type),
            vk::ShaderStageFlags(),
            vk::DescriptorBindingFlagBits()
        };
    }

    DescriptorSetDescription BuildDescriptorSetReflection(const SpvReflectDescriptorSet& descriptorSet)
    {
        DescriptorSetDescription descriptorSetReflection;
        descriptorSetReflection.reserve(descriptorSet.binding_count);

        std::sort(descriptorSet.bindings, descriptorSet.bindings + descriptorSet.binding_count,
                [](const SpvReflectDescriptorBinding* a, const SpvReflectDescriptorBinding* b)
                    {
                        return a->binding < b->binding;
                    });

        for (uint32_t bindingIndex = 0; bindingIndex < descriptorSet.binding_count;)
        {
            const SpvReflectDescriptorBinding* descriptorBinding = descriptorSet.bindings[bindingIndex];

            if (descriptorBinding->binding == descriptorSetReflection.size())
            {
                descriptorSetReflection.push_back(BuildDescriptorReflection(*descriptorBinding));

                ++bindingIndex;
            }
            else
            {
                descriptorSetReflection.emplace_back();
            }
        }

        return descriptorSetReflection;
    }

    std::vector<DescriptorSetDescription> BuildDescriptorSetsReflection(const spv_reflect::ShaderModule& shaderModule)
    {
        uint32_t descriptorSetCount;
        SpvReflectResult result = shaderModule.EnumerateDescriptorSets(&descriptorSetCount, nullptr);
        Assert(result == SPV_REFLECT_RESULT_SUCCESS);

        std::vector<SpvReflectDescriptorSet*> descriptorSets(descriptorSetCount);
        result = shaderModule.EnumerateDescriptorSets(&descriptorSetCount, descriptorSets.data());
        Assert(result == SPV_REFLECT_RESULT_SUCCESS);

        std::ranges::sort(descriptorSets, [](const SpvReflectDescriptorSet* a, const SpvReflectDescriptorSet* b)
            {
                return a->set < b->set;
            });

        std::vector<DescriptorSetDescription> descriptorSetsReflection;
        descriptorSetsReflection.reserve(descriptorSets.size());

        for (size_t setIndex = 0; setIndex < descriptorSets.size();)
        {
            const SpvReflectDescriptorSet* descriptorSet = descriptorSets[setIndex];

            if (descriptorSet->set == descriptorSetsReflection.size())
            {
                descriptorSetsReflection.push_back(BuildDescriptorSetReflection(*descriptorSet));

                ++setIndex;
            }
            else
            {
                descriptorSetsReflection.emplace_back();
            }
        }

        const vk::ShaderStageFlagBits shaderStage = GetShaderStage(shaderModule.GetShaderStage());

        for (DescriptorSetDescription& descriptorSetDescription : descriptorSetsReflection)
        {
            for (DescriptorDescription& descriptorDescription : descriptorSetDescription)
            {
                descriptorDescription.stageFlags = shaderStage;
            }
        }

        return descriptorSetsReflection;
    }

    std::map<std::string, vk::PushConstantRange> BuildPushConstantsReflection(const spv_reflect::ShaderModule& shaderModule)
    {
        uint32_t pushConstantCount;
        SpvReflectResult result = shaderModule.EnumeratePushConstantBlocks(&pushConstantCount, nullptr);
        Assert(result == SPV_REFLECT_RESULT_SUCCESS);

        std::vector<SpvReflectBlockVariable*> pushConstants(pushConstantCount);
        result = shaderModule.EnumeratePushConstantBlocks(&pushConstantCount, pushConstants.data());
        Assert(result == SPV_REFLECT_RESULT_SUCCESS);

        const vk::ShaderStageFlagBits shaderStage = GetShaderStage(shaderModule.GetShaderStage());

        std::map<std::string, vk::PushConstantRange> pushConstantsReflection;

        for (const SpvReflectBlockVariable* pushConstant : pushConstants)
        {
            const vk::PushConstantRange pushConstantRange(shaderStage, pushConstant->offset, pushConstant->size);

            pushConstantsReflection.emplace(std::string(pushConstant->name), pushConstantRange);
        }

        return pushConstantsReflection;
    }

    void MergeDescriptorSetReflections(DescriptorSetDescription& dstDescriptorSet,
            const DescriptorSetDescription& srcDescriptorSet)
    {
        for (size_t i = 0; i < srcDescriptorSet.size(); ++i)
        {
            if (i == dstDescriptorSet.size())
            {
                dstDescriptorSet.push_back(srcDescriptorSet[i]);
            }
            else if (dstDescriptorSet[i].count == 0)
            {
                dstDescriptorSet[i] = srcDescriptorSet[i];
            }
            else
            {
                Assert(dstDescriptorSet[i].type == srcDescriptorSet[i].type);
                Assert(dstDescriptorSet[i].count == srcDescriptorSet[i].count);

                dstDescriptorSet[i].stageFlags |= srcDescriptorSet[i].stageFlags;
                dstDescriptorSet[i].bindingFlags |= srcDescriptorSet[i].bindingFlags;
            }
        }
    }

    void MergeDescriptorSetsReflections(std::vector<DescriptorSetDescription>& dstDescriptorSets,
            const std::vector<DescriptorSetDescription>& srcDescriptorSets)
    {
        for (size_t i = 0; i < srcDescriptorSets.size(); ++i)
        {
            if (i == dstDescriptorSets.size())
            {
                dstDescriptorSets.push_back(srcDescriptorSets[i]);
            }
            else if (srcDescriptorSets[i].empty())
            {
                dstDescriptorSets[i] = srcDescriptorSets[i];
            }
            else
            {
                MergeDescriptorSetReflections(dstDescriptorSets[i], srcDescriptorSets[i]);
            }
        }
    }
}

ShaderSpecialization::ShaderSpecialization(const ShaderSpecialization& other)
    : map(other.map)
    , data(other.data)
{
    info = other.info;
    info.pMapEntries = map.data();
    info.pData = data.data();
}

ShaderSpecialization::ShaderSpecialization(ShaderSpecialization&& other) noexcept
    : map(std::move(other.map))
    , data(std::move(other.data))
{
    info = other.info;
    info.pMapEntries = map.data();
    info.pData = data.data();
}

ShaderSpecialization& ShaderSpecialization::operator=(ShaderSpecialization other)
{
    if (this != &other)
    {
        std::swap(map, other.map);
        std::swap(info, other.info);
        std::swap(data, other.data);
    }

    return *this;
}

std::vector<vk::PipelineShaderStageCreateInfo> ShaderHelpers::CreateShaderStagesCreateInfo(
        const std::vector<ShaderModule>& shaderModules)
{
    std::vector<vk::PipelineShaderStageCreateInfo> createInfo;
    createInfo.reserve(shaderModules.size());

    for (const auto& shaderModule : shaderModules)
    {
        const vk::SpecializationInfo* pSpecializationInfo = nullptr;

        if (!shaderModule.specialization.map.empty())
        {
            pSpecializationInfo = &shaderModule.specialization.info;
        }

        createInfo.emplace_back(vk::PipelineShaderStageCreateFlags(),
                shaderModule.stage, shaderModule.module, "main", pSpecializationInfo);
    }

    return createInfo;
}

ShaderReflection ShaderHelpers::RetrieveShaderReflection(const std::vector<uint32_t>& spirvCode)
{
    const spv_reflect::ShaderModule shaderModule(spirvCode);

    ShaderReflection reflection;
    reflection.descriptorSets = Details::BuildDescriptorSetsReflection(shaderModule);
    reflection.pushConstants = Details::BuildPushConstantsReflection(shaderModule);

    return reflection;
}

ShaderReflection ShaderHelpers::MergeShaderReflections(const std::vector<ShaderReflection>& reflections)
{
    ShaderReflection mergedReflection;

    for (const ShaderReflection& reflection : reflections)
    {
        Details::MergeDescriptorSetsReflections(mergedReflection.descriptorSets, reflection.descriptorSets);
    }

    return mergedReflection;
}
