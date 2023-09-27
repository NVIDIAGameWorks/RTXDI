/***************************************************************************
 # Copyright (c) 2020-2023, NVIDIA CORPORATION.  All rights reserved.
 #
 # NVIDIA CORPORATION and its licensors retain all intellectual property
 # and proprietary rights in and to this software, related documentation
 # and any modifications thereto.  Any use, reproduction, disclosure or
 # distribution of this software and related documentation without an express
 # license agreement from NVIDIA CORPORATION is strictly prohibited.
 **************************************************************************/

#include "rtxdi/ReSTIRGI.h"

namespace rtxdi
{

ReSTIRGIContext::ReSTIRGIContext(const ReSTIRGIStaticParameters& staticParams) :
    m_frameIndex(0),
    m_reservoirBufferParams(CalculateReservoirBufferParameters(staticParams.RenderWidth, staticParams.RenderHeight, staticParams.CheckerboardSamplingMode)),
    m_staticParams(staticParams),
    m_resamplingMode(rtxdi::ReSTIRGI_ResamplingMode::None),
    m_bufferIndices(getDefaultReSTIRGIBufferIndices()),
    m_temporalResamplingParams(getDefaultReSTIRGITemporalResamplingParams()),
    m_spatialResamplingParams(getDefaultReSTIRGISpatialResamplingParams()),
    m_finalShadingParams(getDefaultReSTIRGIFinalShadingParams())
{
}

ReSTIRGIStaticParameters ReSTIRGIContext::getStaticParams() const
{
    return m_staticParams;
}

uint32_t ReSTIRGIContext::getFrameIndex() const
{
    return m_frameIndex;
}

RTXDI_ReservoirBufferParameters ReSTIRGIContext::getReservoirBufferParameters() const
{
    return m_reservoirBufferParams;
}

ReSTIRGI_ResamplingMode ReSTIRGIContext::getResamplingMode() const
{
    return m_resamplingMode;
}

ReSTIRGI_BufferIndices ReSTIRGIContext::getBufferIndices() const
{
    return m_bufferIndices;
}

ReSTIRGI_TemporalResamplingParameters ReSTIRGIContext::getTemporalResamplingParameters() const
{
    return m_temporalResamplingParams;
}

ReSTIRGI_SpatialResamplingParameters ReSTIRGIContext::getSpatialResamplingParameters() const
{
    return m_spatialResamplingParams;
}

ReSTIRGI_FinalShadingParameters ReSTIRGIContext::getFinalShadingParameters() const
{
    return m_finalShadingParams;
}

void ReSTIRGIContext::setFrameIndex(uint32_t frameIndex)
{
    m_frameIndex = frameIndex;
    m_temporalResamplingParams.uniformRandomNumber = JenkinsHash(m_frameIndex);
    updateBufferIndices();
}

void ReSTIRGIContext::setResamplingMode(ReSTIRGI_ResamplingMode resamplingMode)
{
    m_resamplingMode = resamplingMode;
    updateBufferIndices();
}

void ReSTIRGIContext::setTemporalResamplingParameters(const ReSTIRGI_TemporalResamplingParameters& temporalResamplingParams)
{
    m_temporalResamplingParams = temporalResamplingParams;
    m_temporalResamplingParams.uniformRandomNumber = JenkinsHash(m_frameIndex);
}

void ReSTIRGIContext::setSpatialResamplingParameters(const ReSTIRGI_SpatialResamplingParameters& spatialResamplingParams)
{
    m_spatialResamplingParams = spatialResamplingParams;
}

void ReSTIRGIContext::setFinalShadingParameters(const ReSTIRGI_FinalShadingParameters& finalShadingParams)
{
    m_finalShadingParams = finalShadingParams;
}

void ReSTIRGIContext::updateBufferIndices()
{
    switch (m_resamplingMode)
    {
    case rtxdi::ReSTIRGI_ResamplingMode::None:
        m_bufferIndices.secondarySurfaceReSTIRDIOutputBufferIndex = 0;
        m_bufferIndices.finalShadingInputBufferIndex = 0;
        break;
    case rtxdi::ReSTIRGI_ResamplingMode::Temporal:
        m_bufferIndices.secondarySurfaceReSTIRDIOutputBufferIndex = m_frameIndex & 1;
        m_bufferIndices.temporalResamplingInputBufferIndex = !m_bufferIndices.secondarySurfaceReSTIRDIOutputBufferIndex;
        m_bufferIndices.temporalResamplingOutputBufferIndex = m_bufferIndices.secondarySurfaceReSTIRDIOutputBufferIndex;
        m_bufferIndices.finalShadingInputBufferIndex = m_bufferIndices.temporalResamplingOutputBufferIndex;
        break;
    case rtxdi::ReSTIRGI_ResamplingMode::Spatial:
        m_bufferIndices.secondarySurfaceReSTIRDIOutputBufferIndex = 0;
        m_bufferIndices.spatialResamplingInputBufferIndex = 0;
        m_bufferIndices.spatialResamplingOutputBufferIndex = 1;
        m_bufferIndices.finalShadingInputBufferIndex = 1;
        break;
    case rtxdi::ReSTIRGI_ResamplingMode::TemporalAndSpatial:
        m_bufferIndices.secondarySurfaceReSTIRDIOutputBufferIndex = 0;
        m_bufferIndices.temporalResamplingInputBufferIndex = 1;
        m_bufferIndices.temporalResamplingOutputBufferIndex = 0;
        m_bufferIndices.spatialResamplingInputBufferIndex = 0;
        m_bufferIndices.spatialResamplingOutputBufferIndex = 1;
        m_bufferIndices.finalShadingInputBufferIndex = 1;
        break;
    case rtxdi::ReSTIRGI_ResamplingMode::FusedSpatiotemporal:
        m_bufferIndices.secondarySurfaceReSTIRDIOutputBufferIndex = m_frameIndex & 1;
        m_bufferIndices.temporalResamplingInputBufferIndex = !m_bufferIndices.secondarySurfaceReSTIRDIOutputBufferIndex;
        m_bufferIndices.spatialResamplingOutputBufferIndex = m_bufferIndices.secondarySurfaceReSTIRDIOutputBufferIndex;
        m_bufferIndices.finalShadingInputBufferIndex = m_bufferIndices.spatialResamplingOutputBufferIndex;
        break;
    }
}

}