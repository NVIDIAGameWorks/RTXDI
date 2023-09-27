/***************************************************************************
 # Copyright (c) 2020-2023, NVIDIA CORPORATION.  All rights reserved.
 #
 # NVIDIA CORPORATION and its licensors retain all intellectual property
 # and proprietary rights in and to this software, related documentation
 # and any modifications thereto.  Any use, reproduction, disclosure or
 # distribution of this software and related documentation without an express
 # license agreement from NVIDIA CORPORATION is strictly prohibited.
 **************************************************************************/

#include "rtxdi/ReGIR.h"

#include <algorithm>
#include <cassert>
#include <numeric>

#include "rtxdi/RtxdiParameters.h"
#include "rtxdi/RISBufferSegmentAllocator.h"

namespace
{
    constexpr float c_pi = 3.1415926535f;
}

namespace rtxdi
{

    ReGIRContext::ReGIRContext(const ReGIRStaticParameters& params, RISBufferSegmentAllocator& risBufferSegmentAllocator) :
        m_regirCellOffset(0),
        m_regirStaticParameters(params)
    {
        ComputeGridLightSlotCount();
        InitializeOnion(params);
        ComputeOnionJitterCurve();
        AllocateRISBufferSegment(risBufferSegmentAllocator);
    }

    void ReGIRContext::ComputeGridLightSlotCount()
    {
        m_regirGridCalculatedParameters.lightSlotCount = m_regirStaticParameters.gridParameters.GridSize.x
            * m_regirStaticParameters.gridParameters.GridSize.y
            * m_regirStaticParameters.gridParameters.GridSize.z
            * m_regirStaticParameters.LightsPerCell;
    }

    void ReGIRContext::AllocateRISBufferSegment(RISBufferSegmentAllocator& risBufferSegmentAllocator)
    {
        switch (m_regirStaticParameters.Mode)
        {
        default:
        case ReGIRMode::Disabled:
            m_regirCellOffset = 0;
            break;
        case ReGIRMode::Grid:
            m_regirCellOffset = risBufferSegmentAllocator.allocateSegment(m_regirGridCalculatedParameters.lightSlotCount);
            break;
        case ReGIRMode::Onion:
            m_regirCellOffset = risBufferSegmentAllocator.allocateSegment(m_regirOnionCalculatedParameters.lightSlotCount);
        }
    }

    void ReGIRContext::InitializeOnion(const ReGIRStaticParameters& params)
    {
        int numLayerGroups = std::max(1, std::min(RTXDI_ONION_MAX_LAYER_GROUPS, int(params.onionParameters.OnionDetailLayers)));

        float innerRadius = 1.f;

        int totalLayers = 0;
        int totalCells = 1;

        for (int layerGroupIndex = 0; layerGroupIndex < numLayerGroups; layerGroupIndex++)
        {
            const int partitions = layerGroupIndex * 4 + 8;
            const int layerCount = (layerGroupIndex < numLayerGroups - 1) ? 1 : int(params.onionParameters.OnionCoverageLayers) + 1;

            const float radiusRatio = (float(partitions) + c_pi) / (float(partitions) - c_pi);
            const float outerRadius = innerRadius * powf(radiusRatio, float(layerCount));
            const float equatorialAngle = 2 * c_pi / float(partitions);

            ReGIR_OnionLayerGroup layerGroup{};
            layerGroup.ringOffset = int(m_regirOnionCalculatedParameters.regirOnionRings.size());
            layerGroup.innerRadius = innerRadius;
            layerGroup.outerRadius = outerRadius;
            layerGroup.invLogLayerScale = 1.f / logf(radiusRatio);
            layerGroup.invEquatorialCellAngle = 1.f / equatorialAngle;
            layerGroup.equatorialCellAngle = equatorialAngle;
            layerGroup.ringCount = partitions / 4 + 1;
            layerGroup.layerScale = radiusRatio;
            layerGroup.layerCellOffset = totalCells;

            ReGIR_OnionRing ring{};
            ring.cellCount = partitions;
            ring.cellOffset = 0;
            ring.invCellAngle = float(partitions) / (2 * c_pi);
            ring.cellAngle = 1.f / ring.invCellAngle;
            m_regirOnionCalculatedParameters.regirOnionRings.push_back(ring);

            int cellsPerLayer = partitions;
            for (int ringIndex = 1; ringIndex < layerGroup.ringCount; ringIndex++)
            {
                ring.cellCount = std::max(1, int(floorf(float(partitions) * cosf(float(ringIndex) * equatorialAngle))));
                ring.cellOffset = cellsPerLayer;
                ring.invCellAngle = float(ring.cellCount) / (2 * c_pi);
                ring.cellAngle = 1.f / ring.invCellAngle;
                m_regirOnionCalculatedParameters.regirOnionRings.push_back(ring);

                cellsPerLayer += ring.cellCount * 2;
            }

            layerGroup.cellsPerLayer = cellsPerLayer;
            layerGroup.layerCount = layerCount;
            m_regirOnionCalculatedParameters.regirOnionLayers.push_back(layerGroup);

            innerRadius = outerRadius;

            totalCells += cellsPerLayer * layerCount;
            totalLayers += layerCount;
        }

        m_regirOnionCalculatedParameters.regirOnionCells = totalCells;
        m_regirOnionCalculatedParameters.lightSlotCount = m_regirOnionCalculatedParameters.regirOnionCells * m_regirStaticParameters.LightsPerCell;
    }

    static float3 SphericalToCartesian(const float radius, const float azimuth, const float elevation)
    {
        return float3{
            radius * cosf(azimuth) * cosf(elevation),
            radius * sinf(elevation),
            radius * sinf(azimuth) * cosf(elevation)
        };
    }

    static float Distance(const float3& a, const float3& b)
    {
        float3 d{ a.x - b.x, a.y - b.y, a.z - b.z };
        return sqrtf(d.x * d.x + d.y * d.y + d.z * d.z);
    }

    void ReGIRContext::ComputeOnionJitterCurve()
    {
        std::vector<float> cubicRootFactors;
        std::vector<float> linearFactors;

        int layerGroupIndex = 0;
        for (const auto& layerGroup : m_regirOnionCalculatedParameters.regirOnionLayers)
        {
            for (int layerIndex = 0; layerIndex < layerGroup.layerCount; layerIndex++)
            {
                const float innerRadius = layerGroup.innerRadius * powf(layerGroup.layerScale, float(layerIndex));
                const float outerRadius = innerRadius * layerGroup.layerScale;
                const float middleRadius = (innerRadius + outerRadius) * 0.5f;
                float maxCellRadius = 0.f;

                for (int ringIndex = 0; ringIndex < layerGroup.ringCount; ringIndex++)
                {
                    const auto& ring = m_regirOnionCalculatedParameters.regirOnionRings[layerGroup.ringOffset + ringIndex];

                    const float middleElevation = layerGroup.equatorialCellAngle * float(ringIndex);
                    const float vertexElevation = (ringIndex == 0)
                        ? layerGroup.equatorialCellAngle * 0.5f
                        : middleElevation - layerGroup.equatorialCellAngle * 0.5f;

                    const float middleAzimuth = 0.f;
                    const float vertexAzimuth = ring.cellAngle;

                    const float3 middlePoint = SphericalToCartesian(middleRadius, middleAzimuth, middleElevation);
                    const float3 vertexPoint = SphericalToCartesian(outerRadius, vertexAzimuth, vertexElevation);

                    const float cellRadius = Distance(middlePoint, vertexPoint);

                    maxCellRadius = std::max(maxCellRadius, cellRadius);
                }

#if PRINT_JITTER_CURVE
                char buf[256];
                sprintf_s(buf, "%.3f,%.3f\n", middleRadius, maxCellRadius);
                OutputDebugStringA(buf);
#endif

                if (layerGroupIndex < int(m_regirOnionCalculatedParameters.regirOnionLayers.size()) - 1)
                {
                    float cubicRootFactor = maxCellRadius * powf(middleRadius, -1.f / 3.f);
                    cubicRootFactors.push_back(cubicRootFactor);
                }
                else
                {
                    float linearFactor = maxCellRadius / middleRadius;
                    linearFactors.push_back(linearFactor);
                }
            }

            layerGroupIndex++;
        }

        // Compute the median of the cubic root factors, there are some outliers in the curve
        if (!cubicRootFactors.empty())
        {
            std::sort(cubicRootFactors.begin(), cubicRootFactors.end());
            m_regirOnionCalculatedParameters.regirOnionCubicRootFactor = cubicRootFactors[cubicRootFactors.size() / 2];
        }
        else
        {
            m_regirOnionCalculatedParameters.regirOnionCubicRootFactor = 0.f;
        }

        // Compute the average of the linear factors, they're all the same anyway
        float sumOfLinearFactors = std::accumulate(linearFactors.begin(), linearFactors.end(), 0.f);
        m_regirOnionCalculatedParameters.regirOnionLinearFactor = sumOfLinearFactors / std::max(float(linearFactors.size()), 1.f);
    }
  
    ReGIRGridCalculatedParameters rtxdi::ReGIRContext::getReGIRGridCalculatedParameters() const
    {
        return m_regirGridCalculatedParameters;
    }

    ReGIROnionCalculatedParameters rtxdi::ReGIRContext::getReGIROnionCalculatedParameters() const
    {
        return m_regirOnionCalculatedParameters;
    }

    uint32_t rtxdi::ReGIRContext::getReGIRCellOffset() const
    {
        return m_regirCellOffset;
    }

    uint32_t rtxdi::ReGIRContext::getReGIRLightSlotCount() const
    {
        switch (m_regirStaticParameters.Mode)
        {
        case ReGIRMode::Grid:
            return m_regirGridCalculatedParameters.lightSlotCount;
            break;
        case ReGIRMode::Onion:
            return m_regirOnionCalculatedParameters.lightSlotCount;
            break;
        default:
        case ReGIRMode::Disabled:
            return 0;
            break;
        }
    }

    ReGIRDynamicParameters ReGIRContext::getReGIRDynamicParameters() const
    {
        return m_regirDynamicParameters;
    }

    ReGIRStaticParameters ReGIRContext::getReGIRStaticParameters() const
    {
        return m_regirStaticParameters;
    }

    void ReGIRContext::setDynamicParameters(const ReGIRDynamicParameters& regirDynamicParameters)
    {
        m_regirDynamicParameters = regirDynamicParameters;
    }

    bool ReGIRContext::isLocalLightPowerRISEnable() const
    {
        return (m_regirDynamicParameters.presamplingMode == LocalLightReGIRPresamplingMode::Power_RIS) ||
               (m_regirDynamicParameters.fallbackSamplingMode == LocalLightReGIRFallbackSamplingMode::Power_RIS);
    }

}