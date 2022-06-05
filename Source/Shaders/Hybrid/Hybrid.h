#ifndef HYBRID_H
#define HYBRID_H

#ifdef __cplusplus
#define mat4 glm::mat4
#define vec4 glm::vec4
#define vec3 glm::vec3
#define vec2 glm::vec2
#define mat3x4 glm::mat3x4
#endif

#define VERTEX_COUNT 4
#define COEFFICIENT_COUNT 9

struct MaterialData
{
    vec4 baseColorFactor;
    vec4 emissionFactor;
    int baseColorTexture;
    int roughnessMetallicTexture;
    int normalTexture;
    int occlusionTexture;
    int emissionTexture;
    float roughnessFactor;
    float metallicFactor;
    float normalScale;
    float occlusionStrength;
    float alphaCutoff;
    vec2 padding;
};

struct Tetrahedron
{
    int vertices[VERTEX_COUNT];
    int neighbors[VERTEX_COUNT];
    mat3x4 matrix;
};

#ifdef __cplusplus
#undef mat4
#undef vec4
#undef vec3
#undef vec2
#undef mat3x4
static_assert(sizeof(MaterialData) % sizeof(glm::vec4) == 0);
#endif

#endif
