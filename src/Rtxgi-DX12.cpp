/***************************************************************************
 # Copyright (c) 2020-2021, NVIDIA CORPORATION.  All rights reserved.
 #
 # NVIDIA CORPORATION and its licensors retain all intellectual property
 # and proprietary rights in and to this software, related documentation
 # and any modifications thereto.  Any use, reproduction, disclosure or
 # distribution of this software and related documentation without an express
 # license agreement from NVIDIA CORPORATION is strictly prohibited.
 **************************************************************************/

#if defined(WITH_RTXGI) && defined(USE_DX12)

#include "RtxgiIntegration.h"
#include <donut/core/vfs/VFS.h>

#include <nvrhi/common/shader-blob.h>
#include <nvrhi/common/misc.h>
#include <nvrhi/d3d12.h>

#include <rtxgi/ddgi/gfx/DDGIVolume_D3D12.h>
#include <rtxgi/ddgi/DDGIVolumeDescGPU.h>

#include "Profiler.h"

#include <donut/core/math/math.h>

using namespace donut::math;
#include "../shaders/ShaderParameters.h"

using namespace donut;

class RtxgiVolumeDX12 final : public RtxgiVolume // NOLINT(cppcoreguidelines-special-member-functions)
{
    std::unique_ptr<rtxgi::d3d12::DDGIVolume> m_DdgiVolume;
    nvrhi::d3d12::DescriptorIndex m_FirstSrvDescriptor = 0;
    uint32_t m_NumSrvDescriptors = 0;

public:
    RtxgiVolumeDX12(
        nvrhi::IDevice* device,
        const std::weak_ptr<RtxgiIntegration>& parent,
        const std::shared_ptr<vfs::IFileSystem>& fs,
        const std::shared_ptr<engine::DescriptorTableManager>& descriptorTable,
        const rtxgi::DDGIVolumeDesc& volumeDesc)
        : RtxgiVolume(device, parent)
    {
        rtxgi::d3d12::DDGIVolumeResources volumeResources;
        
        std::vector<std::shared_ptr<vfs::IBlob>> blobs;

        std::vector<nvrhi::ShaderConstant> defines;
        defines.push_back({ "RTXGI_DDGI_USE_SHADER_CONFIG_FILE", "1" });
        defines.push_back({ "HLSL", "1" });
        defines.push_back({ "RTXGI_DDGI_BLEND_RADIANCE", "1" });
        defines.push_back({ "RTXGI_DDGI_BLEND_SCROLL_SHARED_MEMORY", "1" });
        LoadShader(volumeResources.managed.probeBlendingIrradianceCS, fs, "/shaders/app/RTXGI/DDGIProbeBlendingCS.bin", defines, blobs);
        defines.pop_back();
        LoadShader(volumeResources.managed.probeBorderRowUpdateIrradianceCS, fs, "/shaders/app/RTXGI/DDGIProbeBorderRowUpdateCS.bin", defines, blobs);
        LoadShader(volumeResources.managed.probeBorderColumnUpdateIrradianceCS, fs, "/shaders/app/RTXGI/DDGIProbeBorderColumnUpdateCS.bin", defines, blobs);
        defines[2].value = "0"; // RTXGI_DDGI_BLEND_RADIANCE
        defines.push_back({ "RTXGI_DDGI_BLEND_SCROLL_SHARED_MEMORY", "1" });
        LoadShader(volumeResources.managed.probeBlendingDistanceCS, fs, "/shaders/app/RTXGI/DDGIProbeBlendingCS.bin", defines, blobs);
        defines.pop_back();
        LoadShader(volumeResources.managed.probeBorderRowUpdateDistanceCS, fs, "/shaders/app/RTXGI/DDGIProbeBorderRowUpdateCS.bin", defines, blobs);
        LoadShader(volumeResources.managed.probeBorderColumnUpdateDistanceCS, fs, "/shaders/app/RTXGI/DDGIProbeBorderColumnUpdateCS.bin", defines, blobs);
        defines.pop_back(); // RTXGI_DDGI_BLEND_RADIANCE
        LoadShader(volumeResources.managed.probeClassification.updateCS, fs, "/shaders/app/RTXGI/DDGIProbeClassificationCS.bin", defines, blobs);
        LoadShader(volumeResources.managed.probeClassification.resetCS, fs, "/shaders/app/RTXGI/DDGIProbeClassificationResetCS.bin", defines, blobs);
        LoadShader(volumeResources.managed.probeRelocation.updateCS, fs, "/shaders/app/RTXGI/DDGIProbeRelocationCS.bin", defines, blobs);
        LoadShader(volumeResources.managed.probeRelocation.resetCS, fs, "/shaders/app/RTXGI/DDGIProbeRelocationResetCS.bin", defines, blobs);

        volumeResources.managed.enabled = true;
        volumeResources.managed.device = device->getNativeObject(nvrhi::ObjectTypes::D3D12_Device);

        nvrhi::d3d12::IDevice* nvrhiDevice = device->getNativeObject(nvrhi::ObjectTypes::Nvrhi_D3D12_Device);
        assert(nvrhiDevice);
        nvrhi::d3d12::IDescriptorHeap* descriptorHeap = nvrhiDevice->getDescriptorHeap(nvrhi::d3d12::DescriptorHeapType::ShaderResrouceView);
        assert(descriptorHeap);

        m_NumSrvDescriptors = 1 // CBV
            + rtxgi::GetDDGIVolumeNumSRVDescriptors() 
            + rtxgi::GetDDGIVolumeNumUAVDescriptors();
        m_FirstSrvDescriptor = descriptorHeap->allocateDescriptors(m_NumSrvDescriptors);

        volumeResources.descriptorHeapDesc.heap = descriptorHeap->getShaderVisibleHeap();
        volumeResources.descriptorHeapDesc.constsOffset = m_FirstSrvDescriptor;
        volumeResources.descriptorHeapDesc.uavOffset = volumeResources.descriptorHeapDesc.constsOffset + 1;
        volumeResources.descriptorHeapDesc.srvOffset = volumeResources.descriptorHeapDesc.uavOffset + rtxgi::GetDDGIVolumeNumUAVDescriptors();

        auto parentShared = m_Parent.lock();
        assert(parentShared);

        volumeResources.constantsBuffer = parentShared->GetConstantBuffer()->getNativeObject(nvrhi::ObjectTypes::D3D12_Resource);

        m_DdgiVolume = std::make_unique<rtxgi::d3d12::DDGIVolume>();
        rtxgi::ERTXGIStatus status = m_DdgiVolume->Create(volumeDesc, volumeResources);

        if (status != rtxgi::OK)
        {
            m_DdgiVolume = nullptr;
            return;
        }


        nvrhi::TextureDesc probeTextureDesc;
        ID3D12Resource* d3dProbeTexture = m_DdgiVolume->GetProbeRayData();
        D3D12_RESOURCE_DESC d3dProbeTextureDesc = d3dProbeTexture->GetDesc();
        assert(d3dProbeTextureDesc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D);
        probeTextureDesc.debugName = "ProbeRayData";
        probeTextureDesc.format = volumeDesc.probeRayDataFormat == 0 ? nvrhi::Format::RG32_FLOAT : nvrhi::Format::RGBA32_FLOAT;
        probeTextureDesc.width = (uint32_t)d3dProbeTextureDesc.Width;
        probeTextureDesc.height = d3dProbeTextureDesc.Height;
        probeTextureDesc.initialState = nvrhi::ResourceStates::UnorderedAccess;
        probeTextureDesc.keepInitialState = true;
        probeTextureDesc.isUAV = true;
        
        m_ProbeRayData = device->createHandleForNativeTexture(nvrhi::ObjectTypes::D3D12_Resource, d3dProbeTexture, probeTextureDesc);

        d3dProbeTexture = m_DdgiVolume->GetProbeIrradiance();
        d3dProbeTextureDesc = d3dProbeTexture->GetDesc();
        assert(d3dProbeTextureDesc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D);
        probeTextureDesc.debugName = "ProbeIrradiance";
        probeTextureDesc.format = volumeDesc.probeIrradianceFormat == 0 ? nvrhi::Format::R10G10B10A2_UNORM : volumeDesc.probeIrradianceFormat == 1 ? nvrhi::Format::RGBA16_FLOAT : nvrhi::Format::RGBA32_FLOAT;
        probeTextureDesc.width = (uint32_t)d3dProbeTextureDesc.Width;
        probeTextureDesc.height = d3dProbeTextureDesc.Height;
        probeTextureDesc.initialState = nvrhi::ResourceStates::ShaderResource;
        probeTextureDesc.keepInitialState = true;
        probeTextureDesc.isUAV = true;

        m_ProbeIrradiance = device->createHandleForNativeTexture(nvrhi::ObjectTypes::D3D12_Resource, d3dProbeTexture, probeTextureDesc);

        d3dProbeTexture = m_DdgiVolume->GetProbeDistance();
        d3dProbeTextureDesc = d3dProbeTexture->GetDesc();
        assert(d3dProbeTextureDesc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D);
        probeTextureDesc.debugName = "ProbeDistance";
        probeTextureDesc.format = volumeDesc.probeDistanceFormat == 0 ? nvrhi::Format::RG16_FLOAT: nvrhi::Format::RG32_FLOAT;
        probeTextureDesc.width = (uint32_t)d3dProbeTextureDesc.Width;
        probeTextureDesc.height = d3dProbeTextureDesc.Height;
        probeTextureDesc.initialState = nvrhi::ResourceStates::ShaderResource;
        probeTextureDesc.keepInitialState = true;
        probeTextureDesc.isUAV = true;

        m_ProbeDistance = device->createHandleForNativeTexture(nvrhi::ObjectTypes::D3D12_Resource, d3dProbeTexture, probeTextureDesc);

        d3dProbeTexture = m_DdgiVolume->GetProbeData();
        d3dProbeTextureDesc = d3dProbeTexture->GetDesc();
        assert(d3dProbeTextureDesc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D);
        probeTextureDesc.debugName = "ProbeData";
        probeTextureDesc.format = volumeDesc.probeDataFormat == 0 ? nvrhi::Format::RGBA16_FLOAT : nvrhi::Format::RGBA32_FLOAT;
        probeTextureDesc.width = (uint32_t)d3dProbeTextureDesc.Width;
        probeTextureDesc.height = d3dProbeTextureDesc.Height;
        probeTextureDesc.initialState = nvrhi::ResourceStates::UnorderedAccess;
        probeTextureDesc.keepInitialState = true;
        probeTextureDesc.isUAV = true;

        m_ProbeData = device->createHandleForNativeTexture(nvrhi::ObjectTypes::D3D12_Resource, d3dProbeTexture, probeTextureDesc);


        m_IrradianceTextureSRV = descriptorTable->CreateDescriptorHandle(nvrhi::BindingSetItem::Texture_SRV(0, m_ProbeIrradiance));
        m_DistanceTextureSRV = descriptorTable->CreateDescriptorHandle(nvrhi::BindingSetItem::Texture_SRV(0, m_ProbeDistance));
        m_ProbeDataTextureSRV = descriptorTable->CreateDescriptorHandle(nvrhi::BindingSetItem::Texture_SRV(0, m_ProbeData));
        m_RayDataTextureUAV = descriptorTable->CreateDescriptorHandle(nvrhi::BindingSetItem::Texture_UAV(0, m_ProbeRayData));
    }

    ~RtxgiVolumeDX12() override
    {
        if (m_NumSrvDescriptors > 0)
        {
            nvrhi::d3d12::IDevice* nvrhiDevice = m_Device->getNativeObject(nvrhi::ObjectTypes::Nvrhi_D3D12_Device);
            nvrhi::d3d12::IDescriptorHeap* descriptorHeap = nvrhiDevice->getDescriptorHeap(nvrhi::d3d12::DescriptorHeapType::ShaderResrouceView);

            descriptorHeap->releaseDescriptors(m_FirstSrvDescriptor, m_NumSrvDescriptors);

            m_FirstSrvDescriptor = -1;
            m_NumSrvDescriptors = 0;
        }

        if (m_DdgiVolume)
        {
            m_DdgiVolume->Destroy();
            m_DdgiVolume = nullptr;
        }
    }
    
    [[nodiscard]] bool IsInitialized() const override { return m_DdgiVolume != nullptr; }
    [[nodiscard]] rtxgi::DDGIVolumeBase* GetVolume() override { return m_DdgiVolume.get(); }
};

std::shared_ptr<RtxgiVolume> RtxgiVolume::CreateDX12(
    nvrhi::IDevice* device,
    const std::weak_ptr<RtxgiIntegration>& parent,
    const std::shared_ptr<vfs::IFileSystem>& fs,
    const std::shared_ptr<engine::DescriptorTableManager>& descriptorTable,
    const rtxgi::DDGIVolumeDesc& volumeDesc)
{
    return std::make_shared<RtxgiVolumeDX12>(device, parent, fs, descriptorTable, volumeDesc);
}

void RtxgiVolume::UpdateVolumesDX12(nvrhi::ICommandList* commandList, const std::vector<std::shared_ptr<RtxgiVolume>>& volumes)
{
    ID3D12GraphicsCommandList* d3dCommandList = commandList->getNativeObject(nvrhi::ObjectTypes::D3D12_GraphicsCommandList);

    std::vector<rtxgi::d3d12::DDGIVolume*> ddgiVolumes;

    for (const auto& volume : volumes)
    {
        if (!volume->IsInitialized())
            continue;

        RtxgiVolumeDX12* volumeDx12 = nvrhi::checked_cast<RtxgiVolumeDX12*>(volume.get());
        ddgiVolumes.push_back(static_cast<rtxgi::d3d12::DDGIVolume*>(volumeDx12->GetVolume()));

        commandList->setTextureState(volumeDx12->m_ProbeIrradiance, nvrhi::AllSubresources, nvrhi::ResourceStates::ShaderResource);
        commandList->setTextureState(volumeDx12->m_ProbeDistance, nvrhi::AllSubresources, nvrhi::ResourceStates::ShaderResource);
        commandList->setTextureState(volumeDx12->m_ProbeData, nvrhi::AllSubresources, nvrhi::ResourceStates::UnorderedAccess);
        commandList->setTextureState(volumeDx12->m_ProbeRayData, nvrhi::AllSubresources, nvrhi::ResourceStates::ShaderResource);
    }
    commandList->commitBarriers();

    rtxgi::d3d12::UpdateDDGIVolumeProbes(d3dCommandList, uint32_t(ddgiVolumes.size()), ddgiVolumes.data());

    rtxgi::d3d12::RelocateDDGIVolumeProbes(d3dCommandList, uint32_t(ddgiVolumes.size()), ddgiVolumes.data());

    rtxgi::d3d12::ClassifyDDGIVolumeProbes(d3dCommandList, uint32_t(ddgiVolumes.size()), ddgiVolumes.data());

    commandList->clearState();
}

#endif // defined(WITH_RTXGI) && defined(USE_DX12)