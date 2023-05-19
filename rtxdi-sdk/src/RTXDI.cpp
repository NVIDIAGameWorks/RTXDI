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
#include <memory>
#include <numeric>
#include <math.h>

#define PRINT_JITTER_CURVE 0

#if PRINT_JITTER_CURVE
#define NOMINMAX
#include <Windows.h>
#endif

using namespace rtxdi;


static bool IsNonzeroPowerOf2(uint32_t i)
{
    return ((i & (i - 1)) == 0) && (i > 0);
}

namespace rtxdi
{

RTXDIContext::RTXDIContext(const RTXDIStaticParameters& params) :
    m_frameIndex(0),
    m_lightBufferParams({}),
    m_staticParams(params)
{
    assert(IsNonzeroPowerOf2(params.localLightPowerRISBufferSegmentParams.tileSize));
    assert(IsNonzeroPowerOf2(params.localLightPowerRISBufferSegmentParams.tileCount));
    assert(params.RenderWidth > 0);
    assert(params.RenderHeight > 0);

    uint32_t renderWidth = (params.CheckerboardSamplingMode == CheckerboardMode::Off)
        ? params.RenderWidth
        : (params.RenderWidth + 1) / 2;
    uint32_t renderWidthBlocks = (renderWidth + RTXDI_RESERVOIR_BLOCK_SIZE - 1) / RTXDI_RESERVOIR_BLOCK_SIZE;
    uint32_t renderHeightBlocks = (params.RenderHeight + RTXDI_RESERVOIR_BLOCK_SIZE - 1) / RTXDI_RESERVOIR_BLOCK_SIZE;
    m_ReservoirBlockRowPitch = renderWidthBlocks * (RTXDI_RESERVOIR_BLOCK_SIZE * RTXDI_RESERVOIR_BLOCK_SIZE);
    m_ReservoirArrayPitch = m_ReservoirBlockRowPitch * renderHeightBlocks;

    m_regirContext = std::make_unique<rtxdi::ReGIRContext>(params.ReGIR);

    m_regirContext->setReGIRCellOffset(m_staticParams.localLightPowerRISBufferSegmentParams.tileCount * m_staticParams.localLightPowerRISBufferSegmentParams.tileSize);
}

InitialSamplingSettings RTXDIContext::getInitialSamplingSettings() const
{
    return m_initialSamplingSettings;
}

TemporalResamplingSettings RTXDIContext::getTemporalResamplingSettings() const
{
    return m_temporalResamplingSettings;
}

BoilingFilterSettings RTXDIContext::getBoilingFilterSettings() const
{
    return m_boilingFilterSettings;
}

SpatialResamplingSettings RTXDIContext::getSpatialResamplingSettings() const
{
    return m_spatialResamplingSettings;
}

ShadingSettings RTXDIContext::getShadingSettings() const
{
    return m_shadingSettings;
}

const RTXDIStaticParameters& RTXDIContext::getStaticParameters() const
{
    return m_staticParams;
}

uint32_t RTXDIContext::GetRisBufferElementCount() const
{
    uint32_t size = 0;
    size += m_staticParams.localLightPowerRISBufferSegmentParams.tileCount * m_staticParams.localLightPowerRISBufferSegmentParams.tileSize;
    size += m_staticParams.environmentLightRISBufferSegmentParams.tileCount * m_staticParams.environmentLightRISBufferSegmentParams.tileSize;
    size += m_regirContext->getReGIRLightSlotCount();

    return size;
}

uint32_t RTXDIContext::GetReservoirBufferElementCount() const
{
    return m_ReservoirArrayPitch;
}

// 32 bit Jenkins hash
static uint32_t JenkinsHash(uint32_t a)
{
    // http://burtleburtle.net/bob/hash/integer.html
    a = (a + 0x7ed55d16) + (a << 12);
    a = (a ^ 0xc761c23c) ^ (a >> 19);
    a = (a + 0x165667b1) + (a << 5);
    a = (a + 0xd3a2646c) ^ (a << 9);
    a = (a + 0xfd7046c5) + (a << 3);
    a = (a ^ 0xb55a4f09) ^ (a >> 16);
    return a;
}

void RTXDIContext::FillRuntimeParameters(RTXDI_RuntimeParameters& runtimeParams) const
{
    runtimeParams.localLightParams.localLightsBufferRegion.firstLightIndex = m_lightBufferParams.firstLocalLight;
    runtimeParams.localLightParams.localLightsBufferRegion.numLights = m_lightBufferParams.numLocalLights;
    runtimeParams.localLightParams.localLightSamplingMode = static_cast<uint32_t>(m_initialSamplingSettings.localLightInitialSamplingMode);
    runtimeParams.localLightParams.risBufferParams.bufferOffset = 0;
    runtimeParams.localLightParams.risBufferParams.tileSize = m_staticParams.localLightPowerRISBufferSegmentParams.tileSize;
    runtimeParams.localLightParams.risBufferParams.tileCount = m_staticParams.localLightPowerRISBufferSegmentParams.tileCount;
    runtimeParams.infiniteLightParams.infiniteLightsBufferRegion.firstLightIndex = m_lightBufferParams.firstInfiniteLight;
    runtimeParams.infiniteLightParams.infiniteLightsBufferRegion.numLights = m_lightBufferParams.numInfiniteLights;
    runtimeParams.environmentLightParams.environmentLightPresent = m_lightBufferParams.environmentLightPresent;
    runtimeParams.environmentLightParams.environmentLightIndex = m_lightBufferParams.environmentLightIndex;
    runtimeParams.environmentLightParams.risBufferParams.bufferOffset = m_regirContext->getReGIRCellOffset() + m_regirContext->getReGIRLightSlotCount();
    runtimeParams.environmentLightParams.risBufferParams.tileCount = m_staticParams.environmentLightRISBufferSegmentParams.tileCount;
    runtimeParams.environmentLightParams.risBufferParams.tileSize = m_staticParams.environmentLightRISBufferSegmentParams.tileSize;
    runtimeParams.resamplingParams.neighborOffsetMask = m_staticParams.NeighborOffsetCount - 1;
    runtimeParams.resamplingParams.reservoirBlockRowPitch = m_ReservoirBlockRowPitch;
    runtimeParams.resamplingParams.reservoirArrayPitch = m_ReservoirArrayPitch;
    runtimeParams.resamplingParams.uniformRandomNumber = JenkinsHash(m_frameIndex);

    switch (m_staticParams.CheckerboardSamplingMode)
    {
    case CheckerboardMode::Black:
        runtimeParams.resamplingParams.activeCheckerboardField = (m_frameIndex & 1) ? 1 : 2;
        break;
    case CheckerboardMode::White:
        runtimeParams.resamplingParams.activeCheckerboardField = (m_frameIndex & 1) ? 2 : 1;
        break;
    default:
        runtimeParams.resamplingParams.activeCheckerboardField = 0;
    }

    m_regirContext->FillRuntimeParameters(runtimeParams);
}

bool RTXDIContext::isLocalLightPowerRISEnabled() const
{
    return (m_initialSamplingSettings.localLightInitialSamplingMode == LocalLightSamplingMode::Power_RIS) ||
           ((m_initialSamplingSettings.localLightInitialSamplingMode == LocalLightSamplingMode::ReGIR_RIS) && m_regirContext->isLocalLightPowerRISEnable());
}

void RTXDIContext::setFrameIndex(uint32_t frameIndex)
{
    m_frameIndex = frameIndex;
}

void RTXDIContext::setLightBufferParameters(const LightBufferParameters& lightBufferParams)
{
    m_lightBufferParams = lightBufferParams;
}

uint32_t RTXDIContext::getFrameIndex() const
{
    return m_frameIndex;
}

const LightBufferParameters& RTXDIContext::getLightBufferParameters() const
{
    return m_lightBufferParams;
}

ReGIRContext& RTXDIContext::getReGIRContext()
{
    return *m_regirContext;
}

void RTXDIContext::FillNeighborOffsetBuffer(uint8_t* buffer) const
{
    // Create a sequence of low-discrepancy samples within a unit radius around the origin
    // for "randomly" sampling neighbors during spatial resampling

    int R = 250;
    const float phi2 = 1.0f / 1.3247179572447f;
    uint32_t num = 0;
    float u = 0.5f;
    float v = 0.5f;
    while (num < m_staticParams.NeighborOffsetCount * 2) {
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

void RTXDIContext::setInitialSamplingSettings(const InitialSamplingSettings& initialSamplingSettings)
{
    m_initialSamplingSettings = initialSamplingSettings;
}

void RTXDIContext::setTemporalResamplingSettings(const TemporalResamplingSettings& temporalResamplingSettings)
{
    m_temporalResamplingSettings = temporalResamplingSettings;
}

void RTXDIContext::setBoilingFilterSettings(const BoilingFilterSettings& boilingFilterSettings)
{
    m_boilingFilterSettings = boilingFilterSettings;
}

void RTXDIContext::setSpatialResamplingSettings(const SpatialResamplingSettings& spatialResamplingSettings)
{
    m_spatialResamplingSettings = spatialResamplingSettings;
}

void RTXDIContext::setShadingSettings(const ShadingSettings& shadingSettings)
{
    m_shadingSettings = shadingSettings;
}

void ComputePdfTextureSize(uint32_t maxItems, uint32_t& outWidth, uint32_t& outHeight, uint32_t& outMipLevels)
{
    // Compute the size of a power-of-2 rectangle that fits all items, 1 item per pixel
    double textureWidth = std::max(1.0, ceil(sqrt(double(maxItems))));
    textureWidth = exp2(ceil(log2(textureWidth)));
    double textureHeight = std::max(1.0, ceil(maxItems / textureWidth));
    textureHeight = exp2(ceil(log2(textureHeight)));
    double textureMips = std::max(1.0, log2(std::max(textureWidth, textureHeight)) + 1.0);

    outWidth = uint32_t(textureWidth);
    outHeight = uint32_t(textureHeight);
    outMipLevels = uint32_t(textureMips);
}

}