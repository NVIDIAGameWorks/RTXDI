/***************************************************************************
 # Copyright (c) 2020-2023, NVIDIA CORPORATION.  All rights reserved.
 #
 # NVIDIA CORPORATION and its licensors retain all intellectual property
 # and proprietary rights in and to this software, related documentation
 # and any modifications thereto.  Any use, reproduction, disclosure or
 # distribution of this software and related documentation without an express
 # license agreement from NVIDIA CORPORATION is strictly prohibited.
 **************************************************************************/

#ifndef RTXDI_PARAMETERS_H
#define RTXDI_PARAMETERS_H

 // Bias correction modes for temporal and spatial resampling:
 // Use (1/M) normalization, which is very biased but also very fast.
#define RTXDI_BIAS_CORRECTION_OFF 0
// Use MIS-like normalization but assume that every sample is visible.
#define RTXDI_BIAS_CORRECTION_BASIC 1
// Use MIS-like normalization with visibility rays. Unbiased.
#define RTXDI_BIAS_CORRECTION_RAY_TRACED 2
// Use pairwise MIS.
#define RTXDI_BIAS_CORRECTION_PAIRWISE 3
// Use pairwise MIS with visibility rays. Unbiased.
#define RTXDI_BIAS_CORRECTION_PAIRWISE_RAY_TRACED 4

#define RTXDI_RESERVOIR_BLOCK_SIZE 16

#define RTXDI_GRAD_FACTOR           3

struct RTXDI_PackedReservoir
{
  uint4 data0;
  uint4 data1;
};

struct ReSTIRGI_PackedReservoir
{
  uint4 hitGeometry;              ///< Hit point's position and normal.
  uint4 lightInfo;                ///< Reservoir information.
};

struct VolumeReSTIR_PackedReservoir
{
  uint4 data0;
};

#endif // RTXDI_PARAMETERS_H