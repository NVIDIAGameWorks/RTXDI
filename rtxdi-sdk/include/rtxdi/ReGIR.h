/***************************************************************************
 # Copyright (c) 2020-2023, NVIDIA CORPORATION.  All rights reserved.
 #
 # NVIDIA CORPORATION and its licensors retain all intellectual property
 # and proprietary rights in and to this software, related documentation
 # and any modifications thereto.  Any use, reproduction, disclosure or
 # distribution of this software and related documentation without an express
 # license agreement from NVIDIA CORPORATION is strictly prohibited.
 **************************************************************************/

#ifndef REGIR_PARAMETERS_H
#define REGIR_PARAMETERS_H

#include <stdint.h>
#include <vector>

#include "ReGIRParameters.h"

struct RTXDI_RuntimeParameters;

namespace rtxdi
{
    class RISBufferSegmentAllocator;

    struct uint3
    {
        uint32_t x;
        uint32_t y;
        uint32_t z;
    };

    struct float3
    {
        float x;
        float y;
        float z;
    };

    enum class ReGIRMode : uint32_t
    {
        Disabled = 0,
        Grid = RTXDI_REGIR_GRID,
        Onion = RTXDI_REGIR_ONION
    };

    struct ReGIRGridStaticParameters
    {
        // Grid dimensions along the primary axes, in cells.
        uint3 GridSize = { 16, 16, 16 };
    };

    struct ReGIROnionStaticParameters
    {
        // Number of onion layers that cover the volume around the center
        // with high detail. These layers have cell size that is proportional
        // to a cubic root of the distance from the center. The number of cells
        // in each detail layer is higher than the number of cells in the previous
        // detail layer.
        // Acceptable values are 0 to RTXDI_ONION_MAX_LAYER_GROUPS.
        uint32_t OnionDetailLayers = 5;

        // Number of onion layers that cover the volume after the detail layers.
        // Each coverage layer has the same number of cells that is determined
        // only by the number of the detail layers. Coverage layers have cell size
        // that is proportional to the distance from the center as a linear function.
        uint32_t OnionCoverageLayers = 10;
    };

    // ReGIR parameters that are used to generate ReGIR data structures
    // Changing these requires recreating the ReGIR context and the associated buffers
    struct ReGIRStaticParameters
    {
        ReGIRMode Mode = ReGIRMode::Onion;

        // Number of light reservoirs computed and stored for each cell.
        uint32_t LightsPerCell = 512;

        ReGIRGridStaticParameters gridParameters;
        ReGIROnionStaticParameters onionParameters;
    };

    // ReGIR parameters generated from the ReGIRGridStaticParameters
    // Changing these requires changing the ReGIRStaticParameters and
    // therefore recreating the ReGIRcontext
    struct ReGIRGridCalculatedParameters
    {
        uint32_t lightSlotCount = 0;
    };

    // ReGIR parameters generated from the ReGIROnionStaticParameters
    // Changing these requires changing the ReGIRStaticParameters and
    // therefore recreating the ReGIRcontext
    struct ReGIROnionCalculatedParameters
    {
        uint32_t lightSlotCount = 0;
        uint32_t regirOnionCells = 0;
        std::vector<ReGIR_OnionLayerGroup> regirOnionLayers;
        std::vector<ReGIR_OnionRing> regirOnionRings;
        float regirOnionCubicRootFactor = 0.f;
        float regirOnionLinearFactor = 0.f;
    };

    enum class LocalLightReGIRPresamplingMode : uint32_t
    {
        Uniform = REGIR_LOCAL_LIGHT_PRESAMPLING_MODE_UNIFORM,
        Power_RIS = REGIR_LOCAL_LIGHT_PRESAMPLING_MODE_POWER_RIS
    };

    enum class LocalLightReGIRFallbackSamplingMode : uint32_t
    {
        Uniform = REGIR_LOCAL_LIGHT_FALLBACK_MODE_UNIFORM,
        Power_RIS = REGIR_LOCAL_LIGHT_FALLBACK_MODE_POWER_RIS
    };

    // ReGIR parameters that can be changed at runtime without requiring any 
    // recreation of buffers or data structures.
    struct ReGIRDynamicParameters
    {
        // Size of the smallest ReGIR cell size in world units
        float regirCellSize = 1.0f;

        // Center of the ReGIR structure in world space
        float3 center = { 0.0f, 0.0f, 0.0f };

        // Light sampling mode to use for local light sampling when the surface falls outside the ReGIR grid
        LocalLightReGIRFallbackSamplingMode fallbackSamplingMode = LocalLightReGIRFallbackSamplingMode::Power_RIS;

        // Light sampling mode ReGIR uses to select lights to fill the ReGIR RIS buffer.
        LocalLightReGIRPresamplingMode presamplingMode = LocalLightReGIRPresamplingMode::Power_RIS;

        // Scale of jitter applied to surface positions when sampling the ReGIR grid,
        // measured in grid cells. The value of 1.0 means plus or minus one grid cell.
        // This jitter scale is provided here because it affects both grid construction
        // (to determine effective cell radii) and sampling.
        float regirSamplingJitter = 1.0f;

        // Number of lights samples to take when filling a ReGIR cell.
        uint32_t regirNumBuildSamples = 8;
    };
    

    // Make this take static ReGIR params, update its dynamic ones
    class ReGIRContext
    {
    public:
        ReGIRContext(const ReGIRStaticParameters& params, RISBufferSegmentAllocator& risBufferSegmentAllocator);

        bool isLocalLightPowerRISEnable() const;

        uint32_t getReGIRCellOffset() const;
        uint32_t getReGIRLightSlotCount() const;
        ReGIRGridCalculatedParameters getReGIRGridCalculatedParameters() const;
        ReGIROnionCalculatedParameters getReGIROnionCalculatedParameters() const;
        ReGIRDynamicParameters getReGIRDynamicParameters() const;
        ReGIRStaticParameters getReGIRStaticParameters() const;

        void setDynamicParameters(const ReGIRDynamicParameters& dynamicParameters);

    private:
        void InitializeOnion(const ReGIRStaticParameters& params);
        void ComputeOnionJitterCurve();
        void ComputeGridLightSlotCount();
        void AllocateRISBufferSegment(RISBufferSegmentAllocator& risBufferSegmentAllocator);

        uint32_t m_regirCellOffset = 0;

        ReGIRStaticParameters m_regirStaticParameters;
        ReGIRDynamicParameters m_regirDynamicParameters;
        ReGIROnionCalculatedParameters m_regirOnionCalculatedParameters;
        ReGIRGridCalculatedParameters m_regirGridCalculatedParameters;
    };
}

#endif // REGIR_PARAMETERS_H