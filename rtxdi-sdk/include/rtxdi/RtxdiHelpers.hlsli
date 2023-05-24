/***************************************************************************
 # Copyright (c) 2020-2023, NVIDIA CORPORATION.  All rights reserved.
 #
 # NVIDIA CORPORATION and its licensors retain all intellectual property
 # and proprietary rights in and to this software, related documentation
 # and any modifications thereto.  Any use, reproduction, disclosure or
 # distribution of this software and related documentation without an express
 # license agreement from NVIDIA CORPORATION is strictly prohibited.
 **************************************************************************/

#ifndef RTXDI_HELPERS_HLSLI
#define RTXDI_HELPERS_HLSLI

#include "RtxdiMath.hlsli"

bool RTXDI_IsActiveCheckerboardPixel(
    uint2 pixelPosition,
    bool previousFrame,
    RTXDI_ResamplingRuntimeParameters params)
{
    if (params.activeCheckerboardField == 0)
        return true;

    return ((pixelPosition.x + pixelPosition.y + int(previousFrame)) & 1) == (params.activeCheckerboardField & 1);
}

void RTXDI_ActivateCheckerboardPixel(inout int2 pixelPosition, bool previousFrame, RTXDI_ResamplingRuntimeParameters params)
{
    if (RTXDI_IsActiveCheckerboardPixel(pixelPosition, previousFrame, params))
        return;
    
    if (previousFrame)
        pixelPosition.x += int(params.activeCheckerboardField) * 2 - 3;
    else
        pixelPosition.x += (pixelPosition.y & 1) != 0 ? 1 : -1;
}

uint2 RTXDI_PixelPosToReservoirPos(uint2 pixelPosition, RTXDI_ResamplingRuntimeParameters params)
{
    if (params.activeCheckerboardField == 0)
        return pixelPosition;

    return uint2(pixelPosition.x >> 1, pixelPosition.y);
}

uint2 RTXDI_ReservoirPosToPixelPos(uint2 reservoirIndex, RTXDI_ResamplingRuntimeParameters params)
{
    if (params.activeCheckerboardField == 0)
        return reservoirIndex;

    uint2 pixelPosition = uint2(reservoirIndex.x << 1, reservoirIndex.y);
    pixelPosition.x += ((pixelPosition.y + params.activeCheckerboardField) & 1);
    return pixelPosition;
}

// Internal SDK function that permutes the pixels sampled from the previous frame.
void RTXDI_ApplyPermutationSampling(inout int2 prevPixelPos, uint uniformRandomNumber)
{
    int2 offset = int2(uniformRandomNumber & 3, (uniformRandomNumber >> 2) & 3);
    prevPixelPos += offset;
 
    prevPixelPos.x ^= 3;
    prevPixelPos.y ^= 3;
    
    prevPixelPos -= offset;
}

uint RTXDI_ReservoirPositionToPointer(
    RTXDI_ResamplingRuntimeParameters params,
    uint2 reservoirPosition,
    uint reservoirArrayIndex)
{
    uint2 blockIdx = reservoirPosition / RTXDI_RESERVOIR_BLOCK_SIZE;
    uint2 positionInBlock = reservoirPosition % RTXDI_RESERVOIR_BLOCK_SIZE;

    return reservoirArrayIndex * params.reservoirArrayPitch
        + blockIdx.y * params.reservoirBlockRowPitch
        + blockIdx.x * (RTXDI_RESERVOIR_BLOCK_SIZE * RTXDI_RESERVOIR_BLOCK_SIZE)
        + positionInBlock.y * RTXDI_RESERVOIR_BLOCK_SIZE
        + positionInBlock.x;
}

#if RTXDI_REGIR_MODE == RTXDI_REGIR_GRID

float RTXDI_ReGIR_GetJitterScale(RTXDI_RuntimeParameters params, float3 worldPos)
{
    return params.regirCommon.samplingJitter * params.regirCommon.cellSize;
}

int RTXDI_ReGIR_WorldPosToCellIndex(RTXDI_RuntimeParameters params, float3 worldPos)
{
    const float3 gridCenter = float3(params.regirCommon.centerX, params.regirCommon.centerY, params.regirCommon.centerZ);
    const int3 gridCellCount = int3(params.regirGrid.cellsX, params.regirGrid.cellsY, params.regirGrid.cellsZ);
    const float3 gridOrigin = gridCenter - float3(gridCellCount) * (params.regirCommon.cellSize * 0.5);
    
    int3 gridCell = int3(floor((worldPos - gridOrigin) / params.regirCommon.cellSize));

    if (gridCell.x < 0 || gridCell.y < 0 || gridCell.z < 0 ||
        gridCell.x >= gridCellCount.x || gridCell.y >= gridCellCount.y || gridCell.z >= gridCellCount.z)
        return -1;

    return gridCell.x + (gridCell.y + (gridCell.z * gridCellCount.y)) * gridCellCount.x;
}

bool RTXDI_ReGIR_CellIndexToWorldPos(RTXDI_RuntimeParameters params, int cellIndex, out float3 cellCenter, out float cellRadius)
{
    const float3 gridCenter = float3(params.regirCommon.centerX, params.regirCommon.centerY, params.regirCommon.centerZ);
    const int3 gridCellCount = int3(params.regirGrid.cellsX, params.regirGrid.cellsY, params.regirGrid.cellsZ);
    const float3 gridOrigin = gridCenter - float3(gridCellCount) * (params.regirCommon.cellSize * 0.5);

    uint3 cellPosition;
    cellPosition.x = cellIndex;
    cellPosition.y = cellPosition.x / params.regirGrid.cellsX;
    cellPosition.x %= params.regirGrid.cellsX;
    cellPosition.z = cellPosition.y / params.regirGrid.cellsY;
    cellPosition.y %= params.regirGrid.cellsY;
    if (cellPosition.z >= params.regirGrid.cellsZ)
    {
        cellCenter = float3(0.0, 0.0, 0.0);
        cellRadius = 0.0;
        return false;
    }

    cellCenter = (float3(cellPosition) + 0.5) * params.regirCommon.cellSize + gridOrigin;
    
    cellRadius = params.regirCommon.cellSize * sqrt(3.0);

    return true;
}

#elif RTXDI_REGIR_MODE == RTXDI_REGIR_ONION

float RTXDI_ReGIR_GetJitterScale(RTXDI_RuntimeParameters params, float3 worldPos)
{
    const float3 onionCenter = float3(params.regirCommon.centerX, params.regirCommon.centerY, params.regirCommon.centerZ);
    const float3 translatedPos = worldPos - onionCenter;

    float distanceToCenter = length(translatedPos) / params.regirCommon.cellSize;
    float jitterScale = max(1.0, max(
        pow(distanceToCenter, 1.0 / 3.0) * params.regirOnion.cubicRootFactor,
        distanceToCenter * params.regirOnion.linearFactor
    ));

    return jitterScale * params.regirCommon.samplingJitter * params.regirCommon.cellSize;
}

int RTXDI_ReGIR_WorldPosToCellIndex(RTXDI_RuntimeParameters params, float3 worldPos)
{
    const float3 onionCenter = float3(params.regirCommon.centerX, params.regirCommon.centerY, params.regirCommon.centerZ);
    const float3 translatedPos = worldPos - onionCenter;

    float r, azimuth, elevation;
    RTXDI_CartesianToSpherical(translatedPos, r, azimuth, elevation);
    azimuth += RTXDI_PI; // Add PI to make sure azimuth doesn't cross zero

    if (r <= params.regirOnion.layers[0].innerRadius)
        return 0;

    RTXDI_OnionLayerGroup layerGroup;

    int layerGroupIndex;
    for (layerGroupIndex = 0; layerGroupIndex < params.regirOnion.numLayerGroups; layerGroupIndex++)
    {
        if (r <= params.regirOnion.layers[layerGroupIndex].outerRadius)
        {
            layerGroup = params.regirOnion.layers[layerGroupIndex];
            break;
        }
    }

    if (layerGroupIndex >= params.regirOnion.numLayerGroups)
        return -1;

    uint layerIndex = uint(floor(max(0, log(r / layerGroup.innerRadius) * layerGroup.invLogLayerScale)));
    layerIndex = min(layerIndex, layerGroup.layerCount - 1); // Guard against numeric errors at the outer shell

    uint ringIndex = uint(floor(abs(elevation) * layerGroup.invEquatorialCellAngle + 0.5));
    RTXDI_OnionRing ring = params.regirOnion.rings[layerGroup.ringOffset + ringIndex];

    if ((layerIndex & 1) != 0)
    {
        azimuth -= ring.cellAngle * 0.5; // Add some variation to the repetitive layers
        if (azimuth < 0)
            azimuth += 2 * RTXDI_PI;
    }

    int cellIndex = int(floor(azimuth * ring.invCellAngle));

    int ringCellOffset = ring.cellOffset;
    if (elevation < 0 && ringIndex > 0)
        ringCellOffset += ring.cellCount;

    return int(cellIndex + ringCellOffset + layerIndex * layerGroup.cellsPerLayer + layerGroup.layerCellOffset);
}

bool RTXDI_ReGIR_CellIndexToWorldPos(RTXDI_RuntimeParameters params, int cellIndex, out float3 cellCenter, out float cellRadius)
{
    const float3 onionCenter = float3(params.regirCommon.centerX, params.regirCommon.centerY, params.regirCommon.centerZ);

    cellCenter = float3(0, 0, 0);
    cellRadius = 0;

    if (cellIndex < 0)
        return false;

    if (cellIndex == 0)
    {
        cellCenter = onionCenter;
        cellRadius = params.regirOnion.layers[0].innerRadius;
        return true;
    }

    RTXDI_OnionLayerGroup layerGroup;
    
    cellIndex -= 1;

    int layerGroupIndex;
    for (layerGroupIndex = 0; layerGroupIndex < params.regirOnion.numLayerGroups; layerGroupIndex++)
    {
        layerGroup = params.regirOnion.layers[layerGroupIndex];
        int cellsPerGroup = layerGroup.cellsPerLayer * layerGroup.layerCount;

        if (cellIndex < cellsPerGroup)
            break;

        cellIndex -= cellsPerGroup;
    }

    if (layerGroupIndex >= params.regirOnion.numLayerGroups)
        return false;

    int layerIndex = cellIndex / layerGroup.cellsPerLayer;
    cellIndex -= layerIndex * layerGroup.cellsPerLayer;

    RTXDI_OnionRing ring;

    int ringIndex;
    for (ringIndex = 0; ringIndex < layerGroup.ringCount; ringIndex++)
    {
        ring = params.regirOnion.rings[layerGroup.ringOffset + ringIndex];

        if (cellIndex < ring.cellOffset + ring.cellCount * (ringIndex > 0 ? 2 : 1))
            break;
    }

    if (ringIndex >= layerGroup.ringCount)
        return false; // shouldn't happen

    cellIndex -= ring.cellOffset;
    float elevation = float(ringIndex) * layerGroup.equatorialCellAngle;
    if (cellIndex >= ring.cellCount)
    {
        elevation = -elevation;
    }

    float azimuth = (float(cellIndex) + 0.5) * ring.cellAngle;

    if ((layerIndex & 1) != 0)
        azimuth += ring.cellAngle * 0.5; // Match the variation added in ...WorldPosToCellIndex()

    azimuth -= RTXDI_PI; // Reverse the PI addition in the position -> cell index translation

    float layerInnerRadius = layerGroup.innerRadius * pow(layerGroup.layerScale, layerIndex);
    float layerOuterRadius = layerInnerRadius * layerGroup.layerScale;

    float r = (layerInnerRadius + layerOuterRadius) * 0.5;

    cellCenter = RTXDI_SphericalToCartesian(r, azimuth, elevation);

    azimuth += ring.cellAngle * 0.5;
    elevation = (elevation == 0) 
        ? layerGroup.equatorialCellAngle * 0.5 
        : (abs(elevation) - layerGroup.equatorialCellAngle * 0.5) * sign(elevation);

    float3 cellCorner = RTXDI_SphericalToCartesian(layerOuterRadius, azimuth, elevation);

    cellRadius = length(cellCorner - cellCenter);

    cellCenter += onionCenter;

    return true;
}

#endif

#if RTXDI_REGIR_MODE != RTXDI_REGIR_DISABLED

float3 RTXDI_VisualizeReGIRCells(RTXDI_RuntimeParameters params, float3 worldPos)
{
    int cellIndex = RTXDI_ReGIR_WorldPosToCellIndex(params, worldPos);
    
    uint cellHash = RTXDI_JenkinsHash(cellIndex);

    float3 cellColor;
    cellColor.x = (cellHash & 0x7ff) / float(0x7ff);
    cellColor.y = ((cellHash >> 11) & 0x7ff) / float(0x7ff);
    cellColor.z = ((cellHash >> 22) & 0x3ff) / float(0x3ff);

    float3 cellCenter;
    float cellRadius;
    bool cellFound = RTXDI_ReGIR_CellIndexToWorldPos(params, cellIndex, cellCenter, cellRadius);

    float distanceToCenter = cellFound ? saturate(length(cellCenter - worldPos) / cellRadius) : 0;

    return distanceToCenter * cellColor;
}
#endif

#ifdef RTXDI_ENABLE_BOILING_FILTER
// RTXDI_BOILING_FILTER_GROUP_SIZE must be defined - 16 is a reasonable value
#define RTXDI_BOILING_FILTER_MIN_LANE_COUNT 32

groupshared float s_weights[(RTXDI_BOILING_FILTER_GROUP_SIZE * RTXDI_BOILING_FILTER_GROUP_SIZE + RTXDI_BOILING_FILTER_MIN_LANE_COUNT - 1) / RTXDI_BOILING_FILTER_MIN_LANE_COUNT];
groupshared uint s_count[(RTXDI_BOILING_FILTER_GROUP_SIZE * RTXDI_BOILING_FILTER_GROUP_SIZE + RTXDI_BOILING_FILTER_MIN_LANE_COUNT - 1) / RTXDI_BOILING_FILTER_MIN_LANE_COUNT];

bool RTXDI_BoilingFilterInternal(
    uint2 LocalIndex,
    float filterStrength, // (0..1]
    float reservoirWeight)
{
    // Boiling happens when some highly unlikely light is discovered and it is relevant
    // for a large surface area around the pixel that discovered it. Then this light sample
    // starts to propagate to the neighborhood through spatiotemporal reuse, which looks like
    // a flash. We can detect such lights because their weight is significantly higher than 
    // the weight of their neighbors. So, compute the average group weight and apply a threshold.

    float boilingFilterMultiplier = 10.f / clamp(filterStrength, 1e-6, 1.0) - 9.f;

    // Start with average nonzero weight within the wavefront
    float waveWeight = WaveActiveSum(reservoirWeight);
    uint waveCount = WaveActiveCountBits(reservoirWeight > 0);

    // Store the results of each wavefront into shared memory
    uint linearThreadIndex = LocalIndex.x + LocalIndex.y * RTXDI_BOILING_FILTER_GROUP_SIZE;
    uint waveIndex = linearThreadIndex / WaveGetLaneCount();

    if (WaveIsFirstLane())
    {
        s_weights[waveIndex] = waveWeight;
        s_count[waveIndex] = waveCount;
    }

    GroupMemoryBarrierWithGroupSync();

    // Reduce the per-wavefront averages into a global average using one wavefront
    if (linearThreadIndex < (RTXDI_BOILING_FILTER_GROUP_SIZE * RTXDI_BOILING_FILTER_GROUP_SIZE + WaveGetLaneCount() - 1) / WaveGetLaneCount())
    {
        waveWeight = s_weights[linearThreadIndex];
        waveCount = s_count[linearThreadIndex];

        waveWeight = WaveActiveSum(waveWeight);
        waveCount = WaveActiveSum(waveCount);

        if (linearThreadIndex == 0)
        {
            s_weights[0] = (waveCount > 0) ? (waveWeight / float(waveCount)) : 0.0;
        }
    }

    GroupMemoryBarrierWithGroupSync();

    // Read the per-group average and apply the threshold
    float averageNonzeroWeight = s_weights[0];
    if (reservoirWeight > averageNonzeroWeight * boilingFilterMultiplier)
    {
        return true;
    }

    return false;
}

#endif // RTXDI_ENABLE_BOILING_FILTER

#endif // RTXDI_HELPERS_HLSLI