/*
 * SPDX-FileCopyrightText: Copyright (c) 2020-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: LicenseRef-NvidiaProprietary
 *
 * NVIDIA CORPORATION, its affiliates and licensors retain all intellectual
 * property and proprietary rights in and to this material, related
 * documentation and any modifications thereto. Any use, reproduction,
 * disclosure or distribution of this material and related documentation
 * without an express license agreement from NVIDIA CORPORATION or
 * its affiliates is strictly prohibited.
 */

#ifndef SHADER_PARAMETERS_H
#define SHADER_PARAMETERS_H

#include <donut/shaders/view_cb.h>
#include <donut/shaders/sky_cb.h>

#include <Rtxdi/DI/ReSTIRDIParameters.h>
#include <Rtxdi/GI/ReSTIRGIParameters.h>
#include <Rtxdi/PT/ReSTIRPTParameters.h>
#include <Rtxdi/ReGIR/ReGIRParameters.h>

#include "ShaderDebug/ReSTIRShaderDebugParameters.h"

#include "BRDFPTParameters.h"
#include "DirectLightingMode.h"
#include "PTParameters.h"

#define TASK_PRIMITIVE_LIGHT_BIT 0x80000000u

#define RTXDI_PRESAMPLING_GROUP_SIZE 256
#define RTXDI_GRID_BUILD_GROUP_SIZE 256
#define RTXDI_SCREEN_SPACE_GROUP_SIZE 8
#define RTXDI_GRAD_FACTOR 3
#define RTXDI_GRAD_STORAGE_SCALE 256.0f
#define RTXDI_GRAD_MAX_VALUE 65504.0f

#define INSTANCE_MASK_OPAQUE 0x01
#define INSTANCE_MASK_ALPHA_TESTED 0x02
#define INSTANCE_MASK_TRANSPARENT 0x04
#define INSTANCE_MASK_ALL 0xFF

#define DENOISER_MODE_OFF 0
#define DENOISER_MODE_RELAX 1
#define DENOISER_MODE_REBLUR 2

#define BACKGROUND_DEPTH 65504.f

#define SPATIAL_HEURISTIC_MAX_NEIGHBORS 4
#define SPATIAL_HEURISTIC_INVALID_NEIGHBOR 0xFFFFFFFFu

#define RAY_COUNT_TRACED(index) ((index) * 2)
#define RAY_COUNT_HITS(index) ((index) * 2 + 1)

#define REPORT_RAY(hit) if (g_PerPassConstants.rayCountBufferIndex >= 0) { \
    InterlockedAdd(u_RayCountBuffer[RAY_COUNT_TRACED(g_PerPassConstants.rayCountBufferIndex)], 1); \
    if (hit) InterlockedAdd(u_RayCountBuffer[RAY_COUNT_HITS(g_PerPassConstants.rayCountBufferIndex)], 1); }


#define INDIRECT_LIGHTING_MODE_NONE 0
#define INDIRECT_LIGHTING_MODE_BRDF 1
#define INDIRECT_LIGHTING_MODE_RESTIRGI 2
#define INDIRECT_LIGHTING_MODE_RESTIRPT 3

#ifdef __cplusplus
enum class IndirectLightingMode : uint32_t
{
    None = INDIRECT_LIGHTING_MODE_NONE,
    Brdf = INDIRECT_LIGHTING_MODE_BRDF,
    ReStirGI = INDIRECT_LIGHTING_MODE_RESTIRGI,
    ReStirPT = INDIRECT_LIGHTING_MODE_RESTIRPT
};
#else
#define IndirectLightingMode uint32_t
#endif

struct BrdfRayTracingConstants
{
    PlanarViewConstants view;

    uint frameIndex;
};

struct PrepareLightsConstants
{
    uint numTasks;
    uint currentFrameLightOffset;
    uint prevFrameLightOffset;
};

struct PrepareLightsTask
{
    uint instanceAndGeometryIndex; // low 12 bits are geometryIndex, mid 19 bits are instanceIndex, high bit is TASK_PRIMITIVE_LIGHT_BIT
    uint triangleCount;
    uint lightBufferOffset;
    int prevLightBufferOffset; // -1 means no previous data
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

    float textureLodBias;
    float textureGradientScale; // 2^textureLodBias
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
    IndirectLightingMode indirectLightingMode;
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
    float noiseClampLow;

    float noiseClampHigh;
    uint checkerboard;
};

struct AccumulationConstants
{
    float2 outputSize;
    float2 inputSize;
    float2 inputTextureSizeInv;
    float2 pixelOffset;
    float blendFactor;
};

struct FilterGradientsConstants
{
    uint2 viewportSize;
    int passIndex;
    uint checkerboard;
};

struct DLSSRRInputFormattingConstants
{
    uint2 viewportSize;
    uint pad1;
    uint pad2;
};

struct ConfidenceConstants
{
    uint2 viewportSize;
    float2 invGradientTextureSize;

    float darknessBias;
    float sensitivity;
    uint checkerboard;
    int inputBufferIndex;

    float blendFactor;
};

struct VisualizationConstants
{
    RTXDI_RuntimeParameters runtimeParams;
    RTXDI_ReservoirBufferParameters restirDIReservoirBufferParams;
    RTXDI_ReservoirBufferParameters restirGIReservoirBufferParams;

    int2 outputSize;
    float2 resolutionScale;

    uint visualizationMode;
    uint inputBufferIndex;
    uint enableAccumulation;
};

struct SceneConstants
{
    uint enableEnvironmentMap; // Global. Affects BRDFRayTracing's GI code, plus RTXDI, ReGIR, etc.
    uint environmentMapTextureIndex; // Global
    float environmentScale;
    float environmentRotation;

    uint enableAlphaTestedGeometry;
    uint enableTransparentGeometry;
    uint2 pad1;
};

struct ResamplingConstants
{
    PlanarViewConstants view;
    PlanarViewConstants prevView;
    PlanarViewConstants prevPrevView;
    RTXDI_RuntimeParameters runtimeParams;
    
    float4 reblurHitDistParams;

    uint pad3;
    uint enablePrevTLAS;
    uint denoiserMode;
    uint discountNaiveSamples;
    
    uint enableBrdfIndirect;
    uint enableBrdfAdditiveBlend;    
    uint enableAccumulation; // StoreShadingOutput
    DirectLightingMode directLightingMode;

    SceneConstants sceneConstants;

    // Common buffer params
    RTXDI_LightBufferParameters lightBufferParams;
    RTXDI_RISBufferSegmentParameters localLightsRISBufferSegmentParams;
    RTXDI_RISBufferSegmentParameters environmentLightRISBufferSegmentParams;

    // Algo-specific params
    RTXDI_Parameters restirDI;
    ReGIR_Parameters regir;
    RTXDI_GIParameters restirGI;
    RTXDI_PTParameters restirPT;
    PTParameters pt;
    BRDFPathTracing_Parameters brdfPT;

    uint visualizeRegirCells;
    uint enableDenoiserPSR;
    uint usePSRMvecForResampling;
    uint updatePSRwithResampling;
    
    uint2 environmentPdfTextureSize;
    uint2 localLightPdfTextureSize;

    ReSTIRShaderDebugParameters debug;
};

struct PathTracerDebugVisualizationConstants
{
    PlanarViewConstants view;
};

struct PerPassConstants
{
    int rayCountBufferIndex;
};

struct SecondaryGBufferData
{
    float3 worldPos;
    uint normal;

    uint2 throughputAndFlags;   // .x = throughput.rg as float16, .y = throughput.b as float16, flags << 16
    uint diffuseAlbedo;         // R11G11B10_UFLOAT
    uint specularAndRoughness;  // R8G8B8A8_Gamma_UFLOAT
    
    float3 emission;
    float pdf;
};

static const uint kSecondaryGBuffer_IsSpecularRay = 1;
static const uint kSecondaryGBuffer_IsDeltaSurface = 2;
static const uint kSecondaryGBuffer_IsEnvironmentMap = 4;

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
    kEnvironment,
    kPoint
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
