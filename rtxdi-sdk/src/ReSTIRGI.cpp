#include "rtxdi/ReSTIRGI.h"

namespace rtxdi
{

constexpr ReSTIRGI_BufferIndices getDefaultReSTIRGIBufferIndices()
{
    ReSTIRGI_BufferIndices bufferIndices = {};
    bufferIndices.secondarySurfaceReSTIRDIOutputBufferIndex = 0;
    bufferIndices.temporalResamplingInputBufferIndex = 0;
    bufferIndices.temporalResamplingOutputBufferIndex = 0;
    bufferIndices.spatialResamplingInputBufferIndex = 0;
    bufferIndices.spatialResamplingOutputBufferIndex = 0;
    return bufferIndices;
}

constexpr ReSTIRGI_TemporalResamplingParameters getDefaultReSTIRGITemporalResamplingParams()
{
    ReSTIRGI_TemporalResamplingParameters params = {};
    params.boilingFilterStrength = 0.2f;
    params.depthThreshold = 0.1f;
    params.enableBoilingFilter = true;
    params.enableFallbackSampling = true;
    params.enablePermutationSampling = false;
    params.maxHistoryLength = 8;
    params.maxReservoirAge = 30;
    params.normalThreshold = 0.6f;
    params.temporalBiasCorrectionMode = ResTIRGI_TemporalBiasCorrectionMode::Basic;
    return params;
}

constexpr ReSTIRGI_SpatialResamplingParameters getDefaultReSTIRGISpatialResamplingParams()
{
    ReSTIRGI_SpatialResamplingParameters params = {};
    params.numSpatialSamples = 2;
    params.spatialBiasCorrectionMode = ResTIRGI_SpatialBiasCorrectionMode::Basic;
    params.spatialDepthThreshold = 0.1f;
    params.spatialNormalThreshold = 0.6f;
    params.spatialSamplingRadius = 32.0f;
    return params;
}

constexpr ReSTIRGI_FinalShadingParameters getDefaultReSTIRGIFinalShadingParams()
{
    ReSTIRGI_FinalShadingParameters params = {};
    params.enableFinalMIS = true;
    params.enableFinalVisibility = true;
    return params;
}

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