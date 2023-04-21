/***************************************************************************
 # Copyright (c) 2020-2021, NVIDIA CORPORATION.  All rights reserved.
 #
 # NVIDIA CORPORATION and its licensors retain all intellectual property
 # and proprietary rights in and to this software, related documentation
 # and any modifications thereto.  Any use, reproduction, disclosure or
 # distribution of this software and related documentation without an express
 # license agreement from NVIDIA CORPORATION is strictly prohibited.
 **************************************************************************/

#pragma once

#include <stdint.h>
#include <vector>

#include "RtxdiParameters.h"

namespace rtxdi
{
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

    // Checkerboard sampling modes match those used in NRD, based on frameIndex:
    // Even frame(0)  Odd frame(1)   ...
    //     B W             W B
    //     W B             B W
    // BLACK and WHITE modes define cells with VALID data
    enum class CheckerboardMode : uint32_t
    {
        Off = 0,
        Black = 1,
        White = 2
    };
    
    enum class ReGIRMode : uint32_t
    {
        Disabled = 0,
        Grid = 1,
        Onion = 2
    };
    
    struct ReGIRContextParameters
    {
        ReGIRMode Mode = ReGIRMode::Disabled;

        // Common settings

        // Number of light reservoirs computed and stored for each cell.
        uint32_t LightsPerCell = 512;

        // Grid mode

        // Grid dimensions along the primary axes, in cells.
        uint3 GridSize = { 16, 16, 16 };

        // Onion mode

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

    struct ContextParameters
    {
        uint32_t TileSize = 1024;
        uint32_t TileCount = 128;
        uint32_t NeighborOffsetCount = 8192;
        uint32_t RenderWidth = 0;
        uint32_t RenderHeight = 0;
        uint32_t EnvironmentTileSize = 1024;
        uint32_t EnvironmentTileCount = 128;

        CheckerboardMode CheckerboardSamplingMode = CheckerboardMode::Off;
        
        ReGIRContextParameters ReGIR;
    };

    struct FrameParameters
    {
        // Linear index of the current frame, used to determine the checkerboard field.
        uint32_t frameIndex = 0;

        // Index of the first local light in the light buffer.
        uint32_t firstLocalLight = 0;

        // Number of local lights available on this frame.
        uint32_t numLocalLights = 0;

        // Index of the first infinite light in the light buffer.
        uint32_t firstInfiniteLight = 0;

        // Number of infinite lights available on this frame. They must be indexed
        // immediately following the local lights.
        uint32_t numInfiniteLights = 0;

        // Enables the use of an importance sampled environment map light.
        bool environmentLightPresent = false;

        // Index of the importance environment light in the light buffer.
        uint32_t environmentLightIndex = RTXDI_INVALID_LIGHT_INDEX;

        // Use image-based importance sampling for local lights
        bool enableLocalLightImportanceSampling = false;

        // Size of the smallest ReGIR cell, in world units.
        float regirCellSize = 1.f;

        // Scale of jitter applied to surface positions when sampling the ReGIR grid,
        // measured in grid cells. The value of 1.0 means plus or minus one grid cell.
        // This jitter scale is provided here because it affects both grid construction
        // (to determine effective cell radii) and sampling.
        float regirSamplingJitter = 1.0f;

        // Center of the ReGIR structure, in world space.
        float3 regirCenter{};
    };


    class Context
    {
    private:
        ContextParameters m_Params;
        
        uint32_t m_ReservoirBlockRowPitch = 0;
        uint32_t m_ReservoirArrayPitch = 0;

        uint32_t m_RegirCellOffset = 0;
        uint32_t m_OnionCells = 0;
        std::vector<RTXDI_OnionLayerGroup> m_OnionLayers;
        std::vector<RTXDI_OnionRing> m_OnionRings;
        float m_OnionCubicRootFactor = 0.f;
        float m_OnionLinearFactor = 0.f;

        void InitializeOnion();
        void ComputeOnionJitterCurve();

    public:
        Context(const ContextParameters& params);
        const ContextParameters& GetParameters() const;
        
        uint32_t GetRisBufferElementCount() const;
        uint32_t GetReservoirBufferElementCount() const;
        uint32_t GetReGIRLightSlotCount() const;

        void FillRuntimeParameters(
            RTXDI_RuntimeParameters& runtimeParams,
            const FrameParameters& frame) const;

        void FillNeighborOffsetBuffer(uint8_t* buffer) const;
    };

    void ComputePdfTextureSize(uint32_t maxItems, uint32_t& outWidth, uint32_t& outHeight, uint32_t& outMipLevels);
}
