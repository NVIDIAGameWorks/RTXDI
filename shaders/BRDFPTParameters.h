/***************************************************************************
 # Copyright (c) 2020-2023, NVIDIA CORPORATION.  All rights reserved.
 #
 # NVIDIA CORPORATION and its licensors retain all intellectual property
 # and proprietary rights in and to this software, related documentation
 # and any modifications thereto.  Any use, reproduction, disclosure or
 # distribution of this software and related documentation without an express
 # license agreement from NVIDIA CORPORATION is strictly prohibited.
 **************************************************************************/

#ifndef RTXDI_BRDFPT_PARAMETERS_H
#define RTXDI_BRDFPT_PARAMETERS_H

#include <rtxdi/ReSTIRDIParameters.h>

struct BRDFPathTracing_MaterialOverrideParameters
{
    float roughnessOverride;
    float metalnessOverride;
    float minSecondaryRoughness;
    uint32_t pad1;
};

// Parameters for running ReSTIR DI on the secondary surface
// Spatial resampling is only enabled if the secondary hit is in the gbuffer (determined via projection)
struct BRDFPathTracing_SecondarySurfaceReSTIRDIParameters
{
    ReSTIRDI_InitialSamplingParameters initialSamplingParams;
    ReSTIRDI_SpatialResamplingParameters spatialResamplingParams;
};

struct BRDFPathTracing_Parameters
{
    uint32_t enableIndirectEmissiveSurfaces;
    uint32_t enableSecondaryResampling;
    uint32_t enableReSTIRGI;
    uint32_t pad1;

    BRDFPathTracing_MaterialOverrideParameters materialOverrideParams;
    BRDFPathTracing_SecondarySurfaceReSTIRDIParameters secondarySurfaceReSTIRDIParams;
};

#endif // RTXDI_BRDFPT_PARAMETERS_H