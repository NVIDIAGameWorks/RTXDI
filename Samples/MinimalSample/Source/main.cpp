/***************************************************************************
 # Copyright (c) 2020-2022, NVIDIA CORPORATION.  All rights reserved.
 #
 # NVIDIA CORPORATION and its licensors retain all intellectual property
 # and proprietary rights in and to this software, related documentation
 # and any modifications thereto.  Any use, reproduction, disclosure or
 # distribution of this software and related documentation without an express
 # license agreement from NVIDIA CORPORATION is strictly prohibited.
 **************************************************************************/

// Include this first just to test the cleanliness
#include <Rtxdi/DI/ReSTIRDI.h>

#include <donut/app/ApplicationBase.h>
#include <donut/app/Camera.h>
#include <donut/engine/ShaderFactory.h>
#include <donut/engine/CommonRenderPasses.h>
#include <donut/engine/TextureCache.h>
#include <donut/engine/BindingCache.h>
#include <donut/engine/Scene.h>
#include <donut/engine/DescriptorTableManager.h>
#include <donut/engine/View.h>
#include <donut/app/DeviceManager.h>
#include <donut/core/log.h>
#include <donut/core/vfs/VFS.h>
#include <donut/core/math/math.h>
#include <nvrhi/utils.h>

#include "RenderTargets.h"
#include "PrepareLightsPass.h"
#include "RenderPass.h"
#include "RtxdiResources.h"
#include "SampleScene.h"
#include "UserInterface.h"

#ifndef _WIN32
#include <unistd.h>
#else
extern "C" {
  // Prefer using the discrete GPU on Optimus laptops
  _declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001;
}
#endif

using namespace donut;
using namespace donut::math;
using namespace std::chrono;

class SceneRenderer : public app::ApplicationBase
{
public:
    SceneRenderer(app::DeviceManager* deviceManager, UIData& ui)
        : ApplicationBase(deviceManager)
        , m_bindingCache(deviceManager->GetDevice())
        , m_ui(ui)
    { 
    }

    [[nodiscard]] std::shared_ptr<engine::ShaderFactory> GetShaderFactory() const
    {
        return m_shaderFactory;
    }

    [[nodiscard]] std::shared_ptr<vfs::IFileSystem> GetRootFs() const
    {
        return m_rootFs;
    }

    bool Init()
    {
        std::filesystem::path mediaPath = app::GetDirectoryWithExecutable().parent_path() / "Assets/Media";
        if (!std::filesystem::exists(mediaPath))
        {
            mediaPath = app::GetDirectoryWithExecutable().parent_path().parent_path() / "Assets/Media";
            if (!std::filesystem::exists(mediaPath))
            {
                log::error("Couldn't locate the 'Assets/Media' folder.");
                return false;
            }
        }

        std::filesystem::path frameworkShaderPath = app::GetDirectoryWithExecutable() / "shaders/framework" / app::GetShaderTypeName(GetDevice()->getGraphicsAPI());
        std::filesystem::path appShaderPath = app::GetDirectoryWithExecutable() / "shaders/minimal-sample" / app::GetShaderTypeName(GetDevice()->getGraphicsAPI());

        log::debug("Mounting %s to %s", mediaPath.string().c_str(), "/Assets/Media");
        log::debug("Mounting %s to %s", frameworkShaderPath.string().c_str(), "/shaders/donut");
        log::debug("Mounting %s to %s", appShaderPath.string().c_str(), "/shaders/app");

        m_rootFs = std::make_shared<vfs::RootFileSystem>();
        m_rootFs->mount("/Assets/Media", mediaPath);
        m_rootFs->mount("/shaders/donut", frameworkShaderPath);
        m_rootFs->mount("/shaders/app", appShaderPath);

        m_shaderFactory = std::make_shared<engine::ShaderFactory>(GetDevice(), m_rootFs, "/shaders");
        m_CommonPasses = std::make_shared<engine::CommonRenderPasses>(GetDevice(), m_shaderFactory);

        {
            nvrhi::BindlessLayoutDesc bindlessLayoutDesc;
            bindlessLayoutDesc.firstSlot = 0;
            bindlessLayoutDesc.registerSpaces = {
                nvrhi::BindingLayoutItem::RawBuffer_SRV(1),
                nvrhi::BindingLayoutItem::Texture_SRV(2),
                nvrhi::BindingLayoutItem::Texture_UAV(3)
            };
            bindlessLayoutDesc.visibility = nvrhi::ShaderType::All;
            bindlessLayoutDesc.maxCapacity = 1024;
            m_bindlessLayout = GetDevice()->createBindlessLayout(bindlessLayoutDesc);
        }

        std::filesystem::path scenePath = "/Assets/Media/Arcade/Arcade.gltf";

        m_descriptorTableManager = std::make_shared<engine::DescriptorTableManager>(GetDevice(), m_bindlessLayout);

        m_TextureCache = std::make_shared<donut::engine::TextureCache>(GetDevice(), m_rootFs, m_descriptorTableManager);
        m_TextureCache->SetInfoLogSeverity(donut::log::Severity::Debug);
        
        m_scene = std::make_shared<SampleScene>(GetDevice(), *m_shaderFactory, m_rootFs, m_TextureCache, m_descriptorTableManager, nullptr);
        
        SetAsynchronousLoadingEnabled(true);
        BeginLoadingScene(m_rootFs, scenePath);
        GetDeviceManager()->SetVsyncEnabled(true);

        m_prepareLightsPass = std::make_unique<PrepareLightsPass>(GetDevice(), m_shaderFactory, m_CommonPasses, m_scene, m_bindlessLayout);
        m_renderPass = std::make_unique<RenderPass>(GetDevice(), m_shaderFactory, m_CommonPasses, m_scene, m_bindlessLayout);


        LoadShaders();
        
        m_commandList = GetDevice()->createCommandList();

        return true;
    }
    
    void SceneLoaded() override
    {
        ApplicationBase::SceneLoaded();

        m_scene->FinishedLoading(GetFrameIndex());
        
        m_camera.LookAt(float3(-1.658f, 1.577f, 1.69f), float3(-0.9645f, 1.2672f, 1.0396f));
        m_camera.SetMoveSpeed(3.f);
        
        m_scene->BuildMeshBLASes(GetDevice());

        m_commandList->open();
        m_scene->BuildTopLevelAccelStruct(m_commandList);
        m_commandList->close();
        GetDevice()->executeCommandList(m_commandList);

        GetDeviceManager()->SetVsyncEnabled(false);

        m_ui.isLoading = false;
    }
    
    void LoadShaders() const
    {
        m_prepareLightsPass->CreatePipeline();
        m_renderPass->CreatePipeline();
    }

    bool LoadScene(std::shared_ptr<vfs::IFileSystem> fs, const std::filesystem::path& sceneFileName) override 
    {
        if (m_scene->Load(sceneFileName))
        {
            return true;
        }

        return false;
    }

    bool KeyboardUpdate(int key, int scancode, int action, int mods) override
    {
        if (key == GLFW_KEY_GRAVE_ACCENT && action == GLFW_PRESS)
        {
            m_ui.showUI = !m_ui.showUI;
            return true;
        }

        if (mods == GLFW_MOD_CONTROL && key == GLFW_KEY_R && action == GLFW_PRESS)
        {
            m_ui.reloadShaders = true;
            return true;
        }

        m_camera.KeyboardUpdate(key, scancode, action, mods);

        return true;
    }

    bool MousePosUpdate(double xpos, double ypos) override
    {
        m_camera.MousePosUpdate(xpos, ypos);
        return true;
    }

    bool MouseButtonUpdate(int button, int action, int mods) override
    {
        m_camera.MouseButtonUpdate(button, action, mods);
        return true;
    }

    void Animate(float fElapsedTimeSeconds) override
    {
        if (m_ui.isLoading)
            return;
        
        m_camera.Animate(fElapsedTimeSeconds);
    }

    void BackBufferResized(const uint32_t width, const uint32_t height, const uint32_t sampleCount) override
    {
        if (m_renderTargets && m_renderTargets->Size.x == int(width) && m_renderTargets->Size.y == int(height))
            return;

        m_bindingCache.Clear();
        m_renderTargets = nullptr;
        m_restirDIContext = nullptr;
        m_rtxdiResources = nullptr;
    }
    
    void SetupView(const nvrhi::FramebufferInfoEx& fbinfo, uint effectiveFrameIndex)
    {
        nvrhi::Viewport windowViewport(float(fbinfo.width), float(fbinfo.height));

        nvrhi::Viewport renderViewport = windowViewport;
        m_view.SetViewport(renderViewport);
        m_view.SetPixelOffset(0.f);

        const float aspectRatio = windowViewport.width() / windowViewport.height();
        m_view.SetMatrices(m_camera.GetWorldToViewMatrix(), perspProjD3DStyleReverse(radians(60.f), aspectRatio, 0.01f));
        m_view.UpdateCache();

        if (m_viewPrevious.GetViewExtent().width() == 0)
            m_viewPrevious = m_view;
    }

    void SetupRenderPasses(const nvrhi::FramebufferInfoEx& fbinfo)
    {
        if (m_ui.reloadShaders)
        {
            GetDevice()->waitForIdle();

            m_shaderFactory->ClearCache();
            
            LoadShaders();

            m_ui.reloadShaders = false;
        }

        bool renderTargetsCreated = false;
        bool rtxdiResourcesCreated = false;
        
        if (!m_restirDIContext)
        {
            rtxdi::ReSTIRDIStaticParameters contextParams;
            contextParams.RenderWidth = fbinfo.width;
            contextParams.RenderHeight = fbinfo.height;

            m_restirDIContext = std::make_unique<rtxdi::ReSTIRDIContext>(contextParams);
        }

        if (!m_renderTargets)
        {
            m_renderTargets = std::make_unique<RenderTargets>(GetDevice(), int2(fbinfo.width, fbinfo.height));

            renderTargetsCreated = true;
        }

        if (!m_rtxdiResources)
        {
            uint32_t numEmissiveMeshes, numEmissiveTriangles;
            m_prepareLightsPass->CountLightsInScene(numEmissiveMeshes, numEmissiveTriangles);
            uint32_t numGeometryInstances = uint32_t(m_scene->GetSceneGraph()->GetGeometryInstancesCount());

            m_rtxdiResources = std::make_unique<RtxdiResources>(GetDevice(), *m_restirDIContext,
                numEmissiveMeshes, numEmissiveTriangles, numGeometryInstances);

            m_prepareLightsPass->CreateBindingSet(*m_rtxdiResources);
            
            rtxdiResourcesCreated = true;
        }
        
        if (renderTargetsCreated || rtxdiResourcesCreated)
        {
            m_renderPass->CreateBindingSet(
                m_scene->GetTopLevelAS(),
                *m_renderTargets,
                *m_rtxdiResources);
        }
    }

    void RenderSplashScreen(nvrhi::IFramebuffer* framebuffer) override
    {
        m_commandList->open();
        nvrhi::utils::ClearColorAttachment(m_commandList, framebuffer, 0, nvrhi::Color(0.f));
        m_commandList->close();
        GetDevice()->executeCommandList(m_commandList);
    }
    
    void RenderScene(nvrhi::IFramebuffer* framebuffer) override
    {   
        const auto& fbinfo = framebuffer->getFramebufferInfo();

        // Setup the viewports and transforms
        SetupView(fbinfo, GetFrameIndex());

        // Make sure that the passes and buffers are created and fit the current render size
        SetupRenderPasses(fbinfo);
        
        m_commandList->open();

        // Compute transforms, update the scene representation on the GPU in case something's animated
        m_scene->Refresh(m_commandList, GetFrameIndex());

        // Write the neighbor offset buffer data (only happens once)
        m_rtxdiResources->InitializeNeighborOffsets(m_commandList, m_restirDIContext->GetStaticParameters().NeighborOffsetCount);
        
        // The light indexing members of frameParameters are written by PrepareLightsPass below
        m_restirDIContext->SetFrameIndex(GetFrameIndex());

        // When the lights are static, there is no need to update them on every frame,
        // but it's simpler to do so.
        RTXDI_LightBufferParameters lightBufferParams = m_prepareLightsPass->Process(m_commandList);

        // Call the rendering pass - this includes primary rays, fused resampling, and shading
        m_renderPass->Render(m_commandList,
            *m_restirDIContext,
            m_view, m_viewPrevious,
            m_ui.lightingSettings,
            lightBufferParams);

        // Copy the render pass output to the swap chain
        m_CommonPasses->BlitTexture(m_commandList, framebuffer, m_renderTargets->HdrColor, &m_bindingCache);
        
        m_commandList->close();
        GetDevice()->executeCommandList(m_commandList);

        // Swap the even and odd frame buffers
        m_renderPass->NextFrame();
        m_renderTargets->NextFrame();

        m_viewPrevious = m_view;
    }

private:
    nvrhi::CommandListHandle m_commandList;

    nvrhi::BindingLayoutHandle m_bindlessLayout;

    std::shared_ptr<vfs::RootFileSystem> m_rootFs;
    std::shared_ptr<engine::ShaderFactory> m_shaderFactory;
    std::shared_ptr<SampleScene> m_scene;
    std::shared_ptr<engine::DescriptorTableManager> m_descriptorTableManager;
    std::unique_ptr<RenderTargets> m_renderTargets;
    app::FirstPersonCamera m_camera;
    engine::PlanarView m_view;
    engine::PlanarView m_viewPrevious;
    engine::BindingCache m_bindingCache;

    std::unique_ptr<rtxdi::ReSTIRDIContext> m_restirDIContext;
    std::unique_ptr<PrepareLightsPass> m_prepareLightsPass;
    std::unique_ptr<RenderPass> m_renderPass;
    std::unique_ptr<RtxdiResources> m_rtxdiResources;

    UIData& m_ui;
};

void ProcessCommandLine(int argc, char** argv, app::DeviceCreationParameters& deviceParams, nvrhi::GraphicsAPI& api)
{
    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "--debug") == 0)
        {
            deviceParams.enableDebugRuntime = true;
            deviceParams.enableNvrhiValidationLayer = true;
        }
        else if (strcmp(argv[i], "--vk") == 0)
        {
            api = nvrhi::GraphicsAPI::VULKAN;
        }
        else
        {
            log::error("Unknown command line argument: %s", argv[i]);
            exit(1);
        }
    }
}

#if defined(_WIN32)
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
#else
int main(int argc, char** argv)
#endif
{
    app::DeviceCreationParameters deviceParams;
    deviceParams.swapChainBufferCount = 3;
    deviceParams.enableRayTracingExtensions = true;
    deviceParams.backBufferWidth = 1920;
    deviceParams.backBufferHeight = 1080;
    deviceParams.vsyncEnabled = true;
    deviceParams.infoLogSeverity = log::Severity::Debug;

    UIData ui;
#if DONUT_WITH_DX12
    nvrhi::GraphicsAPI api = nvrhi::GraphicsAPI::D3D12;
#else
    nvrhi::GraphicsAPI api = nvrhi::GraphicsAPI::VULKAN;
#endif

#if defined(_WIN32)
    ProcessCommandLine(__argc, __argv, deviceParams, api);
#else
    ProcessCommandLine(argc, argv, deviceParams, api);
#endif

    std::unique_ptr<app::DeviceManager> deviceManager(app::DeviceManager::Create(api));
    
    const char* apiString = nvrhi::utils::GraphicsAPIToString(deviceManager->GetGraphicsAPI());

    std::string windowTitle = "Hello RTXDI (" + std::string(apiString) + ")";
    
    log::SetErrorMessageCaption(windowTitle.c_str());

    if (!deviceManager->CreateWindowDeviceAndSwapChain(deviceParams, windowTitle.c_str()))
    {
        log::error("Cannot initialize a %s graphics device.", apiString);
        return 1;
    }
    
    bool rayQuerySupported = deviceManager->GetDevice()->queryFeatureSupport(nvrhi::Feature::RayQuery);

    if (!rayQuerySupported)
    {
        log::error("The GPU (%s) or its driver does not support Ray Queries.", deviceManager->GetRendererString());
        return 1;
    }

    {
        SceneRenderer sceneRenderer(deviceManager.get(), ui);
        if (sceneRenderer.Init())
        {
            UserInterface userInterface(deviceManager.get(), *sceneRenderer.GetRootFs(), ui);
            userInterface.Init(sceneRenderer.GetShaderFactory());

            deviceManager->AddRenderPassToBack(&sceneRenderer);
            deviceManager->AddRenderPassToBack(&userInterface);
            deviceManager->RunMessageLoop();
            deviceManager->GetDevice()->waitForIdle();
            deviceManager->RemoveRenderPass(&sceneRenderer);
            deviceManager->RemoveRenderPass(&userInterface);
        }
    }
    
    deviceManager->Shutdown();

    return 0;
}
