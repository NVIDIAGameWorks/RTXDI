/***************************************************************************
 # Copyright (c) 2020-2023, NVIDIA CORPORATION.  All rights reserved.
 #
 # NVIDIA CORPORATION and its licensors retain all intellectual property
 # and proprietary rights in and to this software, related documentation
 # and any modifications thereto.  Any use, reproduction, disclosure or
 # distribution of this software and related documentation without an express
 # license agreement from NVIDIA CORPORATION is strictly prohibited.
 **************************************************************************/

// Include this first just to test the cleanliness
#include <rtxdi/ImportanceSamplingContext.h>

#include <donut/render/ToneMappingPasses.h>
#include <donut/render/TemporalAntiAliasingPass.h>
#include <donut/app/ApplicationBase.h>
#include <donut/app/Camera.h>
#include <donut/engine/ShaderFactory.h>
#include <donut/engine/CommonRenderPasses.h>
#include <donut/engine/TextureCache.h>
#include <donut/render/BloomPass.h>
#include <donut/render/SkyPass.h>
#include <donut/engine/Scene.h>
#include <donut/engine/FramebufferFactory.h>
#include <donut/engine/DescriptorTableManager.h>
#include <donut/engine/View.h>
#include <donut/engine/IesProfile.h>
#include <donut/app/DeviceManager.h>
#include <donut/core/log.h>
#include <donut/core/vfs/VFS.h>
#include <donut/core/math/math.h>
#include <nvrhi/utils.h>
#ifdef DONUT_WITH_TASKFLOW
#include <taskflow/taskflow.hpp>
#endif

#include "RenderTargets.h"
#include "ConfidencePass.h"
#include "FilterGradientsPass.h"
#include "CompositingPass.h"
#include "AccumulationPass.h"
#include "GBufferPass.h"
#include "GlassPass.h"
#include "PrepareLightsPass.h"
#include "RenderEnvironmentMapPass.h"
#include "GenerateMipsPass.h"
#include "LightingPasses.h"
#include "RtxdiResources.h"
#include "SampleScene.h"
#include "Profiler.h"
#include "UserInterface.h"
#include "VisualizationPass.h"
#include "Testing.h"
#include "DebugViz/DebugVizPasses.h"

#if WITH_NRD
#include "NrdIntegration.h"
#endif

#if WITH_DLSS
#include "DLSS.h"
#endif

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
#include "../shaders/ShaderParameters.h"

static int g_ExitCode = 0;

class SceneRenderer : public app::ApplicationBase
{
private:
    nvrhi::CommandListHandle m_CommandList;
    
    nvrhi::BindingLayoutHandle m_BindlessLayout;

    std::shared_ptr<vfs::RootFileSystem> m_RootFs;
    std::shared_ptr<engine::ShaderFactory> m_ShaderFactory;
    std::shared_ptr<SampleScene> m_Scene;
    std::shared_ptr<engine::DescriptorTableManager> m_DescriptorTableManager;
    std::unique_ptr<render::ToneMappingPass> m_ToneMappingPass;
    std::unique_ptr<render::TemporalAntiAliasingPass> m_TemporalAntiAliasingPass;
    std::unique_ptr<render::BloomPass> m_BloomPass;
    std::shared_ptr<RenderTargets> m_RenderTargets;
    app::FirstPersonCamera m_Camera;
    engine::PlanarView m_View;
    engine::PlanarView m_ViewPrevious;
    engine::PlanarView m_UpscaledView;
    std::shared_ptr<engine::DirectionalLight> m_SunLight;
    std::shared_ptr<EnvironmentLight> m_EnvironmentLight;
    std::shared_ptr<engine::LoadedTexture> m_EnvironmentMap;
    engine::BindingCache m_BindingCache;

    std::unique_ptr<rtxdi::ImportanceSamplingContext> m_isContext;
    std::unique_ptr<RaytracedGBufferPass> m_GBufferPass;
    std::unique_ptr<RasterizedGBufferPass> m_RasterizedGBufferPass;
    std::unique_ptr<PostprocessGBufferPass> m_PostprocessGBufferPass;
    std::unique_ptr<GlassPass> m_GlassPass;
    std::unique_ptr<FilterGradientsPass> m_FilterGradientsPass;
    std::unique_ptr<ConfidencePass> m_ConfidencePass;
    std::unique_ptr<CompositingPass> m_CompositingPass;
    std::unique_ptr<AccumulationPass> m_AccumulationPass;
    std::unique_ptr<PrepareLightsPass> m_PrepareLightsPass;
    std::unique_ptr<RenderEnvironmentMapPass> m_RenderEnvironmentMapPass;
    std::unique_ptr<GenerateMipsPass> m_EnvironmentMapPdfMipmapPass;
    std::unique_ptr<GenerateMipsPass> m_LocalLightPdfMipmapPass;
    std::unique_ptr<LightingPasses> m_LightingPasses;
    std::unique_ptr<VisualizationPass> m_VisualizationPass;
    std::unique_ptr<RtxdiResources> m_RtxdiResources;
    std::unique_ptr<engine::IesProfileLoader> m_IesProfileLoader;
    std::shared_ptr<Profiler> m_Profiler;
    std::unique_ptr<DebugVizPasses> m_DebugVizPasses;

    uint32_t m_RenderFrameIndex = 0;
    
#if WITH_NRD
    std::unique_ptr<NrdIntegration> m_NRD;
#endif

#if WITH_DLSS
    std::unique_ptr<DLSS> m_DLSS;
#endif

    UIData& m_ui;
    CommandLineArguments& m_args;
    uint m_FramesSinceAnimation = 0;
    bool m_PreviousViewValid = false;
    time_point<steady_clock> m_PreviousFrameTimeStamp;

    std::vector<std::shared_ptr<engine::IesProfile>> m_IesProfiles;
    
    dm::float3 m_RegirCenter;
    
    enum class FrameStepMode
    {
        Disabled,
        Wait,
        Step
    };

    FrameStepMode m_FrameStepMode = FrameStepMode::Disabled;

public:
    SceneRenderer(app::DeviceManager* deviceManager, UIData& ui, CommandLineArguments& args)
        : ApplicationBase(deviceManager)
        , m_BindingCache(deviceManager->GetDevice())
        , m_ui(ui)
        , m_args(args)
    { 
        m_ui.resources->camera = &m_Camera;
    }

    [[nodiscard]] std::shared_ptr<engine::ShaderFactory> GetShaderFactory() const
    {
        return m_ShaderFactory;
    }

    [[nodiscard]] std::shared_ptr<vfs::IFileSystem> GetRootFs() const
    {
        return m_RootFs;
    }

    bool Init()
    {
        std::filesystem::path mediaPath = app::GetDirectoryWithExecutable().parent_path() / "rtxdi-assets";
        if (!std::filesystem::exists(mediaPath))
        {
            mediaPath = mediaPath.parent_path().parent_path() / "rtxdi-assets";
            if (!std::filesystem::exists(mediaPath))
            {
                log::error("Couldn't locate the 'rtxdi-assets' folder.");
                return false;
            }
        }

        std::filesystem::path frameworkShaderPath = app::GetDirectoryWithExecutable() / "shaders/framework" / app::GetShaderTypeName(GetDevice()->getGraphicsAPI());
        std::filesystem::path appShaderPath = app::GetDirectoryWithExecutable() / "shaders/rtxdi-sample" / app::GetShaderTypeName(GetDevice()->getGraphicsAPI());

        log::debug("Mounting %s to %s", mediaPath.string().c_str(), "/rtxdi-assets");
        log::debug("Mounting %s to %s", frameworkShaderPath.string().c_str(), "/shaders/donut");
        log::debug("Mounting %s to %s", appShaderPath.string().c_str(), "/shaders/app");

        m_RootFs = std::make_shared<vfs::RootFileSystem>();
        m_RootFs->mount("/rtxdi-assets", mediaPath);
        m_RootFs->mount("/shaders/donut", frameworkShaderPath);
        m_RootFs->mount("/shaders/app", appShaderPath);

        m_ShaderFactory = std::make_shared<engine::ShaderFactory>(GetDevice(), m_RootFs, "/shaders");
        m_CommonPasses = std::make_shared<engine::CommonRenderPasses>(GetDevice(), m_ShaderFactory);

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
            m_BindlessLayout = GetDevice()->createBindlessLayout(bindlessLayoutDesc);
        }

        std::filesystem::path scenePath = "/rtxdi-assets/bistro-rtxdi.scene.json";

        m_DescriptorTableManager = std::make_shared<engine::DescriptorTableManager>(GetDevice(), m_BindlessLayout);

        m_TextureCache = std::make_shared<donut::engine::TextureCache>(GetDevice(), m_RootFs, m_DescriptorTableManager);
        m_TextureCache->SetInfoLogSeverity(donut::log::Severity::Debug);
        
        m_IesProfileLoader = std::make_unique<engine::IesProfileLoader>(GetDevice(), m_ShaderFactory, m_DescriptorTableManager);

        auto sceneTypeFactory = std::make_shared<SampleSceneTypeFactory>();
        m_Scene = std::make_shared<SampleScene>(GetDevice(), *m_ShaderFactory, m_RootFs, m_TextureCache, m_DescriptorTableManager, sceneTypeFactory);
        m_ui.resources->scene = m_Scene;

        SetAsynchronousLoadingEnabled(true);
        BeginLoadingScene(m_RootFs, scenePath);
        GetDeviceManager()->SetVsyncEnabled(true);

        if (!GetDevice()->queryFeatureSupport(nvrhi::Feature::RayQuery))
            m_ui.useRayQuery = false;

        m_Profiler = std::make_shared<Profiler>(*GetDeviceManager());
        m_ui.resources->profiler = m_Profiler;

        m_FilterGradientsPass = std::make_unique<FilterGradientsPass>(GetDevice(), m_ShaderFactory);
        m_ConfidencePass = std::make_unique<ConfidencePass>(GetDevice(), m_ShaderFactory);
        m_CompositingPass = std::make_unique<CompositingPass>(GetDevice(), m_ShaderFactory, m_CommonPasses, m_Scene, m_BindlessLayout);
        m_AccumulationPass = std::make_unique<AccumulationPass>(GetDevice(), m_ShaderFactory);
        m_GBufferPass = std::make_unique<RaytracedGBufferPass>(GetDevice(), m_ShaderFactory, m_CommonPasses, m_Scene, m_Profiler, m_BindlessLayout);
        m_RasterizedGBufferPass = std::make_unique<RasterizedGBufferPass>(GetDevice(), m_ShaderFactory, m_CommonPasses, m_Scene, m_Profiler, m_BindlessLayout);
        m_PostprocessGBufferPass = std::make_unique<PostprocessGBufferPass>(GetDevice(), m_ShaderFactory);
        m_GlassPass = std::make_unique<GlassPass>(GetDevice(), m_ShaderFactory, m_CommonPasses, m_Scene, m_Profiler, m_BindlessLayout);
        m_PrepareLightsPass = std::make_unique<PrepareLightsPass>(GetDevice(), m_ShaderFactory, m_CommonPasses, m_Scene, m_BindlessLayout);
        m_LightingPasses = std::make_unique<LightingPasses>(GetDevice(), m_ShaderFactory, m_CommonPasses, m_Scene, m_Profiler, m_BindlessLayout);


#if WITH_DLSS
        {
#if DONUT_WITH_DX12
            if (GetDevice()->getGraphicsAPI() == nvrhi::GraphicsAPI::D3D12)
                m_DLSS = DLSS::CreateDX12(GetDevice(), *m_ShaderFactory);
#endif
#if DONUT_WITH_VULKAN
            if (GetDevice()->getGraphicsAPI() == nvrhi::GraphicsAPI::VULKAN)
                m_DLSS = DLSS::CreateVK(GetDevice(), *m_ShaderFactory);
#endif
        }
#endif

        LoadShaders();

        std::vector<std::string> profileNames;
        m_RootFs->enumerateFiles("/rtxdi-assets/ies-profiles", { ".ies" }, vfs::enumerate_to_vector(profileNames));

        for (const std::string& profileName : profileNames)
        {
            auto profile = m_IesProfileLoader->LoadIesProfile(*m_RootFs, "/rtxdi-assets/ies-profiles/" + profileName);

            if (profile)
            {
                m_IesProfiles.push_back(profile);
            }
        }
        m_ui.resources->iesProfiles = m_IesProfiles;

        m_CommandList = GetDevice()->createCommandList();

        return true;
    }

    void AssignIesProfiles(nvrhi::ICommandList* commandList)
    {
        for (const auto& light : m_Scene->GetSceneGraph()->GetLights())
        {
            if (light->GetLightType() == LightType_Spot)
            {
                SpotLightWithProfile& spotLight = static_cast<SpotLightWithProfile&>(*light);

                if (spotLight.profileName.empty())
                    continue;

                if (spotLight.profileTextureIndex >= 0)
                    continue;

                auto foundProfile = std::find_if(m_IesProfiles.begin(), m_IesProfiles.end(),
                    [&spotLight](auto it) { return it->name == spotLight.profileName; });

                if (foundProfile != m_IesProfiles.end())
                {
                    m_IesProfileLoader->BakeIesProfile(**foundProfile, commandList);

                    spotLight.profileTextureIndex = (*foundProfile)->textureIndex;
                }
            }
        }
    }

    virtual void SceneLoaded() override
    {
        ApplicationBase::SceneLoaded();

        m_Scene->FinishedLoading(GetFrameIndex());

        m_Camera.LookAt(float3(-7.688f, 2.0f, 5.594f), float3(-7.3341f, 2.0f, 6.5366f));
        m_Camera.SetMoveSpeed(3.f);

        const auto& sceneGraph = m_Scene->GetSceneGraph();

        for (const auto& pLight : sceneGraph->GetLights())
        {
            if (pLight->GetLightType() == LightType_Directional)
            {
                m_SunLight = std::static_pointer_cast<engine::DirectionalLight>(pLight);
                break;
            }
        }

        if (!m_SunLight)
        {
            m_SunLight = std::make_shared<engine::DirectionalLight>();
            sceneGraph->AttachLeafNode(sceneGraph->GetRootNode(), m_SunLight);
            m_SunLight->SetDirection(dm::double3(0.15, -1.0, 0.3));
            m_SunLight->angularSize = 1.f;
        }

        m_CommandList->open();
        AssignIesProfiles(m_CommandList);
        m_CommandList->close();
        GetDevice()->executeCommandList(m_CommandList);

        // Create an environment light
        m_EnvironmentLight = std::make_shared<EnvironmentLight>();
        sceneGraph->AttachLeafNode(sceneGraph->GetRootNode(), m_EnvironmentLight);
        m_EnvironmentLight->SetName("Environment");
        m_ui.environmentMapDirty = 2;
        m_ui.environmentMapIndex = 0;
        
        m_RasterizedGBufferPass->CreateBindingSet();

        m_Scene->BuildMeshBLASes(GetDevice());

        GetDeviceManager()->SetVsyncEnabled(false);

        m_ui.isLoading = false;
    }
    
    void LoadShaders()
    {
        m_FilterGradientsPass->CreatePipeline();
        m_ConfidencePass->CreatePipeline();
        m_CompositingPass->CreatePipeline();
        m_AccumulationPass->CreatePipeline();
        m_GBufferPass->CreatePipeline(m_ui.useRayQuery);
        m_PostprocessGBufferPass->CreatePipeline();
        m_GlassPass->CreatePipeline(m_ui.useRayQuery);
        m_PrepareLightsPass->CreatePipeline();
    }

    virtual bool LoadScene(std::shared_ptr<vfs::IFileSystem> fs, const std::filesystem::path& sceneFileName) override 
    {
        if (m_Scene->Load(sceneFileName))
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

        if (mods == 0 && key == GLFW_KEY_F1 && action == GLFW_PRESS)
        {
            m_FrameStepMode = (m_FrameStepMode == FrameStepMode::Disabled) ? FrameStepMode::Wait : FrameStepMode::Disabled;
            return true;
        }

        if (mods == 0 && key == GLFW_KEY_F2 && action == GLFW_PRESS)
        {
            if (m_FrameStepMode == FrameStepMode::Wait)
                m_FrameStepMode = FrameStepMode::Step;
            return true;
        }

        if (mods == 0 && key == GLFW_KEY_F5 && action == GLFW_PRESS)
        {
            if (m_ui.animationFrame.has_value())
            {
                // Stop benchmark if it's running
                m_ui.animationFrame.reset();
            }
            else
            {
                // Start benchmark otherwise
                m_ui.animationFrame = std::optional<int>(0);
            }
            return true;
        }
        
        m_Camera.KeyboardUpdate(key, scancode, action, mods);

        return true;
    }

    virtual bool MousePosUpdate(double xpos, double ypos) override
    {
        m_Camera.MousePosUpdate(xpos, ypos);
        return true;
    }

    virtual bool MouseButtonUpdate(int button, int action, int mods) override
    {
        if (button == GLFW_MOUSE_BUTTON_RIGHT && action == GLFW_PRESS)
        {
            double mousex = 0, mousey = 0;
            glfwGetCursorPos(GetDeviceManager()->GetWindow(), &mousex, &mousey);

            // Scale the mouse position according to the render resolution scale
            mousex *= m_View.GetViewport().width() / m_UpscaledView.GetViewport().width();
            mousey *= m_View.GetViewport().height() / m_UpscaledView.GetViewport().height();

            m_ui.gbufferSettings.materialReadbackPosition = int2(int(mousex), int(mousey));
            m_ui.gbufferSettings.enableMaterialReadback = true;
            return true;
        }

        m_Camera.MouseButtonUpdate(button, action, mods);
        return true;
    }

    virtual void Animate(float fElapsedTimeSeconds) override
    {
        if (m_ui.isLoading)
            return;

        if (!m_args.saveFrameFileName.empty())
            fElapsedTimeSeconds = 1.f / 60.f;

        m_Camera.Animate(fElapsedTimeSeconds);

        if (m_ui.enableAnimations)
            m_Scene->Animate(fElapsedTimeSeconds * m_ui.animationSpeed);

        if (m_ToneMappingPass)
            m_ToneMappingPass->AdvanceFrame(fElapsedTimeSeconds);
    }

    virtual void BackBufferResized(const uint32_t width, const uint32_t height, const uint32_t sampleCount) override
    {
        // If the render size is overridden from the command line, ignore the window size.
        if (m_args.renderWidth > 0 && m_args.renderHeight > 0)
            return;

        if (m_RenderTargets && m_RenderTargets->Size.x == int(width) && m_RenderTargets->Size.y == int(height))
            return;

        m_BindingCache.Clear();
        m_RenderTargets = nullptr;
        m_isContext = nullptr;
        m_RtxdiResources = nullptr;
        m_TemporalAntiAliasingPass = nullptr;
        m_ToneMappingPass = nullptr;
        m_BloomPass = nullptr;
#if WITH_NRD
        m_NRD = nullptr;
#endif
    }

    void LoadEnvironmentMap()
    {
        if (m_EnvironmentMap)
        {
            // Make sure there is no rendering in-flight before we unload the texture and erase its descriptor.
            // Decsriptor manipulations are synchronous and immediately affect whatever is executing on the GPU.
            GetDevice()->waitForIdle();

            m_TextureCache->UnloadTexture(m_EnvironmentMap);
            
            m_EnvironmentMap = nullptr;
        }

        if (m_ui.environmentMapIndex > 0)
        {
            auto& environmentMaps = m_Scene->GetEnvironmentMaps();
            const std::string& environmentMapPath = environmentMaps[m_ui.environmentMapIndex];

            m_EnvironmentMap = m_TextureCache->LoadTextureFromFileDeferred(environmentMapPath, false);

            if (m_TextureCache->IsTextureLoaded(m_EnvironmentMap))
            {
                m_TextureCache->ProcessRenderingThreadCommands(*m_CommonPasses, 0.f);
                m_TextureCache->LoadingFinished();

                m_EnvironmentMap->bindlessDescriptor = m_DescriptorTableManager->CreateDescriptorHandle(nvrhi::BindingSetItem::Texture_SRV(0, m_EnvironmentMap->texture));
            }
            else
            {
                // Failed to load the file: revert to the procedural map and remove this file from the list.
                m_EnvironmentMap = nullptr;
                environmentMaps.erase(environmentMaps.begin() + m_ui.environmentMapIndex);
                m_ui.environmentMapIndex = 0;
            }
        }
    }

    void SetupView(uint32_t renderWidth, uint32_t renderHeight, const engine::PerspectiveCamera* activeCamera)
    {
        nvrhi::Viewport windowViewport((float)renderWidth, (float)renderHeight);

        if (m_TemporalAntiAliasingPass)
            m_TemporalAntiAliasingPass->SetJitter(m_ui.temporalJitter);

        nvrhi::Viewport renderViewport = windowViewport;
        renderViewport.maxX = roundf(renderViewport.maxX * m_ui.resolutionScale);
        renderViewport.maxY = roundf(renderViewport.maxY * m_ui.resolutionScale);

        m_View.SetViewport(renderViewport);

        if (m_ui.enablePixelJitter && m_TemporalAntiAliasingPass)
        {
            m_View.SetPixelOffset(m_TemporalAntiAliasingPass->GetCurrentPixelOffset());
        }
        else
        {
            m_View.SetPixelOffset(0.f);
        }

        const float aspectRatio = windowViewport.width() / windowViewport.height();
        if (activeCamera)
            m_View.SetMatrices(activeCamera->GetWorldToViewMatrix(), perspProjD3DStyleReverse(activeCamera->verticalFov, aspectRatio, activeCamera->zNear));
        else
            m_View.SetMatrices(m_Camera.GetWorldToViewMatrix(), perspProjD3DStyleReverse(radians(m_ui.verticalFov), aspectRatio, 0.01f));
        m_View.UpdateCache();

        if (m_ViewPrevious.GetViewExtent().width() == 0)
            m_ViewPrevious = m_View;

        m_UpscaledView = m_View;
        m_UpscaledView.SetViewport(windowViewport);
    }

    void SetupRenderPasses(uint32_t renderWidth, uint32_t renderHeight, bool& exposureResetRequired)
    {
        if (m_ui.environmentMapDirty == 2)
        {
            m_EnvironmentMapPdfMipmapPass = nullptr;

            m_ui.environmentMapDirty = 1;
        }

        if (m_ui.reloadShaders)
        {
            GetDevice()->waitForIdle();

            m_ShaderFactory->ClearCache();
            m_TemporalAntiAliasingPass = nullptr;
            m_RenderEnvironmentMapPass = nullptr;
            m_EnvironmentMapPdfMipmapPass = nullptr;
            m_LocalLightPdfMipmapPass = nullptr;
            m_VisualizationPass = nullptr;
            m_DebugVizPasses = nullptr;
            m_ui.environmentMapDirty = 1;

            LoadShaders();
        }

        bool renderTargetsCreated = false;
        bool rtxdiResourcesCreated = false;

        if (!m_RenderEnvironmentMapPass)
        {
            m_RenderEnvironmentMapPass = std::make_unique<RenderEnvironmentMapPass>(GetDevice(), m_ShaderFactory, m_DescriptorTableManager, 2048);
        }
        
        const auto environmentMap = (m_ui.environmentMapIndex > 0)
            ? m_EnvironmentMap->texture.Get()
            : m_RenderEnvironmentMapPass->GetTexture();

        uint32_t numEmissiveMeshes, numEmissiveTriangles;
        m_PrepareLightsPass->CountLightsInScene(numEmissiveMeshes, numEmissiveTriangles);
        uint32_t numPrimitiveLights = uint32_t(m_Scene->GetSceneGraph()->GetLights().size());
        uint32_t numGeometryInstances = uint32_t(m_Scene->GetSceneGraph()->GetGeometryInstancesCount());
        
        uint2 environmentMapSize = uint2(environmentMap->getDesc().width, environmentMap->getDesc().height);

        if (m_RtxdiResources && (
            environmentMapSize.x != m_RtxdiResources->EnvironmentPdfTexture->getDesc().width ||
            environmentMapSize.y != m_RtxdiResources->EnvironmentPdfTexture->getDesc().height ||
            numEmissiveMeshes > m_RtxdiResources->GetMaxEmissiveMeshes() ||
            numEmissiveTriangles > m_RtxdiResources->GetMaxEmissiveTriangles() || 
            numPrimitiveLights > m_RtxdiResources->GetMaxPrimitiveLights() ||
            numGeometryInstances > m_RtxdiResources->GetMaxGeometryInstances()))
        {
            m_RtxdiResources = nullptr;
        }

        if (!m_isContext)
        {
            rtxdi::ImportanceSamplingContext_StaticParameters isStaticParams;
            isStaticParams.CheckerboardSamplingMode = m_ui.restirDIStaticParams.CheckerboardSamplingMode;
            isStaticParams.renderHeight = renderHeight;
            isStaticParams.renderWidth = renderWidth;
            isStaticParams.regirStaticParams = m_ui.regirStaticParams;

            m_isContext = std::make_unique<rtxdi::ImportanceSamplingContext>(isStaticParams);

            m_ui.regirLightSlotCount = m_isContext->getReGIRContext().getReGIRLightSlotCount();
        }

        if (!m_RenderTargets)
        {
            m_RenderTargets = std::make_shared<RenderTargets>(GetDevice(), int2((int)renderWidth, (int)renderHeight));

            m_Profiler->SetRenderTargets(m_RenderTargets);

            m_GBufferPass->CreateBindingSet(m_Scene->GetTopLevelAS(), m_Scene->GetPrevTopLevelAS(), *m_RenderTargets);

            m_PostprocessGBufferPass->CreateBindingSet(*m_RenderTargets);

            m_GlassPass->CreateBindingSet(m_Scene->GetTopLevelAS(), m_Scene->GetPrevTopLevelAS(), *m_RenderTargets);

            m_FilterGradientsPass->CreateBindingSet(*m_RenderTargets);

            m_ConfidencePass->CreateBindingSet(*m_RenderTargets);
            
            m_AccumulationPass->CreateBindingSet(*m_RenderTargets);

            m_RasterizedGBufferPass->CreatePipeline(*m_RenderTargets);

            m_CompositingPass->CreateBindingSet(*m_RenderTargets);

            m_VisualizationPass = nullptr;
            m_DebugVizPasses = nullptr;

            renderTargetsCreated = true;
        }

        if (!m_RtxdiResources)
        {
            uint32_t meshAllocationQuantum = 128;
            uint32_t triangleAllocationQuantum = 1024;
            uint32_t primitiveAllocationQuantum = 128;

            m_RtxdiResources = std::make_unique<RtxdiResources>(
                GetDevice(), 
                m_isContext->getReSTIRDIContext(),
                m_isContext->getRISBufferSegmentAllocator(),
                (numEmissiveMeshes + meshAllocationQuantum - 1) & ~(meshAllocationQuantum - 1),
                (numEmissiveTriangles + triangleAllocationQuantum - 1) & ~(triangleAllocationQuantum - 1),
                (numPrimitiveLights + primitiveAllocationQuantum - 1) & ~(primitiveAllocationQuantum - 1),
                numGeometryInstances,
                environmentMapSize.x,
                environmentMapSize.y);

            m_PrepareLightsPass->CreateBindingSet(*m_RtxdiResources);
            
            rtxdiResourcesCreated = true;

            // Make sure that the environment PDF map is re-generated
            m_ui.environmentMapDirty = 1;
        }
        
        if (!m_EnvironmentMapPdfMipmapPass || rtxdiResourcesCreated)
        {
            m_EnvironmentMapPdfMipmapPass = std::make_unique<GenerateMipsPass>(
                GetDevice(),
                m_ShaderFactory,
                environmentMap,
                m_RtxdiResources->EnvironmentPdfTexture);
        }

        if (!m_LocalLightPdfMipmapPass || rtxdiResourcesCreated)
        {
            m_LocalLightPdfMipmapPass = std::make_unique<GenerateMipsPass>(
                GetDevice(),
                m_ShaderFactory,
                nullptr,
                m_RtxdiResources->LocalLightPdfTexture);
        }

        if (renderTargetsCreated || rtxdiResourcesCreated)
        {
            m_LightingPasses->CreateBindingSet(
                m_Scene->GetTopLevelAS(),
                m_Scene->GetPrevTopLevelAS(),
                *m_RenderTargets,
                *m_RtxdiResources);
        }

        if (rtxdiResourcesCreated || m_ui.reloadShaders)
        {
            // Some RTXDI context settings affect the shader permutations
            m_LightingPasses->CreatePipelines(m_ui.regirStaticParams, m_ui.useRayQuery);
        }

        m_ui.reloadShaders = false;

        if (!m_TemporalAntiAliasingPass)
        {
            render::TemporalAntiAliasingPass::CreateParameters taaParams;
            taaParams.motionVectors = m_RenderTargets->MotionVectors;
            taaParams.unresolvedColor = m_RenderTargets->HdrColor;
            taaParams.resolvedColor = m_RenderTargets->ResolvedColor;
            taaParams.feedback1 = m_RenderTargets->TaaFeedback1;
            taaParams.feedback2 = m_RenderTargets->TaaFeedback2;
            taaParams.useCatmullRomFilter = true;

            m_TemporalAntiAliasingPass = std::make_unique<render::TemporalAntiAliasingPass>(
                GetDevice(), m_ShaderFactory, m_CommonPasses, m_View, taaParams);
        }

        exposureResetRequired = false;
        if (!m_ToneMappingPass)
        {
            render::ToneMappingPass::CreateParameters toneMappingParams;
            m_ToneMappingPass = std::make_unique<render::ToneMappingPass>(GetDevice(), m_ShaderFactory, m_CommonPasses, m_RenderTargets->LdrFramebuffer, m_UpscaledView, toneMappingParams);
            exposureResetRequired = true;
        }

        if (!m_BloomPass)
        {
            m_BloomPass = std::make_unique<render::BloomPass>(GetDevice(), m_ShaderFactory, m_CommonPasses, m_RenderTargets->ResolvedFramebuffer, m_UpscaledView);
        }

        if (!m_VisualizationPass || renderTargetsCreated || rtxdiResourcesCreated)
        {
            m_VisualizationPass = std::make_unique<VisualizationPass>(GetDevice(), *m_CommonPasses, *m_ShaderFactory, *m_RenderTargets, *m_RtxdiResources);
        }

        if (!m_DebugVizPasses || renderTargetsCreated)
        {
            m_DebugVizPasses = std::make_unique<DebugVizPasses>(GetDevice(), m_ShaderFactory, m_Scene, m_BindlessLayout);
            m_DebugVizPasses->CreateBindingSets(*m_RenderTargets, m_RenderTargets->DebugColor);
            m_DebugVizPasses->CreatePipelines();
        }

#if WITH_NRD
        if (!m_NRD)
        {
            m_NRD = std::make_unique<NrdIntegration>(GetDevice(), m_ui.denoisingMethod);
            m_NRD->Initialize(m_RenderTargets->Size.x, m_RenderTargets->Size.y);
        }
#endif
#if WITH_DLSS
        {
            m_DLSS->SetRenderSize(m_RenderTargets->Size.x, m_RenderTargets->Size.y, m_RenderTargets->Size.x, m_RenderTargets->Size.y);
            
            m_ui.dlssAvailable = m_DLSS->IsAvailable();
        }
#endif
    }

    virtual void RenderSplashScreen(nvrhi::IFramebuffer* framebuffer) override
    {
        nvrhi::ITexture* framebufferTexture = framebuffer->getDesc().colorAttachments[0].texture;
        m_CommandList->open();
        m_CommandList->clearTextureFloat(framebufferTexture, nvrhi::AllSubresources, nvrhi::Color(0.f));
        m_CommandList->close();
        GetDevice()->executeCommandList(m_CommandList);

        uint32_t loadedObjects = engine::Scene::GetLoadingStats().ObjectsLoaded;
        uint32_t requestedObjects = engine::Scene::GetLoadingStats().ObjectsTotal;
        uint32_t loadedTextures = m_TextureCache->GetNumberOfLoadedTextures();
        uint32_t finalizedTextures = m_TextureCache->GetNumberOfFinalizedTextures();
        uint32_t requestedTextures = m_TextureCache->GetNumberOfRequestedTextures();
        uint32_t objectMultiplier = 20;
        m_ui.loadingPercentage = (requestedTextures > 0) 
            ? float(loadedTextures + finalizedTextures + loadedObjects * objectMultiplier) / float(requestedTextures * 2 + requestedObjects * objectMultiplier) 
            : 0.f;
    }

    void Resolve(nvrhi::ICommandList* commandList, float accumulationWeight) const
    {
        ProfilerScope scope(*m_Profiler, commandList, ProfilerSection::Resolve);

        switch (m_ui.aaMode)
        {
        case AntiAliasingMode::None: {
            engine::BlitParameters blitParams;
            blitParams.sourceTexture = m_RenderTargets->HdrColor;
            blitParams.sourceBox.m_maxs.x = m_View.GetViewport().width() / m_UpscaledView.GetViewport().width();
            blitParams.sourceBox.m_maxs.y = m_View.GetViewport().height() / m_UpscaledView.GetViewport().height();
            blitParams.targetFramebuffer = m_RenderTargets->ResolvedFramebuffer->GetFramebuffer(m_UpscaledView);
            m_CommonPasses->BlitTexture(commandList, blitParams);
            break;
        }

        case AntiAliasingMode::Accumulation: {
            m_AccumulationPass->Render(commandList, m_View, m_UpscaledView, accumulationWeight);
            m_CommonPasses->BlitTexture(commandList, m_RenderTargets->ResolvedFramebuffer->GetFramebuffer(m_UpscaledView), m_RenderTargets->AccumulatedColor);
            break;
        }

        case AntiAliasingMode::TAA: {
            auto taaParams = m_ui.taaParams;
            if (m_ui.resetAccumulation)
                taaParams.newFrameWeight = 1.f;

            m_TemporalAntiAliasingPass->TemporalResolve(commandList, taaParams, m_PreviousViewValid, m_View, m_UpscaledView);
            break;
        }

#if WITH_DLSS
        case AntiAliasingMode::DLSS: {
            m_DLSS->Render(commandList, *m_RenderTargets, m_ToneMappingPass->GetExposureBuffer(), m_ui.dlssExposureScale, m_ui.dlssSharpness, m_ui.rasterizeGBuffer, m_ui.resetAccumulation, m_View, m_ViewPrevious);
            break;
        }
#endif
        }
    }

    void UpdateReGIRContextFromUI()
    {
        auto& regirContext = m_isContext->getReGIRContext();
        auto dynamicParams = m_ui.regirDynamicParameters;
        dynamicParams.center = { m_RegirCenter.x, m_RegirCenter.y, m_RegirCenter.z };
        regirContext.setDynamicParameters(dynamicParams);
    }

    void UpdateReSTIRDIContextFromUI()
    {
        rtxdi::ReSTIRDIContext& restirDIContext = m_isContext->getReSTIRDIContext();
        ReSTIRDI_InitialSamplingParameters initialSamplingParams = m_ui.restirDI.initialSamplingParams;
        switch (initialSamplingParams.localLightSamplingMode)
        {
        default:
        case ReSTIRDI_LocalLightSamplingMode::Uniform:
            initialSamplingParams.numPrimaryLocalLightSamples = m_ui.restirDI.numLocalLightUniformSamples;
            break;
        case ReSTIRDI_LocalLightSamplingMode::Power_RIS:
            initialSamplingParams.numPrimaryLocalLightSamples = m_ui.restirDI.numLocalLightPowerRISSamples;
            break;
        case ReSTIRDI_LocalLightSamplingMode::ReGIR_RIS:
            initialSamplingParams.numPrimaryLocalLightSamples = m_ui.restirDI.numLocalLightReGIRRISSamples;
            break;
        }
        restirDIContext.setResamplingMode(m_ui.restirDI.resamplingMode);
        restirDIContext.setInitialSamplingParameters(initialSamplingParams);
        restirDIContext.setTemporalResamplingParameters(m_ui.restirDI.temporalResamplingParams);
        restirDIContext.setSpatialResamplingParameters(m_ui.restirDI.spatialResamplingParams);
        restirDIContext.setShadingParameters(m_ui.restirDI.shadingParams);
    }

    void UpdateReSTIRGIContextFromUI()
    {
        rtxdi::ReSTIRGIContext& restirGIContext = m_isContext->getReSTIRGIContext();
        restirGIContext.setResamplingMode(m_ui.restirGI.resamplingMode);
        restirGIContext.setTemporalResamplingParameters(m_ui.restirGI.temporalResamplingParams);
        restirGIContext.setSpatialResamplingParameters(m_ui.restirGI.spatialResamplingParams);
        restirGIContext.setFinalShadingParameters(m_ui.restirGI.finalShadingParams);
    }

    bool IsLocalLightPowerRISEnabled()
    {
        if (m_ui.indirectLightingMode == IndirectLightingMode::ReStirGI)
        {
            ReSTIRDI_InitialSamplingParameters indirectReSTIRDISamplingParams = m_ui.lightingSettings.brdfptParams.secondarySurfaceReSTIRDIParams.initialSamplingParams;
            bool enabled = (indirectReSTIRDISamplingParams.localLightSamplingMode == ReSTIRDI_LocalLightSamplingMode::Power_RIS) ||
                           (indirectReSTIRDISamplingParams.localLightSamplingMode == ReSTIRDI_LocalLightSamplingMode::ReGIR_RIS && m_isContext->getReGIRContext().isLocalLightPowerRISEnable());
            if (enabled)
                return true;
        }
        return m_isContext->isLocalLightPowerRISEnabled();
    }

    void RenderScene(nvrhi::IFramebuffer* framebuffer) override
    {
        if (m_FrameStepMode == FrameStepMode::Wait)
        {
            nvrhi::TextureHandle finalImage;

            if (m_ui.enableToneMapping)
                finalImage = m_RenderTargets->LdrColor;
            else
                finalImage = m_RenderTargets->HdrColor;

            m_CommandList->open();

            m_CommonPasses->BlitTexture(m_CommandList, framebuffer, finalImage, &m_BindingCache);

            m_CommandList->close();
            GetDevice()->executeCommandList(m_CommandList);

            return;
        }

        if (m_FrameStepMode == FrameStepMode::Step)
            m_FrameStepMode = FrameStepMode::Wait;

        const engine::PerspectiveCamera* activeCamera = nullptr;
        uint effectiveFrameIndex = m_RenderFrameIndex;

        if (m_ui.animationFrame.has_value())
        {
            const float animationTime = float(m_ui.animationFrame.value()) * (1.f / 240.f);
            
            auto* animation = m_Scene->GetBenchmarkAnimation();
            if (animation && animationTime < animation->GetDuration())
            {
                (void)animation->Apply(animationTime);
                activeCamera = m_Scene->GetBenchmarkCamera();
                effectiveFrameIndex = m_ui.animationFrame.value();
                m_ui.animationFrame = effectiveFrameIndex + 1;
            }
            else
            {
                m_ui.benchmarkResults = m_Profiler->GetAsText();
                m_ui.animationFrame.reset();

                if (m_args.benchmark)
                {
                    glfwSetWindowShouldClose(GetDeviceManager()->GetWindow(), GLFW_TRUE);
                    log::info("BENCHMARK RESULTS >>>\n\n%s<<<", m_ui.benchmarkResults.c_str());
                }
            }
        }

        bool exposureResetRequired = false;

        if (m_ui.enableFpsLimit && GetFrameIndex() > 0)
        {
            uint64_t expectedFrametime = 1000000 / m_ui.fpsLimit;

            while (true)
            {
                uint64_t currentFrametime = duration_cast<microseconds>(steady_clock::now() - m_PreviousFrameTimeStamp).count();

                if(currentFrametime >= expectedFrametime)
                    break;
#ifdef _WIN32
                Sleep(0);
#else
                usleep(100);
#endif
            }
        }

        m_PreviousFrameTimeStamp = steady_clock::now();

#if WITH_NRD
        if (m_NRD && m_NRD->GetDenoiser() != m_ui.denoisingMethod)
            m_NRD = nullptr; // need to create a new one
#endif

        if (m_ui.resetISContext)
        {
            GetDevice()->waitForIdle();

            m_isContext = nullptr;
            m_RtxdiResources = nullptr;
            m_ui.resetISContext = false;
        }

        if (m_ui.environmentMapDirty == 2)
        {
            LoadEnvironmentMap();
        }

        m_Scene->RefreshSceneGraph(GetFrameIndex());

        const auto& fbinfo = framebuffer->getFramebufferInfo();
        uint32_t renderWidth = fbinfo.width;
        uint32_t renderHeight = fbinfo.height;
        if (m_args.renderWidth > 0 && m_args.renderHeight > 0)
        {
            renderWidth = m_args.renderWidth;
            renderHeight = m_args.renderHeight;
        }
        SetupView(renderWidth, renderHeight, activeCamera);
        SetupRenderPasses(renderWidth, renderHeight, exposureResetRequired);
        if (!m_ui.freezeRegirPosition)
            m_RegirCenter = m_Camera.GetPosition();
        UpdateReSTIRDIContextFromUI();
        UpdateReGIRContextFromUI();
        UpdateReSTIRGIContextFromUI();
#if WITH_DLSS
        if (!m_ui.dlssAvailable && m_ui.aaMode == AntiAliasingMode::DLSS)
            m_ui.aaMode = AntiAliasingMode::TAA;
#endif

        m_GBufferPass->NextFrame();
        m_PostprocessGBufferPass->NextFrame();
        m_LightingPasses->NextFrame();
        m_ConfidencePass->NextFrame();
        m_CompositingPass->NextFrame();
        m_VisualizationPass->NextFrame();
        m_RenderTargets->NextFrame();
        m_GlassPass->NextFrame();
        m_Scene->NextFrame();
        m_DebugVizPasses->NextFrame();
        
        // Advance the TAA jitter offset at half frame rate if accumulation is used with
        // checkerboard rendering. Otherwise, the jitter pattern resonates with the checkerboard,
        // and stipple patterns appear in the accumulated results.
        if (!((m_ui.aaMode == AntiAliasingMode::Accumulation) && (m_isContext->getReSTIRDIContext().getStaticParameters().CheckerboardSamplingMode != rtxdi::CheckerboardMode::Off) && (GetFrameIndex() & 1)))
        {
            m_TemporalAntiAliasingPass->AdvanceFrame();
        }
        
        bool cameraIsStatic = m_PreviousViewValid && m_View.GetViewMatrix() == m_ViewPrevious.GetViewMatrix();
        if (cameraIsStatic && (m_ui.aaMode == AntiAliasingMode::Accumulation) && !m_ui.resetAccumulation)
        {
            m_ui.numAccumulatedFrames += 1;

            if (m_ui.framesToAccumulate > 0)
                m_ui.numAccumulatedFrames = std::min(m_ui.numAccumulatedFrames, m_ui.framesToAccumulate);

            m_Profiler->EnableAccumulation(true);
        }
        else
        {
            m_ui.numAccumulatedFrames = 1;
            m_Profiler->EnableAccumulation(m_ui.animationFrame.has_value());
        }

        float accumulationWeight = 1.f / (float)m_ui.numAccumulatedFrames;

        m_Profiler->ResolvePreviousFrame();
        
        int materialIndex = m_Profiler->GetMaterialReadback();
        if (materialIndex >= 0)
        {
            for (const auto& material : m_Scene->GetSceneGraph()->GetMaterials())
            {
                if (material->materialID == materialIndex)
                {
                    m_ui.resources->selectedMaterial = material;
                    break;
                }
            }
        }
        
        if (m_ui.environmentMapIndex >= 0)
        {
            if (m_EnvironmentMap)
            {
                m_EnvironmentLight->textureIndex = m_EnvironmentMap->bindlessDescriptor.Get();
                const auto& textureDesc = m_EnvironmentMap->texture->getDesc();
                m_EnvironmentLight->textureSize = uint2(textureDesc.width, textureDesc.height);
            }
            else
            {
                m_EnvironmentLight->textureIndex = m_RenderEnvironmentMapPass->GetTextureIndex();
                const auto& textureDesc = m_RenderEnvironmentMapPass->GetTexture()->getDesc();
                m_EnvironmentLight->textureSize = uint2(textureDesc.width, textureDesc.height);
            }
            m_EnvironmentLight->radianceScale = ::exp2f(m_ui.environmentIntensityBias);
            m_EnvironmentLight->rotation = m_ui.environmentRotation / 360.f;  //  +/- 0.5
            m_SunLight->irradiance = (m_ui.environmentMapIndex > 0) ? 0.f : 1.f;
        }
        else
        {
            m_EnvironmentLight->textureIndex = -1;
            m_SunLight->irradiance = 0.f;
        }
        
#if WITH_NRD
        if (!(m_NRD && m_NRD->IsAvailable()))
            m_ui.enableDenoiser = false;

        uint32_t denoiserMode = (m_ui.enableDenoiser)
            ? (m_ui.denoisingMethod == nrd::Denoiser::RELAX_DIFFUSE_SPECULAR) ? DENOISER_MODE_RELAX : DENOISER_MODE_REBLUR
            : DENOISER_MODE_OFF;
#else
        m_ui.enableDenoiser = false;
        uint32_t denoiserMode = DENOISER_MODE_OFF;
#endif

        m_CommandList->open();

        m_Profiler->BeginFrame(m_CommandList);

        AssignIesProfiles(m_CommandList);
        m_Scene->RefreshBuffers(m_CommandList, GetFrameIndex());
        m_RtxdiResources->InitializeNeighborOffsets(m_CommandList, m_isContext->getNeighborOffsetCount());

        if (m_FramesSinceAnimation < 2)
        {
            ProfilerScope scope(*m_Profiler, m_CommandList, ProfilerSection::TlasUpdate);

            m_Scene->UpdateSkinnedMeshBLASes(m_CommandList, GetFrameIndex());
            m_Scene->BuildTopLevelAccelStruct(m_CommandList);
        }
        m_CommandList->compactBottomLevelAccelStructs();

        if (m_ui.environmentMapDirty)
        {
            ProfilerScope scope(*m_Profiler, m_CommandList, ProfilerSection::EnvironmentMap);

            if (m_ui.environmentMapIndex == 0)
            {
                donut::render::SkyParameters params;
                m_RenderEnvironmentMapPass->Render(m_CommandList, *m_SunLight, params);
            }
            
            m_EnvironmentMapPdfMipmapPass->Process(m_CommandList);

            m_ui.environmentMapDirty = 0;
        }

        nvrhi::utils::ClearColorAttachment(m_CommandList, framebuffer, 0, nvrhi::Color(0.f));

        {
            ProfilerScope scope(*m_Profiler, m_CommandList, ProfilerSection::GBufferFill);

            GBufferSettings gbufferSettings = m_ui.gbufferSettings;
            float upscalingLodBias = ::log2f(m_View.GetViewport().width() / m_UpscaledView.GetViewport().width());
            gbufferSettings.textureLodBias += upscalingLodBias;

            if (m_ui.rasterizeGBuffer)
                m_RasterizedGBufferPass->Render(m_CommandList, m_View, m_ViewPrevious, *m_RenderTargets, m_ui.gbufferSettings);
            else
                m_GBufferPass->Render(m_CommandList, m_View, m_ViewPrevious, m_ui.gbufferSettings);

            m_PostprocessGBufferPass->Render(m_CommandList, m_View);
        }

        // The light indexing members of frameParameters are written by PrepareLightsPass below
        rtxdi::ReSTIRDIContext& restirDIContext = m_isContext->getReSTIRDIContext();
        restirDIContext.setFrameIndex(effectiveFrameIndex);
        m_isContext->getReSTIRGIContext().setFrameIndex(effectiveFrameIndex);

        {
            ProfilerScope scope(*m_Profiler, m_CommandList, ProfilerSection::MeshProcessing);
            
            RTXDI_LightBufferParameters lightBufferParams = m_PrepareLightsPass->Process(
                m_CommandList,
                restirDIContext,
                m_Scene->GetSceneGraph()->GetLights(),
                m_EnvironmentMapPdfMipmapPass != nullptr && m_ui.environmentMapImportanceSampling);
            m_isContext->setLightBufferParams(lightBufferParams);

            auto initialSamplingParams = restirDIContext.getInitialSamplingParameters();
            initialSamplingParams.environmentMapImportanceSampling = lightBufferParams.environmentLightParams.lightPresent;
            m_ui.restirDI.initialSamplingParams.environmentMapImportanceSampling = initialSamplingParams.environmentMapImportanceSampling;
            restirDIContext.setInitialSamplingParameters(initialSamplingParams);
        }

        if (IsLocalLightPowerRISEnabled())
        {
            ProfilerScope scope(*m_Profiler, m_CommandList, ProfilerSection::LocalLightPdfMap);
            
            m_LocalLightPdfMipmapPass->Process(m_CommandList);
        }


#if WITH_NRD
        if (restirDIContext.getStaticParameters().CheckerboardSamplingMode != rtxdi::CheckerboardMode::Off)
        {
            m_ui.reblurSettings.checkerboardMode = nrd::CheckerboardMode::BLACK;
            m_ui.relaxSettings.checkerboardMode = nrd::CheckerboardMode::BLACK;
        }
        else
        {
            m_ui.reblurSettings.checkerboardMode = nrd::CheckerboardMode::OFF;
            m_ui.relaxSettings.checkerboardMode = nrd::CheckerboardMode::OFF;
        }
#endif
        
        LightingPasses::RenderSettings lightingSettings = m_ui.lightingSettings;
        lightingSettings.enablePreviousTLAS &= m_ui.enableAnimations;
        lightingSettings.enableAlphaTestedGeometry = m_ui.gbufferSettings.enableAlphaTestedGeometry;
        lightingSettings.enableTransparentGeometry = m_ui.gbufferSettings.enableTransparentGeometry;
#if WITH_NRD
        lightingSettings.reblurDiffHitDistanceParams = &m_ui.reblurSettings.hitDistanceParameters;
        lightingSettings.reblurSpecHitDistanceParams = &m_ui.reblurSettings.hitDistanceParameters;
        lightingSettings.denoiserMode = denoiserMode;
#else
        lightingSettings.denoiserMode = DENOISER_MODE_OFF;
#endif
        if (lightingSettings.denoiserMode == DENOISER_MODE_OFF)
            lightingSettings.enableGradients = false;

        const bool checkerboard = restirDIContext.getStaticParameters().CheckerboardSamplingMode != rtxdi::CheckerboardMode::Off;

        bool enableDirectReStirPass = m_ui.directLightingMode == DirectLightingMode::ReStir;
        bool enableBrdfAndIndirectPass = m_ui.directLightingMode == DirectLightingMode::Brdf || m_ui.indirectLightingMode != IndirectLightingMode::None;
        bool enableIndirect = m_ui.indirectLightingMode != IndirectLightingMode::None;

        // When indirect lighting is enabled, we don't want ReSTIR to be the NRD front-end,
        // it should just write out the raw color data.
        ReSTIRDI_ShadingParameters restirDIShadingParams = m_isContext->getReSTIRDIContext().getShadingParameters();
        restirDIShadingParams.enableDenoiserInputPacking = !enableIndirect;
        m_isContext->getReSTIRDIContext().setShadingParameters(restirDIShadingParams);

        if (!enableDirectReStirPass)
        {
            // Secondary resampling can only be done as a post-process of ReSTIR direct lighting
            lightingSettings.brdfptParams.enableSecondaryResampling = false;

            // Gradients are only produced by the direct ReSTIR pass
            lightingSettings.enableGradients = false;
        }

        if (enableDirectReStirPass || enableIndirect)
        {
            m_LightingPasses->PrepareForLightSampling(m_CommandList,
                *m_isContext,
                m_View, m_ViewPrevious,
                lightingSettings,
                /* enableAccumulation = */ m_ui.aaMode == AntiAliasingMode::Accumulation);
        }

        if (enableDirectReStirPass)
        {
            m_CommandList->clearTextureFloat(m_RenderTargets->Gradients, nvrhi::AllSubresources, nvrhi::Color(0.f));

            m_LightingPasses->RenderDirectLighting(m_CommandList,
                restirDIContext,
                m_View,
                lightingSettings);

            // Post-process the gradients into a confidence buffer usable by NRD
            if (lightingSettings.enableGradients)
            {
                m_FilterGradientsPass->Render(m_CommandList, m_View, checkerboard);
                m_ConfidencePass->Render(m_CommandList, m_View, lightingSettings.gradientLogDarknessBias, lightingSettings.gradientSensitivity, lightingSettings.confidenceHistoryLength, checkerboard);
            }
        }

        if (enableBrdfAndIndirectPass)
        {
            ReSTIRDI_ShadingParameters restirDIShadingParams = m_isContext->getReSTIRDIContext().getShadingParameters();
            restirDIShadingParams.enableDenoiserInputPacking = true;
            m_isContext->getReSTIRDIContext().setShadingParameters(restirDIShadingParams);

            bool enableReSTIRGI = m_ui.indirectLightingMode == IndirectLightingMode::ReStirGI;

            m_LightingPasses->RenderBrdfRays(
                m_CommandList,
                *m_isContext,
                m_View, m_ViewPrevious,
                lightingSettings,
                m_ui.gbufferSettings,
                *m_EnvironmentLight,
                /* enableIndirect = */ enableIndirect,
                /* enableAdditiveBlend = */ enableDirectReStirPass,
                /* enableEmissiveSurfaces = */ m_ui.directLightingMode == DirectLightingMode::Brdf,
                /* enableAccumulation = */ m_ui.aaMode == AntiAliasingMode::Accumulation,
                enableReSTIRGI
                );
        }

        // If none of the passes above were executed, clear the textures to avoid stale data there.
        // It's a weird mode but it can be selected from the UI.
        if (!enableDirectReStirPass && !enableBrdfAndIndirectPass)
        {
            m_CommandList->clearTextureFloat(m_RenderTargets->DiffuseLighting, nvrhi::AllSubresources, nvrhi::Color(0.f));
            m_CommandList->clearTextureFloat(m_RenderTargets->SpecularLighting, nvrhi::AllSubresources, nvrhi::Color(0.f));
        }
        
#if WITH_NRD
        if (m_ui.enableDenoiser)
        {
            ProfilerScope scope(*m_Profiler, m_CommandList, ProfilerSection::Denoising);
            m_CommandList->beginMarker("Denoising");

            const void* methodSettings = (m_ui.denoisingMethod == nrd::Denoiser::RELAX_DIFFUSE_SPECULAR)
                ? (void*)&m_ui.relaxSettings
                : (void*)&m_ui.reblurSettings;

            m_NRD->RunDenoiserPasses(m_CommandList, *m_RenderTargets, m_View, m_ViewPrevious, GetFrameIndex(), lightingSettings.enableGradients, methodSettings, m_ui.debug);
            
            m_CommandList->endMarker();
        }
#endif

        m_CompositingPass->Render(
            m_CommandList,
            m_View,
            m_ViewPrevious,
            denoiserMode,
            checkerboard,
            m_ui,
            *m_EnvironmentLight);

        if (m_ui.gbufferSettings.enableTransparentGeometry)
        {
            ProfilerScope scope(*m_Profiler, m_CommandList, ProfilerSection::Glass);

            m_GlassPass->Render(m_CommandList, m_View,
                *m_EnvironmentLight,
                m_ui.gbufferSettings.normalMapScale,
                m_ui.gbufferSettings.enableMaterialReadback,
                m_ui.gbufferSettings.materialReadbackPosition);
        }

        Resolve(m_CommandList, accumulationWeight);

        if (m_ui.enableBloom)
        {
#if WITH_DLSS
            // Use the unresolved image for bloom when DLSS is active because DLSS can modify HDR values significantly and add bloom flicker.
            nvrhi::ITexture* bloomSource = (m_ui.aaMode == AntiAliasingMode::DLSS && m_ui.resolutionScale == 1.f)
                ? m_RenderTargets->HdrColor
                : m_RenderTargets->ResolvedColor;
#else
            nvrhi::ITexture* bloomSource = m_RenderTargets->ResolvedColor;
#endif

            m_BloomPass->Render(m_CommandList, m_RenderTargets->ResolvedFramebuffer, m_UpscaledView, bloomSource, 32.f, 0.005f);
        }

        // Reference image functionality:
        {
            // When the camera is moved, discard the previously stored image, if any, and disable its display.
            if (!cameraIsStatic)
            {
                m_ui.referenceImageCaptured = false;
                m_ui.referenceImageSplit = 0.f;
            }

            // When the user clicks the "Store" button, copy the ResolvedColor texture into ReferenceColor.
            if (m_ui.storeReferenceImage)
            {
                m_CommandList->copyTexture(m_RenderTargets->ReferenceColor, nvrhi::TextureSlice(), m_RenderTargets->ResolvedColor, nvrhi::TextureSlice());
                m_ui.storeReferenceImage = false;
                m_ui.referenceImageCaptured = true;
            }

            // When the "Split Display" parameter is nonzero, show a portion of the previously stored
            // ReferenceColor texture on the left side of the screen by copying it into the ResolvedColor texture.
            if (m_ui.referenceImageSplit > 0.f)
            {
                engine::BlitParameters blitParams;
                blitParams.sourceTexture = m_RenderTargets->ReferenceColor;
                blitParams.sourceBox.m_maxs = float2(m_ui.referenceImageSplit, 1.f);
                blitParams.targetFramebuffer = m_RenderTargets->ResolvedFramebuffer->GetFramebuffer(nvrhi::AllSubresources);
                blitParams.targetBox = blitParams.sourceBox;
                blitParams.sampler = engine::BlitSampler::Point;
                m_CommonPasses->BlitTexture(m_CommandList, blitParams, &m_BindingCache);
            }
        }

        if(m_ui.enableToneMapping)
        { // Tone mapping
            render::ToneMappingParameters ToneMappingParams;
            ToneMappingParams.minAdaptedLuminance = 0.002f;
            ToneMappingParams.maxAdaptedLuminance = 0.2f;
            ToneMappingParams.exposureBias = m_ui.exposureBias;
            ToneMappingParams.eyeAdaptationSpeedUp = 2.0f;
            ToneMappingParams.eyeAdaptationSpeedDown = 1.0f;

            if (exposureResetRequired)
            {
                ToneMappingParams.eyeAdaptationSpeedUp = 0.f;
                ToneMappingParams.eyeAdaptationSpeedDown = 0.f;
            }

            m_ToneMappingPass->SimpleRender(m_CommandList, ToneMappingParams, m_UpscaledView, m_RenderTargets->ResolvedColor);
        }
        else
        {
            m_CommonPasses->BlitTexture(m_CommandList, m_RenderTargets->LdrFramebuffer->GetFramebuffer(m_UpscaledView), m_RenderTargets->ResolvedColor, &m_BindingCache);
        }

        if (m_ui.visualizationMode != VIS_MODE_NONE)
        {
            bool haveSignal = true;
            uint32_t inputBufferIndex = 0;
            switch(m_ui.visualizationMode)
            {
            case VIS_MODE_DENOISED_DIFFUSE:
            case VIS_MODE_DENOISED_SPECULAR:
                haveSignal = m_ui.enableDenoiser;
                break;

            case VIS_MODE_DIFFUSE_CONFIDENCE:
            case VIS_MODE_SPECULAR_CONFIDENCE:
                haveSignal = m_ui.lightingSettings.enableGradients && m_ui.enableDenoiser;
                break;

            case VIS_MODE_RESERVOIR_WEIGHT:
            case VIS_MODE_RESERVOIR_M:
                inputBufferIndex = m_LightingPasses->GetOutputReservoirBufferIndex();
                haveSignal = m_ui.directLightingMode == DirectLightingMode::ReStir;
                break;
                
            case VIS_MODE_GI_WEIGHT:
            case VIS_MODE_GI_M:
                inputBufferIndex = m_LightingPasses->GetGIOutputReservoirBufferIndex();
                haveSignal = m_ui.indirectLightingMode == IndirectLightingMode::ReStirGI;
                break;
            }

            if (haveSignal)
            {
                m_VisualizationPass->Render(
                    m_CommandList,
                    m_RenderTargets->LdrFramebuffer->GetFramebuffer(m_UpscaledView),
                    m_View,
                    m_UpscaledView,
                    *m_isContext,
                    inputBufferIndex,
                    m_ui.visualizationMode,
                    m_ui.aaMode == AntiAliasingMode::Accumulation);
            }
        }

        switch (m_ui.debugRenderOutputBuffer)
        {
            case DebugRenderOutput::LDRColor:
                m_CommonPasses->BlitTexture(m_CommandList, framebuffer, m_RenderTargets->LdrColor, &m_BindingCache);
                break;
            case DebugRenderOutput::Depth:
                m_CommonPasses->BlitTexture(m_CommandList, framebuffer, m_RenderTargets->Depth, &m_BindingCache);
                break;
            case GBufferDiffuseAlbedo:
                m_DebugVizPasses->RenderUnpackedDiffuseAlbeo(m_CommandList, m_UpscaledView);
                m_CommonPasses->BlitTexture(m_CommandList, framebuffer, m_RenderTargets->DebugColor, &m_BindingCache);
                break;
            case GBufferSpecularRough:
                m_DebugVizPasses->RenderUnpackedSpecularRoughness(m_CommandList, m_UpscaledView);
                m_CommonPasses->BlitTexture(m_CommandList, framebuffer, m_RenderTargets->DebugColor, &m_BindingCache);
                break;
            case GBufferNormals:
                m_DebugVizPasses->RenderUnpackedNormals(m_CommandList, m_UpscaledView);
                m_CommonPasses->BlitTexture(m_CommandList, framebuffer, m_RenderTargets->DebugColor, &m_BindingCache);
                break;
            case GBufferGeoNormals:
                m_DebugVizPasses->RenderUnpackedGeoNormals(m_CommandList, m_UpscaledView);
                m_CommonPasses->BlitTexture(m_CommandList, framebuffer, m_RenderTargets->DebugColor, &m_BindingCache);
                break;
            case GBufferEmissive:
                m_CommonPasses->BlitTexture(m_CommandList, framebuffer, m_RenderTargets->GBufferEmissive, &m_BindingCache);
                break;
            case DiffuseLighting:
                m_CommonPasses->BlitTexture(m_CommandList, framebuffer, m_RenderTargets->DiffuseLighting, &m_BindingCache);
                break;
            case SpecularLighting:
                m_CommonPasses->BlitTexture(m_CommandList, framebuffer, m_RenderTargets->SpecularLighting, &m_BindingCache);
                break;
            case DenoisedDiffuseLighting:
                m_CommonPasses->BlitTexture(m_CommandList, framebuffer, m_RenderTargets->DenoisedDiffuseLighting, &m_BindingCache);
                break;
            case DenoisedSpecularLighting:
                m_CommonPasses->BlitTexture(m_CommandList, framebuffer, m_RenderTargets->DenoisedSpecularLighting, &m_BindingCache);
                break;
            case RestirLuminance:
                m_CommonPasses->BlitTexture(m_CommandList, framebuffer, m_RenderTargets->RestirLuminance, &m_BindingCache);
                break;
            case PrevRestirLuminance:
                m_CommonPasses->BlitTexture(m_CommandList, framebuffer, m_RenderTargets->PrevRestirLuminance, &m_BindingCache);
                break;
            case DiffuseConfidence:
                m_CommonPasses->BlitTexture(m_CommandList, framebuffer, m_RenderTargets->DiffuseConfidence, &m_BindingCache);
                break;
            case SpecularConfidence:
                m_CommonPasses->BlitTexture(m_CommandList, framebuffer, m_RenderTargets->SpecularConfidence, &m_BindingCache);
                break;
            case MotionVectors:
                m_CommonPasses->BlitTexture(m_CommandList, framebuffer, m_RenderTargets->MotionVectors, &m_BindingCache);
        }
        
        m_Profiler->EndFrame(m_CommandList);

        m_CommandList->close();
        GetDevice()->executeCommandList(m_CommandList);

        if (!m_args.saveFrameFileName.empty() && m_RenderFrameIndex == m_args.saveFrameIndex)
        {
            bool success = SaveTexture(GetDevice(), m_RenderTargets->LdrColor, m_args.saveFrameFileName.c_str());

            g_ExitCode = success ? 0 : 1;
            
            glfwSetWindowShouldClose(GetDeviceManager()->GetWindow(), 1);
        }
        
        m_ui.gbufferSettings.enableMaterialReadback = false;
        
        if (m_ui.enableAnimations)
            m_FramesSinceAnimation = 0;
        else
            m_FramesSinceAnimation++;
        
        m_ViewPrevious = m_View;
        m_PreviousViewValid = true;
        m_ui.resetAccumulation = false;
        ++m_RenderFrameIndex;
    }
};

#if defined(_WIN32) && !defined(IS_CONSOLE_APP)
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
#else
int main(int argc, char** argv)
#endif
{
    log::SetCallback(&ApplicationLogCallback);
    
    app::DeviceCreationParameters deviceParams;
    deviceParams.swapChainBufferCount = 3;
    deviceParams.enableRayTracingExtensions = true;
    deviceParams.backBufferWidth = 1920;
    deviceParams.backBufferHeight = 1080;
    deviceParams.vsyncEnabled = true;
    deviceParams.infoLogSeverity = log::Severity::Debug;

    UIData ui;
    CommandLineArguments args;

#if defined(_WIN32) && !defined(IS_CONSOLE_APP)
    ProcessCommandLine(__argc, __argv, deviceParams, ui, args);
#else
    ProcessCommandLine(argc, argv, deviceParams, ui, args);
#endif

    if (args.verbose)
        log::SetMinSeverity(log::Severity::Debug);
    
    app::DeviceManager* deviceManager = app::DeviceManager::Create(args.graphicsApi);

#if DONUT_WITH_VULKAN
    if (args.graphicsApi == nvrhi::GraphicsAPI::VULKAN)
    {
        // Set the extra device feature bit(s)
        deviceParams.deviceCreateInfoCallback = [](VkDeviceCreateInfo& info) {
            auto features = const_cast<VkPhysicalDeviceFeatures*>(info.pEnabledFeatures);
            features->fragmentStoresAndAtomics = VK_TRUE;
#if WITH_DLSS
            features->shaderStorageImageWriteWithoutFormat = VK_TRUE;
#endif
        };

#if WITH_DLSS
        DLSS::GetRequiredVulkanExtensions(
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
#endif

    const char* apiString = nvrhi::utils::GraphicsAPIToString(deviceManager->GetGraphicsAPI());

    std::string windowTitle = std::string(g_ApplicationTitle) + " (" + std::string(apiString) + ")";
    
    log::SetErrorMessageCaption(windowTitle.c_str());

#ifdef _WIN32
    // Disable Window scaling.
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
#endif

    if (!deviceManager->CreateWindowDeviceAndSwapChain(deviceParams, windowTitle.c_str()))
    {
        log::error("Cannot initialize a %s graphics device.", apiString);
        return 1;
    }

    bool rayPipelineSupported = deviceManager->GetDevice()->queryFeatureSupport(nvrhi::Feature::RayTracingPipeline);
    bool rayQuerySupported = deviceManager->GetDevice()->queryFeatureSupport(nvrhi::Feature::RayQuery);

    if (!rayPipelineSupported && !rayQuerySupported)
    {
        log::error("The GPU (%s) or its driver does not support ray tracing.", deviceManager->GetRendererString());
        return 1;
    }

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

    {
        SceneRenderer sceneRenderer(deviceManager, ui, args);
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
    
    deviceManager->Shutdown();

    delete deviceManager;

    return g_ExitCode;
}
