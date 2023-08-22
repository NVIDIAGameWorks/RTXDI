/***************************************************************************
 # Copyright (c) 2020-2023, NVIDIA CORPORATION.  All rights reserved.
 #
 # NVIDIA CORPORATION and its licensors retain all intellectual property
 # and proprietary rights in and to this software, related documentation
 # and any modifications thereto.  Any use, reproduction, disclosure or
 # distribution of this software and related documentation without an express
 # license agreement from NVIDIA CORPORATION is strictly prohibited.
 **************************************************************************/

#include "rtxdi/ImportanceSamplingContext.h"

#include <cassert>

#include "rtxdi/RISBufferSegmentAllocator.h"
#include "rtxdi/ReSTIRDI.h"
#include "rtxdi/ReGIR.h"
#include "rtxdi/ReSTIRGI.h"

namespace
{

bool IsNonzeroPowerOf2(uint32_t i)
{
    return ((i & (i - 1)) == 0) && (i > 0);
}

void debugCheckParameters(const rtxdi::RISBufferSegmentParameters& localLightRISBufferParams,
                          const rtxdi::RISBufferSegmentParameters& environmentLightRISBufferParams)
{
    assert(IsNonzeroPowerOf2(localLightRISBufferParams.tileSize));
    assert(IsNonzeroPowerOf2(localLightRISBufferParams.tileCount));
    assert(IsNonzeroPowerOf2(environmentLightRISBufferParams.tileSize));
    assert(IsNonzeroPowerOf2(environmentLightRISBufferParams.tileCount));
}

}

namespace rtxdi
{

ImportanceSamplingContext::ImportanceSamplingContext(const ImportanceSamplingContext_StaticParameters& isParams)
{
    debugCheckParameters(isParams.localLightRISBufferParams, isParams.environmentLightRISBufferParams);

    m_risBufferSegmentAllocator = std::make_unique<rtxdi::RISBufferSegmentAllocator>();
    m_localLightRISBufferSegmentParams.bufferOffset = m_risBufferSegmentAllocator->allocateSegment(isParams.localLightRISBufferParams.tileCount * isParams.localLightRISBufferParams.tileSize);
    m_localLightRISBufferSegmentParams.tileCount = isParams.localLightRISBufferParams.tileCount;
    m_localLightRISBufferSegmentParams.tileSize = isParams.localLightRISBufferParams.tileSize;
    m_environmentLightRISBufferSegmentParams.bufferOffset = m_risBufferSegmentAllocator->allocateSegment(isParams.environmentLightRISBufferParams.tileCount * isParams.environmentLightRISBufferParams.tileSize);
    m_environmentLightRISBufferSegmentParams.tileCount = isParams.environmentLightRISBufferParams.tileCount;
    m_environmentLightRISBufferSegmentParams.tileSize = isParams.environmentLightRISBufferParams.tileSize;
    
    ReSTIRDIStaticParameters restirDIStaticParams;
    restirDIStaticParams.CheckerboardSamplingMode = isParams.CheckerboardSamplingMode;
    restirDIStaticParams.NeighborOffsetCount = isParams.NeighborOffsetCount;
    restirDIStaticParams.RenderWidth = isParams.renderWidth;
    restirDIStaticParams.RenderHeight = isParams.renderHeight;
    m_restirDIContext = std::make_unique<rtxdi::ReSTIRDIContext>(restirDIStaticParams);

    m_regirContext = std::make_unique<rtxdi::ReGIRContext>(isParams.regirStaticParams, *m_risBufferSegmentAllocator);

    ReSTIRGIStaticParameters restirGIStaticParams;
    restirGIStaticParams.CheckerboardSamplingMode = isParams.CheckerboardSamplingMode;
    restirGIStaticParams.RenderWidth = isParams.renderWidth;
    restirGIStaticParams.RenderHeight = isParams.renderHeight;
    m_restirGIContext = std::make_unique<rtxdi::ReSTIRGIContext>(restirGIStaticParams);
}

ImportanceSamplingContext::~ImportanceSamplingContext()
{

}

ReSTIRDIContext& ImportanceSamplingContext::getReSTIRDIContext()
{
    return *m_restirDIContext;
}

const ReSTIRDIContext& ImportanceSamplingContext::getReSTIRDIContext() const
{
    return *m_restirDIContext;
}

ReGIRContext& ImportanceSamplingContext::getReGIRContext()
{
    return *m_regirContext;
}

const ReGIRContext& ImportanceSamplingContext::getReGIRContext() const
{
    return *m_regirContext;
}

ReSTIRGIContext& ImportanceSamplingContext::getReSTIRGIContext()
{
    return *m_restirGIContext;
}

const ReSTIRGIContext& ImportanceSamplingContext::getReSTIRGIContext() const
{
    return *m_restirGIContext;
}

const RISBufferSegmentAllocator& ImportanceSamplingContext::getRISBufferSegmentAllocator() const
{
    return *m_risBufferSegmentAllocator;
}

const RTXDI_LightBufferParameters& ImportanceSamplingContext::getLightBufferParameters() const
{
    return m_lightBufferParams;
}

const RTXDI_RISBufferSegmentParameters& ImportanceSamplingContext::getLocalLightRISBufferSegmentParams() const
{
    return m_localLightRISBufferSegmentParams;
}

const RTXDI_RISBufferSegmentParameters& ImportanceSamplingContext::getEnvironmentLightRISBufferSegmentParams() const
{
    return m_environmentLightRISBufferSegmentParams;
}

uint32_t ImportanceSamplingContext::getNeighborOffsetCount() const
{
    return m_restirDIContext->getStaticParameters().NeighborOffsetCount;
}

bool ImportanceSamplingContext::isLocalLightPowerRISEnabled() const
{
    bool enabled = false;
    ReSTIRDI_InitialSamplingParameters iss = m_restirDIContext->getInitialSamplingParameters();
    if (iss.localLightSamplingMode == ReSTIRDI_LocalLightSamplingMode::Power_RIS)
        return true;
    if (iss.localLightSamplingMode == ReSTIRDI_LocalLightSamplingMode::ReGIR_RIS)
    {
        if( (m_regirContext->getReGIRDynamicParameters().presamplingMode == LocalLightReGIRPresamplingMode::Power_RIS) ||
            (m_regirContext->getReGIRDynamicParameters().fallbackSamplingMode == LocalLightReGIRFallbackSamplingMode::Power_RIS))
            return true;
    }
    return false;
}

bool ImportanceSamplingContext::isReGIREnabled() const
{
    return (m_restirDIContext->getInitialSamplingParameters().localLightSamplingMode == ReSTIRDI_LocalLightSamplingMode::ReGIR_RIS);
}

void ImportanceSamplingContext::setLightBufferParams(const RTXDI_LightBufferParameters& lightBufferParams)
{
    m_lightBufferParams = lightBufferParams;
}

}