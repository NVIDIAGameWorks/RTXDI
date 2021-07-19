/***************************************************************************
 # Copyright (c) 2020-2021, NVIDIA CORPORATION.  All rights reserved.
 #
 # NVIDIA CORPORATION and its licensors retain all intellectual property
 # and proprietary rights in and to this software, related documentation
 # and any modifications thereto.  Any use, reproduction, disclosure or
 # distribution of this software and related documentation without an express
 # license agreement from NVIDIA CORPORATION is strictly prohibited.
 **************************************************************************/

#ifndef SHADER_PARAMETERS_H
#define SHADER_PARAMETERS_H

#include <donut/shaders/view_cb.h>
#include <donut/shaders/sky_cb.h>
#include <rtxdi/RtxdiParameters.h>

#define TASK_PRIMITIVE_LIGHT_BIT 0x80000000u

#define RTXDI_PRESAMPLING_GROUP_SIZE 256
#define RTXDI_GRID_BUILD_GROUP_SIZE 256
#define RTXDI_SCREEN_SPACE_GROUP_SIZE 16

#define INSTANCE_MASK_OPAQUE 0x01
#define INSTANCE_MASK_ALPHA_TESTED 0x02
#define INSTANCE_MASK_TRANSPARENT 0x04
#define INSTANCE_MASK_ALL 0xFF

#define DENOISER_MODE_OFF 0
#define DENOISER_MODE_REBLUR 1
#define DENOISER_MODE_RELAX 2

#define RAY_COUNT_TRACED(index) ((index) * 2)
#define RAY_COUNT_HITS(index) ((index) * 2 + 1)

#define REPORT_RAY(hit) if (g_PerPassConstants.rayCountBufferIndex >= 0) { \
    InterlockedAdd(u_RayCountBuffer[RAY_COUNT_TRACED(g_PerPassConstants.rayCountBufferIndex)], 1); \
    if (hit) InterlockedAdd(u_RayCountBuffer[RAY_COUNT_HITS(g_PerPassConstants.rayCountBufferIndex)], 1); }

struct BrdfRayTracingConstants
{
    PlanarViewConstants view;

    uint frameIndex;
};

struct PrepareLightsConstants
{
    uint numTasks;
    uint currentFrameLightOffset;
    uint previousFrameLightOffset;
};

struct PrepareLightsTask
{
    uint instanceAndGeometryIndex; // low 12 bits are geometryIndex, mid 19 bits are instanceIndex, high bit is TASK_PRIMITIVE_LIGHT_BIT
    uint triangleCount;
    uint lightBufferOffset;
    int previousLightBufferOffset; // -1 means no previous data
};

struct RenderEnvironmentMapConstants
{
    ProceduralSkyShaderParameters params;

    float2 invTextureSize;
};

struct PreprocessEnvironmentMapConstants
{
    uint2 sourceSize;
    uint sourceMipLevel;
    uint numDestMipLevels;
};

struct GBufferConstants
{
    PlanarViewConstants view;
    PlanarViewConstants viewPrev;

    float roughnessOverride;
    float metalnessOverride;
    float normalMapScale;
    uint enableAlphaTestedGeometry;

    int2 materialReadbackPosition;
    uint materialReadbackBufferIndex;
    uint enableTransparentGeometry;
};

struct GlassConstants
{
    PlanarViewConstants view;
    
    uint enableEnvironmentMap;
    uint environmentMapTextureIndex;
    float environmentScale;
    float environmentRotation;

    int2 materialReadbackPosition;
    uint materialReadbackBufferIndex;
    float normalMapScale;
};

struct CompositingConstants
{
    PlanarViewConstants view;
    PlanarViewConstants viewPrev;

    uint enableTextures;
    uint denoiserMode;
    uint enableEnvironmentMap;
    uint environmentMapTextureIndex;

    float environmentScale;
    float environmentRotation;
};

struct AccumulationConstants
{
    float blendFactor;
};

struct ResamplingConstants
{
    PlanarViewConstants view;
    PlanarViewConstants prevView;
    RTXDI_ResamplingRuntimeParameters runtimeParams;
    
    float4 reblurDiffHitDistParams;
    float4 reblurSpecHitDistParams;

    uint frameIndex;
    uint enablePreviousTLAS;
    uint denoiserMode;
    uint padding;

    uint enableBrdfMIS;
    uint enableBrdfIndirect;
    uint enableBrdfAdditiveBlend;
    uint enableAlphaTestedGeometry;
    
    uint enableTransparentGeometry;
    uint enableDenoiserInputPacking;
    uint visualizeRegirCells;
    uint numPrimaryRegirSamples;

    uint numPrimaryLocalLightSamples;
    uint numPrimaryInfiniteLightSamples;
    uint numIndirectRegirSamples;
    uint numIndirectLocalLightSamples;

    uint numIndirectInfiniteLightSamples;
    uint enableInitialVisibility;
    uint enableFinalVisibility;
    uint initialOutputBufferIndex;

    uint temporalInputBufferIndex;
    uint temporalOutputBufferIndex;
    uint spatialInputBufferIndex;
    uint spatialOutputBufferIndex;

    uint shadeInputBufferIndex;
    uint discardInvisibleSamples;
    uint maxHistoryLength;
    float boilingFilterStrength;

    float temporalDepthThreshold;
    float temporalNormalThreshold;
    float spatialDepthThreshold;
    float spatialNormalThreshold;

    uint temporalBiasCorrection;
    uint spatialBiasCorrection;
    uint numSpatialSamples;
    uint numDisocclusionBoostSamples;

    float spatialSamplingRadius;
    uint reuseFinalVisibility;
    uint finalVisibilityMaxAge;
    float finalVisibilityMaxDistance;

    uint2 environmentPdfTextureSize;
    uint numPrimaryEnvironmentSamples;
    uint numIndirectEnvironmentSamples;

    uint2 localLightPdfTextureSize;
    uint numRegirBuildSamples;
    uint environmentMapImportanceSampling;

    uint enableEnvironmentMap;
    uint environmentMapTextureIndex;
    float environmentScale;
    float environmentRotation;
};

struct PerPassConstants
{
    int rayCountBufferIndex;
};

static const uint kPolymorphicLightTypeShift = 24;
static const uint kPolymorphicLightTypeMask = 0xf;
static const uint kPolymorphicLightShapingEnableBit = 1 << 28;
static const uint kPolymorphicLightIesProfileEnableBit = 1 << 29;
static const float kPolymorphicLightMinLog2Radiance = -8.f;
static const float kPolymorphicLightMaxLog2Radiance = 40.f;

#ifdef __cplusplus
enum class PolymorphicLightType
#else
enum PolymorphicLightType
#endif
{
    kSphere = 0,
    kCylinder,
    kDisk,
    kRect,
    kTriangle,
    kDirectional,
    kEnvironment
};

// Stores shared light information (type) and specific light information
// See PolymorphicLight.hlsli for encoding format
struct PolymorphicLightInfo
{
    // uint4[0]
    float3 center;
    uint colorTypeAndFlags; // RGB8 + uint8 (see the kPolymorphicLight... constants above)

    // uint4[1]
    uint direction1; // oct-encoded
    uint direction2; // oct-encoded
    uint scalars; // 2x float16
    uint logRadiance; // uint16

    // uint4[2] -- optional, contains only shaping data
    uint iesProfileIndex;
    uint primaryAxis; // oct-encoded
    uint cosConeAngleAndSoftness; // 2x float16
    uint padding;
};

#endif // SHADER_PARAMETERS_H