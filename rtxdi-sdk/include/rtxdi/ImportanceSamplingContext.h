/***************************************************************************
 # Copyright (c) 2020-2023, NVIDIA CORPORATION.  All rights reserved.
 #
 # NVIDIA CORPORATION and its licensors retain all intellectual property
 # and proprietary rights in and to this software, related documentation
 # and any modifications thereto.  Any use, reproduction, disclosure or
 # distribution of this software and related documentation without an express
 # license agreement from NVIDIA CORPORATION is strictly prohibited.
 **************************************************************************/

#pragma once

#include <memory>

#include "rtxdi/ReSTIRDI.h"
#include "rtxdi/ReGIR.h"
#include "rtxdi/ReSTIRGI.h"

namespace rtxdi
{

class RISBufferSegmentAllocator;
struct ReSTIRDIStaticParameters;
struct ReGIRStaticParameters;
struct ReSTIRGIStaticParameters;

struct ImportanceSamplingContext_StaticParameters
{
    // RIS buffer params for light presampling
    RISBufferSegmentParameters localLightRISBufferParams = { 1024, 128 };
    RISBufferSegmentParameters environmentLightRISBufferParams = { 1024, 128 };

    // Shared options for ReSTIRDI and ReSTIRGI
    uint32_t NeighborOffsetCount = 8192;
    uint32_t renderWidth = 0;
    uint32_t renderHeight = 0;
    CheckerboardMode CheckerboardSamplingMode = CheckerboardMode::Off;

    // ReGIR params
    ReGIRStaticParameters regirStaticParams = {};
};

class ImportanceSamplingContext
{
public:
    ImportanceSamplingContext(const ImportanceSamplingContext_StaticParameters& isParams);
    ~ImportanceSamplingContext();

    ReSTIRDIContext& getReSTIRDIContext();
    const ReSTIRDIContext& getReSTIRDIContext() const;
    ReGIRContext& getReGIRContext();
    const ReGIRContext& getReGIRContext() const;
    ReSTIRGIContext& getReSTIRGIContext();
    const ReSTIRGIContext& getReSTIRGIContext() const;

    const RISBufferSegmentAllocator& getRISBufferSegmentAllocator() const;

    const RTXDI_LightBufferParameters& getLightBufferParameters() const;
    const RTXDI_RISBufferSegmentParameters& getLocalLightRISBufferSegmentParams() const;
    const RTXDI_RISBufferSegmentParameters& getEnvironmentLightRISBufferSegmentParams() const;
    uint32_t getNeighborOffsetCount() const;

    bool isLocalLightPowerRISEnabled() const;
    bool isReGIREnabled() const;

    void setLightBufferParams(const RTXDI_LightBufferParameters& lightBufferParams);

private:
    std::unique_ptr<RISBufferSegmentAllocator> m_risBufferSegmentAllocator;
    std::unique_ptr<ReSTIRDIContext> m_restirDIContext;
    std::unique_ptr<ReGIRContext> m_regirContext;
    std::unique_ptr<ReSTIRGIContext> m_restirGIContext;

    // Common buffer params
    RTXDI_LightBufferParameters m_lightBufferParams;
    RTXDI_RISBufferSegmentParameters m_localLightRISBufferSegmentParams;
    RTXDI_RISBufferSegmentParameters m_environmentLightRISBufferSegmentParams;
};

}