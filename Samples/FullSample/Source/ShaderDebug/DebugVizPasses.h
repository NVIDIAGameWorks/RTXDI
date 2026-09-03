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

#include <memory>

#include "PackedDataVizPass.h"

class RtxdiResources;
class RenderTargets;
enum class PT_RESERVOIR_VIS_MODE;

class DebugVizPasses
{
public:
    DebugVizPasses(
        nvrhi::IDevice* device,
        std::shared_ptr<donut::engine::ShaderFactory> shaderFactory,
        std::shared_ptr<donut::engine::Scene> scene,
        nvrhi::IBindingLayout* bindlessLayout);

    void CreatePipelines();

    void CreateBindingSets(RenderTargets& renderTargets, RtxdiResources& rtxdiResources, nvrhi::TextureHandle dst);

    void RenderUnpackedNormals(nvrhi::ICommandList* commandList, const donut::engine::IView& view);
    void RenderUnpackedGeoNormals(nvrhi::ICommandList* commandList, const donut::engine::IView& view);
    void RenderUnpackedDiffuseAlbeo(nvrhi::ICommandList* commandList, const donut::engine::IView& view);
    void RenderUnpackedSpecularRoughness(nvrhi::ICommandList* commandList, const donut::engine::IView& view);
    void RenderUnpackedPSRDiffuseAlbedo(nvrhi::ICommandList* commandList, const donut::engine::IView& view);
    void RenderUnpackedPSRSpecularF0(nvrhi::ICommandList* commandList, const donut::engine::IView& view);
    void RenderPTDuplicationMap(nvrhi::ICommandList* commandList, const donut::engine::IView& view);
    void RenderSmoothedPTDuplicationMap(nvrhi::ICommandList* commandList, const donut::engine::IView& view);
    void RenderPTDecorrelationFactor(nvrhi::ICommandList* commandList, const donut::engine::IView& view);
    void RenderPTSampleID(nvrhi::ICommandList* commandList, const donut::engine::IView& view);

    void NextFrame();

private:
    std::unique_ptr<PackedDataVizPass> m_gBufferNormalsViz;
    std::unique_ptr<PackedDataVizPass> m_gBufferGeoNormalsViz;
    std::unique_ptr<PackedDataVizPass> m_gBufferDiffuseAlbedoViz;
    std::unique_ptr<PackedDataVizPass> m_gBufferSpecularRoughnessViz;
    std::unique_ptr<PackedDataVizPass> m_psrDiffuseAlbedoViz;
    std::unique_ptr<PackedDataVizPass> m_psrSpecularF0Viz;
    std::unique_ptr<PackedDataVizPass> m_ptDuplicationMapViz;
    std::unique_ptr<PackedDataVizPass> m_smoothedPtDuplicationMapViz;
    std::unique_ptr<PackedDataVizPass> m_ptDecorrelationFactorViz;
    std::unique_ptr<PackedDataVizPass> m_ptSampleIDViz;
};
