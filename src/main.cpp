/***************************************************************************
 # Copyright (c) 2020-2021, NVIDIA CORPORATION.  All rights reserved.
 #
 # NVIDIA CORPORATION and its licensors retain all intellectual property
 # and proprietary rights in and to this software, related documentation
 # and any modifications thereto.  Any use, reproduction, disclosure or
 # distribution of this software and related documentation without an express
 # license agreement from NVIDIA CORPORATION is strictly prohibited.
 **************************************************************************/

// Include this first just to test the cleanliness
#include <rtxdi/RTXDI.h>

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

#if WITH_NRD
#include "NrdIntegration.h"
#endif

#ifndef WIN32
#include <unistd.h>
#endif

using namespace donut;
using namespace donut::math;
using namespace std::chrono;

#include "../shaders/ShaderParameters.h"

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
    std::unique_ptr<RenderTargets> m_RenderTargets;
    app::FirstPersonCamera m_Camera;
    engine::PlanarView m_View;
    engine::PlanarView m_ViewPrevious;
    std::shared_ptr<engine::DirectionalLight> m_SunLight;
    std::shared_ptr<EnvironmentLight> m_EnvironmentLight;
    std::shared_ptr<engine::LoadedTexture> m_EnvironmentMap;
    engine::BindingCache m_BindingCache;

    std::unique_ptr<rtxdi::Context> m_RtxdiContext;
    std::unique_ptr<RaytracedGBufferPass> m_GBufferPass;
    std::unique_ptr<RasterizedGBufferPass> m_RasterizedGBufferPass;
    std::unique_ptr<GlassPass> m_GlassPass;
    std::unique_ptr<CompositingPass> m_CompositingPass;
    std::unique_ptr<AccumulationPass> m_AccumulationPass;
    std::unique_ptr<PrepareLightsPass> m_PrepareLightsPass;
    std::unique_ptr<RenderEnvironmentMapPass> m_RenderEnvironmentMapPass;
    std::unique_ptr<GenerateMipsPass> m_EnvironmentMapPdfMipmapPass;
    std::unique_ptr<GenerateMipsPass> m_LocalLightPdfMipmapPass;
    std::unique_ptr<LightingPasses> m_LightingPasses;
    std::unique_ptr<RtxdiResources> m_RtxdiResources;
    std::unique_ptr<engine::IesProfileLoader> m_IesProfileLoader;
    std::shared_ptr<Profiler> m_Profiler;
    
#if WITH_NRD
    std::unique_ptr<NrdIntegration> m_NRD;
#endif

    UIData& m_ui;
    uint m_FramesSinceAnimation = 0;
    bool m_PreviousViewValid = false;
    time_point<steady_clock> m_PreviousFrameTimeStamp;
    int m_MaterialReadbackCountdown = 0;

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
    SceneRenderer(app::DeviceManager* deviceManager, UIData& ui)
        : ApplicationBase(deviceManager)
        , m_BindingCache(deviceManager->GetDevice())
        , m_ui(ui)
    { 
        m_ui.camera = &m_Camera;
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
        std::filesystem::path mediaPath = app::GetDirectoryWithExecutable().parent_path() / "media";
        std::filesystem::path frameworkShaderPath = app::GetDirectoryWithExecutable() / "shaders/framework" / app::GetShaderTypeName(GetDevice()->getGraphicsAPI());
        std::filesystem::path appShaderPath = app::GetDirectoryWithExecutable() / "shaders/rtxdi-sample" / app::GetShaderTypeName(GetDevice()->getGraphicsAPI());

        log::info("Mounting %s to %s", mediaPath.string().c_str(), "/media");
        log::info("Mounting %s to %s", frameworkShaderPath.string().c_str(), "/shaders/donut");
        log::info("Mounting %s to %s", appShaderPath.string().c_str(), "/shaders/app");

        m_RootFs = std::make_shared<vfs::RootFileSystem>();
        m_RootFs->mount("/media", mediaPath);
        m_RootFs->mount("/shaders/donut", frameworkShaderPath);
        m_RootFs->mount("/shaders/app", appShaderPath);

        m_ShaderFactory = std::make_shared<engine::ShaderFactory>(GetDevice(), m_RootFs, "/shaders");
        m_CommonPasses = std::make_shared<engine::CommonRenderPasses>(GetDevice(), m_ShaderFactory);

        {
            nvrhi::BindlessLayoutDesc bindlessLayoutDesc;
            bindlessLayoutDesc.firstSlot = 0;
            bindlessLayoutDesc.registerSpaces = {
                nvrhi::BindingLayoutItem::RawBuffer_SRV(1),
                nvrhi::BindingLayoutItem::Texture_SRV(2)
            };
            bindlessLayoutDesc.visibility = nvrhi::ShaderType::All;
            bindlessLayoutDesc.maxCapacity = 1024;
            m_BindlessLayout = GetDevice()->createBindlessLayout(bindlessLayoutDesc);
        }

        std::filesystem::path scenePath = "/media/bistro-rtxdi.scene.json";

        m_DescriptorTableManager = std::make_shared<engine::DescriptorTableManager>(GetDevice(), m_BindlessLayout);

        m_TextureCache = std::make_shared<donut::engine::TextureCache>(GetDevice(), m_RootFs, m_DescriptorTableManager);
        
        m_IesProfileLoader = std::make_unique<engine::IesProfileLoader>(GetDevice(), m_ShaderFactory, m_DescriptorTableManager);

        auto sceneTypeFactory = std::make_shared<SampleSceneTypeFactory>();
        m_Scene = std::make_shared<SampleScene>(GetDevice(), *m_ShaderFactory, m_RootFs, m_TextureCache, m_DescriptorTableManager, sceneTypeFactory);
        m_ui.scene = m_Scene;

        SetAsynchronousLoadingEnabled(true);
        BeginLoadingScene(m_RootFs, scenePath);
        GetDeviceManager()->SetVsyncEnabled(true);

        if (!GetDevice()->queryFeatureSupport(nvrhi::Feature::RayQuery))
            m_ui.useRayQuery = false;

        m_Profiler = std::make_shared<Profiler>(*GetDeviceManager());
        m_ui.profiler = m_Profiler;

        m_CompositingPass = std::make_unique<CompositingPass>(GetDevice(), m_ShaderFactory, m_CommonPasses, m_Scene, m_BindlessLayout);
        m_AccumulationPass = std::make_unique<AccumulationPass>(GetDevice(), m_ShaderFactory);
        m_GBufferPass = std::make_unique<RaytracedGBufferPass>(GetDevice(), m_ShaderFactory, m_CommonPasses, m_Scene, m_Profiler, m_BindlessLayout);
        m_RasterizedGBufferPass = std::make_unique<RasterizedGBufferPass>(GetDevice(), m_ShaderFactory, m_CommonPasses, m_Scene, m_Profiler, m_BindlessLayout);
        m_GlassPass = std::make_unique<GlassPass>(GetDevice(), m_ShaderFactory, m_CommonPasses, m_Scene, m_Profiler, m_BindlessLayout);
        m_PrepareLightsPass = std::make_unique<PrepareLightsPass>(GetDevice(), m_ShaderFactory, m_CommonPasses, m_Scene, m_BindlessLayout);
        m_LightingPasses = std::make_unique<LightingPasses>(GetDevice(), m_ShaderFactory, m_CommonPasses, m_Scene, m_Profiler, m_BindlessLayout);

        LoadShaders();

        std::vector<std::string> profileNames;
        m_RootFs->enumerateFiles("/media/ies-profiles", { ".ies" }, vfs::enumerate_to_vector(profileNames));

        for (const std::string& profileName : profileNames)
        {
            auto profile = m_IesProfileLoader->LoadIesProfile(*m_RootFs, "/media/ies-profiles/" + profileName);

            if (profile)
            {
                m_IesProfiles.push_back(profile);
            }
        }
        m_ui.iesProfiles = m_IesProfiles;

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
        m_CompositingPass->CreatePipeline();
        m_AccumulationPass->CreatePipeline();
        m_GBufferPass->CreatePipeline(m_ui.useRayQuery);
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
            m_ui.gbufferSettings.materialReadbackPosition = int2(int(mousex), int(mousey));
            m_ui.gbufferSettings.enableMaterialReadback = true;
            m_MaterialReadbackCountdown = 0;
            return true;
        }

        m_Camera.MouseButtonUpdate(button, action, mods);
        return true;
    }

    virtual void Animate(float fElapsedTimeSeconds) override
    {
        if (m_ui.isLoading)
            return;

        m_Camera.Animate(fElapsedTimeSeconds);

        if (m_ui.enableAnimations)
            m_Scene->Animate(fElapsedTimeSeconds * m_ui.animationSpeed);

        if (m_ToneMappingPass)
            m_ToneMappingPass->AdvanceFrame(fElapsedTimeSeconds);
    }

    virtual void BackBufferResizing() override
    { 
        m_BindingCache.Clear();
        m_RenderTargets = nullptr;
        m_RtxdiContext = nullptr;
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

    void SetupView(const nvrhi::FramebufferInfo& fbinfo, uint effectiveFrameIndex, const engine::PerspectiveCamera* activeCamera)
    {
        nvrhi::Viewport windowViewport(float(fbinfo.width), float(fbinfo.height));

        if (m_TemporalAntiAliasingPass)
            m_TemporalAntiAliasingPass->SetJitter(m_ui.temporalJitter);

        m_View.SetViewport(windowViewport);

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
            m_View.SetMatrices(m_Camera.GetWorldToViewMatrix(), perspProjD3DStyleReverse(radians(60.f), aspectRatio, 0.1f));
        m_View.UpdateCache();

        if (m_ViewPrevious.GetViewExtent().width() == 0)
            m_ViewPrevious = m_View;
    }

    void SetupRenderPasses(const nvrhi::FramebufferInfo& fbinfo, bool& exposureResetRequired)
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

        uint2 environmentMapSize = uint2(environmentMap->getDesc().width, environmentMap->getDesc().height);

        if (m_RtxdiResources && (
            environmentMapSize.x != m_RtxdiResources->EnvironmentPdfTexture->getDesc().width ||
            environmentMapSize.y != m_RtxdiResources->EnvironmentPdfTexture->getDesc().height ||
            numEmissiveMeshes > m_RtxdiResources->GetMaxEmissiveMeshes() ||
            numEmissiveTriangles > m_RtxdiResources->GetMaxEmissiveTriangles() || 
            numPrimitiveLights > m_RtxdiResources->GetMaxPrimitiveLights()))
        {
            m_RtxdiResources = nullptr;
        }

        if (!m_RenderTargets)
        {
            m_RenderTargets = std::make_unique<RenderTargets>(GetDevice(), int2(fbinfo.width, fbinfo.height));

            m_GBufferPass->CreateBindingSet(m_Scene->GetTopLevelAS(), m_Scene->GetPrevTopLevelAS(), *m_RenderTargets);

            m_GlassPass->CreateBindingSet(m_Scene->GetTopLevelAS(), m_Scene->GetPrevTopLevelAS(), *m_RenderTargets);

            m_CompositingPass->CreateBindingSet(*m_RenderTargets);

            m_AccumulationPass->CreateBindingSet(*m_RenderTargets);

            m_RasterizedGBufferPass->CreatePipeline(*m_RenderTargets);

            renderTargetsCreated = true;
        }

        if (!m_RtxdiContext)
        {
            m_ui.rtxdiContextParams.RenderWidth = fbinfo.width;
            m_ui.rtxdiContextParams.RenderHeight = fbinfo.height;

            m_RtxdiContext = std::make_unique<rtxdi::Context>(m_ui.rtxdiContextParams);

            m_ui.regirLightSlotCount = m_RtxdiContext->GetReGIRLightSlotCount();
        }

        if (!m_RtxdiResources)
        {
            uint32_t meshAllocationQuantum = 128;
            uint32_t triangleAllocationQuantum = 1024;
            uint32_t primitiveAllocationQuantum = 128;

            m_RtxdiResources = std::make_unique<RtxdiResources>(
                GetDevice(), 
                *m_RtxdiContext, 
                (numEmissiveMeshes + meshAllocationQuantum - 1) & ~(meshAllocationQuantum - 1),
                (numEmissiveTriangles + triangleAllocationQuantum - 1) & ~(triangleAllocationQuantum - 1),
                (numPrimitiveLights + primitiveAllocationQuantum - 1) & ~(primitiveAllocationQuantum - 1),
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
            m_LightingPasses->CreatePipelines(m_ui.rtxdiContextParams, m_ui.useRayQuery);
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
            m_ToneMappingPass = std::make_unique<render::ToneMappingPass>(GetDevice(), m_ShaderFactory, m_CommonPasses, m_RenderTargets->LdrFramebuffer, m_View, toneMappingParams);
            exposureResetRequired = true;
        }

        if (!m_BloomPass)
        {
            m_BloomPass = std::make_unique<render::BloomPass>(GetDevice(), m_ShaderFactory, m_CommonPasses, m_RenderTargets->ResolvedFramebuffer, m_View);
        }

#if WITH_NRD
        if (!m_NRD)
        {
            m_NRD = std::make_unique<NrdIntegration>(GetDevice(), m_ui.denoisingMethod);
            m_NRD->Initialize(m_RenderTargets->Size.x, m_RenderTargets->Size.y);
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

    void RenderScene(nvrhi::IFramebuffer* framebuffer) override
    {
        if (m_FrameStepMode == FrameStepMode::Wait)
        {
            nvrhi::TextureHandle finalImage;

            if (m_ui.enableToneMapping)
                finalImage = m_RenderTargets->LdrColor;
            else if (m_ui.enableAccumulation)
                finalImage = m_RenderTargets->AccumulatedColor;
            else if (m_ui.enableTAA)
                finalImage = m_RenderTargets->ResolvedColor;
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
        uint effectiveFrameIndex = m_ui.freezeRandom ? 0 : GetFrameIndex();

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
#ifdef WIN32
                Sleep(0);
#else
                usleep(100);
#endif
            }
        }

        m_PreviousFrameTimeStamp = steady_clock::now();

#if WITH_NRD
        if (m_NRD && m_NRD->GetMethod() != m_ui.denoisingMethod)
            m_NRD = nullptr; // need to create a new one
#endif

        if (m_ui.resetRtxdiContext)
        {
            m_RtxdiContext = nullptr;
            m_RtxdiResources = nullptr;
            m_ui.resetRtxdiContext = false;
        }

        if (m_ui.environmentMapDirty == 2)
        {
            LoadEnvironmentMap();
        }

        m_Scene->RefreshSceneGraph(GetFrameIndex());

        const auto& fbinfo = framebuffer->getFramebufferInfo();
        SetupView(fbinfo, effectiveFrameIndex, activeCamera);
        SetupRenderPasses(fbinfo, exposureResetRequired);
        if (!m_ui.freezeRegirPosition)
            m_RegirCenter = m_Camera.GetPosition();

        m_GBufferPass->NextFrame();
        m_LightingPasses->NextFrame();
        m_CompositingPass->NextFrame();
        m_RenderTargets->NextFrame();
        m_GlassPass->NextFrame();
        m_Scene->NextFrame();
        
        // Advance the TAA jitter offset at half frame rate if accumulation is used with
        // checkerboard rendering. Otherwise, the jitter pattern resonates with the checkerboard,
        // and stipple patterns appear in the accumulated results.
        if (!(m_ui.enableAccumulation && (m_RtxdiContext->GetParameters().CheckerboardSamplingMode != rtxdi::CheckerboardMode::Off) && (GetFrameIndex() & 1)))
        {
            m_TemporalAntiAliasingPass->AdvanceFrame();
        }
        
        bool cameraIsStatic = m_PreviousViewValid && m_View.GetViewMatrix() == m_ViewPrevious.GetViewMatrix();
        if (cameraIsStatic && m_ui.enableAccumulation && !m_ui.resetAccumulation)
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
        m_ui.resetAccumulation = false;

        m_Profiler->ResolvePreviousFrame();

        if (m_MaterialReadbackCountdown > 0)
        {
            m_MaterialReadbackCountdown -= 1;

            if (m_MaterialReadbackCountdown == 0)
            {
                m_ui.selectedMaterial.reset();
                int materialIndex = m_Profiler->GetMaterialReadback();
                for (const auto& material : m_Scene->GetSceneGraph()->GetMaterials())
                {
                    if (material->materialID == materialIndex)
                    {
                        m_ui.selectedMaterial = material;
                        break;
                    }
                }
            }
        }
        
        if (m_ui.environmentMapIndex >= 0)
        {
            m_EnvironmentLight->textureIndex = m_EnvironmentMap 
                ? m_EnvironmentMap->bindlessDescriptor.Get()
                : m_RenderEnvironmentMapPass->GetTextureIndex();
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
            ? (m_ui.denoisingMethod == nrd::Method::RELAX_DIFFUSE_SPECULAR) ? DENOISER_MODE_RELAX : DENOISER_MODE_REBLUR
            : DENOISER_MODE_OFF;
#else
        m_ui.enableDenoiser = false;
        uint32_t denoiserMode = DENOISER_MODE_OFF;
#endif

        m_CommandList->open();

        m_Profiler->BeginFrame(m_CommandList);

        AssignIesProfiles(m_CommandList);
        m_Scene->RefreshBuffers(m_CommandList, GetFrameIndex());
        m_RtxdiResources->InitializeNeighborOffsets(m_CommandList, *m_RtxdiContext);

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

            if (m_ui.rasterizeGBuffer)
                m_RasterizedGBufferPass->Render(m_CommandList, m_View, m_ViewPrevious, *m_RenderTargets, m_ui.gbufferSettings);
            else
                m_GBufferPass->Render(m_CommandList, m_View, m_ViewPrevious, m_ui.gbufferSettings);
        }

        rtxdi::FrameParameters frameParameters;
        // The light indexing members of frameParameters are written by PrepareLightsPass below
        frameParameters.frameIndex = effectiveFrameIndex;
        frameParameters.regirCenter = { m_RegirCenter.x, m_RegirCenter.y, m_RegirCenter.z };
        frameParameters.regirCellSize = m_ui.regirCellSize;
        frameParameters.regirSamplingJitter = m_ui.regirSamplingJitter;
        frameParameters.enableLocalLightImportanceSampling = m_ui.enableLocalLightImportanceSampling;

        {
            ProfilerScope scope(*m_Profiler, m_CommandList, ProfilerSection::MeshProcessing);
            
            m_PrepareLightsPass->Process(
                m_CommandList,
                *m_RtxdiContext,
                m_Scene->GetSceneGraph()->GetLights(),
                m_EnvironmentMapPdfMipmapPass != nullptr && m_ui.environmentMapImportanceSampling,
                frameParameters);
        }

        if (m_ui.enableLocalLightImportanceSampling)
        {
            ProfilerScope scope(*m_Profiler, m_CommandList, ProfilerSection::LocalLightPdfMap);
            
            m_LocalLightPdfMipmapPass->Process(m_CommandList);
        }

#if WITH_NRD
        if (m_RtxdiContext->GetParameters().CheckerboardSamplingMode != rtxdi::CheckerboardMode::Off)
        {
            m_ui.reblurSettings.diffuseSettings.checkerboardMode = nrd::CheckerboardMode::BLACK;
            m_ui.reblurSettings.specularSettings.checkerboardMode = nrd::CheckerboardMode::BLACK;
        }
        else
        {
            m_ui.reblurSettings.diffuseSettings.checkerboardMode = nrd::CheckerboardMode::OFF;
            m_ui.reblurSettings.specularSettings.checkerboardMode = nrd::CheckerboardMode::OFF;
        }
#endif

        m_Profiler->BeginSection(m_CommandList, ProfilerSection::LightingTotal);


        LightingPasses::RenderSettings lightingSettings = m_ui.lightingSettings;
        lightingSettings.enablePreviousTLAS &= m_ui.enableAnimations;
        lightingSettings.enableAlphaTestedGeometry = m_ui.gbufferSettings.enableAlphaTestedGeometry;
        lightingSettings.enableTransparentGeometry = m_ui.gbufferSettings.enableTransparentGeometry;
#if WITH_NRD
        lightingSettings.reblurDiffHitDistanceParams = &m_ui.reblurSettings.diffuseSettings.hitDistanceParameters;
        lightingSettings.reblurSpecHitDistanceParams = &m_ui.reblurSettings.specularSettings.hitDistanceParameters;
        lightingSettings.denoiserMode = denoiserMode;
#else
        lightingSettings.denoiserMode = DENOISER_MODE_OFF;
#endif
        
        if (m_ui.renderingMode == RenderingMode::ReStirDirectOnly || m_ui.renderingMode == RenderingMode::ReStirDirectBrdfMIS || m_ui.renderingMode == RenderingMode::ReStirDirectBrdfIndirect)
        {
            // In the combined mode (ReStirDirectBrdfIndirect), we don't want ReSTIR to be the NRD front-end,
            // it should just write out the raw color data.
            lightingSettings.enableDenoiserInputPacking = (m_ui.renderingMode == RenderingMode::ReStirDirectBrdfMIS || m_ui.renderingMode != RenderingMode::ReStirDirectBrdfIndirect);

            m_LightingPasses->Render(m_CommandList,
                *m_RtxdiContext,
                m_View, m_ViewPrevious,
                lightingSettings,
                frameParameters,
                /* enableSpecularMis = */ m_ui.renderingMode == RenderingMode::ReStirDirectBrdfMIS || m_ui.renderingMode == RenderingMode::ReStirDirectBrdfIndirect);
        }
        
        if (m_ui.renderingMode == RenderingMode::BrdfDirectOnly || m_ui.renderingMode == RenderingMode::ReStirDirectBrdfMIS || m_ui.renderingMode == RenderingMode::ReStirDirectBrdfIndirect)
        {
            lightingSettings.enableDenoiserInputPacking = true;

            m_LightingPasses->RenderBrdfRays(
                m_CommandList,
                *m_RtxdiContext,
                m_View,
                lightingSettings,
                frameParameters,
                *m_EnvironmentLight,
                /* enableIndirect = */ m_ui.renderingMode == RenderingMode::ReStirDirectBrdfIndirect,
                /* enableAdditiveBlend = */ m_ui.renderingMode == RenderingMode::ReStirDirectBrdfMIS || m_ui.renderingMode == RenderingMode::ReStirDirectBrdfIndirect,
                /* enableSpecularMis = */ m_ui.renderingMode == RenderingMode::ReStirDirectBrdfMIS || m_ui.renderingMode == RenderingMode::ReStirDirectBrdfIndirect);
        }

        m_Profiler->EndSection(m_CommandList, ProfilerSection::LightingTotal);

#if WITH_NRD
        if (m_ui.enableDenoiser)
        {
            ProfilerScope scope(*m_Profiler, m_CommandList, ProfilerSection::Denoising);
            m_CommandList->beginMarker("Denoising");

            const void* methodSettings = (m_ui.denoisingMethod == nrd::Method::RELAX_DIFFUSE_SPECULAR)
                ? (void*)&m_ui.relaxSettings
                : (void*)&m_ui.reblurSettings;

            m_NRD->RunDenoiserPasses(m_CommandList, *m_RenderTargets, m_View, m_ViewPrevious, GetFrameIndex(), methodSettings);
            
            m_CommandList->endMarker();
        }
#endif

        m_CompositingPass->Render(m_CommandList, m_View, m_ViewPrevious, m_ui.enableTextures, denoiserMode, *m_EnvironmentLight);

        if (m_ui.gbufferSettings.enableTransparentGeometry)
        {
            ProfilerScope scope(*m_Profiler, m_CommandList, ProfilerSection::Glass);

            m_GlassPass->Render(m_CommandList, m_View,
                *m_EnvironmentLight,
                m_ui.gbufferSettings.normalMapScale,
                m_ui.gbufferSettings.enableMaterialReadback,
                m_ui.gbufferSettings.materialReadbackPosition);
        }

        nvrhi::TextureHandle finalHdrImage = m_RenderTargets->HdrColor;

        if (m_ui.enableAccumulation)
        {
            m_AccumulationPass->Render(m_CommandList, m_View, accumulationWeight);

            finalHdrImage = m_RenderTargets->AccumulatedColor;
        }
        else if (m_ui.enableTAA)
        {
            m_TemporalAntiAliasingPass->TemporalResolve(m_CommandList, m_ui.taaParams, m_PreviousViewValid, m_View, m_ViewPrevious);
            
            if (m_ui.enableBloom)
            {
                m_BloomPass->Render(m_CommandList, m_RenderTargets->ResolvedFramebuffer, m_View, m_RenderTargets->ResolvedColor, 32.f, 0.005f);
            }

            finalHdrImage = m_RenderTargets->ResolvedColor;
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

            m_ToneMappingPass->SimpleRender(m_CommandList, ToneMappingParams, m_View, finalHdrImage);
            
            m_CommonPasses->BlitTexture(m_CommandList, framebuffer, m_RenderTargets->LdrColor, &m_BindingCache);
        }
        else
        {
            m_CommonPasses->BlitTexture(m_CommandList, framebuffer, finalHdrImage, &m_BindingCache);
        }
        
        m_Profiler->EndFrame(m_CommandList);

        m_CommandList->close();
        GetDevice()->executeCommandList(m_CommandList);

        if (m_ui.gbufferSettings.enableMaterialReadback)
        {
            m_ui.gbufferSettings.enableMaterialReadback = false;
            m_MaterialReadbackCountdown = 2; // i.e. in 2 frames read the material index
        }

        if (m_ui.enableAnimations)
            m_FramesSinceAnimation = 0;
        else
            m_FramesSinceAnimation++;
        
        m_ViewPrevious = m_View;
        m_PreviousViewValid = true;
    }
};

#ifdef WIN32
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
#else
int main()
#endif
{
#if USE_DX12 && USE_VK
    const nvrhi::GraphicsAPI api = app::GetGraphicsAPIFromCommandLine(__argc, __argv);

    if (api == nvrhi::GraphicsAPI::D3D11)
    {
        log::error("D3D11 is not supported by this application.");
        return 1;
    }
#elif USE_DX12
    const nvrhi::GraphicsAPI api = nvrhi::GraphicsAPI::D3D12;
#elif USE_VK
    const nvrhi::GraphicsAPI api = nvrhi::GraphicsAPI::VULKAN;
#endif

    app::DeviceManager* deviceManager = app::DeviceManager::Create(api);

    app::DeviceCreationParameters deviceParams;
    deviceParams.swapChainBufferCount = 3;
    deviceParams.enableRayTracingExtensions = true;
    deviceParams.backBufferWidth = 1920;
    deviceParams.backBufferHeight = 1080;
    deviceParams.vsyncEnabled = true;
#ifdef _DEBUG
    deviceParams.enableDebugRuntime = true; 
    deviceParams.enableNvrhiValidationLayer = true;
#endif

    const char* apiString = nvrhi::utils::GraphicsAPIToString(deviceManager->GetGraphicsAPI());

    std::string windowTitle = "RTX Direct Illumination SDK Sample (" + std::string(apiString) + ")";
    
    log::SetErrorMessageCaption(windowTitle.c_str());

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
    
    {
        UIData ui;
        SceneRenderer sceneRenderer(deviceManager, ui);
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
    }
    
    deviceManager->Shutdown();

    delete deviceManager;

    return 0;
}
