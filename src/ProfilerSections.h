/***************************************************************************
 # Copyright (c) 2021-2023, NVIDIA CORPORATION.  All rights reserved.
 #
 # NVIDIA CORPORATION and its licensors retain all intellectual property
 # and proprietary rights in and to this software, related documentation
 # and any modifications thereto.  Any use, reproduction, disclosure or
 # distribution of this software and related documentation without an express
 # license agreement from NVIDIA CORPORATION is strictly prohibited.
 **************************************************************************/

#pragma once

struct ProfilerSection
{
    enum Enum
    {
        TlasUpdate,
        EnvironmentMap,
        GBufferFill,
        MeshProcessing,
        LocalLightPdfMap,
        PresampleLights,
        PresampleEnvMap,
        PresampleReGIR,
        InitialSamples,
        TemporalResampling,
        SpatialResampling,
        Shading,
        BrdfRays,
        ShadeSecondary,
        GITemporalResampling,
        GISpatialResampling,
        GIFusedResampling,
        GIFinalShading,
        Gradients,
        Denoising,
        Glass,
        Resolve,
        Frame,

        // Not really a section, just using a slot in the count buffer
        MaterialReadback,

        Count
    };
};
