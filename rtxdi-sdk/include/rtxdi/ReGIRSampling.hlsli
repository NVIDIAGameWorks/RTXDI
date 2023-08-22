/***************************************************************************
 # Copyright (c) 2020-2023, NVIDIA CORPORATION.  All rights reserved.
 #
 # NVIDIA CORPORATION and its licensors retain all intellectual property
 # and proprietary rights in and to this software, related documentation
 # and any modifications thereto.  Any use, reproduction, disclosure or
 # distribution of this software and related documentation without an express
 # license agreement from NVIDIA CORPORATION is strictly prohibited.
 **************************************************************************/

#ifndef RTXDI_REGIR_SAMPLING_FUNCTIONS_HLSLI
#define RTXDI_REGIR_SAMPLING_FUNCTIONS_HLSLI

#if RTXDI_REGIR_MODE == RTXDI_REGIR_GRID

float RTXDI_ReGIR_GetJitterScale(ReGIR_Parameters params, float3 worldPos)
{
    return params.commonParams.samplingJitter * params.commonParams.cellSize;
}

int RTXDI_ReGIR_WorldPosToCellIndex(ReGIR_Parameters params, float3 worldPos)
{
    const float3 gridCenter = float3(params.commonParams.centerX, params.commonParams.centerY, params.commonParams.centerZ);
    const int3 gridCellCount = int3(params.gridParams.cellsX, params.gridParams.cellsY, params.gridParams.cellsZ);
    const float3 gridOrigin = gridCenter - float3(gridCellCount) * (params.commonParams.cellSize * 0.5);
    
    int3 gridCell = int3(floor((worldPos - gridOrigin) / params.commonParams.cellSize));

    if (gridCell.x < 0 || gridCell.y < 0 || gridCell.z < 0 ||
        gridCell.x >= gridCellCount.x || gridCell.y >= gridCellCount.y || gridCell.z >= gridCellCount.z)
        return -1;

    return gridCell.x + (gridCell.y + (gridCell.z * gridCellCount.y)) * gridCellCount.x;
}

bool RTXDI_ReGIR_CellIndexToWorldPos(ReGIR_Parameters params, int cellIndex, out float3 cellCenter, out float cellRadius)
{
    const float3 gridCenter = float3(params.commonParams.centerX, params.commonParams.centerY, params.commonParams.centerZ);
    const int3 gridCellCount = int3(params.gridParams.cellsX, params.gridParams.cellsY, params.gridParams.cellsZ);
    const float3 gridOrigin = gridCenter - float3(gridCellCount) * (params.commonParams.cellSize * 0.5);

    uint3 cellPosition;
    cellPosition.x = cellIndex;
    cellPosition.y = cellPosition.x / params.gridParams.cellsX;
    cellPosition.x %= params.gridParams.cellsX;
    cellPosition.z = cellPosition.y / params.gridParams.cellsY;
    cellPosition.y %= params.gridParams.cellsY;
    if (cellPosition.z >= params.gridParams.cellsZ)
    {
        cellCenter = float3(0.0, 0.0, 0.0);
        cellRadius = 0.0;
        return false;
    }

    cellCenter = (float3(cellPosition) + 0.5) * params.commonParams.cellSize + gridOrigin;
    
    cellRadius = params.commonParams.cellSize * sqrt(3.0);

    return true;
}

#elif RTXDI_REGIR_MODE == RTXDI_REGIR_ONION

float RTXDI_ReGIR_GetJitterScale(ReGIR_Parameters params, float3 worldPos)
{
    const float3 onionCenter = float3(params.commonParams.centerX, params.commonParams.centerY, params.commonParams.centerZ);
    const float3 translatedPos = worldPos - onionCenter;

    float distanceToCenter = length(translatedPos) / params.commonParams.cellSize;
    float jitterScale = max(1.0, max(
        pow(distanceToCenter, 1.0 / 3.0) * params.onionParams.cubicRootFactor,
        distanceToCenter * params.onionParams.linearFactor
    ));

    return jitterScale * params.commonParams.samplingJitter * params.commonParams.cellSize;
}

int RTXDI_ReGIR_WorldPosToCellIndex(ReGIR_Parameters params, float3 worldPos)
{
    const float3 onionCenter = float3(params.commonParams.centerX, params.commonParams.centerY, params.commonParams.centerZ);
    const float3 translatedPos = worldPos - onionCenter;

    float r, azimuth, elevation;
    RTXDI_CartesianToSpherical(translatedPos, r, azimuth, elevation);
    azimuth += RTXDI_PI; // Add PI to make sure azimuth doesn't cross zero

    if (r <= params.onionParams.layers[0].innerRadius)
        return 0;

    ReGIR_OnionLayerGroup layerGroup;

    int layerGroupIndex;
    for (layerGroupIndex = 0; layerGroupIndex < params.onionParams.numLayerGroups; layerGroupIndex++)
    {
        if (r <= params.onionParams.layers[layerGroupIndex].outerRadius)
        {
            layerGroup = params.onionParams.layers[layerGroupIndex];
            break;
        }
    }

    if (layerGroupIndex >= params.onionParams.numLayerGroups)
        return -1;

    uint layerIndex = uint(floor(max(0, log(r / layerGroup.innerRadius) * layerGroup.invLogLayerScale)));
    layerIndex = min(layerIndex, layerGroup.layerCount - 1); // Guard against numeric errors at the outer shell

    uint ringIndex = uint(floor(abs(elevation) * layerGroup.invEquatorialCellAngle + 0.5));
    ReGIR_OnionRing ring = params.onionParams.rings[layerGroup.ringOffset + ringIndex];

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

bool RTXDI_ReGIR_CellIndexToWorldPos(ReGIR_Parameters params, int cellIndex, out float3 cellCenter, out float cellRadius)
{
    const float3 onionCenter = float3(params.commonParams.centerX, params.commonParams.centerY, params.commonParams.centerZ);

    cellCenter = float3(0, 0, 0);
    cellRadius = 0;

    if (cellIndex < 0)
        return false;

    if (cellIndex == 0)
    {
        cellCenter = onionCenter;
        cellRadius = params.onionParams.layers[0].innerRadius;
        return true;
    }

    ReGIR_OnionLayerGroup layerGroup;
    
    cellIndex -= 1;

    int layerGroupIndex;
    for (layerGroupIndex = 0; layerGroupIndex < params.onionParams.numLayerGroups; layerGroupIndex++)
    {
        layerGroup = params.onionParams.layers[layerGroupIndex];
        int cellsPerGroup = layerGroup.cellsPerLayer * layerGroup.layerCount;

        if (cellIndex < cellsPerGroup)
            break;

        cellIndex -= cellsPerGroup;
    }

    if (layerGroupIndex >= params.onionParams.numLayerGroups)
        return false;

    int layerIndex = cellIndex / layerGroup.cellsPerLayer;
    cellIndex -= layerIndex * layerGroup.cellsPerLayer;

    ReGIR_OnionRing ring;

    int ringIndex;
    for (ringIndex = 0; ringIndex < layerGroup.ringCount; ringIndex++)
    {
        ring = params.onionParams.rings[layerGroup.ringOffset + ringIndex];

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

float3 RTXDI_VisualizeReGIRCells(ReGIR_Parameters params, float3 worldPos)
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

#endif // RTXDI_REGIR_SAMPLING_FUNCTIONS_HLSLI