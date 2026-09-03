#ifndef RAB_MATERIAL_HLSLI
#define RAB_MATERIAL_HLSLI

static const float kMinRoughness = 0.05f;

struct RAB_Material
{
    float unused;
};

RAB_Material RAB_EmptyMaterial()
{
    RAB_Material material;

    return material;
}

float3 GetDiffuseAlbedo(RAB_Material material)
{
    return float3(0.0, 0.0, 0.0);
}

float3 GetSpecularF0(RAB_Material material)
{
    return float3(0.0, 0.0, 0.0);
}

float GetRoughness(RAB_Material material)
{
    return 0.0;
}

RAB_Material RAB_GetGBufferMaterial(int2 pixelPosition, bool prevFrame)
{
    return RAB_EmptyMaterial();
}

bool RAB_AreMaterialsSimilar(RAB_Material a, RAB_Material b)
{
    return true;
}

#endif // RAB_MATERIAL_HLSLI
