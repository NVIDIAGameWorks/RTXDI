/***************************************************************************
 # Copyright (c) 2020-2023, NVIDIA CORPORATION.  All rights reserved.
 #
 # NVIDIA CORPORATION and its licensors retain all intellectual property
 # and proprietary rights in and to this software, related documentation
 # and any modifications thereto.  Any use, reproduction, disclosure or
 # distribution of this software and related documentation without an express
 # license agreement from NVIDIA CORPORATION is strictly prohibited.
 **************************************************************************/

#pragma once

#include <donut/engine/SceneGraph.h>
#include <nvrhi/nvrhi.h>
#include <rtxdi/ReSTIRDI.h>
#include <memory>
#include <unordered_map>


namespace donut::engine
{
    class CommonRenderPasses;
    class ShaderFactory;
    class Scene;
    class Light;
}

class RtxdiResources;

class PrepareLightsPass
{
private:
    nvrhi::DeviceHandle m_Device;

    nvrhi::ShaderHandle m_ComputeShader;
    nvrhi::ComputePipelineHandle m_ComputePipeline;
    nvrhi::BindingLayoutHandle m_BindingLayout;
    nvrhi::BindingSetHandle m_BindingSet;
    nvrhi::BindingLayoutHandle m_BindlessLayout;

    nvrhi::BufferHandle m_TaskBuffer;
    nvrhi::BufferHandle m_PrimitiveLightBuffer;
    nvrhi::BufferHandle m_LightIndexMappingBuffer;
    nvrhi::BufferHandle m_GeometryInstanceToLightBuffer;
    nvrhi::TextureHandle m_LocalLightPdfTexture;
    
    uint32_t m_MaxLightsInBuffer;
    bool m_OddFrame = false;
    
    std::shared_ptr<donut::engine::ShaderFactory> m_ShaderFactory;
    std::shared_ptr<donut::engine::CommonRenderPasses> m_CommonPasses;
    std::shared_ptr<donut::engine::Scene> m_Scene;

    std::unordered_map<size_t, uint32_t> m_InstanceLightBufferOffsets; // hash(instance*, geometryIndex) -> bufferOffset
    std::unordered_map<const donut::engine::Light*, uint32_t> m_PrimitiveLightBufferOffsets;

public:
    PrepareLightsPass(
        nvrhi::IDevice* device,
        std::shared_ptr<donut::engine::ShaderFactory> shaderFactory,
        std::shared_ptr<donut::engine::CommonRenderPasses> commonPasses,
        std::shared_ptr<donut::engine::Scene> scene,
        nvrhi::IBindingLayout* bindlessLayout);

    void CreatePipeline();
    void CreateBindingSet(RtxdiResources& resources);
    void CountLightsInScene(uint32_t& numEmissiveMeshes, uint32_t& numEmissiveTriangles);
    
    RTXDI_LightBufferParameters Process(
        nvrhi::ICommandList* commandList, 
        const rtxdi::ReSTIRDIContext& context, 
        const std::vector<std::shared_ptr<donut::engine::Light>>& sceneLights,
        bool enableImportanceSampledEnvironmentLight);
};
