/***************************************************************************
 # Copyright (c) 2020-2023, NVIDIA CORPORATION.  All rights reserved.
 #
 # NVIDIA CORPORATION and its licensors retain all intellectual property
 # and proprietary rights in and to this software, related documentation
 # and any modifications thereto.  Any use, reproduction, disclosure or
 # distribution of this software and related documentation without an express
 # license agreement from NVIDIA CORPORATION is strictly prohibited.
 **************************************************************************/

#ifndef RTXDI_REGIR_PARAMETERS_H
#define RTXDI_REGIR_PARAMETERS_H

#include "RtxdiTypes.h"

#define RTXDI_ONION_MAX_LAYER_GROUPS 8
#define RTXDI_ONION_MAX_RINGS 52

#define RTXDI_REGIR_DISABLED 0
#define RTXDI_REGIR_GRID 1
#define RTXDI_REGIR_ONION 2

#ifndef RTXDI_REGIR_MODE
#define RTXDI_REGIR_MODE RTXDI_REGIR_DISABLED
#endif 

struct ReGIR_OnionLayerGroup
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
    int pad1;
};

struct ReGIR_OnionRing
{
    float cellAngle;
    float invCellAngle;
    int cellOffset;
    int cellCount;
};

#define REGIR_LOCAL_LIGHT_PRESAMPLING_MODE_UNIFORM 0
#define REGIR_LOCAL_LIGHT_PRESAMPLING_MODE_POWER_RIS 1

#define REGIR_LOCAL_LIGHT_FALLBACK_MODE_UNIFORM 0
#define REGIR_LOCAL_LIGHT_FALLBACK_MODE_POWER_RIS 1

struct ReGIR_CommonParameters
{
    uint32_t localLightSamplingFallbackMode;
    float centerX;
    float centerY;
    float centerZ;

    uint32_t risBufferOffset;
    uint32_t lightsPerCell;
    float cellSize;
    float samplingJitter;

    uint32_t localLightPresamplingMode;
    uint32_t numRegirBuildSamples; // PresampleReGIR.hlsl -> RTXDI_PresampleLocalLightsForReGIR
    uint32_t pad1;
    uint32_t pad2;
};

struct ReGIR_GridParameters
{
    uint32_t cellsX;
    uint32_t cellsY;
    uint32_t cellsZ;
    uint32_t pad1;
};

struct ReGIR_OnionParameters
{
    ReGIR_OnionLayerGroup layers[RTXDI_ONION_MAX_LAYER_GROUPS];
    ReGIR_OnionRing rings[RTXDI_ONION_MAX_RINGS];

    uint32_t numLayerGroups;
    float cubicRootFactor;
    float linearFactor;
    float pad1;
};

struct ReGIR_Parameters
{
    ReGIR_CommonParameters commonParams;
    ReGIR_GridParameters gridParams;
    ReGIR_OnionParameters onionParams;
};

#endif // RTXDI_REGIR_PARAMETERS_H