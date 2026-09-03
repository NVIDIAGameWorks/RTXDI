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

#include <string>

#include <nvrhi/utils.h>

#include "App/CommandLineArgs.h"
#include "App/LoggingCallback.h"
#include "App/SceneRenderer.h"
#if DONUT_WITH_DX12
#include "nvapi.h"
#endif
#include "SharedShaderInclude/NvapiIntegration.h"

#include <donut/render/DLSS.h>

#ifndef _WIN32
#include <unistd.h>
#else
extern "C" {
  // Prefer using the discrete GPU on Optimus laptops
  _declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001;
}
#endif

extern const std::string g_ApplicationTitle = "RTX Direct Illumination SDK Sample";

void ApplyLoggingSettings(const CommandLineArguments& args, const std::string& windowTitle)
{
    if (args.appStartupSettings.verbose)
        log::SetMinSeverity(log::Severity::Debug);
    log::SetErrorMessageCaption(windowTitle.c_str());

    // Logging output settings for shader debug print
    donut::log::SetMinSeverity(donut::log::Severity::Debug);
    donut::log::EnableOutputToConsole(true);
    donut::log::EnableOutputToDebug(true);
}

#if DONUT_WITH_VULKAN

void ProcessVulkanDeviceParams(app::DeviceCreationParameters& deviceParams, const CommandLineArguments& args)
{
    if (args.graphicsApi == nvrhi::GraphicsAPI::VULKAN)
    {
        // Required for SPV_KHR_compute_shader_derivatives (used by shaders that need ddx/ddy in compute)
        deviceParams.requiredVulkanDeviceExtensions.push_back(VK_KHR_COMPUTE_SHADER_DERIVATIVES_EXTENSION_NAME);
        // Required for vkCmdPipelineBarrier2 and other synchronization2 APIs
        deviceParams.requiredVulkanDeviceExtensions.push_back(VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME);

        // Set the extra device feature bit(s)
        deviceParams.deviceCreateInfoCallback = [](VkDeviceCreateInfo& info) {
            auto features = const_cast<VkPhysicalDeviceFeatures*>(info.pEnabledFeatures);
            features->fragmentStoresAndAtomics = VK_TRUE;
#if DONUT_WITH_DLSS
            features->shaderStorageImageWriteWithoutFormat = VK_TRUE;
#endif

            // Prepend required feature structs
            static VkPhysicalDeviceComputeShaderDerivativesFeaturesKHR computeDerivativesFeatures = {};
            computeDerivativesFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COMPUTE_SHADER_DERIVATIVES_FEATURES_KHR;
            computeDerivativesFeatures.pNext = (void*)info.pNext;
            computeDerivativesFeatures.computeDerivativeGroupQuads = VK_TRUE;
            computeDerivativesFeatures.computeDerivativeGroupLinear = VK_FALSE;
            info.pNext = &computeDerivativesFeatures;

            static VkPhysicalDeviceSynchronization2FeaturesKHR synchronization2features = {};
            synchronization2features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES_KHR;
            synchronization2features.pNext = (void*)info.pNext;
            synchronization2features.synchronization2 = VK_TRUE;
            info.pNext = &synchronization2features;

            void* pNext = (void*)info.pNext;
            while (pNext != nullptr)
            {
                VkPhysicalDeviceVulkan11Features* featureStruct = (VkPhysicalDeviceVulkan11Features*)pNext;
                if (featureStruct->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES)
                {
                    featureStruct->uniformAndStorageBuffer16BitAccess = VK_TRUE;
                    break;
                }
                pNext = featureStruct->pNext;
            }

        };

#if DONUT_WITH_DLSS
        donut::render::DLSS::GetRequiredVulkanExtensions(
            deviceParams.optionalVulkanInstanceExtensions,
            deviceParams.optionalVulkanDeviceExtensions);

        // Currently, DLSS on Vulkan produces these validation errors. Silence them.
        // Re-evaluate when updating DLSS.

        // VkDeviceCreateInfo->ppEnabledExtensionNames must not contain both VK_KHR_buffer_device_address and VK_EXT_buffer_device_address
        deviceParams.ignoredVulkanValidationMessageLocations.push_back(0xffffffff83a6bda8);

        // If VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT is set, bufferDeviceAddress must be enabled.
        deviceParams.ignoredVulkanValidationMessageLocations.push_back(0xfffffffff972dfbf);

        // vkCmdCuLaunchKernelNVX: required parameter pLaunchInfo->pParams specified as NULL.
        deviceParams.ignoredVulkanValidationMessageLocations.push_back(0x79de34d4);
#endif
    }
}
#endif

void ProcessApiSpecificDeviceParams(app::DeviceCreationParameters& deviceParams, const CommandLineArguments& args)
{
#if DONUT_WITH_VULKAN
    ProcessVulkanDeviceParams(deviceParams, args);
#endif
}

bool IsHardwareSupported(app::DeviceManager* deviceManager)
{
    bool rayPipelineSupported = deviceManager->GetDevice()->queryFeatureSupport(nvrhi::Feature::RayTracingPipeline);
    bool rayQuerySupported = deviceManager->GetDevice()->queryFeatureSupport(nvrhi::Feature::RayQuery);

    if (!rayPipelineSupported && !rayQuerySupported)
    {
        log::error("The GPU (%s) or its driver does not support ray tracing.", deviceManager->GetRendererString());
        return false;
    }

    return true;
}

void ToggleBackgroundOptimization(const CommandLineArguments& args, app::DeviceManager* deviceManager)
{
#if DONUT_WITH_DX12
    if (args.graphicsApi == nvrhi::GraphicsAPI::D3D12 && args.disableBackgroundOptimization)
    {
        // On DX12, optionally disable the background shader optimization because it leads to stutter on some NV driver versions (496.61 specifically).

        nvrhi::RefCountPtr<ID3D12Device> device = (ID3D12Device*)deviceManager->GetDevice()->getNativeObject(nvrhi::ObjectTypes::D3D12_Device);
        nvrhi::RefCountPtr<ID3D12Device6> device6;

        if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&device6))))
        {
            HRESULT hr = device6->SetBackgroundProcessingMode(
                D3D12_BACKGROUND_PROCESSING_MODE_DISABLE_PROFILING_BY_SYSTEM,
                D3D12_MEASUREMENTS_ACTION_DISCARD_PREVIOUS,
                nullptr, nullptr);

            if (FAILED(hr))
            {
                log::info("Call to ID3D12Device6::SetBackgroundProcessingMode(...) failed, HRESULT = 0x%08x. Expect stutter.", hr);
            }
        }
    }
#endif
}

void EnableNVAPI(nvrhi::GraphicsAPI graphicsApi, app::DeviceManager* deviceManager)
{
#if DONUT_WITH_DX12
    if (graphicsApi == nvrhi::GraphicsAPI::D3D12)
    {
        nvrhi::RefCountPtr<ID3D12Device> device = (ID3D12Device*)deviceManager->GetDevice()->getNativeObject(nvrhi::ObjectTypes::D3D12_Device);
        nvrhi::RefCountPtr<ID3D12Device6> device6;

        if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&device6))))
        {
            NvAPI_D3D12_SetNvShaderExtnSlotSpace(device6, RTXDI_NVAPI_SHADER_EXT_SLOT, RTXDI_NVAPI_SHADER_EXT_SPACE_D3D12);
        }
    }
#endif
}

void RunSample(app::DeviceManager* deviceManager, const CommandLineArguments& args, UIData& ui)
{
    SceneRenderer sceneRenderer(deviceManager, ui, args.appStartupSettings);
    if (sceneRenderer.Init())
    {
        UserInterface userInterface(deviceManager, *sceneRenderer.GetRootFs(), ui);
        userInterface.Init(sceneRenderer.GetShaderFactory());

        deviceManager->AddRenderPassToBack(&sceneRenderer);
        deviceManager->AddRenderPassToBack(&userInterface);
        deviceManager->RunMessageLoop();
        deviceManager->GetDevice()->waitForIdle();
        deviceManager->RemoveRenderPass(&sceneRenderer);
        deviceManager->RemoveRenderPass(&userInterface);
    }

    // Clear the shared pointers from 'ui' to graphics objects
    ui.resources.reset();
}

#if defined(_WIN32) && !defined(IS_CONSOLE_APP)
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
#else
int main(int argc, char** argv)
#endif
{
    log::SetCallback(&ApplicationLogCallback);

    app::DeviceCreationParameters deviceParams;
#if DONUT_WITH_AFTERMATH
    deviceParams.enableAftermath = true;
#endif
    deviceParams.swapChainBufferCount = 3;
    deviceParams.enableRayTracingExtensions = true;
    deviceParams.backBufferWidth = 1920;
    deviceParams.backBufferHeight = 1080;
    deviceParams.vsyncEnabled = true;
    deviceParams.infoLogSeverity = log::Severity::Debug;

    UIData ui;
    CommandLineArguments args;

#if defined(_WIN32) && !defined(IS_CONSOLE_APP)
    ProcessCommandLine(__argc, __argv, g_ApplicationTitle, deviceParams, ui, args);
#else
    ProcessCommandLine(argc, argv, g_ApplicationTitle, deviceParams, ui, args);
#endif

    std::unique_ptr<app::DeviceManager> deviceManager(app::DeviceManager::Create(args.graphicsApi));
    const char* apiString = nvrhi::utils::GraphicsAPIToString(deviceManager->GetGraphicsAPI());
    std::string windowTitle = g_ApplicationTitle + " (" + std::string(apiString) + ")";

    ApplyLoggingSettings(args, windowTitle);

#ifdef _WIN32
    // Disable Window scaling.
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
#endif

    ProcessApiSpecificDeviceParams(deviceParams, args);
    if (!deviceManager->CreateWindowDeviceAndSwapChain(deviceParams, windowTitle.c_str()))
    {
        log::error("Cannot initialize a %s graphics device.", apiString);
        return 1;
    }
    ui.graphicsAPI = args.graphicsApi;

    if (!IsHardwareSupported(deviceManager.get()))
        return 1;

    ToggleBackgroundOptimization(args, deviceManager.get());
    EnableNVAPI(args.graphicsApi, deviceManager.get());

    RunSample(deviceManager.get(), args, ui);
    
    deviceManager->Shutdown();

    return 0;
}
