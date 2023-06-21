#pragma once

#include <stdint.h>
#include "rtxdi/ReSTIRGIParameters.h"
#include "rtxdi/RTXDIUtils.h"

namespace rtxdi
{

static constexpr uint32_t c_NumReSTIRGIReservoirBuffers = 2;

struct ReSTIRGIStaticParameters
{
    uint32_t RenderWidth = 0;
    uint32_t RenderHeight = 0;
    CheckerboardMode CheckerboardSamplingMode = CheckerboardMode::Off;
};

enum class ReSTIRGI_ResamplingMode : uint32_t
{
    None = 0,
    Temporal = 1,
    Spatial = 2,
    TemporalAndSpatial = 3,
    FusedSpatiotemporal = 4,
};

constexpr ReSTIRGI_BufferIndices getDefaultReSTIRGIBufferIndices();
constexpr ReSTIRGI_TemporalResamplingParameters getDefaultReSTIRGITemporalResamplingParams();
constexpr ReSTIRGI_SpatialResamplingParameters getDefaultReSTIRGISpatialResamplingParams();
constexpr ReSTIRGI_FinalShadingParameters getDefaultReSTIRGIFinalShadingParams();

class ReSTIRGIContext
{
public:
    ReSTIRGIContext(const ReSTIRGIStaticParameters& params);

    ReSTIRGIStaticParameters getStaticParams() const;

    uint32_t getFrameIndex() const;
    RTXDI_ReservoirBufferParameters getReservoirBufferParameters() const;
    ReSTIRGI_ResamplingMode getResamplingMode() const;
    ReSTIRGI_BufferIndices getBufferIndices() const;
    ReSTIRGI_TemporalResamplingParameters getTemporalResamplingParameters() const;
    ReSTIRGI_SpatialResamplingParameters getSpatialResamplingParameters() const;
    ReSTIRGI_FinalShadingParameters getFinalShadingParameters() const;

    void setFrameIndex(uint32_t frameIndex);
    void setResamplingMode(ReSTIRGI_ResamplingMode resamplingMode);
    void setTemporalResamplingParameters(const ReSTIRGI_TemporalResamplingParameters& temporalResamplingParams);
    void setSpatialResamplingParameters(const ReSTIRGI_SpatialResamplingParameters& spatialResamplingParams);
    void setFinalShadingParameters(const ReSTIRGI_FinalShadingParameters& finalShadingParams);

    static uint32_t numReservoirBuffers;

private:
    ReSTIRGIStaticParameters m_staticParams;

    uint32_t m_frameIndex;
    RTXDI_ReservoirBufferParameters m_reservoirBufferParams;
    ReSTIRGI_ResamplingMode m_resamplingMode;
    ReSTIRGI_BufferIndices m_bufferIndices;
    ReSTIRGI_TemporalResamplingParameters m_temporalResamplingParams;
    ReSTIRGI_SpatialResamplingParameters m_spatialResamplingParams;
    ReSTIRGI_FinalShadingParameters m_finalShadingParams;

    void updateBufferIndices();
};

}