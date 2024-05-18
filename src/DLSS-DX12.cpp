/***************************************************************************
 # Copyright (c) 2021-2023, NVIDIA CORPORATION.  All rights reserved.
 #
 # NVIDIA CORPORATION and its licensors retain all intellectual property
 # and proprietary rights in and to this software, related documentation
 # and any modifications thereto.  Any use, reproduction, disclosure or
 # distribution of this software and related documentation without an express
 # license agreement from NVIDIA CORPORATION is strictly prohibited.
 **************************************************************************/

#if WITH_DLSS && DONUT_WITH_DX12

#include <nvsdk_ngx.h>
#include <nvsdk_ngx_helpers.h>

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

class DLSS_DX12 : public DLSS
{
public:
    DLSS_DX12(nvrhi::IDevice* device, donut::engine::ShaderFactory& shaderFactory)
        : DLSS(device, shaderFactory)
    {
        ID3D12Device* d3ddevice = device->getNativeObject(nvrhi::ObjectTypes::D3D12_Device);

        auto executablePath = donut::app::GetDirectoryWithExecutable().generic_string();
        std::wstring executablePathW;
        executablePathW.assign(executablePath.begin(), executablePath.end());
        
        NVSDK_NGX_FeatureCommonInfo featureCommonInfo = {};
        featureCommonInfo.LoggingInfo.LoggingCallback = NgxLogCallback;
        featureCommonInfo.LoggingInfo.MinimumLoggingLevel = NVSDK_NGX_LOGGING_LEVEL_OFF;
        featureCommonInfo.LoggingInfo.DisableOtherLoggingSinks = true;

        NVSDK_NGX_Result result = NVSDK_NGX_D3D12_Init(c_ApplicationID, executablePathW.c_str(), d3ddevice, &featureCommonInfo);

        if (result != NVSDK_NGX_Result_Success)
        {
            log::warning("Cannot initialize NGX, Result = 0x%08x (%ls)", result, GetNGXResultAsString(result));
            return;
        }

        result = NVSDK_NGX_D3D12_GetCapabilityParameters(&m_Parameters);

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
            NVSDK_NGX_D3D12_ReleaseFeature(m_DlssHandle);
            m_DlssHandle = nullptr;
        }

        m_FeatureCommandList->open();
        ID3D12GraphicsCommandList* d3dcmdlist = m_FeatureCommandList->getNativeObject(nvrhi::ObjectTypes::D3D12_GraphicsCommandList);

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

        NVSDK_NGX_Result result = NGX_D3D12_CREATE_DLSS_EXT(d3dcmdlist, 1, 1, &m_DlssHandle, m_Parameters, &dlssParams);
        
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

        commandList->beginMarker("DLSS");

        ComputeExposure(commandList, toneMapperExposureBuffer, exposureScale);

        ID3D12GraphicsCommandList* d3dcmdlist = commandList->getNativeObject(nvrhi::ObjectTypes::D3D12_GraphicsCommandList);

        nvrhi::ITexture* depthTexture = gbufferWasRasterized ? renderTargets.DeviceDepth : renderTargets.DeviceDepthUAV;

        commandList->setTextureState(renderTargets.HdrColor, nvrhi::AllSubresources, nvrhi::ResourceStates::ShaderResource);
        commandList->setTextureState(renderTargets.ResolvedColor, nvrhi::AllSubresources, nvrhi::ResourceStates::UnorderedAccess);
        commandList->setTextureState(depthTexture, nvrhi::AllSubresources, nvrhi::ResourceStates::ShaderResource);
        commandList->setTextureState(renderTargets.MotionVectors, nvrhi::AllSubresources, nvrhi::ResourceStates::ShaderResource);
        commandList->setTextureState(m_ExposureTexture, nvrhi::AllSubresources, nvrhi::ResourceStates::ShaderResource);
        commandList->commitBarriers();

        NVSDK_NGX_D3D12_DLSS_Eval_Params evalParams = {};
        evalParams.Feature.pInColor = renderTargets.HdrColor->getNativeObject(nvrhi::ObjectTypes::D3D12_Resource);
        evalParams.Feature.pInOutput = renderTargets.ResolvedColor->getNativeObject(nvrhi::ObjectTypes::D3D12_Resource);
        evalParams.Feature.InSharpness = sharpness;
        evalParams.pInDepth = depthTexture->getNativeObject(nvrhi::ObjectTypes::D3D12_Resource);
        evalParams.pInMotionVectors = renderTargets.MotionVectors->getNativeObject(nvrhi::ObjectTypes::D3D12_Resource);
        evalParams.pInExposureTexture = m_ExposureTexture->getNativeObject(nvrhi::ObjectTypes::D3D12_Resource);
        evalParams.InReset = resetHistory;
        evalParams.InJitterOffsetX = view.GetPixelOffset().x;
        evalParams.InJitterOffsetY = view.GetPixelOffset().y;
        evalParams.InRenderSubrectDimensions.Width = view.GetViewExtent().width();
        evalParams.InRenderSubrectDimensions.Height = view.GetViewExtent().height();

        NVSDK_NGX_Result result = NGX_D3D12_EVALUATE_DLSS_EXT(d3dcmdlist, m_DlssHandle, m_Parameters, &evalParams);

        commandList->clearState();

        commandList->endMarker();

        if (result != NVSDK_NGX_Result_Success)
        {
            log::warning("Failed to evaluate DLSS feature: 0x%08x", result);
            return;
        }
    }

    ~DLSS_DX12() override
    {
        if (m_DlssHandle)
        {
            NVSDK_NGX_D3D12_ReleaseFeature(m_DlssHandle);
            m_DlssHandle = nullptr;
        }

        if (m_Parameters)
        {
            NVSDK_NGX_D3D12_DestroyParameters(m_Parameters);
            m_Parameters = nullptr;
        }

        NVSDK_NGX_D3D12_Shutdown();
    }
};

std::unique_ptr<DLSS> DLSS::CreateDX12(nvrhi::IDevice* device, donut::engine::ShaderFactory& shaderFactory)
{
    return std::make_unique<DLSS_DX12>(device, shaderFactory);
}

#endif
