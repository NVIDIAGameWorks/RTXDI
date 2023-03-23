/***************************************************************************
 # Copyright (c) 2020-2021, NVIDIA CORPORATION.  All rights reserved.
 #
 # NVIDIA CORPORATION and its licensors retain all intellectual property
 # and proprietary rights in and to this software, related documentation
 # and any modifications thereto.  Any use, reproduction, disclosure or
 # distribution of this software and related documentation without an express
 # license agreement from NVIDIA CORPORATION is strictly prohibited.
 **************************************************************************/

#pragma once

#include <memory>
#include <utility>
#include <nvrhi/nvrhi.h>
#include <donut/core/math/math.h>
#include <donut/engine/DescriptorTableManager.h>

#include "RayTracingPass.h"

class RenderTargets;
class Profiler;
class RtxgiIntegration;
struct RtxgiParameters;
struct RtxgiVolumeParameters;

namespace donut::vfs
{
    class IBlob;
    class IFileSystem;
}

namespace donut::engine
{
    class IView;
    class ShaderFactory;
}

namespace rtxgi
{
    class DDGIVolumeBase;
    struct DDGIVolumeDesc;
    struct ShaderBytecode;
}

namespace ShaderMake
{
    struct ShaderConstant;
}

namespace rtxdi
{
    struct ContextParameters;
}

class RtxgiVolume  // NOLINT(cppcoreguidelines-special-member-functions)
{
protected:
    nvrhi::DeviceHandle m_Device;
    std::weak_ptr<RtxgiIntegration> m_Parent;

    nvrhi::TextureHandle m_ProbeRayData;
    nvrhi::TextureHandle m_ProbeIrradiance;
    nvrhi::TextureHandle m_ProbeDistance;
    nvrhi::TextureHandle m_ProbeData;

    donut::engine::DescriptorHandle m_IrradianceTextureSRV;
    donut::engine::DescriptorHandle m_DistanceTextureSRV;
    donut::engine::DescriptorHandle m_ProbeDataTextureSRV;
    donut::engine::DescriptorHandle m_RayDataTextureUAV;

    bool m_ResourceIndicesUpdated = false;
    
    static bool LoadShader(rtxgi::ShaderBytecode& dest, const std::shared_ptr<donut::vfs::IFileSystem>& fs, const char* shaderName,
        std::vector<ShaderMake::ShaderConstant> defines, std::vector<std::shared_ptr<donut::vfs::IBlob>>& blobs);

public:
    static std::shared_ptr<RtxgiVolume> CreateDX12(
        nvrhi::IDevice* device,
        const std::weak_ptr<RtxgiIntegration>& parent,
        const std::shared_ptr<donut::vfs::IFileSystem>& fs,
        const std::shared_ptr<donut::engine::DescriptorTableManager>& descriptorTable,
        const rtxgi::DDGIVolumeDesc& volumeDesc);

    static std::shared_ptr<RtxgiVolume> CreateVK(
        nvrhi::IDevice* device,
        const std::weak_ptr<RtxgiIntegration>& parent,
        const std::shared_ptr<donut::vfs::IFileSystem>& fs,
        const std::shared_ptr<donut::engine::DescriptorTableManager>& descriptorTable,
        const rtxgi::DDGIVolumeDesc& volumeDesc);

    static void UpdateVolumesDX12(nvrhi::ICommandList* commandList, const std::vector<std::shared_ptr<RtxgiVolume>>& volumes);
    static void UpdateVolumesVK(nvrhi::ICommandList* commandList, const std::vector<std::shared_ptr<RtxgiVolume>>& volumes);

    explicit RtxgiVolume(nvrhi::IDevice* device, std::weak_ptr<RtxgiIntegration> parent)
        : m_Device(device)
        , m_Parent(std::move(parent))
    { }

    virtual ~RtxgiVolume() = default;

    [[nodiscard]]
    virtual bool IsInitialized() const = 0;

    [[nodiscard]] virtual rtxgi::DDGIVolumeBase* GetVolume() = 0;

    void UpdateConstants(nvrhi::ICommandList* commandList);

    void SetResourceStatesForTracing(nvrhi::ICommandList* commandList) const;

    void Trace(nvrhi::ICommandList* commandList,
        nvrhi::IBindingSet* ligthingBindings,
        nvrhi::IDescriptorTable* descriptorTable);
    
    void SetOrigin(const dm::float3 origin);
    void SetParameters(const RtxgiParameters& params);
    void SetVolumeParameters(const RtxgiVolumeParameters& params);
};

class RtxgiIntegration : public std::enable_shared_from_this<RtxgiIntegration>  // NOLINT(cppcoreguidelines-special-member-functions)
{
protected:
    nvrhi::DeviceHandle m_Device;

    nvrhi::BufferHandle m_RtxgiConstantBuffer;
    nvrhi::BufferHandle m_VolumeResourceIndicesBuffer;
    nvrhi::SamplerHandle m_ProbeSampler;

    std::unique_ptr<RayTracingPass> m_ProbeTracingPass;
    nvrhi::BindingLayoutHandle m_ProbeTracingLayout;
    nvrhi::BindingSetHandle m_ProbeTracingSet;

    nvrhi::rt::AccelStructHandle m_ProbeBLAS;
    nvrhi::rt::AccelStructHandle m_DebugTLAS;
    std::unique_ptr<RayTracingPass> m_DebugPass;
    nvrhi::BufferHandle m_DebugConstantBuffer;
    nvrhi::BindingLayoutHandle m_DebugBindingLayout;
    nvrhi::BindingSetHandle m_DebugBindingSet;
    nvrhi::BindingSetHandle m_PrevDebugBindingSet;
    
    nvrhi::BufferHandle m_DebugTLASInstanceBuffer;
    nvrhi::BindingLayoutHandle m_DebugInstancesBindingLayout;
    nvrhi::BindingSetHandle m_DebugInstancesBindingSet;
    nvrhi::ComputePipelineHandle m_DebugInstancesPipeline;

    std::shared_ptr<donut::vfs::IFileSystem> m_FileSystem;
    std::shared_ptr<donut::engine::DescriptorTableManager> m_DescriptorTable;
    std::vector<std::shared_ptr<RtxgiVolume>> m_Volumes;

    void CreateDebugResources(nvrhi::ICommandList* commandList, RtxgiVolume* volume, RenderTargets& renderTargets);

public:

    RtxgiIntegration(
        nvrhi::IDevice* device,
        const std::shared_ptr<donut::vfs::IFileSystem>& fs,
        const std::shared_ptr<donut::engine::DescriptorTableManager>& descriptorTable);

    std::shared_ptr<RtxgiVolume> CreateVolume(const RtxgiVolumeParameters& params);
    std::shared_ptr<RtxgiVolume> GetVolume(int index);
    void RemoveVolume(int index);

    void RenderDebug(
        nvrhi::ICommandList* commandList,
        nvrhi::IDescriptorTable* descriptorTable,
        RenderTargets& renderTargets,
        int volumeIndex,
        const donut::engine::IView& view);

    void InitializePasses(
        donut::engine::ShaderFactory& shaderFactory,
        nvrhi::IBindingLayout* lightingLayout,
        nvrhi::IBindingLayout* bindlessLayout,
        const rtxdi::ContextParameters& contextParameters,
        bool useRayQuery);

    void UpdateAllVolumes(
        nvrhi::ICommandList* commandList,
        nvrhi::IBindingSet* ligthingBindings,
        nvrhi::IDescriptorTable* descriptorTable,
        Profiler& profiler);

    void InvalidateRenderTargets();
    
    void NextFrame();

    [[nodiscard]] nvrhi::IBuffer* GetConstantBuffer() const { return m_RtxgiConstantBuffer; }
    [[nodiscard]] nvrhi::IBuffer* GetVolumeResourceIndicesBuffer() const { return m_VolumeResourceIndicesBuffer; }
    [[nodiscard]] nvrhi::ISampler* GetProbeSampler() const { return m_ProbeSampler; }
    [[nodiscard]] RayTracingPass& GetProbeTracingPass() const { return *m_ProbeTracingPass; }
    [[nodiscard]] nvrhi::IBindingSet* GetProbeTracingBindingSet() const { return m_ProbeTracingSet; }
    [[nodiscard]] int GetNumVolumes() const { return int(m_Volumes.size()); }
};
