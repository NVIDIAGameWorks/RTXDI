/***************************************************************************
 # Copyright (c) 2020-2021, NVIDIA CORPORATION.  All rights reserved.
 #
 # NVIDIA CORPORATION and its licensors retain all intellectual property
 # and proprietary rights in and to this software, related documentation
 # and any modifications thereto.  Any use, reproduction, disclosure or
 # distribution of this software and related documentation without an express
 # license agreement from NVIDIA CORPORATION is strictly prohibited.
 **************************************************************************/

#ifndef HELPER_FUNCTIONS_HLSLI
#define HELPER_FUNCTIONS_HLSLI

#include <donut/shaders/utils.hlsli>
#include <rtxdi/RtxdiMath.hlsli>

static const float c_pi = 3.1415926535;

struct RandomSamplerState
{
    uint seed;
    uint index;
};

RandomSamplerState initRandomSampler(uint2 pixelPos, uint frameIndex)
{
    RandomSamplerState state;

    uint linearPixelIndex = RTXDI_ZCurveToLinearIndex(pixelPos);

    state.index = 1;
    state.seed = RTXDI_JenkinsHash(linearPixelIndex) + frameIndex;

    return state;
}

uint murmur3(inout RandomSamplerState r)
{
#define ROT32(x, y) ((x << y) | (x >> (32 - y)))

    // https://en.wikipedia.org/wiki/MurmurHash
    uint c1 = 0xcc9e2d51;
    uint c2 = 0x1b873593;
    uint r1 = 15;
    uint r2 = 13;
    uint m = 5;
    uint n = 0xe6546b64;

    uint hash = r.seed;
    uint k = r.index++;
    k *= c1;
    k = ROT32(k, r1);
    k *= c2;

    hash ^= k;
    hash = ROT32(hash, r2) * m + n;

    hash ^= 4;
    hash ^= (hash >> 16);
    hash *= 0x85ebca6b;
    hash ^= (hash >> 13);
    hash *= 0xc2b2ae35;
    hash ^= (hash >> 16);

#undef ROT32

    return hash;
}

float sampleUniformRng(inout RandomSamplerState r)
{
    uint v = murmur3(r);
    const uint one = asuint(1.f);
    const uint mask = (1 << 23) - 1;
    return asfloat((mask & v) | one) - 1.f;
}

float3 sampleTriangle(float2 rndSample)
{
    float sqrtx = sqrt(rndSample.x);

    return float3(
        1 - sqrtx,
        sqrtx * (1 - rndSample.y),
        sqrtx * rndSample.y);
}

float2 sampleDisk(float2 rand)
{
    float angle = 2 * c_pi * rand.x;
    return float2(cos(angle), sin(angle)) * sqrt(rand.y);
}

float3 sampleCosHemisphere(float2 rand, out float solidAnglePdf)
{
    float2 tangential = sampleDisk(rand);
    float elevation = sqrt(saturate(1.0 - rand.y));

    solidAnglePdf = elevation / c_pi;

    return float3(tangential.xy, elevation);
}

float3 sampleSphere(float2 rand, out float solidAnglePdf)
{
    // See (6-8) in https://mathworld.wolfram.com/SpherePointPicking.html

    rand.y = rand.y * 2.0 - 1.0;

    float2 tangential = sampleDisk(float2(rand.x, 1.0 - square(rand.y)));
    float elevation = rand.y;

    solidAnglePdf = 0.25f / c_pi;

    return float3(tangential.xy, elevation);
}

// For converting an area measure pdf to solid angle measure pdf
float pdfAtoW(float pdfA, float distance_, float cosTheta)
{
    return pdfA * square(distance_) / cosTheta;
}

// Pack [0.0, 1.0] float to a uint of a given bit depth
#define PACK_UFLOAT_TEMPLATE(size)                      \
uint Pack_R ## size ## _UFLOAT(float r, float d = 0.5f) \
{                                                       \
    const uint mask = (1U << size) - 1U;                \
                                                        \
    return (uint)floor(r * mask + d) & mask;            \
}                                                       \
                                                        \
float Unpack_R ## size ## _UFLOAT(uint r)               \
{                                                       \
    const uint mask = (1U << size) - 1U;                \
                                                        \
    return (float)(r & mask) / (float)mask;             \
}

PACK_UFLOAT_TEMPLATE(8)
PACK_UFLOAT_TEMPLATE(16)

uint Pack_R8G8B8_UFLOAT(float3 rgb, float3 d = float3(0.5f, 0.5f, 0.5f))
{
    uint r = Pack_R8_UFLOAT(rgb.r, d.r);
    uint g = Pack_R8_UFLOAT(rgb.g, d.g) << 8;
    uint b = Pack_R8_UFLOAT(rgb.b, d.b) << 16;
    return r | g | b;
}

float3 Unpack_R8G8B8_UFLOAT(uint rgb)
{
    float r = Unpack_R8_UFLOAT(rgb);
    float g = Unpack_R8_UFLOAT(rgb >> 8);
    float b = Unpack_R8_UFLOAT(rgb >> 16);
    return float3(r, g, b);
}

uint Pack_R8G8B8A8_UFLOAT(float4 rgba, float4 d = float4(0.5f, 0.5f, 0.5f, 0.5f))
{
    uint r = Pack_R8_UFLOAT(rgba.r, d.r);
    uint g = Pack_R8_UFLOAT(rgba.g, d.g) << 8;
    uint b = Pack_R8_UFLOAT(rgba.b, d.b) << 16;
    uint a = Pack_R8_UFLOAT(rgba.a, d.a) << 24;
    return r | g | b | a;
}

float4 Unpack_R8G8B8A8_UFLOAT(uint rgba)
{
    float r = Unpack_R8_UFLOAT(rgba);
    float g = Unpack_R8_UFLOAT(rgba >> 8);
    float b = Unpack_R8_UFLOAT(rgba >> 16);
    float a = Unpack_R8_UFLOAT(rgba >> 24);
    return float4(r, g, b, a);
}

uint Pack_R16G16_UFLOAT(float2 rg, float2 d = float2(0.5f, 0.5f))
{
    uint r = Pack_R16_UFLOAT(rg.r, d.r);
    uint g = Pack_R16_UFLOAT(rg.g, d.g) << 16;
    return r | g;
}

float2 Unpack_R16G16_UFLOAT(uint rg)
{
    float r = Unpack_R16_UFLOAT(rg);
    float g = Unpack_R16_UFLOAT(rg >> 16);
    return float2(r, g);
}

// Todo: FLOAT is not consistent with the rest of the naming here, they should be changed
// to UNORM as they do not actually decode into full floats but are rather normalized unsigned
// floats, whereas this should be a SFLOAT.
uint Pack_R16G16_FLOAT(float2 rg)
{
    uint r = f32tof16(rg.r);
    uint g = f32tof16(rg.g) << 16;
    return r | g;
}

float2 Unpack_R16G16_FLOAT(uint rg)
{
    uint2 d = uint2(rg, rg >> 16);
    return f16tof32(d);
}

float Unpack_R8_SNORM(uint value)
{
    int signedValue = int(value << 24) >> 24;
    return clamp(float(signedValue) / 127.0, -1.0, 1.0);
}

float3 Unpack_RGB8_SNORM(uint value)
{
    return float3(
        Unpack_R8_SNORM(value),
        Unpack_R8_SNORM(value >> 8),
        Unpack_R8_SNORM(value >> 16)
    );
}

float calcLuminance(float3 color)
{
    return dot(color.xyz, float3(0.299f, 0.587f, 0.114f));
}

/*https://graphics.pixar.com/library/OrthonormalB/paper.pdf*/
void branchlessONB(in float3 n, out float3 b1, out float3 b2)
{
    float sign = n.z >= 0.0f ? 1.0f : -1.0f;
    float a = -1.0f / (sign + n.z);
    float b = n.x * n.y * a;
    b1 = float3(1.0f + sign * n.x * n.x * a, sign * b, -sign * n.x);
    b2 = float3(b, sign + n.y * n.y * a, -n.y);
}

float3 sphericalDirection(float sinTheta, float cosTheta, float sinPhi, float cosPhi, float3 x, float3 y, float3 z)
{
    return sinTheta * cosPhi * x + sinTheta * sinPhi * y + cosTheta * z;
}

void getReflectivity(float metalness, float3 baseColor, out float3 o_albedo, out float3 o_baseReflectivity)
{
    o_albedo = baseColor * (1.0 - metalness);
    o_baseReflectivity = lerp(0.04, baseColor, metalness);
}


float3 sampleGGX_VNDF(float3 Ve, float roughness, float2 random)
{
    float alpha = square(roughness);

    float3 Vh = normalize(float3(alpha * Ve.x, alpha * Ve.y, Ve.z));

    float lensq = square(Vh.x) + square(Vh.y);
    float3 T1 = lensq > 0.0 ? float3(-Vh.y, Vh.x, 0.0) / sqrt(lensq) : float3(1.0, 0.0, 0.0);
    float3 T2 = cross(Vh, T1);

    float r = sqrt(random.x);
    float phi = 2.0 * c_pi * random.y;
    float t1 = r * cos(phi);
    float t2 = r * sin(phi);
    float s = 0.5 * (1.0 + Vh.z);
    t2 = (1.0 - s) * sqrt(1.0 - square(t1)) + s * t2;

    float3 Nh = t1 * T1 + t2 * T2 + sqrt(max(0.0, 1.0 - square(t1) - square(t2))) * Vh;

    // Tangent space H
    float3 Ne = float3(alpha * Nh.x, alpha * Nh.y, max(0.0, Nh.z));
    return Ne;
}

float2 interpolate(float2 vertices[3], float3 bary)
{
    return vertices[0] * bary[0] + vertices[1] * bary[1] + vertices[2] * bary[2];
}

float3 interpolate(float3 vertices[3], float3 bary)
{
    return vertices[0] * bary[0] + vertices[1] * bary[1] + vertices[2] * bary[2];
}

float2 directionToEquirectUV(float3 normalizedDirection)
{
    float elevation = asin(normalizedDirection.y);
    float azimuth = 0;
    if (abs(normalizedDirection.y) < 1.0)
        azimuth = atan2(normalizedDirection.z, normalizedDirection.x);

    float2 uv;
    uv.x = azimuth / (2 * c_pi) - 0.25;
    uv.y = 0.5 - elevation / c_pi;

    return uv;
}

float3 equirectUVToDirection(float2 uv, out float cosElevation)
{
    float azimuth = (uv.x + 0.25) * (2 * c_pi);
    float elevation = (0.5 - uv.y) * c_pi;
    cosElevation = cos(elevation);

    return float3(
        cos(azimuth) * cosElevation,
        sin(elevation),
        sin(azimuth) * cosElevation
    );
}

#endif // HELPER_FUNCTIONS_HLSLI