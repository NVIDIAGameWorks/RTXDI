/*
 * SPDX-FileCopyrightText: Copyright (c) 2021-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: LicenseRef-NvidiaProprietary
 *
 * NVIDIA CORPORATION, its affiliates and licensors retain all intellectual
 * property and proprietary rights in and to this material, related
 * documentation and any modifications thereto. Any use, reproduction,
 * disclosure or distribution of this material and related documentation
 * without an express license agreement from NVIDIA CORPORATION or
 * its affiliates is strictly prohibited.
 */

#pragma once

// Must match Profiler.cpp::g_SectionNames
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
        PTGenerateInitialSamples,
        PTTemporalResampling,
        PTSpatialNeighborSelection,
        PTSpatialResampling,
        PTComputeDuplicationMap,
        PTComputeSmoothedDuplicationMap,
        PTFinalShading,
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
