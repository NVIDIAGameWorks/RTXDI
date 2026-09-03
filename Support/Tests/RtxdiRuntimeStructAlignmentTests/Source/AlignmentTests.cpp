#include <cstdint>

#include "Rtxdi/DI/ReSTIRDIParameters.h"
#include "Rtxdi/GI/ReSTIRGIParameters.h"
#include "Rtxdi/LightSampling/RISBufferSegmentParameters.h"
#include "Rtxdi/PT/ReSTIRPTParameters.h"
#include "Rtxdi/ReGIR/ReGIRParameters.h"

// Compile-time tests to ensure that all structs intended for use in
// constant buffers are 16-byte aligned.

static_assert(sizeof(RTXDI_DIBufferIndices) % 16 == 0);
static_assert(sizeof(RTXDI_DIInitialSamplingParameters) % 16 == 0);
static_assert(sizeof(RTXDI_DITemporalResamplingParameters) % 16 == 0);
static_assert(sizeof(RTXDI_DISpatialResamplingParameters) % 16 == 0);
static_assert(sizeof(RTXDI_DISpatioTemporalResamplingParameters) % 16 == 0);
static_assert(sizeof(RTXDI_ShadingParameters) % 16 == 0);
static_assert(sizeof(RTXDI_Parameters) % 16 == 0);

static_assert(sizeof(RTXDI_GIBufferIndices) % 16 == 0);
static_assert(sizeof(RTXDI_GITemporalResamplingParameters) % 16 == 0);
static_assert(sizeof(RTXDI_GISpatialResamplingParameters) % 16 == 0);
static_assert(sizeof(RTXDI_GISpatioTemporalResamplingParameters) % 16 == 0);
static_assert(sizeof(RTXDI_GIFinalShadingParameters) % 16 == 0);
static_assert(sizeof(RTXDI_GIParameters) % 16 == 0);

static_assert(sizeof(RTXDI_RISBufferSegmentParameters) % 16 == 0);

static_assert(sizeof(RTXDI_PTBufferIndices) % 16 == 0);
static_assert(sizeof(RTXDI_PTInitialSamplingParameters) % 16 == 0);
static_assert(sizeof(RTXDI_PTDecorrelationParameters) % 16 == 0);
static_assert(sizeof(RTXDI_PTHybridShiftPerFrameParameters) % 16 == 0);
static_assert(sizeof(RTXDI_PTReconnectionParameters) % 16 == 0);
static_assert(sizeof(RTXDI_PTTemporalResamplingParameters) % 16 == 0);
static_assert(sizeof(RTXDI_PTSpatialResamplingParameters) % 16 == 0);
static_assert(sizeof(RTXDI_PTParameters) % 16 == 0);

static_assert(sizeof(ReGIR_OnionLayerGroup) % 16 == 0);
static_assert(sizeof(ReGIR_OnionRing) % 16 == 0);
static_assert(sizeof(ReGIR_CommonParameters) % 16 == 0);
static_assert(sizeof(ReGIR_GridParameters) % 16 == 0);
static_assert(sizeof(ReGIR_OnionParameters) % 16 == 0);
static_assert(sizeof(ReGIR_Parameters) % 16 == 0);
