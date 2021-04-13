/***************************************************************************
 # Copyright (c) 2020-2021, NVIDIA CORPORATION.  All rights reserved.
 #
 # NVIDIA CORPORATION and its licensors retain all intellectual property
 # and proprietary rights in and to this software, related documentation
 # and any modifications thereto.  Any use, reproduction, disclosure or
 # distribution of this software and related documentation without an express
 # license agreement from NVIDIA CORPORATION is strictly prohibited.
 **************************************************************************/

#include <rtxdi/RTXDI.h>
#include <cassert>
#include <algorithm>
#include <vector>
#include <numeric>
#include <math.h>

#define PRINT_JITTER_CURVE 0

#if PRINT_JITTER_CURVE
#define NOMINMAX
#include <Windows.h>
#endif

using namespace rtxdi;

constexpr float c_pi = 3.1415926535f;

static bool IsNonzeroPowerOf2(uint32_t i)
{
    return ((i & (i - 1)) == 0) && (i > 0);
}

rtxdi::Context::Context(const ContextParameters& params)
    : m_Params(params)
{
    assert(IsNonzeroPowerOf2(params.TileSize));
    assert(IsNonzeroPowerOf2(params.TileCount));
    assert(params.RenderWidth > 0);
    assert(params.RenderHeight > 0);

    uint32_t renderWidth = (params.CheckerboardSamplingMode == CheckerboardMode::Off)
        ? params.RenderWidth
        : (params.RenderWidth + 1) / 2;
    uint32_t renderWidthBlocks = (renderWidth + RTXDI_RESERVOIR_BLOCK_SIZE - 1) / RTXDI_RESERVOIR_BLOCK_SIZE;
    uint32_t renderHeightBlocks = (params.RenderHeight + RTXDI_RESERVOIR_BLOCK_SIZE - 1) / RTXDI_RESERVOIR_BLOCK_SIZE;
    m_ReservoirBlockRowPitch = renderWidthBlocks * (RTXDI_RESERVOIR_BLOCK_SIZE * RTXDI_RESERVOIR_BLOCK_SIZE);
    m_ReservoirArrayPitch = m_ReservoirBlockRowPitch * renderHeightBlocks;

    m_RegirCellOffset = m_Params.TileCount * m_Params.TileSize;

    InitializeOnion();
    ComputeOnionJitterCurve();
}

void Context::InitializeOnion()
{
    int numLayerGroups = std::max(1, std::min(RTXDI_ONION_MAX_LAYER_GROUPS, int(m_Params.ReGIR.OnionDetailLayers)));

    float innerRadius = 1.f;

    int totalLayers = 0;
    int totalCells = 1;

    for (int layerGroupIndex = 0; layerGroupIndex < numLayerGroups; layerGroupIndex++)
    {
        const int partitions = layerGroupIndex * 4 + 8;
        const int layerCount = (layerGroupIndex < numLayerGroups - 1) ? 1 : int(m_Params.ReGIR.OnionCoverageLayers) + 1;
        
        const float radiusRatio = (float(partitions) + c_pi) / (float(partitions) - c_pi);
        const float outerRadius = innerRadius * powf(radiusRatio, float(layerCount));
        const float equatorialAngle = 2 * c_pi / float(partitions);

        RTXDI_OnionLayerGroup layerGroup{};
        layerGroup.ringOffset = int(m_OnionRings.size());
        layerGroup.innerRadius = innerRadius;
        layerGroup.outerRadius = outerRadius;
        layerGroup.invLogLayerScale = 1.f / logf(radiusRatio);
        layerGroup.invEquatorialCellAngle = 1.f / equatorialAngle;
        layerGroup.equatorialCellAngle = equatorialAngle;
        layerGroup.ringCount = partitions / 4 + 1;
        layerGroup.layerScale = radiusRatio;
        layerGroup.layerCellOffset = totalCells;

        RTXDI_OnionRing ring{};
        ring.cellCount = partitions;
        ring.cellOffset = 0;
        ring.invCellAngle = float(partitions) / (2 * c_pi);
        ring.cellAngle = 1.f / ring.invCellAngle;
        m_OnionRings.push_back(ring);

        int cellsPerLayer = partitions;
        for (int ringIndex = 1; ringIndex < layerGroup.ringCount; ringIndex++)
        {
            ring.cellCount = std::max(1, int(floorf(float(partitions) * cosf(float(ringIndex) * equatorialAngle))));
            ring.cellOffset = cellsPerLayer;
            ring.invCellAngle = float(ring.cellCount) / (2 * c_pi);
            ring.cellAngle = 1.f / ring.invCellAngle;
            m_OnionRings.push_back(ring);

            cellsPerLayer += ring.cellCount * 2;
        }

        layerGroup.cellsPerLayer = cellsPerLayer;
        layerGroup.layerCount = layerCount;
        m_OnionLayers.push_back(layerGroup);

        innerRadius = outerRadius;
        
        totalCells += cellsPerLayer * layerCount;
        totalLayers += layerCount;
    }

    m_OnionCells = totalCells;
}

static float3 SphericalToCartesian(const float radius, const float azimuth, const float elevation)
{
    return float3{
        radius * cosf(azimuth) * cosf(elevation),
        radius * sinf(elevation),
        radius * sinf(azimuth) * cosf(elevation)
    };
}

static float Distance(const float3& a, const float3& b)
{
    float3 d{ a.x - b.x, a.y - b.y, a.z - b.z };
    return sqrtf(d.x * d.x + d.y * d.y + d.z * d.z);
}

void Context::ComputeOnionJitterCurve()
{
    std::vector<float> cubicRootFactors;
    std::vector<float> linearFactors;

    int layerGroupIndex = 0;
    for (const auto& layerGroup : m_OnionLayers)
    {
        for (int layerIndex = 0; layerIndex < layerGroup.layerCount; layerIndex++)
        {
            const float innerRadius = layerGroup.innerRadius * powf(layerGroup.layerScale, float(layerIndex));
            const float outerRadius = innerRadius * layerGroup.layerScale;
            const float middleRadius = (innerRadius + outerRadius) * 0.5f;
            float maxCellRadius = 0.f;

            for (int ringIndex = 0; ringIndex < layerGroup.ringCount; ringIndex++)
            {
                const auto& ring = m_OnionRings[layerGroup.ringOffset + ringIndex];

                const float middleElevation = layerGroup.equatorialCellAngle * float(ringIndex);
                const float vertexElevation = (ringIndex == 0)
                    ? layerGroup.equatorialCellAngle * 0.5f
                    : middleElevation - layerGroup.equatorialCellAngle * 0.5f;

                const float middleAzimuth = 0.f;
                const float vertexAzimuth = ring.cellAngle;

                const float3 middlePoint = SphericalToCartesian(middleRadius, middleAzimuth, middleElevation);
                const float3 vertexPoint = SphericalToCartesian(outerRadius, vertexAzimuth, vertexElevation);

                const float cellRadius = Distance(middlePoint, vertexPoint);

                maxCellRadius = std::max(maxCellRadius, cellRadius);
            }

#if PRINT_JITTER_CURVE
            char buf[256];
            sprintf_s(buf, "%.3f,%.3f\n", middleRadius, maxCellRadius);
            OutputDebugStringA(buf);
#endif

            if (layerGroupIndex < int(m_OnionLayers.size()) - 1)
            {
                float cubicRootFactor = maxCellRadius * powf(middleRadius, -1.f / 3.f);
                cubicRootFactors.push_back(cubicRootFactor);
            }
            else
            {
                float linearFactor = maxCellRadius / middleRadius;
                linearFactors.push_back(linearFactor);
            }
        }

        layerGroupIndex++;
    }

    // Compute the median of the cubic root factors, there are some outliers in the curve
    if (!cubicRootFactors.empty())
    {
        std::sort(cubicRootFactors.begin(), cubicRootFactors.end());
        m_OnionCubicRootFactor = cubicRootFactors[cubicRootFactors.size() / 2];
    }
    else
    {
        m_OnionCubicRootFactor = 0.f;
    }

    // Compute the average of the linear factors, they're all the same anyway
    float sumOfLinearFactors = std::accumulate(linearFactors.begin(), linearFactors.end(), 0.f);
    m_OnionLinearFactor = sumOfLinearFactors / std::max(float(linearFactors.size()), 1.f);
}

const rtxdi::ContextParameters& rtxdi::Context::GetParameters() const
{
    return m_Params;
}

uint32_t rtxdi::Context::GetRisBufferElementCount() const
{
    uint32_t size = 0;
    size += m_Params.TileCount * m_Params.TileSize;
    size += m_Params.EnvironmentTileCount * m_Params.EnvironmentTileSize;
    size += GetReGIRLightSlotCount();

    return size;
}

uint32_t rtxdi::Context::GetReGIRLightSlotCount() const
{
    switch (m_Params.ReGIR.Mode)
    {
    case ReGIRMode::Disabled:
        return 0;

    case ReGIRMode::Grid:
        return m_Params.ReGIR.GridSize.x
            * m_Params.ReGIR.GridSize.y
            * m_Params.ReGIR.GridSize.z
            * m_Params.ReGIR.LightsPerCell;

    case ReGIRMode::Onion:
        return m_OnionCells * m_Params.ReGIR.LightsPerCell;
    }

    return 0;
}

uint32_t rtxdi::Context::GetReservoirBufferElementCount() const
{
    return m_ReservoirArrayPitch;
}

void rtxdi::Context::FillRuntimeParameters(
    RTXDI_ResamplingRuntimeParameters& runtimeParams,
    const FrameParameters& frame) const
{
    runtimeParams.firstLocalLight = frame.firstLocalLight;
    runtimeParams.numLocalLights = frame.numLocalLights;
    runtimeParams.firstInfiniteLight = frame.firstInfiniteLight;
    runtimeParams.numInfiniteLights = frame.numInfiniteLights;
    runtimeParams.environmentLightPresent = frame.environmentLightPresent;
    runtimeParams.environmentLightIndex = frame.environmentLightIndex;
    runtimeParams.neighborOffsetMask = m_Params.NeighborOffsetCount - 1;
    runtimeParams.tileSize = m_Params.TileSize;
    runtimeParams.tileCount = m_Params.TileCount;
    runtimeParams.enableLocalLightImportanceSampling = frame.enableLocalLightImportanceSampling;
    runtimeParams.reservoirBlockRowPitch = m_ReservoirBlockRowPitch;
    runtimeParams.reservoirArrayPitch = m_ReservoirArrayPitch;
    runtimeParams.environmentRisBufferOffset = m_RegirCellOffset + GetReGIRLightSlotCount();
    runtimeParams.environmentTileCount = m_Params.EnvironmentTileCount;
    runtimeParams.environmentTileSize = m_Params.EnvironmentTileSize;
    runtimeParams.regirGrid.cellsX = m_Params.ReGIR.GridSize.x;
    runtimeParams.regirGrid.cellsY = m_Params.ReGIR.GridSize.y;
    runtimeParams.regirGrid.cellsZ = m_Params.ReGIR.GridSize.z;
    runtimeParams.regirCommon.risBufferOffset = m_RegirCellOffset;
    runtimeParams.regirCommon.lightsPerCell = m_Params.ReGIR.LightsPerCell;
    runtimeParams.regirCommon.centerX = frame.regirCenter.x;
    runtimeParams.regirCommon.centerY = frame.regirCenter.y;
    runtimeParams.regirCommon.centerZ = frame.regirCenter.z;
    runtimeParams.regirCommon.cellSize = (m_Params.ReGIR.Mode == ReGIRMode::Onion)
        ? frame.regirCellSize * 0.5f // Onion operates with radii, while "size" feels more like diameter
        : frame.regirCellSize;
    runtimeParams.regirCommon.enable = m_Params.ReGIR.Mode != ReGIRMode::Disabled;
    runtimeParams.regirCommon.samplingJitter = std::max(0.f, frame.regirSamplingJitter * 2.f);
    runtimeParams.regirOnion.cubicRootFactor = m_OnionCubicRootFactor;
    runtimeParams.regirOnion.linearFactor = m_OnionLinearFactor;
    runtimeParams.regirOnion.numLayerGroups = uint32_t(m_OnionLayers.size());

    switch (m_Params.CheckerboardSamplingMode)
    {
    case CheckerboardMode::Black:
        runtimeParams.activeCheckerboardField = (frame.frameIndex & 1) ? 1 : 2;
        break;
    case CheckerboardMode::White:
        runtimeParams.activeCheckerboardField = (frame.frameIndex & 1) ? 2 : 1;
        break;
    default:
        runtimeParams.activeCheckerboardField = 0;
    }

    assert(m_OnionLayers.size() <= RTXDI_ONION_MAX_LAYER_GROUPS);
    for(int group = 0; group < int(m_OnionLayers.size()); group++)
    {
        runtimeParams.regirOnion.layers[group] = m_OnionLayers[group];
        runtimeParams.regirOnion.layers[group].innerRadius *= runtimeParams.regirCommon.cellSize;
        runtimeParams.regirOnion.layers[group].outerRadius *= runtimeParams.regirCommon.cellSize;
    }
    
    assert(m_OnionRings.size() <= RTXDI_ONION_MAX_RINGS);
    for (int n = 0; n < int(m_OnionRings.size()); n++)
    {
        runtimeParams.regirOnion.rings[n] = m_OnionRings[n];
    }
}

void rtxdi::Context::FillNeighborOffsetBuffer(uint8_t* buffer) const
{
    // Create a sequence of low-discrepancy samples within a unit radius around the origin
    // for "randomly" sampling neighbors during spatial resampling

    int R = 250;
    const float phi2 = 1.0f / 1.3247179572447f;
    uint32_t num = 0;
    float u = 0.5f;
    float v = 0.5f;
    while (num < m_Params.NeighborOffsetCount * 2) {
        u += phi2;
        v += phi2 * phi2;
        if (u >= 1.0f) u -= 1.0f;
        if (v >= 1.0f) v -= 1.0f;

        float rSq = (u - 0.5f) * (u - 0.5f) + (v - 0.5f) * (v - 0.5f);
        if (rSq > 0.25f)
            continue;

        buffer[num++] = int8_t((u - 0.5f) * R);
        buffer[num++] = int8_t((v - 0.5f) * R);
    }
}

void rtxdi::ComputePdfTextureSize(uint32_t maxItems, uint32_t& outWidth, uint32_t& outHeight, uint32_t& outMipLevels)
{
    // Compute the size of a power-of-2 rectangle that fits all items, 1 item per pixel
    double textureWidth = std::max(1.0, ceil(sqrt(double(maxItems))));
    textureWidth = exp2(ceil(log2(textureWidth)));
    double textureHeight = std::max(1.0, ceil(maxItems / textureWidth));
    textureHeight = exp2(ceil(log2(textureHeight)));
    double textureMips = std::max(1.0, log2(std::max(textureWidth, textureHeight)));

    outWidth = uint32_t(textureWidth);
    outHeight = uint32_t(textureHeight);
    outMipLevels = uint32_t(textureMips);
}
