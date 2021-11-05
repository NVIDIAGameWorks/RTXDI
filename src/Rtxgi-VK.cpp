/***************************************************************************
 # Copyright (c) 2020-2021, NVIDIA CORPORATION.  All rights reserved.
 #
 # NVIDIA CORPORATION and its licensors retain all intellectual property
 # and proprietary rights in and to this software, related documentation
 # and any modifications thereto.  Any use, reproduction, disclosure or
 # distribution of this software and related documentation without an express
 # license agreement from NVIDIA CORPORATION is strictly prohibited.
 **************************************************************************/

#if defined(WITH_RTXGI) && defined(USE_VK)

#include "RtxgiIntegration.h"
#include <donut/core/vfs/VFS.h>

#include <nvrhi/common/shader-blob.h>
#include <nvrhi/common/misc.h>
#include <nvrhi/vulkan.h>

#include <rtxgi/ddgi/gfx/DDGIVolume_VK.h>
#include <rtxgi/ddgi/DDGIVolumeDescGPU.h>

#include "VulkanExtensions.h"
#include "Profiler.h"

#include <donut/core/math/math.h>

using namespace donut::math;
#include "../shaders/ShaderParameters.h"

using namespace donut;

class RtxgiVolumeVK final : public RtxgiVolume  // NOLINT(cppcoreguidelines-special-member-functions)
{
    std::unique_ptr<rtxgi::vulkan::DDGIVolume> m_DdgiVolume;

    VkDescriptorPool m_descriptorPool = nullptr;
    
public:
    RtxgiVolumeVK(
        nvrhi::IDevice* device,
        const std::weak_ptr<RtxgiIntegration>& parent,
        const std::shared_ptr<vfs::IFileSystem>& fs,
        const std::shared_ptr<engine::DescriptorTableManager>& descriptorTable,
        const rtxgi::DDGIVolumeDesc& volumeDesc)
        : RtxgiVolume(device, parent)
    {
        VkInstance vkInstance = device->getNativeObject(nvrhi::ObjectTypes::VK_Instance);
        VkDevice vkDevice = device->getNativeObject(nvrhi::ObjectTypes::VK_Device);
        VkPhysicalDevice vkPhysicalDevice = device->getNativeObject(nvrhi::ObjectTypes::VK_PhysicalDevice);

        LoadInstanceExtensions(vkInstance);
        LoadDeviceExtensions(vkDevice);

        rtxgi::vulkan::DDGIVolumeResources volumeResources;
        
        std::vector<std::shared_ptr<vfs::IBlob>> blobs;

        std::vector<nvrhi::ShaderConstant> defines;
        defines.push_back({ "RTXGI_DDGI_USE_SHADER_CONFIG_FILE", "1" });
        defines.push_back({ "HLSL", "1" });
        defines.push_back({ "RTXGI_DDGI_BLEND_RADIANCE", "1" });
        LoadShader(volumeResources.managed.probeBlendingIrradianceCS, fs, "/shaders/app/RTXGI/DDGIProbeBlendingCS.bin", defines, blobs);
        LoadShader(volumeResources.managed.probeBorderRowUpdateIrradianceCS, fs, "/shaders/app/RTXGI/DDGIProbeBorderRowUpdateCS.bin", defines, blobs);
        LoadShader(volumeResources.managed.probeBorderColumnUpdateIrradianceCS, fs, "/shaders/app/RTXGI/DDGIProbeBorderColumnUpdateCS.bin", defines, blobs);
        defines[2].value = "0"; // RTXGI_DDGI_BLEND_RADIANCE
        LoadShader(volumeResources.managed.probeBlendingDistanceCS, fs, "/shaders/app/RTXGI/DDGIProbeBlendingCS.bin", defines, blobs);
        LoadShader(volumeResources.managed.probeBorderRowUpdateDistanceCS, fs, "/shaders/app/RTXGI/DDGIProbeBorderRowUpdateCS.bin", defines, blobs);
        LoadShader(volumeResources.managed.probeBorderColumnUpdateDistanceCS, fs, "/shaders/app/RTXGI/DDGIProbeBorderColumnUpdateCS.bin", defines, blobs);
        defines.pop_back(); // RTXGI_DDGI_BLEND_RADIANCE
        LoadShader(volumeResources.managed.probeClassification.updateCS, fs, "/shaders/app/RTXGI/DDGIProbeClassificationCS.bin", defines, blobs);
        LoadShader(volumeResources.managed.probeClassification.resetCS, fs, "/shaders/app/RTXGI/DDGIProbeClassificationResetCS.bin", defines, blobs);
        LoadShader(volumeResources.managed.probeRelocation.updateCS, fs, "/shaders/app/RTXGI/DDGIProbeRelocationCS.bin", defines, blobs);
        LoadShader(volumeResources.managed.probeRelocation.resetCS, fs, "/shaders/app/RTXGI/DDGIProbeRelocationResetCS.bin", defines, blobs);

        volumeResources.managed.enabled = true;
        volumeResources.managed.device = vkDevice;
        volumeResources.managed.physicalDevice = vkPhysicalDevice;

        VkDescriptorPoolSize poolSize = {};
        poolSize.descriptorCount = 5;

        VkDescriptorPoolCreateInfo poolCreateInfo = {};
        poolCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolCreateInfo.maxSets = 1;
        poolCreateInfo.pPoolSizes = &poolSize;
        poolCreateInfo.poolSizeCount = 1;
        vkCreateDescriptorPool(volumeResources.managed.device, &poolCreateInfo, nullptr, &m_descriptorPool);

        auto parentShared = m_Parent.lock();
        assert(parentShared);

        volumeResources.managed.descriptorPool = m_descriptorPool;
        volumeResources.constantsBuffer = parentShared->GetConstantBuffer()->getNativeObject(nvrhi::ObjectTypes::VK_Buffer);

        nvrhi::CommandListHandle commandList = device->createCommandList();
        commandList->open();

        VkCommandBuffer vkCmdBuffer = commandList->getNativeObject(nvrhi::ObjectTypes::VK_CommandBuffer);

        m_DdgiVolume = std::make_unique<rtxgi::vulkan::DDGIVolume>();
        rtxgi::ERTXGIStatus status = m_DdgiVolume->Create(vkCmdBuffer, volumeDesc, volumeResources);

        commandList->close();
        device->executeCommandList(commandList);
        
        if (status != rtxgi::OK)
        {
            m_DdgiVolume = nullptr;
            return;
        }

        

        nvrhi::TextureDesc probeTextureDesc;
        VkImage vkProbeTexture = m_DdgiVolume->GetProbeRayData();
        probeTextureDesc.debugName = "ProbeRayData";
        probeTextureDesc.format = volumeDesc.probeRayDataFormat == 0 ? nvrhi::Format::RG32_FLOAT : nvrhi::Format::RGBA32_FLOAT;
        probeTextureDesc.width = 1; // we don't know this here, but it shouldn't matter
        probeTextureDesc.height = 1;
        probeTextureDesc.initialState = nvrhi::ResourceStates::UnorderedAccess;
        probeTextureDesc.keepInitialState = true;
        probeTextureDesc.isUAV = true;
        
        m_ProbeRayData = device->createHandleForNativeTexture(nvrhi::ObjectTypes::VK_Image, vkProbeTexture, probeTextureDesc);

        vkProbeTexture = m_DdgiVolume->GetProbeIrradiance();
        probeTextureDesc.debugName = "ProbeIrradiance";
        probeTextureDesc.format = volumeDesc.probeIrradianceFormat == 0 ? nvrhi::Format::R10G10B10A2_UNORM : nvrhi::Format::RGBA32_FLOAT;
        probeTextureDesc.initialState = nvrhi::ResourceStates::UnorderedAccess;
        probeTextureDesc.keepInitialState = true;
        probeTextureDesc.isUAV = true;

        m_ProbeIrradiance = device->createHandleForNativeTexture(nvrhi::ObjectTypes::VK_Image, vkProbeTexture, probeTextureDesc);

        vkProbeTexture = m_DdgiVolume->GetProbeDistance();
        probeTextureDesc.debugName = "ProbeDistance";
        probeTextureDesc.format = volumeDesc.probeDistanceFormat == 0 ? nvrhi::Format::RG16_FLOAT: nvrhi::Format::RG32_FLOAT;
        probeTextureDesc.initialState = nvrhi::ResourceStates::UnorderedAccess;
        probeTextureDesc.keepInitialState = true;
        probeTextureDesc.isUAV = true;

        m_ProbeDistance = device->createHandleForNativeTexture(nvrhi::ObjectTypes::VK_Image, vkProbeTexture, probeTextureDesc);
        
        vkProbeTexture = m_DdgiVolume->GetProbeData();
        probeTextureDesc.debugName = "ProbeData";
        probeTextureDesc.format = volumeDesc.probeDataFormat == 0 ? nvrhi::Format::RGBA16_FLOAT : nvrhi::Format::RGBA32_FLOAT;
        probeTextureDesc.initialState = nvrhi::ResourceStates::UnorderedAccess;
        probeTextureDesc.keepInitialState = true;
        probeTextureDesc.isUAV = true;

        m_ProbeData = device->createHandleForNativeTexture(nvrhi::ObjectTypes::VK_Image, vkProbeTexture, probeTextureDesc);


        m_IrradianceTextureSRV = descriptorTable->CreateDescriptorHandle(nvrhi::BindingSetItem::Texture_SRV(0, m_ProbeIrradiance));
        m_DistanceTextureSRV = descriptorTable->CreateDescriptorHandle(nvrhi::BindingSetItem::Texture_SRV(0, m_ProbeDistance));
        m_ProbeDataTextureSRV = descriptorTable->CreateDescriptorHandle(nvrhi::BindingSetItem::Texture_SRV(0, m_ProbeData));
        m_RayDataTextureUAV = descriptorTable->CreateDescriptorHandle(nvrhi::BindingSetItem::Texture_UAV(0, m_ProbeRayData));
    }

    ~RtxgiVolumeVK() override
    {
        if (m_descriptorPool)
        {
            VkDevice device = m_Device->getNativeObject(nvrhi::ObjectTypes::VK_Device);
            vkDestroyDescriptorPool(device, m_descriptorPool, nullptr);
            m_descriptorPool = nullptr;
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

std::shared_ptr<RtxgiVolume> RtxgiVolume::CreateVK(
    nvrhi::IDevice* device,
    const std::weak_ptr<RtxgiIntegration>& parent,
    const std::shared_ptr<vfs::IFileSystem>& fs,
    const std::shared_ptr<engine::DescriptorTableManager>& descriptorTable,
    const rtxgi::DDGIVolumeDesc& volumeDesc)
{
    return std::make_shared<RtxgiVolumeVK>(device, parent, fs, descriptorTable, volumeDesc);
}

void RtxgiVolume::UpdateVolumesVK(nvrhi::ICommandList* commandList, const std::vector<std::shared_ptr<RtxgiVolume>>& volumes)
{
    VkCommandBuffer vkCmdBuffer = commandList->getNativeObject(nvrhi::ObjectTypes::VK_CommandBuffer);

    std::vector<rtxgi::vulkan::DDGIVolume*> ddgiVolumes;

    for (const auto& volume : volumes)
    {
        if (!volume->IsInitialized())
            continue;

        RtxgiVolumeVK* volumeVk = nvrhi::checked_cast<RtxgiVolumeVK*>(volume.get());
        ddgiVolumes.push_back(nvrhi::checked_cast<rtxgi::vulkan::DDGIVolume*>(volumeVk->GetVolume()));

        commandList->setTextureState(volumeVk->m_ProbeIrradiance, nvrhi::AllSubresources, nvrhi::ResourceStates::UnorderedAccess);
        commandList->setTextureState(volumeVk->m_ProbeDistance, nvrhi::AllSubresources, nvrhi::ResourceStates::UnorderedAccess);
        commandList->setTextureState(volumeVk->m_ProbeData, nvrhi::AllSubresources, nvrhi::ResourceStates::UnorderedAccess);
        commandList->setTextureState(volumeVk->m_ProbeRayData, nvrhi::AllSubresources, nvrhi::ResourceStates::UnorderedAccess);
    }
    commandList->commitBarriers();

    rtxgi::vulkan::UpdateDDGIVolumeProbes(vkCmdBuffer, uint32_t(ddgiVolumes.size()), ddgiVolumes.data());

    rtxgi::vulkan::RelocateDDGIVolumeProbes(vkCmdBuffer, uint32_t(ddgiVolumes.size()), ddgiVolumes.data());

    rtxgi::vulkan::ClassifyDDGIVolumeProbes(vkCmdBuffer, uint32_t(ddgiVolumes.size()), ddgiVolumes.data());

    commandList->clearState();
}

#endif // defined(WITH_RTXGI) && defined(USE_VK)