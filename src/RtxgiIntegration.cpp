/***************************************************************************
 # Copyright (c) 2020-2021, NVIDIA CORPORATION.  All rights reserved.
 #
 # NVIDIA CORPORATION and its licensors retain all intellectual property
 # and proprietary rights in and to this software, related documentation
 # and any modifications thereto.  Any use, reproduction, disclosure or
 # distribution of this software and related documentation without an express
 # license agreement from NVIDIA CORPORATION is strictly prohibited.
 **************************************************************************/

#if defined(WITH_RTXGI)

#include "RtxgiIntegration.h"
#include <donut/engine/ShaderFactory.h>
#include <donut/core/vfs/VFS.h>
#include <donut/core/log.h>

#include <ShaderMake/ShaderBlob.h>
#include <nvrhi/utils.h>

#include "../shaders/RTXGI/DDGIShaderConfig.h"
#include <rtxgi/ddgi/DDGIVolume.h>

#include <donut/core/math/math.h>

#include "Profiler.h"
#include "RenderTargets.h"
#include "UserInterface.h"
#include "SampleScene.h"
#include "donut/engine/View.h"
using namespace donut::math;
#include "../shaders/ShaderParameters.h"

using namespace donut;

constexpr uint32_t c_MaxVolumes = 8;

bool RtxgiVolume::LoadShader(rtxgi::ShaderBytecode& dest, const std::shared_ptr<vfs::IFileSystem>& fs, const char* shaderName,
    std::vector<ShaderMake::ShaderConstant> defines, std::vector<std::shared_ptr<vfs::IBlob>>& blobs)
{
    auto data = fs->readFile(shaderName);
    if (!data)
        return false;

    if (!ShaderMake::FindPermutationInBlob(data->data(), data->size(),
        defines.empty() ? nullptr : defines.data(), uint32_t(defines.size()),
        &dest.pData, &dest.size))
    {
        auto errorMessage = ShaderMake::FormatShaderNotFoundMessage(data->data(), data->size(),
            defines.empty() ? nullptr : defines.data(), uint32_t(defines.size()));

        log::error("%s", errorMessage.c_str());

        return false;
    }

    blobs.push_back(data);

    return true;
}

RtxgiIntegration::RtxgiIntegration(
    nvrhi::IDevice* device,
    const std::shared_ptr<donut::vfs::IFileSystem>& fs,
    const std::shared_ptr<donut::engine::DescriptorTableManager>& descriptorTable)
    : m_Device(device)
    , m_FileSystem(fs)
    , m_DescriptorTable(descriptorTable)
{
    // Don't insert the RTXGI internal perf markers because they break the batching of probe updates on the GPU.
    rtxgi::SetInsertPerfMarkers(false);
}

void RtxgiIntegration::InitializePasses(
    engine::ShaderFactory& shaderFactory,
    nvrhi::IBindingLayout* lightingLayout,
    nvrhi::IBindingLayout* bindlessLayout,
    const rtxdi::ContextParameters& contextParameters,
    bool useRayQuery)
{
    auto samplerDesc = nvrhi::SamplerDesc()
        .setAllAddressModes(nvrhi::SamplerAddressMode::Wrap)
        .setAllFilters(true);

    m_ProbeSampler = m_Device->createSampler(samplerDesc);
    
    auto constantBufferDesc = nvrhi::BufferDesc()
        .setDebugName("RtxgiConstants")
        .setByteSize(sizeof(rtxgi::DDGIVolumeDescGPUPacked) * c_MaxVolumes)
        .setStructStride(sizeof(rtxgi::DDGIVolumeDescGPUPacked))
        .setInitialState(nvrhi::ResourceStates::ShaderResource)
        .setKeepInitialState(true);

    m_RtxgiConstantBuffer = m_Device->createBuffer(constantBufferDesc);

    auto indexBufferDesc = nvrhi::BufferDesc()
        .setDebugName("RtxgiVolumeResourceIndices")
        .setByteSize(sizeof(DDGIVolumeResourceIndices) * c_MaxVolumes)
        .setStructStride(sizeof(DDGIVolumeResourceIndices))
        .setInitialState(nvrhi::ResourceStates::ShaderResource)
        .setKeepInitialState(true);

    m_VolumeResourceIndicesBuffer = m_Device->createBuffer(indexBufferDesc);

    auto setDesc = nvrhi::BindingSetDesc()
        .addItem(nvrhi::BindingSetItem::StructuredBuffer_SRV(40, m_RtxgiConstantBuffer))
        .addItem(nvrhi::BindingSetItem::StructuredBuffer_SRV(41, m_VolumeResourceIndicesBuffer))
        .addItem(nvrhi::BindingSetItem::Sampler(40, m_ProbeSampler));

    nvrhi::utils::CreateBindingSetAndLayout(m_Device, nvrhi::ShaderType::All, 0,
        setDesc, m_ProbeTracingLayout, m_ProbeTracingSet);

    m_ProbeTracingPass = std::make_unique<RayTracingPass>();
    m_ProbeTracingPass->Init(m_Device, shaderFactory, "app/RTXGI/ProbeTrace.hlsl", { LightingPasses::GetRegirMacro(contextParameters) },
        useRayQuery, 16, lightingLayout, m_ProbeTracingLayout, bindlessLayout);

    auto instanceLayoutDesc = nvrhi::BindingLayoutDesc()
        .setVisibility(nvrhi::ShaderType::Compute)
        .addItem(nvrhi::BindingLayoutItem::VolatileConstantBuffer(0))
        .addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(0))
        .addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(1))
        .addItem(nvrhi::BindingLayoutItem::StructuredBuffer_UAV(0));

    m_DebugInstancesBindingLayout = m_Device->createBindingLayout(instanceLayoutDesc);

    auto instanceComputeShader = shaderFactory.CreateShader("app/RTXGI/ProbeInstances.hlsl", "main", nullptr, nvrhi::ShaderType::Compute);

    m_DebugInstancesPipeline = m_Device->createComputePipeline(nvrhi::ComputePipelineDesc()
        .addBindingLayout(m_DebugInstancesBindingLayout)
        .addBindingLayout(bindlessLayout)
        .setComputeShader(instanceComputeShader));

    auto debugLayoutDesc = nvrhi::BindingLayoutDesc()
        .setVisibility(nvrhi::ShaderType::All)
        .addItem(nvrhi::BindingLayoutItem::VolatileConstantBuffer(0))
        .addItem(nvrhi::BindingLayoutItem::RayTracingAccelStruct(0))
        .addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(1))
        .addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(2))
        .addItem(nvrhi::BindingLayoutItem::Texture_SRV(3))
        .addItem(nvrhi::BindingLayoutItem::Texture_UAV(0))
        .addItem(nvrhi::BindingLayoutItem::Sampler(0));

    m_DebugBindingLayout = m_Device->createBindingLayout(debugLayoutDesc);
    
    m_DebugPass = std::make_unique<RayTracingPass>();
    m_DebugPass->Init(m_Device, shaderFactory, "app/RTXGI/ProbeDebug.hlsl", {}, true, 16,
        m_DebugBindingLayout, nullptr, bindlessLayout);
}

void RtxgiIntegration::UpdateAllVolumes(
    nvrhi::ICommandList* commandList,
    nvrhi::IBindingSet* ligthingBindings,
    nvrhi::IDescriptorTable* descriptorTable,
    Profiler& profiler)
{
    for (const auto& volume : m_Volumes)
    {
        volume->UpdateConstants(commandList);
    }

    for (const auto& volume : m_Volumes)
    {
        volume->SetResourceStatesForTracing(commandList);
    }
    commandList->commitBarriers();


    commandList->beginMarker("RTXGI Probe Tracing");
    profiler.BeginSection(commandList, ProfilerSection::RtxgiProbeTracing);

    for (const auto& volume : m_Volumes)
    {
        volume->Trace(commandList, ligthingBindings, descriptorTable);
    }

    profiler.EndSection(commandList, ProfilerSection::RtxgiProbeTracing);
    commandList->endMarker();


    commandList->beginMarker("RTXGI Probe Updates");
    profiler.BeginSection(commandList, ProfilerSection::RtxgiProbeUpdates);
#ifdef USE_DX12
    if (m_Device->getGraphicsAPI() == nvrhi::GraphicsAPI::D3D12)
    {
        RtxgiVolume::UpdateVolumesDX12(commandList, m_Volumes);
    }
    else
#endif
    {
#ifdef USE_VK
        RtxgiVolume::UpdateVolumesVK(commandList, m_Volumes);
#endif
    }
    profiler.EndSection(commandList, ProfilerSection::RtxgiProbeUpdates);
    commandList->endMarker();
}

void RtxgiIntegration::InvalidateRenderTargets()
{
    m_DebugBindingSet = nullptr;
    m_PrevDebugBindingSet = nullptr;
}

void RtxgiIntegration::RenderDebug(
    nvrhi::ICommandList* commandList,
    nvrhi::IDescriptorTable* descriptorTable,
    RenderTargets& renderTargets,
    int volumeIndex,
    const donut::engine::IView& view)
{
    if (volumeIndex < 0 || volumeIndex >= int(m_Volumes.size()))
        return;

    const std::shared_ptr<RtxgiVolume>& volume = m_Volumes[volumeIndex];
    assert(volume);

    uint32_t numProbes = volume->GetVolume()->GetNumProbes();
    if (numProbes == 0)
        return;

    CreateDebugResources(commandList, volume.get(), renderTargets);
    
    uint64_t blasDeviceAddress = m_ProbeBLAS->getDeviceAddress();

    ProbeDebugConstants constants = {};
    view.FillPlanarViewConstants(constants.view);
    constants.blasDeviceAddressLow = (uint32_t)blasDeviceAddress;
    constants.blasDeviceAddressHigh = (uint32_t)(blasDeviceAddress >> 32);
    constants.volumeIndex = uint32_t(volumeIndex);
    commandList->writeBuffer(m_DebugConstantBuffer, &constants, sizeof(constants));

    auto instanceState = nvrhi::ComputeState()
        .setPipeline(m_DebugInstancesPipeline)
        .addBindingSet(m_DebugInstancesBindingSet)
        .addBindingSet(descriptorTable);

    commandList->setComputeState(instanceState);
    commandList->dispatch(dm::div_ceil(numProbes, 256));

    commandList->setAccelStructState(m_ProbeBLAS, nvrhi::ResourceStates::AccelStructBuildBlas);
    
    commandList->buildTopLevelAccelStructFromBuffer(m_DebugTLAS, m_DebugTLASInstanceBuffer, 0, numProbes);

    m_DebugPass->Execute(commandList, view.GetViewExtent().width(), view.GetViewExtent().height(), 
        m_DebugBindingSet, nullptr, descriptorTable);
}

void RtxgiIntegration::NextFrame()
{
    std::swap(m_DebugBindingSet, m_PrevDebugBindingSet);
}

std::shared_ptr<RtxgiVolume> RtxgiIntegration::CreateVolume(const RtxgiVolumeParameters& params)
{
    if (m_Volumes.size() >= c_MaxVolumes)
    {
        log::warning("Cannot create a new RTXGI volume: the maximum supported "
            "number of volumes (%d) has been reached.", c_MaxVolumes);
        return nullptr;
    }

    rtxgi::DDGIVolumeDesc volumeDesc;
    volumeDesc.index = uint32_t(m_Volumes.size());
    volumeDesc.name = params.name;
    volumeDesc.rngSeed = 0;
    volumeDesc.probeCounts = { params.probeCounts.x, params.probeCounts.y, params.probeCounts.z };
    volumeDesc.origin = { params.origin.x, params.origin.y, params.origin.z };
    volumeDesc.probeSpacing = { params.probeSpacing, params.probeSpacing, params.probeSpacing };
    volumeDesc.eulerAngles = { dm::radians(params.eulerAngles.x), dm::radians(params.eulerAngles.y), dm::radians(params.eulerAngles.z) };
    volumeDesc.probeNumRays = RTXGI_DDGI_BLEND_RAYS_PER_PROBE;
    volumeDesc.probeRayDataFormat = 1;
    volumeDesc.probeIrradianceFormat = 1;
    volumeDesc.probeDistanceFormat = 0;
    volumeDesc.probeNumIrradianceTexels = RTXGI_DDGI_PROBE_NUM_TEXELS;
    volumeDesc.probeNumDistanceTexels = RTXGI_DDGI_PROBE_NUM_TEXELS;
    volumeDesc.probeRelocationEnabled = true;
    volumeDesc.probeClassificationEnabled = true;
    volumeDesc.probeMinFrontfaceDistance = 0.2f;
    volumeDesc.movementType = params.scrolling
        ? rtxgi::EDDGIVolumeMovementType::Scrolling
        : rtxgi::EDDGIVolumeMovementType::Default;

    std::shared_ptr<RtxgiVolume> volume;

#if USE_DX12
    if (m_Device->getGraphicsAPI() == nvrhi::GraphicsAPI::D3D12)
        volume = RtxgiVolume::CreateDX12(m_Device, weak_from_this(), m_FileSystem, m_DescriptorTable, volumeDesc);
    else
#endif
#if USE_VK
    if (m_Device->getGraphicsAPI() == nvrhi::GraphicsAPI::VULKAN)
        volume = RtxgiVolume::CreateVK(m_Device, weak_from_this(), m_FileSystem, m_DescriptorTable, volumeDesc);
    else
#endif
    {
        nvrhi::utils::NotSupported();
        return nullptr;
    }
    
    m_Volumes.push_back(volume);

    return volume;
}

std::shared_ptr<RtxgiVolume> RtxgiIntegration::GetVolume(int index)
{
    if (index < 0 || index > int(m_Volumes.size()))
        return nullptr;

    return m_Volumes[index];
}

void RtxgiIntegration::RemoveVolume(int index)
{
    if (index < 0 || index > int(m_Volumes.size()))
        return;

    m_Volumes.erase(m_Volumes.begin() + index);

    for (uint32_t i = 0; i < uint32_t(m_Volumes.size()); ++i)
        m_Volumes[i]->GetVolume()->SetIndex(i);
}

void RtxgiVolume::UpdateConstants(nvrhi::ICommandList* commandList)
{
    if (!IsInitialized())
        return;

    auto parentShared = m_Parent.lock();
    assert(parentShared);

    GetVolume()->Update();

    rtxgi::DDGIVolumeDescGPUPacked volumeDesc = GetVolume()->GetDescGPUPacked();
    commandList->writeBuffer(parentShared->GetConstantBuffer(), &volumeDesc, sizeof(volumeDesc),
        sizeof(volumeDesc) * GetVolume()->GetIndex());

    if (!m_ResourceIndicesUpdated)
    {
        DDGIVolumeResourceIndices resourceIndices{};
        resourceIndices.irradianceTextureSRV = m_IrradianceTextureSRV.Get();
        resourceIndices.distanceTextureSRV = m_DistanceTextureSRV.Get();
        resourceIndices.probeDataTextureSRV = m_ProbeDataTextureSRV.Get();
        resourceIndices.rayDataTextureUAV = m_RayDataTextureUAV.Get();

        commandList->writeBuffer(parentShared->GetVolumeResourceIndicesBuffer(), &resourceIndices, sizeof(resourceIndices),
            sizeof(resourceIndices) * GetVolume()->GetIndex());

        m_ResourceIndicesUpdated = true;
    }
}

void RtxgiVolume::SetResourceStatesForTracing(nvrhi::ICommandList* commandList) const
{
    commandList->setTextureState(m_ProbeIrradiance, nvrhi::AllSubresources, nvrhi::ResourceStates::ShaderResource);
    commandList->setTextureState(m_ProbeDistance, nvrhi::AllSubresources, nvrhi::ResourceStates::ShaderResource);
    commandList->setTextureState(m_ProbeData, nvrhi::AllSubresources, nvrhi::ResourceStates::ShaderResource);
    commandList->setTextureState(m_ProbeRayData, nvrhi::AllSubresources, nvrhi::ResourceStates::UnorderedAccess);
}

void RtxgiVolume::Trace(nvrhi::ICommandList* commandList, nvrhi::IBindingSet* ligthingBindings, nvrhi::IDescriptorTable* descriptorTable)
{
    if (!IsInitialized())
        return;

    auto parentShared = m_Parent.lock();
    assert(parentShared);

    PerPassConstants pushConstants{};
    pushConstants.rayCountBufferIndex = ProfilerSection::RtxgiProbeTracing;
    pushConstants.rtxgiVolumeIndex = GetVolume()->GetIndex();

    parentShared->GetProbeTracingPass().Execute(commandList,
        GetVolume()->GetNumRaysPerProbe(), GetVolume()->GetNumProbes(),
        ligthingBindings, parentShared->GetProbeTracingBindingSet(), descriptorTable,
        &pushConstants, sizeof(pushConstants));
}

void RtxgiVolume::SetOrigin(const dm::float3 origin)
{
    auto* volume = GetVolume();
    volume->SetScrollAnchor({ origin.x, origin.y, origin.z });
}

void RtxgiVolume::SetParameters(const RtxgiParameters& params)
{
    auto* volume = GetVolume();
    volume->SetProbeHysteresis(params.hysteresis);
    volume->SetProbeBrightnessThreshold(params.brightnessThreshold);
    volume->SetProbeIrradianceThreshold(params.irradianceThreshold);
    volume->SetProbeRelocationEnabled(params.probeRelocation);
    volume->SetProbeClassificationEnabled(params.probeClassification);
    volume->SetMinFrontFaceDistance(params.minFrontFaceDistanceFraction * volume->GetDesc().probeSpacing.x);
    volume->SetProbeRelocationNeedsReset(params.resetRelocation);
}

void RtxgiVolume::SetVolumeParameters(const RtxgiVolumeParameters& params)
{
    auto* volume = GetVolume();
    volume->SetProbeSpacing({ params.probeSpacing, params.probeSpacing, params.probeSpacing });
    if (!params.scrolling)
        volume->SetOrigin({ params.origin.x, params.origin.y, params.origin.z });
    volume->SetEulerAngles({ dm::radians(params.eulerAngles.x), dm::radians(params.eulerAngles.y), dm::radians(params.eulerAngles.z) });
}

void RtxgiIntegration::CreateDebugResources(nvrhi::ICommandList* commandList, RtxgiVolume* volume, RenderTargets& renderTargets)
{
    if (!m_ProbeBLAS)
    {
        auto blasDesc = nvrhi::rt::AccelStructDesc()
            .setDebugName("RtxgiDebugProbe")
            .setIsTopLevel(false)
            .addBottomLevelGeometry(nvrhi::rt::GeometryDesc()
                .setAABBs(nvrhi::rt::GeometryAABBs()
                    .setCount(1)));

        m_ProbeBLAS = m_Device->createAccelStruct(blasDesc);

        auto bufferDesc = nvrhi::BufferDesc()
            .setByteSize(6 * sizeof(float))
            .setIsAccelStructBuildInput(true)
            .setInitialState(nvrhi::ResourceStates::Common)
            .setKeepInitialState(true);

        nvrhi::BufferHandle buffer = m_Device->createBuffer(bufferDesc);

        float aabb[] = {
            -1.f, -1.f, -1.f,
             1.f,  1.f, 1.f
        };

        commandList->writeBuffer(buffer, aabb, sizeof(aabb), 0);

        auto geometryDesc = nvrhi::rt::GeometryDesc()
            .setAABBs(nvrhi::rt::GeometryAABBs().setCount(1).setBuffer(buffer));

        commandList->buildBottomLevelAccelStruct(m_ProbeBLAS, &geometryDesc, 1);
    }

    uint32_t numProbesInVolume = volume->GetVolume()->GetNumProbes();


    if (!m_DebugTLAS || m_DebugTLAS->getDesc().topLevelMaxInstances < numProbesInVolume)
    {
        auto tlasDesc = nvrhi::rt::AccelStructDesc()
            .setDebugName("RtxgiDebugTLAS")
            .setIsTopLevel(true)
            .setTopLevelMaxInstances(numProbesInVolume);

        m_DebugTLAS = m_Device->createAccelStruct(tlasDesc);

        auto debugInstanceBufferDesc = nvrhi::BufferDesc()
            .setDebugName("RtxgiDebugInstances")
            .setByteSize(sizeof(nvrhi::rt::InstanceDesc) * numProbesInVolume)
            .setStructStride(sizeof(nvrhi::rt::InstanceDesc))
            .setIsAccelStructBuildInput(true)
            .setCanHaveUAVs(true)
            .setKeepInitialState(true)
            .setInitialState(nvrhi::ResourceStates::UnorderedAccess);

        m_DebugTLASInstanceBuffer = m_Device->createBuffer(debugInstanceBufferDesc);

        m_DebugInstancesBindingSet = nullptr;
        m_DebugBindingSet = nullptr;
        m_PrevDebugBindingSet = nullptr;
    }

    if (!m_DebugConstantBuffer)
    {
        auto constantBufferDesc = nvrhi::utils::CreateVolatileConstantBufferDesc(
            sizeof(ProbeDebugConstants), "ProbeDebugConstants", 16);
        m_DebugConstantBuffer = m_Device->createBuffer(constantBufferDesc);

        m_DebugInstancesBindingSet = nullptr;
        m_DebugBindingSet = nullptr;
        m_PrevDebugBindingSet = nullptr;
    }

    if (!m_DebugBindingSet)
    {
        for (int currentFrame = 0; currentFrame <= 1; currentFrame++)
        {
            auto debugSetDesc = nvrhi::BindingSetDesc()
                .addItem(nvrhi::BindingSetItem::ConstantBuffer(0, m_DebugConstantBuffer))
                .addItem(nvrhi::BindingSetItem::RayTracingAccelStruct(0, m_DebugTLAS))
                .addItem(nvrhi::BindingSetItem::StructuredBuffer_SRV(1, m_RtxgiConstantBuffer))
                .addItem(nvrhi::BindingSetItem::StructuredBuffer_SRV(2, m_VolumeResourceIndicesBuffer))
                .addItem(nvrhi::BindingSetItem::Texture_SRV(3, currentFrame ? renderTargets.Depth : renderTargets.PrevDepth))
                .addItem(nvrhi::BindingSetItem::Texture_UAV(0, renderTargets.HdrColor))
                .addItem(nvrhi::BindingSetItem::Sampler(0, m_ProbeSampler));

            nvrhi::BindingSetHandle bindingSet = m_Device->createBindingSet(debugSetDesc, m_DebugBindingLayout);
            
            if (currentFrame)
                m_DebugBindingSet = bindingSet;
            else
                m_PrevDebugBindingSet = bindingSet;
        }
    }

    if (!m_DebugInstancesBindingSet)
    {
        auto instanceBindingSetDesc = nvrhi::BindingSetDesc()
            .addItem(nvrhi::BindingSetItem::ConstantBuffer(0, m_DebugConstantBuffer))
            .addItem(nvrhi::BindingSetItem::StructuredBuffer_SRV(0, m_RtxgiConstantBuffer))
            .addItem(nvrhi::BindingSetItem::StructuredBuffer_SRV(1, m_VolumeResourceIndicesBuffer))
            .addItem(nvrhi::BindingSetItem::StructuredBuffer_UAV(0, m_DebugTLASInstanceBuffer));

        m_DebugInstancesBindingSet = m_Device->createBindingSet(instanceBindingSetDesc, m_DebugInstancesBindingLayout);
    }
}

#endif // defined(WITH_RTXGI)