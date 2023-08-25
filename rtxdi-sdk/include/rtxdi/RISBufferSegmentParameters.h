/***************************************************************************
 # Copyright (c) 2020-2023, NVIDIA CORPORATION.  All rights reserved.
 #
 # NVIDIA CORPORATION and its licensors retain all intellectual property
 # and proprietary rights in and to this software, related documentation
 # and any modifications thereto.  Any use, reproduction, disclosure or
 # distribution of this software and related documentation without an express
 # license agreement from NVIDIA CORPORATION is strictly prohibited.
 **************************************************************************/

#ifndef RTXDI_RIS_BUFFER_SEGMENT_PARAMETERS
#define RTXDI_RIS_BUFFER_SEGMENT_PARAMETERS

#include "RtxdiTypes.h"

struct RTXDI_RISBufferSegmentParameters
{
    uint32_t bufferOffset;
    uint32_t tileSize;
    uint32_t tileCount;
    uint32_t pad1;
};

#endif // RTXDI_RIS_BUFFER_SEGMENT_PARAMETERS