/***************************************************************************
 # Copyright (c) 2020-2023, NVIDIA CORPORATION.  All rights reserved.
 #
 # NVIDIA CORPORATION and its licensors retain all intellectual property
 # and proprietary rights in and to this software, related documentation
 # and any modifications thereto.  Any use, reproduction, disclosure or
 # distribution of this software and related documentation without an express
 # license agreement from NVIDIA CORPORATION is strictly prohibited.
 **************************************************************************/

#include <rtxdi/ReSTIRDI.h>
#include <cassert>
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

namespace rtxdi
{


const uint32_t ReSTIRDIContext::NumReservoirBuffers = 3;

void debugCheckParameters(const ReSTIRDIStaticParameters& params)
{
    assert(params.RenderWidth > 0);
    assert(params.RenderHeight > 0);
}

ReSTIRDIContext::ReSTIRDIContext(const ReSTIRDIStaticParameters& params) :
    m_staticParams(params),
    m_frameIndex(0),
    m_resamplingMode(ReSTIRDI_ResamplingMode::TemporalAndSpatial),
    m_reservoirBufferParams(CalculateReservoirBufferParameters(params.RenderWidth, params.RenderHeight, params.CheckerboardSamplingMode)),
    m_bufferIndices(getDefaultReSTIRDIBufferIndices()),
    m_initialSamplingParams(getDefaultReSTIRDIInitialSamplingParams()),
    m_temporalResamplingParams(getDefaultReSTIRDITemporalResamplingParams()),
    m_spatialResamplingParams(getDefaultReSTIRDISpatialResamplingParams()),
    m_shadingParams(getDefaultReSTIRDIShadingParams())
{
    debugCheckParameters(params);
    updateCheckerboardField();
    m_runtimeParams.neighborOffsetMask = m_staticParams.NeighborOffsetCount - 1;
    updateBufferIndices();
}

ReSTIRDI_ResamplingMode ReSTIRDIContext::getResamplingMode() const
{
    return m_resamplingMode;
}

RTXDI_RuntimeParameters ReSTIRDIContext::getRuntimeParams() const
{
    return m_runtimeParams;
}

RTXDI_ReservoirBufferParameters ReSTIRDIContext::getReservoirBufferParameters() const
{
    return m_reservoirBufferParams;
}

ReSTIRDI_BufferIndices ReSTIRDIContext::getBufferIndices() const
{
    return m_bufferIndices;
}

ReSTIRDI_InitialSamplingParameters ReSTIRDIContext::getInitialSamplingParameters() const
{
    return m_initialSamplingParams;
}

ReSTIRDI_TemporalResamplingParameters ReSTIRDIContext::getTemporalResamplingParameters() const
{
    return m_temporalResamplingParams;
}

ReSTIRDI_SpatialResamplingParameters ReSTIRDIContext::getSpatialResamplingParameters() const
{
    return m_spatialResamplingParams;
}

ReSTIRDI_ShadingParameters ReSTIRDIContext::getShadingParameters() const
{
    return m_shadingParams;
}

const ReSTIRDIStaticParameters& ReSTIRDIContext::getStaticParameters() const
{
    return m_staticParams;
}

void ReSTIRDIContext::setFrameIndex(uint32_t frameIndex)
{
    m_frameIndex = frameIndex;
    m_temporalResamplingParams.uniformRandomNumber = JenkinsHash(m_frameIndex);
    m_LastFrameOutputReservoir = m_CurrentFrameOutputReservoir;
    updateBufferIndices();
    updateCheckerboardField();
}

uint32_t ReSTIRDIContext::getFrameIndex() const
{
    return m_frameIndex;
}

void ReSTIRDIContext::setResamplingMode(ReSTIRDI_ResamplingMode resamplingMode)
{
    m_resamplingMode = resamplingMode;
    updateBufferIndices();
}

void ReSTIRDIContext::setInitialSamplingParameters(const ReSTIRDI_InitialSamplingParameters& initialSamplingParams)
{
    m_initialSamplingParams = initialSamplingParams;
}

void ReSTIRDIContext::setTemporalResamplingParameters(const ReSTIRDI_TemporalResamplingParameters& temporalResamplingParams)
{
    m_temporalResamplingParams = temporalResamplingParams;
    m_temporalResamplingParams.uniformRandomNumber = JenkinsHash(m_frameIndex);
}

void ReSTIRDIContext::setSpatialResamplingParameters(const ReSTIRDI_SpatialResamplingParameters& spatialResamplingParams)
{
    ReSTIRDI_SpatialResamplingParameters srp = spatialResamplingParams;
    srp.neighborOffsetMask = m_spatialResamplingParams.neighborOffsetMask;
    m_spatialResamplingParams = srp;
}

void ReSTIRDIContext::setShadingParameters(const ReSTIRDI_ShadingParameters& shadingParams)
{
    m_shadingParams = shadingParams;
}

void ReSTIRDIContext::updateBufferIndices()
{
    const bool useTemporalResampling =
        m_resamplingMode == ReSTIRDI_ResamplingMode::Temporal ||
        m_resamplingMode == ReSTIRDI_ResamplingMode::TemporalAndSpatial ||
        m_resamplingMode == ReSTIRDI_ResamplingMode::FusedSpatiotemporal;

    const bool useSpatialResampling =
        m_resamplingMode == ReSTIRDI_ResamplingMode::Spatial ||
        m_resamplingMode == ReSTIRDI_ResamplingMode::TemporalAndSpatial ||
        m_resamplingMode == ReSTIRDI_ResamplingMode::FusedSpatiotemporal;


    if (m_resamplingMode == ReSTIRDI_ResamplingMode::FusedSpatiotemporal)
    {
        m_bufferIndices.initialSamplingOutputBufferIndex = (m_LastFrameOutputReservoir + 1) % ReSTIRDIContext::NumReservoirBuffers;
        m_bufferIndices.temporalResamplingInputBufferIndex = m_LastFrameOutputReservoir;
        m_bufferIndices.shadingInputBufferIndex = m_bufferIndices.initialSamplingOutputBufferIndex;
    }
    else
    {
        m_bufferIndices.initialSamplingOutputBufferIndex = (m_LastFrameOutputReservoir + 1) % ReSTIRDIContext::NumReservoirBuffers;
        m_bufferIndices.temporalResamplingInputBufferIndex = m_LastFrameOutputReservoir;
        m_bufferIndices.temporalResamplingOutputBufferIndex = (m_bufferIndices.temporalResamplingInputBufferIndex + 1) % ReSTIRDIContext::NumReservoirBuffers;
        m_bufferIndices.spatialResamplingInputBufferIndex = useTemporalResampling
            ? m_bufferIndices.temporalResamplingOutputBufferIndex
            : m_bufferIndices.initialSamplingOutputBufferIndex;
        m_bufferIndices.spatialResamplingOutputBufferIndex = (m_bufferIndices.spatialResamplingInputBufferIndex + 1) % ReSTIRDIContext::NumReservoirBuffers;
        m_bufferIndices.shadingInputBufferIndex = useSpatialResampling
            ? m_bufferIndices.spatialResamplingOutputBufferIndex
            : m_bufferIndices.temporalResamplingOutputBufferIndex;
    }
    m_CurrentFrameOutputReservoir = m_bufferIndices.shadingInputBufferIndex;
}

void ReSTIRDIContext::updateCheckerboardField()
{
    switch (m_staticParams.CheckerboardSamplingMode)
    {
    case CheckerboardMode::Black:
        m_runtimeParams.activeCheckerboardField = (m_frameIndex & 1) ? 1 : 2;
        break;
    case CheckerboardMode::White:
        m_runtimeParams.activeCheckerboardField = (m_frameIndex & 1) ? 2 : 1;
        break;
    default:
        m_runtimeParams.activeCheckerboardField = 0;
    }
}

}