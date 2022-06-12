#include <tiny_gltf.h>

#include "Engine2/Scene2.hpp"

#include "Engine/Render/RenderContext.hpp"
#include "Engine/Render/Vulkan/VulkanConfig.hpp"
#include "Engine/Render/Vulkan/VulkanContext.hpp"
#include "Engine2/Components2.hpp"
#include "Engine2/RenderComponent.hpp"
#include "Engine2/Material.hpp"
#include "Engine2/Primitive.hpp"

#include "Utils/Assert.hpp"
#include "Utils/Helpers.hpp"
#include "Utils/Logger.hpp"

namespace Details
{
    using NodeFunctor = std::function<entt::entity(const tinygltf::Node&, entt::entity)>;

    static vk::Format GetFormat(const tinygltf::Image& image)
    {
        Assert(image.bits == 8);
        Assert(image.pixel_type == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE);

        switch (image.component)
        {
        case 1:
            return vk::Format::eR8Unorm;
        case 2:
            return vk::Format::eR8G8Unorm;
        case 3:
            return vk::Format::eR8G8B8Unorm;
        case 4:
            return vk::Format::eR8G8B8A8Unorm;
        default:
            Assert(false);
            return vk::Format::eUndefined;
        }
    }

    static vk::Filter GetSamplerFilter(int32_t filter)
    {
        switch (filter)
        {
        case TINYGLTF_TEXTURE_FILTER_NEAREST:
        case TINYGLTF_TEXTURE_FILTER_NEAREST_MIPMAP_NEAREST:
        case TINYGLTF_TEXTURE_FILTER_NEAREST_MIPMAP_LINEAR:
            return vk::Filter::eNearest;
        case TINYGLTF_TEXTURE_FILTER_LINEAR:
        case TINYGLTF_TEXTURE_FILTER_LINEAR_MIPMAP_NEAREST:
        case TINYGLTF_TEXTURE_FILTER_LINEAR_MIPMAP_LINEAR:
            return vk::Filter::eLinear;
        default:
            Assert(false);
            return vk::Filter::eLinear;
        }
    }

    static vk::SamplerMipmapMode GetSamplerMipmapMode(int32_t filter)
    {
        switch (filter)
        {
        case TINYGLTF_TEXTURE_FILTER_NEAREST:
        case TINYGLTF_TEXTURE_FILTER_NEAREST_MIPMAP_NEAREST:
        case TINYGLTF_TEXTURE_FILTER_LINEAR:
        case TINYGLTF_TEXTURE_FILTER_LINEAR_MIPMAP_NEAREST:
            return vk::SamplerMipmapMode::eNearest;
        case TINYGLTF_TEXTURE_FILTER_NEAREST_MIPMAP_LINEAR:
        case TINYGLTF_TEXTURE_FILTER_LINEAR_MIPMAP_LINEAR:
            return vk::SamplerMipmapMode::eLinear;
        default:
            Assert(false);
            return vk::SamplerMipmapMode::eLinear;
        }
    }

    static vk::SamplerAddressMode GetSamplerAddressMode(int32_t wrap)
    {
        switch (wrap)
        {
        case TINYGLTF_TEXTURE_WRAP_REPEAT:
            return vk::SamplerAddressMode::eRepeat;
        case TINYGLTF_TEXTURE_WRAP_CLAMP_TO_EDGE:
            return vk::SamplerAddressMode::eClampToEdge;
        case TINYGLTF_TEXTURE_WRAP_MIRRORED_REPEAT:
            return vk::SamplerAddressMode::eMirroredRepeat;
        default:
            Assert(false);
            return vk::SamplerAddressMode::eRepeat;
        }
    }

    static vk::IndexType GetIndexType(int32_t componentType)
    {
        switch (componentType)
        {
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
            return vk::IndexType::eUint16;
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
            return vk::IndexType::eUint32;
        default:
            Assert(false);
            return vk::IndexType::eNoneKHR;
        }
    }

    template <glm::length_t L>
    static glm::vec<L, float, glm::defaultp> GetVec(const std::vector<double>& values)
    {
        const glm::length_t valueCount = static_cast<glm::length_t>(values.size());

        glm::vec<L, float, glm::defaultp> result(0.0f);

        for (glm::length_t i = 0; i < valueCount && i < L; ++i)
        {
            result[i] = static_cast<float>(values[i]);
        }

        return result;
    }

    static glm::quat GetQuaternion(const std::vector<double>& values)
    {
        Assert(values.size() == glm::quat::length());

        return glm::make_quat(values.data());
    }

    static size_t GetAccessorValueSize(const tinygltf::Accessor& accessor)
    {
        const int32_t count = tinygltf::GetNumComponentsInType(accessor.type);
        Assert(count >= 0);

        const int32_t size = tinygltf::GetComponentSizeInBytes(accessor.componentType);
        Assert(size >= 0);

        return static_cast<size_t>(count) * static_cast<size_t>(size);
    }

    template <class T>
    static DataView<T> GetAccessorDataView(const tinygltf::Model& model,
            const tinygltf::Accessor& accessor)
    {
        const tinygltf::BufferView& bufferView = model.bufferViews[accessor.bufferView];
        Assert(bufferView.byteStride == 0 || bufferView.byteStride == GetAccessorValueSize(accessor));

        const size_t offset = bufferView.byteOffset + accessor.byteOffset;
        const T* data = reinterpret_cast<const T*>(model.buffers[bufferView.buffer].data.data() + offset);

        return DataView<T>(data, accessor.count);
    }

    static ByteView GetAccessorByteView(const tinygltf::Model& model,
            const tinygltf::Accessor& accessor)
    {
        const tinygltf::BufferView& bufferView = model.bufferViews[accessor.bufferView];
        Assert(bufferView.byteStride == 0);

        const size_t offset = bufferView.byteOffset + accessor.byteOffset;
        const uint8_t* data = model.buffers[bufferView.buffer].data.data() + offset;

        return DataView<uint8_t>(data, bufferView.byteLength - accessor.byteOffset);
    }
    
    template<class T>
    static void CalculateNormals(const DataView<T>& indices,
            std::vector<Primitive::Vertex>& vertices)
    {
        for (auto& vertex : vertices)
        {
            vertex.normal = glm::vec3();
        }

        for (size_t i = 0; i < indices.size; i = i + 3)
        {
            const glm::vec3& position0 = vertices[indices[i]].position;
            const glm::vec3& position1 = vertices[indices[i + 1]].position;
            const glm::vec3& position2 = vertices[indices[i + 2]].position;

            const glm::vec3 edge1 = position1 - position0;
            const glm::vec3 edge2 = position2 - position0;

            const glm::vec3 normal = glm::normalize(glm::cross(edge1, edge2));

            vertices[indices[i]].normal += normal;
            vertices[indices[i + 1]].normal += normal;
            vertices[indices[i + 2]].normal += normal;
        }

        for (auto& vertex : vertices)
        {
            vertex.normal = glm::normalize(vertex.normal);
        }
    }

    static void CalculateNormals(vk::IndexType indexType, 
            const ByteView& indices, std::vector<Primitive::Vertex>& vertices)
    {
        switch (indexType)
        {
        case vk::IndexType::eUint16:
            CalculateNormals(DataView<uint16_t>(indices), vertices);
            break;
        case vk::IndexType::eUint32:
            CalculateNormals(DataView<uint32_t>(indices), vertices);
            break;
        default:
            Assert(false);
            break;
        }
    }

    template<class T>
    static void CalculateTangents(const DataView<T>& indices,
        std::vector<Primitive::Vertex>& vertices)
    {
        for (auto& vertex : vertices)
        {
            vertex.tangent = glm::vec3();
        }

        for (size_t i = 0; i < indices.size; i = i + 3)
        {
            const glm::vec3& position0 = vertices[indices[i]].position;
            const glm::vec3& position1 = vertices[indices[i + 1]].position;
            const glm::vec3& position2 = vertices[indices[i + 2]].position;

            const glm::vec3 edge1 = position1 - position0;
            const glm::vec3 edge2 = position2 - position0;

            const glm::vec2& texCoord0 = vertices[indices[i]].texCoord;
            const glm::vec2& texCoord1 = vertices[indices[i + 1]].texCoord;
            const glm::vec2& texCoord2 = vertices[indices[i + 2]].texCoord;

            const glm::vec2 deltaTexCoord1 = texCoord1 - texCoord0;
            const glm::vec2 deltaTexCoord2 = texCoord2 - texCoord0;

            float d = deltaTexCoord1.x * deltaTexCoord2.y - deltaTexCoord1.y * deltaTexCoord2.x;

            if (d == 0.0f)
            {
                d = 1.0f;
            }

            const glm::vec3 tangent = (edge1 * deltaTexCoord2.y - edge2 * deltaTexCoord1.y) / d;

            vertices[indices[i]].tangent += tangent;
            vertices[indices[i + 1]].tangent += tangent;
            vertices[indices[i + 2]].tangent += tangent;
        }

        for (auto& vertex : vertices)
        {
            if (glm::length(vertex.tangent) > 0.0f)
            {
                vertex.tangent = glm::normalize(vertex.tangent);
            }
            else
            {
                vertex.tangent.x = 1.0f;
            }
        }
    }

    static void CalculateTangents(vk::IndexType indexType,
        const ByteView& indices, std::vector<Primitive::Vertex>& vertices)
    {
        switch (indexType)
        {
        case vk::IndexType::eUint16:
            CalculateTangents(DataView<uint16_t>(indices), vertices);
            break;
        case vk::IndexType::eUint32:
            CalculateTangents(DataView<uint32_t>(indices), vertices);
            break;
        default:
            Assert(false);
            break;
        }
    }

    static void EnumerateNodes(const tinygltf::Model& model, const NodeFunctor& functor)
    {
        using Enumerator = std::function<void(const tinygltf::Node&, entt::entity)>;

        const Enumerator enumerator = [&](const tinygltf::Node& node, entt::entity parent)
            {
                const entt::entity entity = functor(node, parent);

                for (const auto& childIndex : node.children)
                {
                    const tinygltf::Node& child = model.nodes[childIndex];

                    enumerator(child, entity);
                }
            };

        for (const auto& scene : model.scenes)
        {
            for (const auto& nodeIndex : scene.nodes)
            {
                const tinygltf::Node& node = model.nodes[nodeIndex];

                enumerator(node, entt::null);
            }
        }
    }

    static std::vector<Texture> CreateTextures(const tinygltf::Model& model)
    {
        std::vector<Texture> textures;
        textures.reserve(model.images.size());

        for (const auto& image : model.images)
        {
            const vk::Format format = GetFormat(image);
            const vk::Extent2D extent = VulkanHelpers::GetExtent(image.width, image.height);

            textures.push_back(VulkanContext::textureManager->CreateTexture(format, extent, ByteView(image.image)));
        }

        return textures;
    }

    static std::vector<vk::Sampler> CreateSamplers(const tinygltf::Model& model)
    {
        std::vector<vk::Sampler> samplers;
        samplers.reserve(model.samplers.size());

        for (const auto& sampler : model.samplers)
        {
            Assert(sampler.wrapS == sampler.wrapT);

            const SamplerDescription samplerDescription{
                GetSamplerFilter(sampler.magFilter),
                GetSamplerFilter(sampler.minFilter),
                GetSamplerMipmapMode(sampler.magFilter),
                GetSamplerAddressMode(sampler.wrapS),
                VulkanConfig::kMaxAnisotropy,
                0.0f, std::numeric_limits<float>::max(),
                false
            };

            samplers.push_back(VulkanContext::textureManager->CreateSampler(samplerDescription));
        }

        return samplers;
    }

    static Material RetrieveMaterial(const tinygltf::Material& gltfMaterial)
    {
        Assert(gltfMaterial.pbrMetallicRoughness.baseColorTexture.texCoord == 0);
        Assert(gltfMaterial.pbrMetallicRoughness.metallicRoughnessTexture.texCoord == 0);
        Assert(gltfMaterial.normalTexture.texCoord == 0);
        Assert(gltfMaterial.occlusionTexture.texCoord == 0);
        Assert(gltfMaterial.emissiveTexture.texCoord == 0);

        Material material{};

        material.data.baseColorTexture = gltfMaterial.pbrMetallicRoughness.baseColorTexture.index;
        material.data.roughnessMetallicTexture = gltfMaterial.pbrMetallicRoughness.metallicRoughnessTexture.index;
        material.data.normalTexture = gltfMaterial.normalTexture.index;
        material.data.occlusionTexture = gltfMaterial.occlusionTexture.index;
        material.data.emissionTexture = gltfMaterial.emissiveTexture.index;

        material.data.baseColorFactor = GetVec<4>(gltfMaterial.pbrMetallicRoughness.baseColorFactor);
        material.data.emissionFactor = GetVec<4>(gltfMaterial.emissiveFactor);

        material.data.roughnessFactor = static_cast<float>(gltfMaterial.pbrMetallicRoughness.roughnessFactor);
        material.data.metallicFactor = static_cast<float>(gltfMaterial.pbrMetallicRoughness.metallicFactor);
        material.data.normalScale = static_cast<float>(gltfMaterial.normalTexture.scale);
        material.data.occlusionStrength = static_cast<float>(gltfMaterial.occlusionTexture.strength);
        material.data.alphaCutoff = static_cast<float>(gltfMaterial.alphaCutoff);

        if (gltfMaterial.alphaMode != "OPAQUE")
        {
            material.flags |= MaterialFlagBits::eAlphaTest;
        }
        if (gltfMaterial.doubleSided)
        {
            material.flags |= MaterialFlagBits::eDoubleSided;
        }
        if (gltfMaterial.normalTexture.index >= 0)
        {
            material.flags |= MaterialFlagBits::eNormalMapping;
        }

        return material;
    }

    static std::vector<Primitive::Vertex> RetrieveVertices(
            const tinygltf::Model& model, const tinygltf::Primitive& gltfPrimitive)
    {
        Assert(gltfPrimitive.attributes.contains("POSITION"));
        const tinygltf::Accessor& positionsAccessor = model.accessors[gltfPrimitive.attributes.at("POSITION")];

        Assert(positionsAccessor.type == TINYGLTF_TYPE_VEC3);
        Assert(positionsAccessor.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT);
        const DataView<glm::vec3> positions = GetAccessorDataView<glm::vec3>(model, positionsAccessor);

        DataView<glm::vec3> normals;
        if (gltfPrimitive.attributes.contains("NORMAL"))
        {
            const tinygltf::Accessor& normalsAccessor = model.accessors[gltfPrimitive.attributes.at("NORMAL")];

            Assert(normalsAccessor.type == TINYGLTF_TYPE_VEC3);
            Assert(normalsAccessor.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT);
            normals = GetAccessorDataView<glm::vec3>(model, normalsAccessor);
        }

        DataView<glm::vec3> tangents;
        if (gltfPrimitive.attributes.contains("TANGENT"))
        {
            const tinygltf::Accessor& tangentsAccessor = model.accessors[gltfPrimitive.attributes.at("TANGENT")];

            Assert(tangentsAccessor.type == TINYGLTF_TYPE_VEC3);
            Assert(tangentsAccessor.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT);
            tangents = GetAccessorDataView<glm::vec3>(model, tangentsAccessor);
        }

        DataView<glm::vec2> texCoords;
        if (gltfPrimitive.attributes.contains("TEXCOORD_0"))
        {
            const tinygltf::Accessor& texCoordsAccessor = model.accessors[gltfPrimitive.attributes.at("TEXCOORD_0")];

            Assert(texCoordsAccessor.type == TINYGLTF_TYPE_VEC2);
            Assert(texCoordsAccessor.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT);
            texCoords = GetAccessorDataView<glm::vec2>(model, texCoordsAccessor);
        }

        std::vector<Primitive::Vertex> vertices(positions.size);

        for (size_t i = 0; i < vertices.size(); ++i)
        {
            vertices[i].position = positions[i];

            if (normals.data != nullptr)
            {
                Assert(normals.size == vertices.size());
                vertices[i].normal = normals[i];
            }

            if (tangents.data != nullptr)
            {
                Assert(tangents.size == vertices.size());
                vertices[i].tangent = tangents[i];
            }

            if (texCoords.data != nullptr)
            {
                Assert(texCoords.size == vertices.size());
                vertices[i].texCoord = texCoords[i];
            }
        }

        return vertices;
    }

    static Primitive RetrievePrimitive(
            const tinygltf::Model& model, const tinygltf::Primitive& gltfPrimitive)
    {
        Assert(gltfPrimitive.indices >= 0);

        const tinygltf::Accessor& indicesAccessor = model.accessors[gltfPrimitive.indices];

        const vk::IndexType indexType = GetIndexType(indicesAccessor.componentType);

        const ByteView indices = GetAccessorByteView(model, indicesAccessor);

        std::vector<Primitive::Vertex> vertices = RetrieveVertices(model, gltfPrimitive);

        if (!gltfPrimitive.attributes.contains("NORMAL"))
        {
            CalculateNormals(indexType, indices, vertices);
        }
        if (!gltfPrimitive.attributes.contains("TANGENT"))
        {
            CalculateTangents(indexType, indices, vertices);
        }

        Primitive primitive;

        primitive.indexType = indexType;

        primitive.indexCount = static_cast<uint32_t>(indicesAccessor.count);
        primitive.indexBuffer = BufferHelpers::CreateBufferWithData(
                vk::BufferUsageFlagBits::eIndexBuffer, indices);

        primitive.vertexCount = static_cast<uint32_t>(vertices.size());
        primitive.vertexBuffer = BufferHelpers::CreateBufferWithData(
                vk::BufferUsageFlagBits::eVertexBuffer, ByteView(vertices));

        return primitive;
    }
    
    static glm::mat4 RetrieveTransform(const tinygltf::Node& node)
    {
        if (!node.matrix.empty())
        {
            return glm::make_mat4(node.matrix.data());
        }

        glm::mat4 scaleMatrix(1.0f);
        if (!node.scale.empty())
        {
            const glm::vec3 scale = GetVec<3>(node.scale);
            scaleMatrix = glm::scale(Matrix4::kIdentity, scale);
        }

        glm::mat4 rotationMatrix(1.0f);
        if (!node.rotation.empty())
        {
            const glm::quat rotation = GetQuaternion(node.rotation);
            rotationMatrix = glm::toMat4(rotation);
        }

        glm::mat4 translationMatrix(1.0f);
        if (!node.translation.empty())
        {
            const glm::vec3 translation = GetVec<3>(node.translation);
            translationMatrix = glm::translate(Matrix4::kIdentity, translation);
        }

        return translationMatrix * rotationMatrix * scaleMatrix;
    }
}

class SceneLoader
{
public:
    SceneLoader(const Filepath& path, Scene2& scene_)
        : scene(scene_)
    {
        LoadModel(path);

        LoadTextures();

        LoadPrimitives();
        
        LoadMaterials();

        LoadNodes();

        scene.ctx().emplace<tinygltf::Model>(std::move(model));
    }

private:
    tinygltf::Model model;

    Scene2& scene;

    void LoadModel(const Filepath& path)
    {
        tinygltf::TinyGLTF loader;
        std::string errors;
        std::string warnings;

        const bool result = loader.LoadASCIIFromFile(&model, &errors, &warnings, path.GetAbsolute());

        if (!warnings.empty())
        {
            LogW << "Scene loaded with warnings:\n" << warnings;
        }

        if (!errors.empty())
        {
            LogE << "Failed to load scene:\n" << errors;
        }

        Assert(result);
    }

    void LoadTextures() const
    {
        scene.textures = Details::CreateTextures(model);
        scene.samplers = Details::CreateSamplers(model);

        scene.materialTextures.reserve(model.textures.size());

        for (const tinygltf::Texture& texture : model.textures)
        {
            Assert(texture.source >= 0);

            const vk::ImageView view = scene.textures[texture.source].view;

            vk::Sampler sampler = RenderContext::defaultSampler;
            if (texture.sampler >= 0)
            {
                sampler = scene.samplers[texture.sampler];
            }

            scene.materialTextures.emplace_back(view, sampler);
        }
    }

    void LoadPrimitives() const
    {
        scene.primitives.reserve(model.meshes.size());

        for (const tinygltf::Mesh& mesh : model.meshes)
        {
            for (const tinygltf::Primitive& primitive : mesh.primitives)
            {
                scene.primitives.push_back(Details::RetrievePrimitive(model, primitive));
            }
        }
    }

    void LoadMaterials() const
    {
        scene.materials.reserve(model.materials.size());

        for (const tinygltf::Material& material : model.materials)
        {
            scene.materials.push_back(Details::RetrieveMaterial(material));
        }
    }

    void LoadNodes() const
    {
        Details::EnumerateNodes(model, [&](const tinygltf::Node& node, entt::entity parentEntity)
            {
                const entt::entity entity = scene.create();

                AddTransformComponent(entity, parentEntity, node);

                if (node.mesh >= 0)
                {
                    AddRenderComponent(entity, node.mesh);
                }

                if (node.camera >= 0)
                {
                    scene.emplace<CameraComponent>(entity);
                }

                if (node.extras.Has("environment"))
                {
                    scene.emplace<EnvironmentComponent>(entity);
                }

                return entity;
            });
    }

    void AddTransformComponent(entt::entity entity, entt::entity parent, const tinygltf::Node& node) const
    {
        TransformComponent& tc = scene.emplace<TransformComponent>(entity);

        if (parent != entt::null)
        {
            tc.parent = &scene.get<TransformComponent>(entity);
        }

        tc.localTransform = Details::RetrieveTransform(node);
        tc.worldTransform = TransformComponent::AccumulateTransform(tc);
    }

    void AddRenderComponent(entt::entity entity, uint32_t meshIndex) const
    {
        RenderComponent& rc = scene.emplace<RenderComponent>(entity);

        const tinygltf::Mesh& mesh = model.meshes[meshIndex];

        size_t meshOffset = 0;
        for (size_t i = 0; i < meshIndex; ++i)
        {
            meshOffset += model.meshes[i].primitives.size();
        }

        rc.renderObjects.resize(mesh.primitives.size());

        for (size_t i = 0; i < mesh.primitives.size(); ++i)
        {
            const tinygltf::Primitive& primitive = mesh.primitives[i];

            Assert(primitive.material >= 0);
            
            rc.renderObjects[i].primitive = static_cast<uint32_t>(meshOffset + i);
            rc.renderObjects[i].material = static_cast<uint32_t>(primitive.material);
        }
    }
};

Scene2::Scene2() = default;
Scene2::~Scene2() = default;

void Scene2::Load(const Filepath& path)
{
    SceneLoader loader(path, *this);
}
