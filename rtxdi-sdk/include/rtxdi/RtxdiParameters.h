/***************************************************************************
 # Copyright (c) 2020-2022, NVIDIA CORPORATION.  All rights reserved.
 #
 # NVIDIA CORPORATION and its licensors retain all intellectual property
 # and proprietary rights in and to this software, related documentation
 # and any modifications thereto.  Any use, reproduction, disclosure or
 # distribution of this software and related documentation without an express
 # license agreement from NVIDIA CORPORATION is strictly prohibited.
 **************************************************************************/

#ifndef RTXDI_PARAMETERS_H
#define RTXDI_PARAMETERS_H

#include "RtxdiTypes.h"

// Flag that is used in the RIS buffer to identify that a light is 
// stored in a compact form.
#define RTXDI_LIGHT_COMPACT_BIT 0x80000000u

// Light index mask for the RIS buffer.
#define RTXDI_LIGHT_INDEX_MASK 0x7fffffff

// Reservoirs are stored in a structured buffer in a block-linear layout.
// This constant defines the size of that block, measured in pixels.
#define RTXDI_RESERVOIR_BLOCK_SIZE 16

// Bias correction modes for temporal and spatial resampling:
// Use (1/M) normalization, which is very biased but also very fast.
#define RTXDI_BIAS_CORRECTION_OFF 0
// Use MIS-like normalization but assume that every sample is visible.
#define RTXDI_BIAS_CORRECTION_BASIC 1
// Use pairwise MIS normalization (assuming every sample is visible).  Better perf & specular quality
#define RTXDI_BIAS_CORRECTION_PAIRWISE 2
// Use MIS-like normalization with visibility rays. Unbiased.
#define RTXDI_BIAS_CORRECTION_RAY_TRACED 3


#define RTXDI_ONION_MAX_LAYER_GROUPS 8
#define RTXDI_ONION_MAX_RINGS 52

#define RTXDI_REGIR_DISABLED 0
#define RTXDI_REGIR_GRID 1
#define RTXDI_REGIR_ONION 2

#ifndef RTXDI_REGIR_MODE
#define RTXDI_REGIR_MODE RTXDI_REGIR_DISABLED
#endif 

#define RTXDI_INVALID_LIGHT_INDEX (0xffffffffu)

#ifndef __cplusplus
static const uint RTXDI_InvalidLightIndex = RTXDI_INVALID_LIGHT_INDEX;
#endif

struct RTXDI_OnionLayerGroup
{
    float innerRadius;
    float outerRadius;
    float invLogLayerScale;
    int layerCount;

    float invEquatorialCellAngle;
    int cellsPerLayer;
    int ringOffset;
    int ringCount;

    float equatorialCellAngle;
    float layerScale;
    int layerCellOffset;
    int pad;
};

struct RTXDI_OnionRing
{
    float cellAngle;
    float invCellAngle;
    int cellOffset;
    int cellCount;
};

struct RTXDI_ReGIRCommonParameters
{
    uint32_t enable;
    float centerX;
    float centerY;
    float centerZ;

    uint32_t risBufferOffset;
    uint32_t lightsPerCell;
    float cellSize;
    float samplingJitter;
};

struct RTXDI_ReGIRGridParameters
{
    uint32_t cellsX;
    uint32_t cellsY;
    uint32_t cellsZ;
    uint32_t pad;
};

struct RTXDI_ReGIROnionParameters
{
    RTXDI_OnionLayerGroup layers[RTXDI_ONION_MAX_LAYER_GROUPS];
    RTXDI_OnionRing rings[RTXDI_ONION_MAX_RINGS];

    uint32_t numLayerGroups;
    float cubicRootFactor;
    float linearFactor;
    float pad;
};

struct RTXDI_ResamplingRuntimeParameters
{
    uint32_t firstLocalLight;
    uint32_t numLocalLights;
    uint32_t firstInfiniteLight;
    uint32_t numInfiniteLights;

    uint32_t environmentLightPresent;
    uint32_t environmentLightIndex;
    uint32_t tileSize;
    uint32_t tileCount;

    uint32_t activeCheckerboardField; // 0 - no checkerboard, 1 - odd pixels, 2 - even pixels
    uint32_t enableLocalLightImportanceSampling;
    uint32_t reservoirBlockRowPitch;
    uint32_t reservoirArrayPitch;

    uint32_t environmentRisBufferOffset;
    uint32_t environmentTileSize;
    uint32_t environmentTileCount;
    uint32_t neighborOffsetMask;

    uint32_t uniformRandomNumber;
    uint32_t pad1;
    uint32_t pad2;
    uint32_t pad3;

    RTXDI_ReGIRCommonParameters regirCommon;
    RTXDI_ReGIRGridParameters regirGrid;
    RTXDI_ReGIROnionParameters regirOnion;
};

struct RTXDI_PackedReservoir
{
    uint32_t lightData;
    uint32_t uvData;
    uint32_t mVisibility;
    uint32_t distanceAge;
    float targetPdf;
    float weight;
};

#endif // RTXDI_PARAMETERS_H