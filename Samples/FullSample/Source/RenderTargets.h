/*
 * SPDX-FileCopyrightText: Copyright (c) 2020-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

#include <donut/core/math/math.h>
#include <nvrhi/nvrhi.h>
#include <memory>

namespace donut::engine
{
    class FramebufferFactory;
}

class RenderTargets
{
public:
    nvrhi::TextureHandle DeviceDepth;
    nvrhi::TextureHandle DeviceDepthUAV;
    nvrhi::TextureHandle Depth;
    nvrhi::TextureHandle PrevDepth;
    nvrhi::TextureHandle GBufferDiffuseAlbedo;
    nvrhi::TextureHandle GBufferSpecularRough;
    nvrhi::TextureHandle GBufferNormals;
    nvrhi::TextureHandle GBufferGeoNormals;
    nvrhi::TextureHandle GBufferEmissive;
    nvrhi::TextureHandle PrevGBufferDiffuseAlbedo;
    nvrhi::TextureHandle PrevGBufferSpecularRough;
    nvrhi::TextureHandle PrevGBufferNormals;
    nvrhi::TextureHandle PrevGBufferGeoNormals;
    nvrhi::TextureHandle MotionVectors;
    nvrhi::TextureHandle NormalRoughness; // for NRD

    nvrhi::TextureHandle PSRMotionVectors;
    nvrhi::TextureHandle PSRNormalRoughness;
    nvrhi::TextureHandle PSRDepth;
    nvrhi::TextureHandle PSRHitT;
    // PSR material/direction buffers (packed: R11G11B10 for albedo/F0, oct for view/light dir)
    nvrhi::TextureHandle PSRDiffuseAlbedo;
    nvrhi::TextureHandle PSRSpecularF0;
    nvrhi::TextureHandle PSRLightDir;

    // DLSS RR buffers in float3 format, rather than uint used by the rest of the pipeline
    nvrhi::TextureHandle PSRDiffuseAlbedo_RR;
    nvrhi::TextureHandle PSRSpecularF0_RR;

    nvrhi::TextureHandle HdrColor;
    nvrhi::TextureHandle LdrColor;
    nvrhi::TextureHandle DiffuseLighting;
    nvrhi::TextureHandle SpecularLighting;
    nvrhi::TextureHandle DenoisedDiffuseLighting;
    nvrhi::TextureHandle DenoisedSpecularLighting;
    nvrhi::TextureHandle NrdValidation;
    nvrhi::TextureHandle TaaFeedback1;
    nvrhi::TextureHandle TaaFeedback2;
    nvrhi::TextureHandle ResolvedColor;
    nvrhi::TextureHandle AccumulatedColor;
    nvrhi::TextureHandle RestirLuminance;
    nvrhi::TextureHandle PrevRestirLuminance;

    nvrhi::TextureHandle DirectLightingRaw;
    nvrhi::TextureHandle IndirectLightingRaw;

    nvrhi::TextureHandle PTSampleIDTexture;
    nvrhi::TextureHandle PTDuplicationMap;
    nvrhi::TextureHandle SmoothedPTDuplicationMap;
    nvrhi::TextureHandle PrevSmoothedPTDuplicationMap;
    nvrhi::TextureHandle PTDecorrelationFactor;
    nvrhi::TextureHandle NeighborSelectionGBuffer;
    nvrhi::TextureHandle PrevNeighborSelectionGBuffer;

    nvrhi::TextureHandle Gradients;
    nvrhi::TextureHandle TemporalSamplePositions;
    nvrhi::TextureHandle DiffuseConfidence;
    nvrhi::TextureHandle SpecularConfidence;
    nvrhi::TextureHandle PrevDiffuseConfidence;
    nvrhi::TextureHandle PrevSpecularConfidence;

    nvrhi::TextureHandle DebugColor;
    nvrhi::TextureHandle ReferenceColor;

    std::shared_ptr<donut::engine::FramebufferFactory> LdrFramebuffer;
    std::shared_ptr<donut::engine::FramebufferFactory> ResolvedFramebuffer;
    std::shared_ptr<donut::engine::FramebufferFactory> GBufferFramebuffer;
    std::shared_ptr<donut::engine::FramebufferFactory> PrevGBufferFramebuffer;

    dm::int2 Size;

    RenderTargets(nvrhi::IDevice* device, dm::int2 size);

    bool IsUpdateRequired(dm::int2 size);
    void NextFrame();
};
