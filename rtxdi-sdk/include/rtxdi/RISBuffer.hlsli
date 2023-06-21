/***************************************************************************
 # Copyright (c) 2020-2023, NVIDIA CORPORATION.  All rights reserved.
 #
 # NVIDIA CORPORATION and its licensors retain all intellectual property
 # and proprietary rights in and to this software, related documentation
 # and any modifications thereto.  Any use, reproduction, disclosure or
 # distribution of this software and related documentation without an express
 # license agreement from NVIDIA CORPORATION is strictly prohibited.
 **************************************************************************/

#ifndef RTXDI_RIS_BUFFER_HLSLI
#define RTXDI_RIS_BUFFER_HLSLI

struct RTXDI_RISTileInfo
{
    uint risTileOffset;
    uint risTileSize;
};

void RTXDI_RandomlySelectLightDataFromRISTile(
    inout RAB_RandomSamplerState rng,
    RTXDI_RISTileInfo bufferInfo,
    out uint2 tileData,
    out uint risBufferPtr)
{
    float rnd = RAB_GetNextRandom(rng);
    uint risSample = min(uint(floor(rnd * bufferInfo.risTileSize)), bufferInfo.risTileSize - 1);
    risBufferPtr = risSample + bufferInfo.risTileOffset;
    tileData = RTXDI_RIS_BUFFER[risBufferPtr];
}

RTXDI_RISTileInfo RTXDI_RandomlySelectRISTile(
    inout RAB_RandomSamplerState coherentRng,
    RTXDI_RISBufferSegmentParameters params)
{
    RTXDI_RISTileInfo risTileInfo;
    float tileRnd = RAB_GetNextRandom(coherentRng);
    uint tileIndex = uint(tileRnd * params.tileCount);
    risTileInfo.risTileOffset = tileIndex * params.tileSize + params.bufferOffset;
    risTileInfo.risTileSize = params.tileSize;
    return risTileInfo;
}

#endif // RTXDI_RIS_BUFFER_HLSLI