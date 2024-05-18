/***************************************************************************
 # Copyright (c) 2021-2023, NVIDIA CORPORATION.  All rights reserved.
 #
 # NVIDIA CORPORATION and its licensors retain all intellectual property
 # and proprietary rights in and to this software, related documentation
 # and any modifications thereto.  Any use, reproduction, disclosure or
 # distribution of this software and related documentation without an express
 # license agreement from NVIDIA CORPORATION is strictly prohibited.
 **************************************************************************/

#if WITH_DLSS && DONUT_WITH_VULKAN

#include <vulkan/vulkan.h>
#include <nvsdk_ngx_vk.h>
#include <nvsdk_ngx_helpers_vk.h>
#include <nvrhi/vulkan.h>

#include "DLSS.h"
#include "RenderTargets.h"
#include <donut/engine/View.h>
#include <donut/app/ApplicationBase.h>
#include <donut/core/log.h>

using namespace donut;

static void NVSDK_CONV NgxLogCallback(const char* message, NVSDK_NGX_Logging_Level loggingLevel, NVSDK_NGX_Feature sourceComponent)
{
    log::info("NGX: %s", message);
}

class DLSS_VK : public DLSS
{
public:
    DLSS_VK(nvrhi::IDevice* device, donut::engine::ShaderFactory& shaderFactory)
        : DLSS(device, shaderFactory)
    {
        VkInstance vkInstance = device->getNativeObject(nvrhi::ObjectTypes::VK_Instance);
        VkPhysicalDevice vkPhysicalDevice = device->getNativeObject(nvrhi::ObjectTypes::VK_PhysicalDevice);
        VkDevice vkDevice= device->getNativeObject(nvrhi::ObjectTypes::VK_Device);

        auto executablePath = donut::app::GetDirectoryWithExecutable().generic_string();
        std::wstring executablePathW;
        executablePathW.assign(executablePath.begin(), executablePath.end());

        NVSDK_NGX_FeatureCommonInfo featureCommonInfo = {};
        featureCommonInfo.LoggingInfo.LoggingCallback = NgxLogCallback;
        featureCommonInfo.LoggingInfo.MinimumLoggingLevel = NVSDK_NGX_LOGGING_LEVEL_OFF;
        featureCommonInfo.LoggingInfo.DisableOtherLoggingSinks = true;

        NVSDK_NGX_Result result = NVSDK_NGX_VULKAN_Init(c_ApplicationID,
            executablePathW.c_str(), vkInstance, vkPhysicalDevice, vkDevice, &featureCommonInfo);

        if (result != NVSDK_NGX_Result_Success)
        {
            log::warning("Cannot initialize NGX, Result = 0x%08x (%ls)", result, GetNGXResultAsString(result));
            return;
        }
        
        result = NVSDK_NGX_VULKAN_GetCapabilityParameters(&m_Parameters);

        if (result != NVSDK_NGX_Result_Success)
            return;

        int dlssAvailable = 0;
        result = m_Parameters->Get(NVSDK_NGX_Parameter_SuperSampling_Available, &dlssAvailable);
        if (result != NVSDK_NGX_Result_Success || !dlssAvailable)
        {
            result = NVSDK_NGX_Result_Fail;
            NVSDK_NGX_Parameter_GetI(m_Parameters, NVSDK_NGX_Parameter_SuperSampling_FeatureInitResult, (int*)&result);
            log::warning("NVIDIA DLSS is not available on this system, FeatureInitResult = 0x%08x (%ls)",
                result, GetNGXResultAsString(result));
            return;
        }

        m_FeatureSupported = true;
    }

    void SetRenderSize(
        uint32_t inputWidth, uint32_t inputHeight,
        uint32_t outputWidth, uint32_t outputHeight) override
    {
        if (!m_FeatureSupported)
            return;

        if (m_InputWidth == inputWidth && m_InputHeight == inputHeight && m_OutputWidth == outputWidth && m_OutputHeight == outputHeight)
            return;
        
        if (m_DlssHandle)
        {
            m_Device->waitForIdle();
            NVSDK_NGX_VULKAN_ReleaseFeature(m_DlssHandle);
            m_DlssHandle = nullptr;
        }

        m_FeatureCommandList->open();
        VkCommandBuffer vkCmdBuf = m_FeatureCommandList->getNativeObject(nvrhi::ObjectTypes::VK_CommandBuffer);

        NVSDK_NGX_DLSS_Create_Params dlssParams = {};
        dlssParams.Feature.InWidth = inputWidth;
        dlssParams.Feature.InHeight = inputHeight;
        dlssParams.Feature.InTargetWidth = outputWidth;
        dlssParams.Feature.InTargetHeight = outputHeight;
        dlssParams.Feature.InPerfQualityValue = NVSDK_NGX_PerfQuality_Value_MaxQuality;
        dlssParams.InFeatureCreateFlags =
            NVSDK_NGX_DLSS_Feature_Flags_IsHDR |
            NVSDK_NGX_DLSS_Feature_Flags_DepthInverted |
            NVSDK_NGX_DLSS_Feature_Flags_MVLowRes;

        NVSDK_NGX_Result result = NGX_VULKAN_CREATE_DLSS_EXT(vkCmdBuf, 1, 1, &m_DlssHandle, m_Parameters, &dlssParams);

        m_FeatureCommandList->close();
        m_Device->executeCommandList(m_FeatureCommandList);

        if (result != NVSDK_NGX_Result_Success)
        {
            log::warning("Failed to create a DLSS feautre, Result = 0x%08x (%ls)", result, GetNGXResultAsString(result));
            return;
        }

        m_IsAvailable = true;

        m_InputWidth = inputWidth;
        m_InputHeight = inputHeight;
        m_OutputWidth = outputWidth;
        m_OutputHeight = outputHeight;
    }

    static void FillTextureResource(NVSDK_NGX_Resource_VK& resource, nvrhi::ITexture* texture)
    {
        const nvrhi::TextureDesc& desc = texture->getDesc();
        resource.ReadWrite = desc.isUAV;
        resource.Type = NVSDK_NGX_RESOURCE_VK_TYPE_VK_IMAGEVIEW;

        auto& viewInfo = resource.Resource.ImageViewInfo;
        viewInfo.Image = texture->getNativeObject(nvrhi::ObjectTypes::VK_Image);
        viewInfo.ImageView = texture->getNativeView(nvrhi::ObjectTypes::VK_ImageView);
        viewInfo.Format = VkFormat(nvrhi::vulkan::convertFormat(desc.format));
        viewInfo.Width = desc.width;
        viewInfo.Height = desc.height;
        viewInfo.SubresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.SubresourceRange.baseArrayLayer = 0;
        viewInfo.SubresourceRange.layerCount = 1;
        viewInfo.SubresourceRange.baseMipLevel = 0;
        viewInfo.SubresourceRange.levelCount = 1;
    }
    
    void Render(
        nvrhi::ICommandList* commandList,
        const RenderTargets& renderTargets,
        nvrhi::IBuffer* toneMapperExposureBuffer,
        float exposureScale,
        float sharpness,
        bool gbufferWasRasterized,
        bool resetHistory,
        const donut::engine::PlanarView& view,
        const donut::engine::PlanarView& viewPrev) override
    {
        if (!m_IsAvailable)
            return;

        ComputeExposure(commandList, toneMapperExposureBuffer, exposureScale);

        VkCommandBuffer vkCmdBuf = commandList->getNativeObject(nvrhi::ObjectTypes::VK_CommandBuffer);

        nvrhi::ITexture* depthTexture = gbufferWasRasterized ? renderTargets.DeviceDepth : renderTargets.DeviceDepthUAV;

        NVSDK_NGX_Resource_VK inColorResource;
        NVSDK_NGX_Resource_VK outColorResource;
        NVSDK_NGX_Resource_VK depthResource;
        NVSDK_NGX_Resource_VK motionVectorResource;
        NVSDK_NGX_Resource_VK exposureResource;
        FillTextureResource(inColorResource, renderTargets.HdrColor);
        FillTextureResource(outColorResource, renderTargets.ResolvedColor);
        FillTextureResource(depthResource, depthTexture);
        FillTextureResource(motionVectorResource, renderTargets.MotionVectors);
        FillTextureResource(exposureResource, m_ExposureTexture);

        commandList->setTextureState(renderTargets.HdrColor, nvrhi::AllSubresources, nvrhi::ResourceStates::ShaderResource);
        commandList->setTextureState(renderTargets.ResolvedColor, nvrhi::AllSubresources, nvrhi::ResourceStates::UnorderedAccess);
        commandList->setTextureState(depthTexture, nvrhi::AllSubresources, nvrhi::ResourceStates::ShaderResource);
        commandList->setTextureState(renderTargets.MotionVectors, nvrhi::AllSubresources, nvrhi::ResourceStates::ShaderResource);
        commandList->setTextureState(m_ExposureTexture, nvrhi::AllSubresources, nvrhi::ResourceStates::ShaderResource);
        commandList->commitBarriers();
        
        NVSDK_NGX_VK_DLSS_Eval_Params evalParams = {};
        evalParams.Feature.pInColor = &inColorResource;
        evalParams.Feature.pInOutput = &outColorResource;
        evalParams.Feature.InSharpness = sharpness;
        evalParams.pInDepth = &depthResource;
        evalParams.pInMotionVectors = &motionVectorResource;
        evalParams.pInExposureTexture = &exposureResource;
        evalParams.InReset = resetHistory;
        evalParams.InJitterOffsetX = view.GetPixelOffset().x;
        evalParams.InJitterOffsetY = view.GetPixelOffset().y;
        evalParams.InRenderSubrectDimensions.Width = view.GetViewExtent().width();
        evalParams.InRenderSubrectDimensions.Height = view.GetViewExtent().height();

        NVSDK_NGX_Result result = NGX_VULKAN_EVALUATE_DLSS_EXT(vkCmdBuf, m_DlssHandle, m_Parameters, &evalParams);

        commandList->clearState();

        if (result != NVSDK_NGX_Result_Success)
        {
            log::warning("Failed to evaluate DLSS feature: 0x%08x", result);
            return;
        }
    }

    ~DLSS_VK() override
    {
        if (m_DlssHandle)
        {
            NVSDK_NGX_VULKAN_ReleaseFeature(m_DlssHandle);
            m_DlssHandle = nullptr;
        }

        if (m_Parameters)
        {
            NVSDK_NGX_VULKAN_DestroyParameters(m_Parameters);
            m_Parameters = nullptr;
        }

        NVSDK_NGX_VULKAN_Shutdown();
    }
};

std::unique_ptr<DLSS> DLSS::CreateVK(nvrhi::IDevice* device, donut::engine::ShaderFactory& shaderFactory)
{
    return std::make_unique<DLSS_VK>(device, shaderFactory);
}

void DLSS::GetRequiredVulkanExtensions(std::vector<std::string>& instanceExtensions, std::vector<std::string>& deviceExtensions)
{
    unsigned int instanceExtCount = 0;
    unsigned int deviceExtCount = 0;
    const char** pInstanceExtensions = nullptr;
    const char** pDeviceExtensions = nullptr;
    NVSDK_NGX_VULKAN_RequiredExtensions(&instanceExtCount, &pInstanceExtensions, &deviceExtCount, &pDeviceExtensions);

    for (unsigned int i = 0; i < instanceExtCount; i++)
    {
        instanceExtensions.push_back(pInstanceExtensions[i]);
    }

    for (unsigned int i = 0; i < deviceExtCount; i++)
    {
        // VK_EXT_buffer_device_address is incompatible with Vulkan 1.2 and causes a validation error
        if (!strcmp(pDeviceExtensions[i], VK_EXT_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME))
            continue;

        deviceExtensions.push_back(pDeviceExtensions[i]);
    }
}

#endif
