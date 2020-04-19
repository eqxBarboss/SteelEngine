#pragma once

#include "Engine/Render/Vulkan/Resources/Texture.hpp"
#include "Engine/Filesystem.hpp"

class TextureCache
{
public:
    TextureCache() = default;
    ~TextureCache();

    Texture GetTexture(const Filepath &filepath, const SamplerDescription &samplerDescription);

    vk::Sampler GetSampler(const SamplerDescription &description);

    Texture CreateCubeTexture(const Texture &equirectangularTexture,
            const vk::Extent2D &extent, const SamplerDescription &samplerDescription);

private:
    struct TextureEntry
    {
        vk::Image image;
        vk::ImageView view;
    };

    std::unordered_map<Filepath, TextureEntry> textures;
    std::unordered_map<SamplerDescription, vk::Sampler> samplers;
};
