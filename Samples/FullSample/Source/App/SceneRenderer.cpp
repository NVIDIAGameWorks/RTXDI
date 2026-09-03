/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: LicenseRef-NvidiaProprietary
 *
 * NVIDIA CORPORATION, its affiliates and licensors retain all intellectual
 * property and proprietary rights in and to this material, related
 * documentation and any modifications thereto. Any use, reproduction,
 * disclosure or distribution of this material and related documentation
 * without an express license agreement from NVIDIA CORPORATION or
 * its affiliates is strictly prohibited.
 */

#include "SceneRenderer.h"

#include <donut/render/ToneMappingPasses.h>
#include <donut/render/TemporalAntiAliasingPass.h>
#include <donut/engine/ShaderFactory.h>
#include <donut/engine/CommonRenderPasses.h>
#include <donut/engine/TextureCache.h>
#include <donut/render/BloomPass.h>
#include <donut/render/DLSS.h>
#include <donut/render/SkyPass.h>
#include <donut/engine/Scene.h>
#include <donut/engine/FramebufferFactory.h>
#include <donut/engine/DescriptorTableManager.h>
#include <donut/engine/IesProfile.h>
#include <donut/app/DeviceManager.h>
#include <donut/core/log.h>
#include <donut/core/vfs/VFS.h>
#include <donut/core/math/math.h>
#include <nvrhi/utils.h>
#ifdef DONUT_WITH_TASKFLOW
#include <taskflow/taskflow.hpp>
#endif
#include <donut/core/math/math.h>

#include "ShaderDebug/ShaderPrint.h"
#include "Profiler.h"
#include "RenderTargets.h"
#include "RtxdiResources.h"
#include "Scene/SampleScene.h"
#include "Scene/Lights.h"
#include "Screenshot.h"
#include "UserInterface.h"

using namespace donut::math;
#include "ShaderDebug/DebugVizPasses.h"
#include "RenderPasses/DenoisingPasses/ConfidencePass.h"
#include "RenderPasses/DenoisingPasses/DLSSRRInputFormattingPass.h"
#include "RenderPasses/DenoisingPasses/FilterGradientsPass.h"
#include "RenderPasses/AccumulationPass.h"
#include "RenderPasses/CompositingPass.h"
#include "RenderPasses/GBufferPass.h"
#include "RenderPasses/GenerateMipsPass.h"
#include "RenderPasses/GlassPass.h"
#include "RenderPasses/LightingPasses/LightingPasses.h"
#include "RenderPasses/PrepareLightsPass.h"
#include "RenderPasses/RenderEnvironmentMapPass.h"
#include "ShaderDebug/VisualizationPass.h"
#include "ShaderDebug/ReservoirSubfieldVizPasses/DIReservoirVizPass.h"
#include "ShaderDebug/ReservoirSubfieldVizPasses/GIReservoirVizPass.h"
#include "ShaderDebug/ReservoirSubfieldVizPasses/PTReservoirVizPass.h"
#include "ShaderDebug/PTPathViz/PTPathVizPass.h"

struct DefaultCameraSettingsForScene
{
    float3 position = float3(0.0f, 0.0f, 0.0f);
    float3 direction = float3(1.0f, 0.0f, 0.0f);
    float movementSpeed = 1.0f;
    float rotateSpeed = 1.0f;
};

DefaultCameraSettingsForScene GetDefaultCameraSettingsForScene(SceneAsset sa)
{
    switch (sa)
    {
    case SceneAsset::Arcade:
        return { .position = float3(-1.658f, 1.577f, 1.69f),
                 .direction = float3(0.693521f, -0.30981f, -0.65042f),
                 .movementSpeed = 3.0f,
                 .rotateSpeed = 0.005f };
    case SceneAsset::Bistro:
    case SceneAsset::BistroMirror:
        return { .position = float3(-7.688f, 2.0f, 5.594f),
                 .direction = float3(0.3539f, 0.0f, 0.9426f),
                 .movementSpeed = 3.0f,
                 .rotateSpeed = 0.005f };
    case SceneAsset::CornellBox:
        return { .position = float3(0.0f, 5.0f, -15.0),
                 .direction = float3(0.0f, 0.0f, 1.0f),
                 .movementSpeed = 3.0f,
                 .rotateSpeed = 0.005f};
    default:
        log::warning("Loading default settings for scene without default settings. Update your code.");
        return { .position = float3(0.0f, 1.0f, 0.0),
                 .direction = float3(0.0f, 0.0f, 1.0f),
                 .movementSpeed = 1.0f,
                 .rotateSpeed = 0.005f };
    }
}

SceneRenderer::SceneRenderer(app::DeviceManager* deviceManager, UIData& ui, const SceneRendererStartupSettings& args)
    : ApplicationBase(deviceManager)
    , m_bindingCache(deviceManager->GetDevice())
    , m_ui(ui)
    , m_args(args)
{
    m_ui.resources->camera = &m_camera;
}

SceneRenderer::~SceneRenderer()
{

}

[[nodiscard]] std::shared_ptr<engine::ShaderFactory> SceneRenderer::GetShaderFactory() const
{
    return m_shaderFactory;
}

[[nodiscard]] std::shared_ptr<vfs::IFileSystem> SceneRenderer::GetRootFs() const
{
    return m_rootFs;
}

bool SceneRenderer::Init()
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
    std::filesystem::path appShaderPath = app::GetDirectoryWithExecutable() / "shaders/full-sample" / app::GetShaderTypeName(GetDevice()->getGraphicsAPI());

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

    // std::filesystem::path scenePath = "/Assets/Media/bistro-rtxdi.scene.json";
    std::filesystem::path scenePath;
    switch (m_args.sceneAsset)
    {
    case SceneAsset::Arcade:
        scenePath = "/Assets/Media/Arcade/Arcade.gltf";
        break;
    case SceneAsset::Bistro:
        scenePath = "/Assets/Media/bistro-rtxdi.scene.json";
        break;
    default:
        log::warning("Loading unrecognized scene enum, defaulting to BistroMirror scene");
    case SceneAsset::BistroMirror:
        scenePath = "/Assets/Media/bistro_with_mirrors-rtxdi.scene.json";
        break;
    case SceneAsset::CornellBox:
        scenePath = "/Assets/Media/CornellBox/CornellBox.gltf";
        break;
    }

    m_descriptorTableManager = std::make_shared<engine::DescriptorTableManager>(GetDevice(), m_bindlessLayout);

    m_TextureCache = std::make_shared<donut::engine::TextureCache>(GetDevice(), m_rootFs, m_descriptorTableManager);
    m_TextureCache->SetInfoLogSeverity(donut::log::Severity::Debug);

    m_iesProfileLoader = std::make_unique<engine::IesProfileLoader>(GetDevice(), m_shaderFactory, m_descriptorTableManager);

    auto sceneTypeFactory = std::make_shared<SampleSceneTypeFactory>();
    m_scene = std::make_shared<SampleScene>(GetDevice(), *m_shaderFactory, m_rootFs, m_TextureCache, m_descriptorTableManager, sceneTypeFactory);
    m_ui.resources->scene = m_scene;

    SetAsynchronousLoadingEnabled(true);
    BeginLoadingScene(m_rootFs, scenePath);
    GetDeviceManager()->SetVsyncEnabled(true);

    if (!GetDevice()->queryFeatureSupport(nvrhi::Feature::RayQuery))
        m_ui.useRayQuery = false;

    m_profiler = std::make_shared<Profiler>(*GetDeviceManager());
    m_ui.resources->profiler = m_profiler;

    m_filterGradientsPass = std::make_unique<FilterGradientsPass>(GetDevice(), m_shaderFactory);
    m_DLSSRRInputFormattingPass = std::make_unique<DLSSRRInputFormattingPass>(GetDevice(), m_shaderFactory);
    m_confidencePass = std::make_unique<ConfidencePass>(GetDevice(), m_shaderFactory);
    m_compositingPass = std::make_unique<CompositingPass>(GetDevice(), m_shaderFactory, m_CommonPasses, m_scene, m_bindlessLayout);
    m_accumulationPass = std::make_unique<AccumulationPass>(GetDevice(), m_shaderFactory);
    m_gBufferPass = std::make_unique<RaytracedGBufferPass>(GetDevice(), m_shaderFactory, m_CommonPasses, m_scene, m_profiler, m_bindlessLayout);
    m_rasterizedGBufferPass = std::make_unique<RasterizedGBufferPass>(GetDevice(), m_shaderFactory, m_CommonPasses, m_scene, m_profiler, m_bindlessLayout);
    m_postprocessGBufferPass = std::make_unique<PostprocessGBufferPass>(GetDevice(), m_shaderFactory);
    m_glassPass = std::make_unique<GlassPass>(GetDevice(), m_shaderFactory, m_CommonPasses, m_scene, m_profiler, m_bindlessLayout);
    m_prepareLightsPass = std::make_unique<PrepareLightsPass>(GetDevice(), m_shaderFactory, m_CommonPasses, m_scene, m_bindlessLayout);
    m_lightingPasses = std::make_unique<LightingPasses>(GetDevice(), m_shaderFactory, m_CommonPasses, m_scene, m_profiler, m_bindlessLayout);
    m_ptPathVizPass = std::make_unique<PTPathVizPass>(GetDevice(), m_shaderFactory, m_scene, m_profiler, m_bindlessLayout);

    uint32_t shaderDebugPrintBuffersizeInBytes = 1024 * 16;
    uint32_t numFramesInFlight = 2;
    m_shaderDebugPrintPass = std::make_unique<ShaderPrintReadbackPass>(
        GetDevice(), m_shaderFactory, shaderDebugPrintBuffersizeInBytes, numFramesInFlight);

#if DONUT_WITH_DLSS
    m_dlssSR = donut::render::DLSS::Create(GetDevice(), *m_shaderFactory, app::GetDirectoryWithExecutable().generic_string());
    m_dlssRR = donut::render::DLSS::Create(GetDevice(), *m_shaderFactory, app::GetDirectoryWithExecutable().generic_string());
#endif

    LoadShaders();

    std::vector<std::string> profileNames;
    m_rootFs->enumerateFiles("/Assets/Media/ies-profiles", { ".ies" }, vfs::enumerate_to_vector(profileNames));

    for (const std::string& profileName : profileNames)
    {
        auto profile = m_iesProfileLoader->LoadIesProfile(*m_rootFs, "/Assets/Media/ies-profiles/" + profileName);

        if (profile)
        {
            m_iesProfiles.push_back(profile);
        }
    }
    m_ui.resources->iesProfiles = m_iesProfiles;

    m_commandList = GetDevice()->createCommandList();

    return true;
}

void SceneRenderer::AssignIesProfiles(nvrhi::ICommandList* commandList)
{
    for (const auto& light : m_scene->GetSceneGraph()->GetLights())
    {
        if (light->GetLightType() == LightType_Spot)
        {
            SpotLightWithProfile& spotLight = static_cast<SpotLightWithProfile&>(*light);

            if (spotLight.profileName.empty())
                continue;

            if (spotLight.profileTextureIndex >= 0)
                continue;

            auto foundProfile = std::find_if(m_iesProfiles.begin(), m_iesProfiles.end(),
                [&spotLight](auto it) { return it->name == spotLight.profileName; });

            if (foundProfile != m_iesProfiles.end())
            {
                m_iesProfileLoader->BakeIesProfile(**foundProfile, commandList);

                spotLight.profileTextureIndex = (*foundProfile)->textureIndex;
            }
        }
    }
}

void SceneRenderer::LoadInitialCameraSettings()
{
    DefaultCameraSettingsForScene defaults = GetDefaultCameraSettingsForScene(m_args.sceneAsset);
    float3 camPos = m_args.cameraPosition.has_value() ? m_args.cameraPosition.value() : defaults.position;
    float3 dir = m_args.cameraDirection.has_value() ? m_args.cameraDirection.value() : defaults.direction;
    m_camera.LookTo(camPos, dir);

    m_camera.SetRotateSpeed(m_args.cameraRotateSpeed.has_value() ? m_args.cameraRotateSpeed.value() : defaults.rotateSpeed);
    m_camera.SetMoveSpeed(m_args.cameraMovementSpeed.has_value() ? m_args.cameraMovementSpeed.value() : defaults.movementSpeed);
}

void SceneRenderer::SceneLoaded()
{
    ApplicationBase::SceneLoaded();

    m_scene->FinishedLoading(GetFrameIndex());

    LoadInitialCameraSettings();

    const auto& sceneGraph = m_scene->GetSceneGraph();

    for (const auto& pLight : sceneGraph->GetLights())
    {
        if (pLight->GetLightType() == LightType_Directional)
        {
            m_sunLight = std::static_pointer_cast<engine::DirectionalLight>(pLight);
            break;
        }
    }

    if (!m_sunLight)
    {
        m_sunLight = std::make_shared<engine::DirectionalLight>();
        sceneGraph->AttachLeafNode(sceneGraph->GetRootNode(), m_sunLight);
        m_sunLight->SetDirection(dm::double3(0.15, -1.0, 0.3));
        m_sunLight->angularSize = 1.f;
    }

    m_commandList->open();
    AssignIesProfiles(m_commandList);
    m_commandList->close();
    GetDevice()->executeCommandList(m_commandList);

    // Create an environment light
    m_environmentLight = std::make_shared<EnvironmentLight>();
    sceneGraph->AttachLeafNode(sceneGraph->GetRootNode(), m_environmentLight);
    m_environmentLight->SetName("Environment");
    m_ui.environmentMapDirty = 2;
    m_ui.environmentMapIndex = 0;

    m_rasterizedGBufferPass->CreateBindingSet();

    m_scene->BuildMeshBLASes(GetDevice());

    GetDeviceManager()->SetVsyncEnabled(false);

    m_ui.isLoading = false;
}

void SceneRenderer::LoadShaders()
{
    m_filterGradientsPass->CreatePipeline();
    m_DLSSRRInputFormattingPass->CreatePipeline();
    m_confidencePass->CreatePipeline();
    m_compositingPass->CreatePipeline();
    m_accumulationPass->CreatePipeline();
    m_gBufferPass->CreatePipeline(m_ui.useRayQuery);
    m_postprocessGBufferPass->CreatePipeline();
    m_glassPass->CreatePipeline(m_ui.useRayQuery);
    m_prepareLightsPass->CreatePipeline();
    if(m_renderTargets) m_ptPathVizPass->CreatePipelines(*m_renderTargets);
}

bool SceneRenderer::LoadScene(std::shared_ptr<vfs::IFileSystem> fs, const std::filesystem::path& sceneFileName)
{
    if (m_scene->Load(sceneFileName))
    {
        return true;
    }

    return false;
}

bool SceneRenderer::KeyboardUpdate(int key, int scancode, int action, int mods)
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
        m_frameStepMode = (m_frameStepMode == FrameStepMode::Disabled) ? FrameStepMode::Wait : FrameStepMode::Disabled;
        return true;
    }

    if (mods == 0 && key == GLFW_KEY_F2 && action == GLFW_PRESS)
    {
        if (m_frameStepMode == FrameStepMode::Wait)
            m_frameStepMode = FrameStepMode::Step;
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

    if (mods == 0 && action == GLFW_PRESS)
    {
        switch (key)
        {
        case GLFW_KEY_1:
            m_ui.indirectLightingMode = IndirectLightingMode::None;
            m_ui.resetAccumulation = true;
            return true;
            break;
        case GLFW_KEY_2:
            m_ui.indirectLightingMode = IndirectLightingMode::Brdf;
            m_ui.resetAccumulation = true;
            return true;
            break;
        case GLFW_KEY_3:
            m_ui.indirectLightingMode = IndirectLightingMode::ReStirGI;
            m_ui.resetAccumulation = true;
            return true;
            break;
        case GLFW_KEY_4:
            m_ui.indirectLightingMode = IndirectLightingMode::ReStirPT;
            m_ui.resetAccumulation = true;
            return true;
            break;

        case GLFW_KEY_B:
            m_ui.aaMode = AntiAliasingMode::Accumulation;
            return true;
            break;
        case GLFW_KEY_G:
            m_ui.aaMode = AntiAliasingMode::None;
            m_ui.resetAccumulation = true;
            return true;
            break;
        case GLFW_KEY_T:
            m_ui.resetAccumulation = true;
            return true;
            break;
        }
    }

    if (mods == GLFW_MOD_SHIFT && action == GLFW_PRESS)
    {
        switch (key)
        {
        case GLFW_KEY_1:
            m_ui.restirGI.resamplingMode = rtxdi::ReSTIRGI_ResamplingMode::None;
            m_ui.restirPT.resamplingMode = rtxdi::ReSTIRPT_ResamplingMode::None;
            m_ui.resetAccumulation = true;
            return true;
            break;
        case GLFW_KEY_2:
            m_ui.restirGI.resamplingMode = rtxdi::ReSTIRGI_ResamplingMode::Temporal;
            m_ui.restirPT.resamplingMode = rtxdi::ReSTIRPT_ResamplingMode::Temporal;
            m_ui.resetAccumulation = true;
            return true;
            break;
        case GLFW_KEY_3:
            m_ui.restirGI.resamplingMode = rtxdi::ReSTIRGI_ResamplingMode::Spatial;
            m_ui.restirPT.resamplingMode = rtxdi::ReSTIRPT_ResamplingMode::Spatial;
            m_ui.resetAccumulation = true;
            return true;
            break;
        case GLFW_KEY_4:
            m_ui.restirGI.resamplingMode = rtxdi::ReSTIRGI_ResamplingMode::TemporalAndSpatial;
            m_ui.restirPT.resamplingMode = rtxdi::ReSTIRPT_ResamplingMode::TemporalAndSpatial;
            m_ui.resetAccumulation = true;
            return true;
            break;
        }
    }

    if (mods == GLFW_MOD_CONTROL && action == GLFW_PRESS)
    {
        switch (key)
        {
        case GLFW_KEY_1:
            m_ui.lightingSettings.ptParameters.lightSamplingMode = PTInitialSamplingLightSamplingMode::EmissiveOnly;
            m_ui.resetAccumulation = true;
            break;
        case GLFW_KEY_2:
            m_ui.lightingSettings.ptParameters.lightSamplingMode = PTInitialSamplingLightSamplingMode::NeeOnly;
            m_ui.resetAccumulation = true;
            break;
        case GLFW_KEY_3:
            m_ui.lightingSettings.ptParameters.lightSamplingMode = PTInitialSamplingLightSamplingMode::Mis;
            m_ui.resetAccumulation = true;
            break;
        }
    }

    if (mods == 0 && key == GLFW_KEY_SPACE && action == GLFW_PRESS)
    {
        m_shouldUpdatePtViz = true;
        m_shouldRunShaderDebugPrint = true;
        return true;
    }

    m_camera.KeyboardUpdate(key, scancode, action, mods);

    return true;
}

bool SceneRenderer::MousePosUpdate(double xpos, double ypos)
{
    m_camera.MousePosUpdate(xpos, ypos);
    return true;
}

bool SceneRenderer::MouseButtonUpdate(int button, int action, int mods)
{
    if (button == GLFW_MOUSE_BUTTON_RIGHT && action == GLFW_PRESS)
    {
        double mousex = 0, mousey = 0;
        glfwGetCursorPos(GetDeviceManager()->GetWindow(), &mousex, &mousey);

        // Scale the mouse position according to the render resolution scale
        mousex *= m_view.GetViewport().width() / m_upscaledView.GetViewport().width();
        mousey *= m_view.GetViewport().height() / m_upscaledView.GetViewport().height();

        m_ui.gbufferSettings.materialReadbackPosition = int2(int(mousex), int(mousey));
        m_ui.gbufferSettings.enableMaterialReadback = true;

        uint2 mouseCoords = uint2(int(mousex), int(mousey));
        m_ui.lightingSettings.restirShaderDebugParams.mouseSelectedPixel = mouseCoords;
        m_shaderDebugPrintPass->SetSelectedPixelCoordinates(mouseCoords);
        m_shouldUpdatePtViz = true;
        m_shouldRunShaderDebugPrint = true;
        return true;
    }
    if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_RELEASE)
    {
        double mousex = 0, mousey = 0;
        glfwGetCursorPos(GetDeviceManager()->GetWindow(), &mousex, &mousey);

        // Scale the mouse position according to the render resolution scale
        mousex *= m_view.GetViewport().width() / m_upscaledView.GetViewport().width();
        mousey *= m_view.GetViewport().height() / m_upscaledView.GetViewport().height();
    }

    m_camera.MouseButtonUpdate(button, action, mods);
    return true;
}

void SceneRenderer::Animate(float fElapsedTimeSeconds)
{
    if (m_ui.isLoading)
        return;

    if (!m_args.saveFrameFileName.empty())
        fElapsedTimeSeconds = 1.f / 60.f;

    m_camera.Animate(fElapsedTimeSeconds);

    if (m_ui.enableAnimations)
        m_scene->Animate(fElapsedTimeSeconds * m_ui.animationSpeed);

    if (m_toneMappingPass)
        m_toneMappingPass->AdvanceFrame(fElapsedTimeSeconds);
}

void SceneRenderer::BackBufferResized(const uint32_t width, const uint32_t height, const uint32_t sampleCount)
{
    // If the render size is overridden from the command line, ignore the window size.
    if (m_args.renderWidth > 0 && m_args.renderHeight > 0)
        return;

    if (m_renderTargets && m_renderTargets->Size.x == int(width) && m_renderTargets->Size.y == int(height))
        return;

    m_bindingCache.Clear();
    m_renderTargets = nullptr;
    m_isContext = nullptr;
    m_rtxdiResources = nullptr;
    m_temporalAntiAliasingPass = nullptr;
    m_toneMappingPass = nullptr;
    m_bloomPass = nullptr;
#if WITH_NRD
    m_nrd = nullptr;
#endif
#if DONUT_WITH_DLSS
    m_dlssInitAttempted = false;
#endif
}

void SceneRenderer::RenderWaitFrame(nvrhi::IFramebuffer* framebuffer)
{
    nvrhi::TextureHandle finalImage;

    if (m_ui.enableToneMapping)
        finalImage = m_renderTargets->LdrColor;
    else
        finalImage = m_renderTargets->HdrColor;

    m_commandList->open();

    m_CommonPasses->BlitTexture(m_commandList, framebuffer, finalImage, &m_bindingCache);

    m_commandList->close();
    GetDevice()->executeCommandList(m_commandList);
}

void SceneRenderer::UpdateEnvironmentMap()
{
    if (m_ui.environmentMapDirty == 2)
    {
        if (m_environmentMap)
        {
            // Make sure there is no rendering in-flight before we unload the texture and erase its descriptor.
            // Decsriptor manipulations are synchronous and immediately affect whatever is executing on the GPU.
            GetDevice()->waitForIdle();

            m_TextureCache->UnloadTexture(m_environmentMap);

            m_environmentMap = nullptr;
        }

        if (m_ui.environmentMapIndex > 0)
        {
            auto& environmentMaps = m_scene->GetEnvironmentMaps();
            const std::string& environmentMapPath = environmentMaps[m_ui.environmentMapIndex];

            m_environmentMap = m_TextureCache->LoadTextureFromFileDeferred(environmentMapPath, false);

            if (m_TextureCache->IsTextureLoaded(m_environmentMap))
            {
                m_TextureCache->ProcessRenderingThreadCommands(*m_CommonPasses, 0.f);
                m_TextureCache->LoadingFinished();

                m_environmentMap->bindlessDescriptor = m_descriptorTableManager->CreateDescriptorHandle(nvrhi::BindingSetItem::Texture_SRV(0, m_environmentMap->texture));
            }
            else
            {
                // Failed to load the file: revert to the procedural map and remove this file from the list.
                m_environmentMap = nullptr;
                environmentMaps.erase(environmentMaps.begin() + m_ui.environmentMapIndex);
                m_ui.environmentMapIndex = 0;
            }
        }
    }
}

void SceneRenderer::SetupView(uint32_t renderWidth, uint32_t renderHeight, const engine::PerspectiveCamera* activeCamera)
{
    nvrhi::Viewport windowViewport((float)renderWidth, (float)renderHeight);

    if (m_temporalAntiAliasingPass)
        m_temporalAntiAliasingPass->SetJitter(m_ui.temporalJitter);

    nvrhi::Viewport renderViewport = windowViewport;
    renderViewport.maxX = roundf(renderViewport.maxX * m_ui.resolutionScale);
    renderViewport.maxY = roundf(renderViewport.maxY * m_ui.resolutionScale);

    m_view.SetViewport(renderViewport);

    if (m_ui.enablePixelJitter && m_temporalAntiAliasingPass)
    {
        m_view.SetPixelOffset(m_temporalAntiAliasingPass->GetCurrentPixelOffset());
    }
    else
    {
        m_view.SetPixelOffset(0.f);
    }

    const float aspectRatio = windowViewport.width() / windowViewport.height();
    if (activeCamera)
        m_view.SetMatrices(activeCamera->GetWorldToViewMatrix(), perspProjD3DStyleReverse(activeCamera->verticalFov, aspectRatio, activeCamera->zNear));
    else
        m_view.SetMatrices(m_camera.GetWorldToViewMatrix(), perspProjD3DStyleReverse(radians(m_ui.verticalFov), aspectRatio, 0.01f));
    m_view.UpdateCache();

    if (m_viewPrevious.GetViewExtent().width() == 0)
        m_viewPrevious = m_view;
    if (m_viewPreviousPrevious.GetViewExtent().width() == 0)
        m_viewPreviousPrevious = m_viewPrevious;

    m_upscaledView = m_view;
    m_upscaledView.SetViewport(windowViewport);
}

void SceneRenderer::SetupRenderPasses(uint32_t renderWidth, uint32_t renderHeight, bool& exposureResetRequired)
{
    if (m_ui.environmentMapDirty == 2)
    {
        m_environmentMapPdfMipmapPass = nullptr;

        m_ui.environmentMapDirty = 1;
    }

    if (m_ui.reloadShaders)
    {
        GetDevice()->waitForIdle();

        m_shaderFactory->ClearCache();
        m_temporalAntiAliasingPass = nullptr;
        m_renderEnvironmentMapPass = nullptr;
        m_environmentMapPdfMipmapPass = nullptr;
        m_localLightPdfMipmapPass = nullptr;
        m_visualizationPass = nullptr;
        m_debugVizPasses = nullptr;
        m_diReservoirVizPass = nullptr;
        m_giReservoirVizPass = nullptr;
        m_ptReservoirVizPass = nullptr;
        m_ui.environmentMapDirty = 1;

        LoadShaders();
    }

    bool renderTargetsCreated = false;

    if (!m_renderEnvironmentMapPass)
    {
        m_renderEnvironmentMapPass = std::make_unique<RenderEnvironmentMapPass>(GetDevice(), m_shaderFactory, m_descriptorTableManager, 2048);
    }

    if (!m_isContext)
    {
        rtxdi::ImportanceSamplingContext_StaticParameters isStaticParams;
        isStaticParams.CheckerboardSamplingMode = m_ui.restirDIStaticParams.CheckerboardSamplingMode;
        isStaticParams.NeighborOffsetCount = m_ui.restirDIStaticParams.NeighborOffsetCount;
        isStaticParams.renderHeight = renderHeight;
        isStaticParams.renderWidth = renderWidth;
        isStaticParams.regirStaticParams = m_ui.regirStaticParams;

        m_isContext = std::make_unique<rtxdi::ImportanceSamplingContext>(isStaticParams);

        m_ui.regirLightSlotCount = m_isContext->GetReGIRContext().GetReGIRLightSlotCount();
    }

    if (!m_renderTargets)
    {
        m_renderTargets = std::make_shared<RenderTargets>(GetDevice(), int2((int)renderWidth, (int)renderHeight));

        m_profiler->SetRenderTargets(m_renderTargets);

        m_gBufferPass->CreateBindingSet(m_scene->GetTopLevelAS(), m_scene->GetPrevTopLevelAS(), *m_renderTargets);

        m_postprocessGBufferPass->CreateBindingSet(*m_renderTargets);

        m_glassPass->CreateBindingSet(m_scene->GetTopLevelAS(), m_scene->GetPrevTopLevelAS(), *m_renderTargets);

        m_filterGradientsPass->CreateBindingSet(*m_renderTargets);

        m_DLSSRRInputFormattingPass->CreateBindingSet(*m_renderTargets);

        m_confidencePass->CreateBindingSet(*m_renderTargets);

        m_accumulationPass->CreateBindingSet(*m_renderTargets);

        m_rasterizedGBufferPass->CreatePipeline(*m_renderTargets);

        m_compositingPass->CreateBindingSet(*m_renderTargets);

        m_ptPathVizPass->CreatePipelines(*m_renderTargets);
        m_ptPathVizPass->CreateBindingSets();

        m_visualizationPass = nullptr;
        m_debugVizPasses = nullptr;
        m_diReservoirVizPass = nullptr;
        m_giReservoirVizPass = nullptr;
        m_ptReservoirVizPass = nullptr;
        renderTargetsCreated = true;
    }

    UpdateRtxdiResources();

    if (!m_environmentMapPdfMipmapPass || m_rtxdiResourcesCreated)
    {
        m_environmentMapPdfMipmapPass = std::make_unique<GenerateMipsPass>(
            GetDevice(),
            m_shaderFactory,
            GetEnvironmentMapTexture(),
            m_rtxdiResources->EnvironmentPdfTexture);
    }

    if (!m_localLightPdfMipmapPass || m_rtxdiResourcesCreated)
    {
        m_localLightPdfMipmapPass = std::make_unique<GenerateMipsPass>(
            GetDevice(),
            m_shaderFactory,
            nullptr,
            m_rtxdiResources->LocalLightPdfTexture);
    }

    if (renderTargetsCreated)
    {
        m_ptPathVizPass->CreatePipelines(*m_renderTargets);
        m_ptPathVizPass->CreateBindingSets();
    }

    if (renderTargetsCreated || m_rtxdiResourcesCreated)
    {
        m_lightingPasses->CreateBindingSet(
            m_scene->GetTopLevelAS(),
            m_scene->GetPrevTopLevelAS(),
            *m_renderTargets,
            *m_ptPathVizPass,
            *m_shaderDebugPrintPass,
            *m_rtxdiResources);
    }

    if (m_rtxdiResourcesCreated || m_ui.reloadShaders)
    {
        // Some RTXDI context settings affect the shader permutations
        m_lightingPasses->CreatePipelines(m_ui.regirStaticParams, m_ui.useRayQuery);
    }

    m_ui.reloadShaders = false;

    if (!m_temporalAntiAliasingPass)
    {
        render::TemporalAntiAliasingPass::CreateParameters taaParams;
        taaParams.motionVectors = m_renderTargets->MotionVectors;
        taaParams.unresolvedColor = m_renderTargets->HdrColor;
        taaParams.resolvedColor = m_renderTargets->ResolvedColor;
        taaParams.feedback1 = m_renderTargets->TaaFeedback1;
        taaParams.feedback2 = m_renderTargets->TaaFeedback2;
        taaParams.useCatmullRomFilter = true;

        m_temporalAntiAliasingPass = std::make_unique<render::TemporalAntiAliasingPass>(
            GetDevice(), m_shaderFactory, m_CommonPasses, m_view, taaParams);
    }

    exposureResetRequired = false;
    if (!m_toneMappingPass)
    {
        render::ToneMappingPass::CreateParameters toneMappingParams;
        m_toneMappingPass = std::make_unique<render::ToneMappingPass>(GetDevice(), m_shaderFactory, m_CommonPasses, m_renderTargets->LdrFramebuffer, m_upscaledView, toneMappingParams);
        exposureResetRequired = true;
    }

    if (!m_bloomPass)
    {
        m_bloomPass = std::make_unique<render::BloomPass>(GetDevice(), m_shaderFactory, m_CommonPasses, m_renderTargets->ResolvedFramebuffer, m_upscaledView);
    }

    if (!m_visualizationPass || renderTargetsCreated || m_rtxdiResourcesCreated)
    {
        m_visualizationPass = std::make_unique<VisualizationPass>(GetDevice(), *m_CommonPasses, *m_shaderFactory, *m_renderTargets, *m_rtxdiResources);
    }

    if (!m_debugVizPasses || renderTargetsCreated)
    {
        m_debugVizPasses = std::make_unique<DebugVizPasses>(GetDevice(), m_shaderFactory, m_scene, m_bindlessLayout);
        m_debugVizPasses->CreatePipelines();
        m_debugVizPasses->CreateBindingSets(*m_renderTargets, *m_rtxdiResources, m_renderTargets->DebugColor);

    }

    if (!m_diReservoirVizPass || renderTargetsCreated)
    {
        m_diReservoirVizPass = std::make_unique<DIReservoirVizPass>(GetDevice(), m_shaderFactory, m_scene, m_bindlessLayout);
        m_diReservoirVizPass->CreateBindingSet(m_rtxdiResources->DIReservoirBuffer, m_renderTargets->DebugColor, m_lightingPasses->GetConstantBuffer());
    }

    if (!m_giReservoirVizPass || renderTargetsCreated)
    {
        m_giReservoirVizPass = std::make_unique<GIReservoirVizPass>(GetDevice(), m_shaderFactory, m_scene, m_bindlessLayout);
        m_giReservoirVizPass->CreateBindingSet(m_rtxdiResources->GIReservoirBuffer, m_renderTargets->DebugColor);
    }

    if (!m_ptReservoirVizPass || renderTargetsCreated)
    {
        m_ptReservoirVizPass = std::make_unique<PTReservoirVizPass>(GetDevice(), m_shaderFactory, m_scene, m_bindlessLayout);
        m_ptReservoirVizPass->CreateBindingSet(m_rtxdiResources->PTReservoirBuffer, m_renderTargets->DebugColor, m_lightingPasses->GetConstantBuffer());
    }

#if WITH_NRD
    if (!m_nrd)
    {
        m_nrd = std::make_unique<NrdIntegration>(GetDevice(), m_ui.nrdDenoisingMethod);
        m_nrd->Initialize(m_renderTargets->Size.x, m_renderTargets->Size.y);
    }
#endif
#if DONUT_WITH_DLSS
    if(!m_dlssInitAttempted)
    {
        InitDLSS();
    }
#else
    // DLSS (and therefore DLSS-RR) isn't compiled in, so the default DLSS-RR denoiser can't be used:
    // fall back to NRD and switch the PT defaults to the NRD profile (once - the condition is false
    // afterwards since the UI can't reselect DLSS-RR without support).
    if (m_ui.denoiserMode == DenoiserMode::DLSS_RR)
    {
        m_ui.denoiserMode = DenoiserMode::NRD_REBLUR;
        if (m_ui.autoApplyDLSSRRPreset)
            m_ui.ApplyNonDLSSRRPreset();
    }
#endif
}

void SceneRenderer::RenderSplashScreen(nvrhi::IFramebuffer* framebuffer)
{
    nvrhi::ITexture* framebufferTexture = framebuffer->getDesc().colorAttachments[0].texture;
    m_commandList->open();
    m_commandList->clearTextureFloat(framebufferTexture, nvrhi::AllSubresources, nvrhi::Color(0.f));
    m_commandList->close();
    GetDevice()->executeCommandList(m_commandList);

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


void SceneRenderer::UpdateReGIRContextFromUI()
{
    if (!m_ui.freezeRegirPosition)
        m_regirCenter = m_camera.GetPosition();
    auto& regirContext = m_isContext->GetReGIRContext();
    auto dynamicParams = m_ui.regirDynamicParameters;
    dynamicParams.center = { m_regirCenter.x, m_regirCenter.y, m_regirCenter.z };
    regirContext.SetDynamicParameters(dynamicParams);
}

RTXDI_DISpatioTemporalResamplingParameters SceneRenderer::SpatioTemporalParamsFromIndividualParams(const RTXDI_DITemporalResamplingParameters& tParams,
    const RTXDI_DISpatialResamplingParameters& sParams)
{
    RTXDI_DISpatioTemporalResamplingParameters stParams = {};
    stParams.biasCorrectionMode = static_cast<ReSTIRDI_SpatioTemporalBiasCorrectionMode>(sParams.biasCorrectionMode);
    stParams.depthThreshold = tParams.depthThreshold;
    stParams.discountNaiveSamples = sParams.discountNaiveSamples;
    stParams.enableMaterialSimilarityTest = sParams.enableMaterialSimilarityTest;
    stParams.enablePermutationSampling = tParams.enablePermutationSampling;
    stParams.enableVisibilityShortcut = tParams.enableVisibilityShortcut;
    stParams.maxHistoryLength = tParams.maxHistoryLength;
    stParams.normalThreshold = tParams.normalThreshold;
    stParams.numDisocclusionBoostSamples = sParams.numDisocclusionBoostSamples;
    stParams.numSamples = sParams.numSamples;
    stParams.samplingRadius = sParams.samplingRadius;
    stParams.uniformRandomNumber = tParams.uniformRandomNumber;
    return stParams;
}

RTXDI_GISpatioTemporalResamplingParameters SceneRenderer::SpatioTemporalParamsFromIndividualParams(const RTXDI_GITemporalResamplingParameters& tParams,
    const RTXDI_GISpatialResamplingParameters& sParams)
{
    RTXDI_GISpatioTemporalResamplingParameters stParams = {};
    stParams.normalThreshold = tParams.normalThreshold;
    stParams.depthThreshold = tParams.depthThreshold;
    stParams.biasCorrectionMode = sParams.biasCorrectionMode;
    stParams.numSamples = sParams.numSamples;
    stParams.samplingRadius = sParams.samplingRadius;
    stParams.maxHistoryLength = tParams.maxHistoryLength;
    stParams.enableFallbackSampling = tParams.enableFallbackSampling;
    stParams.maxReservoirAge = tParams.maxReservoirAge;
    stParams.enablePermutationSampling = tParams.enablePermutationSampling;
    stParams.uniformRandomNumber = tParams.uniformRandomNumber;
    return stParams;
}

void SceneRenderer::UpdateReSTIRDIContextFromUI()
{
    rtxdi::ReSTIRDIContext& restirDIContext = m_isContext->GetReSTIRDIContext();
    RTXDI_DIInitialSamplingParameters initialSamplingParams = m_ui.restirDI.initialSamplingParams;
    initialSamplingParams.numLocalLightSamples = m_ui.restirDI.neeLocalLightSampling.GetSelectedLocalLightSampleCount();
    restirDIContext.SetResamplingMode(m_ui.restirDI.resamplingMode);
    restirDIContext.SetInitialSamplingParameters(initialSamplingParams);
    restirDIContext.SetTemporalResamplingParameters(m_ui.restirDI.temporalResamplingParams);
    restirDIContext.SetBoilingFilterParameters(m_ui.restirDI.boilingFilter);
    restirDIContext.SetSpatialResamplingParameters(m_ui.restirDI.spatialResamplingParams);
    restirDIContext.SetSpatioTemporalResamplingParameters(SpatioTemporalParamsFromIndividualParams(m_ui.restirDI.temporalResamplingParams, m_ui.restirDI.spatialResamplingParams));
    restirDIContext.SetShadingParameters(m_ui.restirDI.shadingParams);
}

void SceneRenderer::UpdateReSTIRGIContextFromUI()
{
    rtxdi::ReSTIRGIContext& restirGIContext = m_isContext->GetReSTIRGIContext();
    restirGIContext.SetResamplingMode(m_ui.restirGI.resamplingMode);
    restirGIContext.SetTemporalResamplingParameters(m_ui.restirGI.temporalResamplingParams);
    restirGIContext.SetBoilingFilterParameters(m_ui.restirGI.boilingFilter);
    restirGIContext.SetSpatialResamplingParameters(m_ui.restirGI.spatialResamplingParams);
    restirGIContext.SetSpatioTemporalResamplingParameters(SpatioTemporalParamsFromIndividualParams(m_ui.restirGI.temporalResamplingParams, m_ui.restirGI.spatialResamplingParams));
    restirGIContext.SetFinalShadingParameters(m_ui.restirGI.finalShadingParams);
}

void SceneRenderer::UpdateReSTIRPTContextFromUI()
{
    auto& restirPTContext = m_isContext->GetReSTIRPTContext();
    restirPTContext.SetResamplingMode(m_ui.restirPT.resamplingMode);
    restirPTContext.SetInitialSamplingParameters(m_ui.restirPT.initialSampling);
    restirPTContext.SetDecorrelationParameters(m_ui.restirPT.decorrelation);

    // The reservoir shares a single "age" field for both age-based rejection and the duplication
    // map's temporal (stagnancy) channel. Whenever the duplication map is generated, age is
    // repurposed as stagnancy, so age-based rejection must be disabled to keep the two consistent.
    RTXDI_PTTemporalResamplingParameters temporalParams = m_ui.restirPT.temporalResampling;
    if (rtxdi::NeedsDuplicationMap(temporalParams, m_ui.restirPT.decorrelation))
        temporalParams.enableAgeBasedRejection = false;
    restirPTContext.SetTemporalResamplingParameters(temporalParams);
    restirPTContext.SetHybridShiftParameters(m_ui.restirPT.hybridShift);
    restirPTContext.SetReconnectionParameters(m_ui.restirPT.reconnection);
    restirPTContext.SetBoilingFilterParameters(m_ui.restirPT.boilingFilter);
    restirPTContext.SetSpatialResamplingParameters(m_ui.restirPT.spatialResampling);
}

bool SceneRenderer::IsLocalLightPowerRISEnabled()
{
    if (m_ui.indirectLightingMode == IndirectLightingMode::ReStirGI)
    {
        RTXDI_DIInitialSamplingParameters indirectReSTIRDISamplingParams = m_ui.lightingSettings.brdfptParams.secondarySurfaceReSTIRDIParams.initialSamplingParams;
        bool enabled = (indirectReSTIRDISamplingParams.localLightSamplingMode == ReSTIRDI_LocalLightSamplingMode::Power_RIS) ||
            (indirectReSTIRDISamplingParams.localLightSamplingMode == ReSTIRDI_LocalLightSamplingMode::ReGIR_RIS && m_isContext->GetReGIRContext().IsLocalLightPowerRISEnabled());
        if (enabled)
            return true;
    }
    else if (m_ui.indirectLightingMode == IndirectLightingMode::ReStirPT)
    {
        RTXDI_DIInitialSamplingParameters ptNeeISParams = m_ui.lightingSettings.ptParameters.nee.initialSamplingParams;
        bool enabled = (ptNeeISParams.localLightSamplingMode == ReSTIRDI_LocalLightSamplingMode::Power_RIS) ||
            (ptNeeISParams.localLightSamplingMode == ReSTIRDI_LocalLightSamplingMode::ReGIR_RIS && m_isContext->GetReGIRContext().IsLocalLightPowerRISEnabled());
        if (enabled)
            return true;
    }
    return m_isContext->IsLocalLightPowerRISEnabled();
}

void SceneRenderer::RenderScene(nvrhi::IFramebuffer* framebuffer)
{
    if (ProcessFrameStepMode(framebuffer))
        return;

    const engine::PerspectiveCamera* activeCamera = nullptr;
    uint effectiveFrameIndex = m_renderFrameIndex;

    RunBenchmarkAnimation(activeCamera, effectiveFrameIndex);
    EnforceFpsLimit();

#if WITH_NRD
    if (m_nrd && m_nrd->GetDenoiser() != m_ui.nrdDenoisingMethod)
        m_nrd = nullptr; // need to create a new one
#endif

    if (m_ui.resetISContext)
    {
        GetDevice()->waitForIdle();

        m_isContext = nullptr;
        m_rtxdiResources = nullptr;
        m_ui.resetISContext = false;
    }

    UpdateEnvironmentMap();

    m_scene->RefreshSceneGraph(GetFrameIndex());

    const auto& fbinfo = framebuffer->getFramebufferInfo();
    uint32_t renderWidth = fbinfo.width;
    uint32_t renderHeight = fbinfo.height;
    if (m_args.renderWidth > 0 && m_args.renderHeight > 0)
    {
        renderWidth = m_args.renderWidth;
        renderHeight = m_args.renderHeight;
    }
    SetupView(renderWidth, renderHeight, activeCamera);
    bool exposureResetRequired = false;
    SetupRenderPasses(renderWidth, renderHeight, exposureResetRequired);
    UpdateReSTIRDIContextFromUI();
    UpdateReGIRContextFromUI();
    UpdateReSTIRGIContextFromUI();
    UpdateReSTIRPTContextFromUI();
    m_shouldRunShaderDebugPrint = m_ui.debugOutputSettings.shaderDebugPrintOnClickOrAlways ? m_shouldRunShaderDebugPrint : true;
    m_shaderDebugPrintPass->Enable(m_ui.debugOutputSettings.enableShaderDebugPrint && m_shouldRunShaderDebugPrint);
    m_shouldRunShaderDebugPrint = m_ui.debugOutputSettings.shaderDebugPrintOnClickOrAlways ? false : true;
#if DONUT_WITH_DLSS
    if (!m_ui.dlssAvailable && m_ui.aaMode == AntiAliasingMode::DLSS_SR)
        m_ui.aaMode = AntiAliasingMode::TAA;
    // fall back to NRD if DLSS-RR isn't supported.
    if (!m_ui.dlssRRSupported && m_ui.denoiserMode == DenoiserMode::DLSS_RR)
        m_ui.denoiserMode = DenoiserMode::NRD_REBLUR;
#endif

    AdvanceFrameForRenderPasses();

    bool cameraIsStatic = m_prevViewValid && m_view.GetViewMatrix() == m_viewPrevious.GetViewMatrix();
    ProcessAccumulationModeLogic(cameraIsStatic);

    float accumulationWeight = 1.f / (float)m_ui.numAccumulatedFrames;

    m_profiler->ResolvePreviousFrame();

    UpdateSelectedMaterialInUI();
    UpdateEnvironmentLightAndSunLight();

#if WITH_NRD
    bool nrdAvailable = m_nrd && m_nrd->IsAvailable();

    // DLSS-RR is a denoiser mode but it denoises during the post-processing resolve, so
    // the NRD/lighting path treats it as "off". Other modes map directly to the DENOISER_MODE_* values.
    uint32_t denoiserMode = (m_ui.denoiserMode == DenoiserMode::DLSS_RR)
        ? (uint32_t)DENOISER_MODE_OFF
        : (uint32_t)m_ui.denoiserMode;
#else
    m_ui.enableDenoiser = false;
    uint32_t denoiserMode = DENOISER_MODE_OFF;
#endif

    m_commandList->open();
    m_profiler->BeginFrame(m_commandList);
    m_shaderDebugPrintPass->InitializeFrame(m_commandList);

    AssignIesProfiles(m_commandList);
    m_scene->RefreshBuffers(m_commandList, GetFrameIndex());
    m_rtxdiResources->InitializeNeighborOffsets(m_commandList, m_isContext->GetNeighborOffsetCount()); // TODO Move to be next to other RTXDI passes?
    UpdateAccelerationStructure();
    EnvironmentMap();
    nvrhi::utils::ClearColorAttachment(m_commandList, framebuffer, 0, nvrhi::Color(0.f));
    GBuffer();

    // The light indexing members of frameParameters are written by PrepareLightsPass below
    rtxdi::ReSTIRDIContext& restirDIContext = m_isContext->GetReSTIRDIContext();

    UpdateRtxdiFrameIndex(effectiveFrameIndex);
    PrepareLights();
    ProcessLocalLightPdfMipMap();
    ApplyNrdCheckerboardSettings();
    CopyPSRBuffers();

    const bool checkerboard = restirDIContext.GetStaticParameters().CheckerboardSamplingMode != rtxdi::CheckerboardMode::Off;

    bool enableDirectReStirPass = m_ui.lightingSettings.directLightingMode == DirectLightingMode::ReStir;
    bool enableBrdfAndIndirectPass = m_ui.lightingSettings.directLightingMode == DirectLightingMode::Brdf || m_ui.indirectLightingMode != IndirectLightingMode::None;
    bool enableIndirect = m_ui.indirectLightingMode != IndirectLightingMode::None;

    // When indirect lighting is enabled, we don't want ReSTIR to be the NRD front-end,
    // it should just write out the raw color data.
    RTXDI_ShadingParameters restirDIShadingParams = m_isContext->GetReSTIRDIContext().GetShadingParameters();
    restirDIShadingParams.enableDenoiserInputPacking = !enableIndirect;
    m_isContext->GetReSTIRDIContext().SetShadingParameters(restirDIShadingParams);

    LightingPasses::RenderSettings lightingSettings = GetFullyProcessedLightingSettings(denoiserMode, enableDirectReStirPass);
    PresampleLights(enableDirectReStirPass, enableIndirect, lightingSettings);
    RenderDirectLighting(enableDirectReStirPass, checkerboard, lightingSettings);
    RenderIndirectLighting(enableBrdfAndIndirectPass, enableDirectReStirPass, lightingSettings);
    HandleNoLightingCase(enableDirectReStirPass, enableBrdfAndIndirectPass);
    m_DLSSRRInputFormattingPass->Render(m_commandList, m_view);
    Denoiser(lightingSettings);
    m_compositingPass->Render(m_commandList, m_view, m_viewPrevious, denoiserMode, checkerboard, m_ui, *m_environmentLight);
    TransparentGeometry();
    ResolveAA(m_commandList, accumulationWeight);
    Bloom();
    ReferenceImage(cameraIsStatic);
    ToneMapping(exposureResetRequired);
    DebugPathViz();
    FinalFramebufferOutput(framebuffer);

    m_shaderDebugPrintPass->ReadBackPrintData(m_commandList);

    m_profiler->EndFrame(m_commandList);

    m_commandList->close();
    GetDevice()->executeCommandList(m_commandList);

    ProcessScreenshotFrame();
    OutputShaderMessages();

    m_ui.gbufferSettings.enableMaterialReadback = false;

    if (m_ui.enableAnimations)
        m_framesSinceAnimation = 0;
    else
        m_framesSinceAnimation++;

    m_viewPreviousPrevious = m_viewPrevious;
    m_viewPrevious = m_view;
    m_prevViewValid = true;
    m_ui.resetAccumulation = false;
    ++m_renderFrameIndex;
}

nvrhi::ITexture* SceneRenderer::GetEnvironmentMapTexture()
{
    return (m_ui.environmentMapIndex > 0) ? m_environmentMap->texture.Get() : m_renderEnvironmentMapPass->GetTexture();
}

void SceneRenderer::UpdateRtxdiResources()
{
    m_rtxdiResourcesCreated = false;

    uint32_t numEmissiveMeshes, numEmissiveTriangles;
    m_prepareLightsPass->CountLightsInScene(numEmissiveMeshes, numEmissiveTriangles);
    uint32_t numPrimitiveLights = uint32_t(m_scene->GetSceneGraph()->GetLights().size());
    uint32_t numGeometryInstances = uint32_t(m_scene->GetSceneGraph()->GetGeometryInstancesCount());

    const auto environmentMap = GetEnvironmentMapTexture();

    uint2 environmentMapSize = uint2(environmentMap->getDesc().width, environmentMap->getDesc().height);

    if (!m_rtxdiResources || (
        environmentMapSize.x != m_rtxdiResources->EnvironmentPdfTexture->getDesc().width ||
        environmentMapSize.y != m_rtxdiResources->EnvironmentPdfTexture->getDesc().height ||
        numEmissiveMeshes > m_rtxdiResources->GetMaxEmissiveMeshes() ||
        numEmissiveTriangles > m_rtxdiResources->GetMaxEmissiveTriangles() ||
        numPrimitiveLights > m_rtxdiResources->GetMaxPrimitiveLights() ||
        numGeometryInstances > m_rtxdiResources->GetMaxGeometryInstances()))
    {
        uint32_t meshAllocationQuantum = 128;
        uint32_t triangleAllocationQuantum = 1024;
        uint32_t primitiveAllocationQuantum = 128;

        m_rtxdiResources = std::make_unique<RtxdiResources>(
            GetDevice(),
            m_isContext->GetReSTIRDIContext(),
            m_isContext->GetRISBufferSegmentAllocator(),
            (numEmissiveMeshes + meshAllocationQuantum - 1) & ~(meshAllocationQuantum - 1),
            (numEmissiveTriangles + triangleAllocationQuantum - 1) & ~(triangleAllocationQuantum - 1),
            (numPrimitiveLights + primitiveAllocationQuantum - 1) & ~(primitiveAllocationQuantum - 1),
            numGeometryInstances,
            environmentMapSize.x,
            environmentMapSize.y);

        m_prepareLightsPass->CreateBindingSet(*m_rtxdiResources);

        m_rtxdiResourcesCreated = true;

        // Make sure that the environment PDF map is re-generated
        m_ui.environmentMapDirty = 1;
    }
}

#if DONUT_WITH_DLSS
void SceneRenderer::InitDLSS()
{
    donut::render::DLSS::InitParameters dlssParams = {};
    dlssParams.inputWidth = m_renderTargets->Size.x;
    dlssParams.inputHeight = m_renderTargets->Size.y;
    dlssParams.outputWidth = m_renderTargets->Size.x;
    dlssParams.outputHeight = m_renderTargets->Size.y;
    m_dlssSR->Init(dlssParams);

    m_ui.dlssAvailable = m_dlssSR->IsDlssInitialized();
    m_ui.dlssSRSupported = m_dlssSR->IsDlssSupported();

    dlssParams.useRayReconstruction = true;
    m_dlssRR->Init(dlssParams);
    m_ui.dlssRRSupported = m_dlssRR->IsRayReconstructionSupported();
    m_dlssInitAttempted = true;

    // DLSS-RR is the default denoiser; if Ray Reconstruction isn't supported on this device, fall
    // back to NRD and switch the PT defaults (applied for DLSS-RR at construction) to the NRD profile.
    if (!m_ui.dlssRRSupported && m_ui.denoiserMode == DenoiserMode::DLSS_RR)
    {
        m_ui.denoiserMode = DenoiserMode::NRD_REBLUR;
        if (m_ui.autoApplyDLSSRRPreset)
            m_ui.ApplyNonDLSSRRPreset();
    }
}
#endif

bool SceneRenderer::ProcessFrameStepMode(nvrhi::IFramebuffer* framebuffer)
{
    if (m_frameStepMode == FrameStepMode::Wait)
    {
        RenderWaitFrame(framebuffer);
        return true;
    }

    if (m_frameStepMode == FrameStepMode::Step)
        m_frameStepMode = FrameStepMode::Wait;

    return false;
}

void SceneRenderer::RunBenchmarkAnimation(const engine::PerspectiveCamera*& activeCamera, uint& effectiveFrameIndex)
{
    if (m_ui.animationFrame.has_value())
    {
        const float animationTime = float(m_ui.animationFrame.value()) * (1.f / 240.f);

        auto* animation = m_scene->GetBenchmarkAnimation();
        if (animation && animationTime < animation->GetDuration())
        {
            (void)animation->Apply(animationTime);
            activeCamera = m_scene->GetBenchmarkCamera();
            effectiveFrameIndex = m_ui.animationFrame.value();
            m_ui.animationFrame = effectiveFrameIndex + 1;
        }
        else
        {
            m_ui.benchmarkResults = m_profiler->GetAsText();
            m_ui.animationFrame.reset();

            if (m_args.benchmark)
            {
                glfwSetWindowShouldClose(GetDeviceManager()->GetWindow(), GLFW_TRUE);
                log::info("BENCHMARK RESULTS >>>\n\n%s<<<", m_ui.benchmarkResults.c_str());
            }
        }
    }
}

void SceneRenderer::AdvanceFrameForRenderPasses()
{
    m_gBufferPass->NextFrame();
    m_postprocessGBufferPass->NextFrame();
    m_lightingPasses->NextFrame();
    m_confidencePass->NextFrame();
    m_compositingPass->NextFrame();
    m_visualizationPass->NextFrame();
    m_renderTargets->NextFrame();
    m_glassPass->NextFrame();
    m_scene->NextFrame();
    m_debugVizPasses->NextFrame();

    // Advance the TAA jitter offset at half frame rate if accumulation is used with
    // checkerboard rendering. Otherwise, the jitter pattern resonates with the checkerboard,
    // and stipple patterns appear in the accumulated results.
    if (!((m_ui.aaMode == AntiAliasingMode::Accumulation) && (m_isContext->GetReSTIRDIContext().GetStaticParameters().CheckerboardSamplingMode != rtxdi::CheckerboardMode::Off) && (GetFrameIndex() & 1)))
    {
        m_temporalAntiAliasingPass->AdvanceFrame();
    }
}

void SceneRenderer::ProcessAccumulationModeLogic(bool cameraIsStatic)
{
    if (cameraIsStatic && (m_ui.aaMode == AntiAliasingMode::Accumulation) && !m_ui.resetAccumulation)
    {
        m_ui.numAccumulatedFrames += 1;

        if (m_ui.framesToAccumulate > 0)
            m_ui.numAccumulatedFrames = std::min(m_ui.numAccumulatedFrames, m_ui.framesToAccumulate);

        m_profiler->EnableAccumulation(true);
    }
    else
    {
        m_ui.numAccumulatedFrames = 1;
        m_profiler->EnableAccumulation(m_ui.animationFrame.has_value());
    }
}

void SceneRenderer::UpdateSelectedMaterialInUI()
{
    int materialIndex = m_profiler->GetMaterialReadback();
    if (materialIndex >= 0)
    {
        for (const auto& material : m_scene->GetSceneGraph()->GetMaterials())
        {
            if (material->materialID == materialIndex)
            {
                m_ui.resources->selectedMaterial = material;
                break;
            }
        }
    }
}

void SceneRenderer::UpdateEnvironmentLightAndSunLight()
{
    if (m_ui.environmentMapIndex >= 0)
    {
        if (m_environmentMap)
        {
            m_environmentLight->textureIndex = m_environmentMap->bindlessDescriptor.Get();
            const auto& textureDesc = m_environmentMap->texture->getDesc();
            m_environmentLight->textureSize = uint2(textureDesc.width, textureDesc.height);
        }
        else
        {
            m_environmentLight->textureIndex = m_renderEnvironmentMapPass->GetTextureIndex();
            const auto& textureDesc = m_renderEnvironmentMapPass->GetTexture()->getDesc();
            m_environmentLight->textureSize = uint2(textureDesc.width, textureDesc.height);
        }
        m_environmentLight->radianceScale = ::exp2f(m_ui.environmentIntensityBias);
        m_environmentLight->rotation = m_ui.environmentRotation / 360.f;  //  +/- 0.5
        m_sunLight->irradiance = (m_ui.environmentMapIndex > 0) ? 0.f : 1.f;
    }
    else
    {
        m_environmentLight->textureIndex = -1;
        m_sunLight->irradiance = 0.f;
    }
}

void SceneRenderer::EnforceFpsLimit()
{
    if (m_ui.enableFpsLimit && GetFrameIndex() > 0)
    {
        uint64_t expectedFrametime = 1000000 / m_ui.fpsLimit;

        while (true)
        {
            uint64_t currentFrametime = duration_cast<microseconds>(steady_clock::now() - m_prevFrameTimeStamp).count();

            if (currentFrametime >= expectedFrametime)
                break;
#ifdef _WIN32
            Sleep(0);
#else
            usleep(100);
#endif
        }
    }
    m_prevFrameTimeStamp = steady_clock::now();
}

void SceneRenderer::UpdateAccelerationStructure()
{
    if (m_framesSinceAnimation < 2)
    {
        ProfilerScope scope(*m_profiler, m_commandList, ProfilerSection::TlasUpdate);

        m_scene->UpdateSkinnedMeshBLASes(m_commandList, GetFrameIndex());
        m_scene->BuildTopLevelAccelStruct(m_commandList);
    }
    m_commandList->compactBottomLevelAccelStructs();
}

void SceneRenderer::EnvironmentMap()
{
    if (m_ui.environmentMapDirty)
    {
        ProfilerScope scope(*m_profiler, m_commandList, ProfilerSection::EnvironmentMap);

        if (m_ui.environmentMapIndex == 0)
        {
            donut::render::SkyParameters params;
            m_renderEnvironmentMapPass->Render(m_commandList, *m_sunLight, params);
        }

        m_environmentMapPdfMipmapPass->Process(m_commandList);

        m_ui.environmentMapDirty = 0;
    }
}

void SceneRenderer::GBuffer()
{
    ProfilerScope scope(*m_profiler, m_commandList, ProfilerSection::GBufferFill);

    GBufferSettings gbufferSettings = m_ui.gbufferSettings;
    float upscalingLodBias = ::log2f(m_view.GetViewport().width() / m_upscaledView.GetViewport().width());
    gbufferSettings.textureLodBias += upscalingLodBias;

    if (m_ui.rasterizeGBuffer)
        m_rasterizedGBufferPass->Render(m_commandList, m_view, m_viewPrevious, *m_renderTargets, m_ui.gbufferSettings);
    else
        m_gBufferPass->Render(m_commandList, m_view, m_viewPrevious, m_ui.gbufferSettings);

    m_postprocessGBufferPass->Render(m_commandList, m_view);
}

void SceneRenderer::CopyPSRBuffers()
{
    m_commandList->beginMarker("Copy PSR Buffers");
    // PSRMotionVectors will be used for temporal resampling, so non-PSR pixels need to have opaque motion vectors populated
    m_commandList->copyTexture(m_renderTargets->PSRMotionVectors, nvrhi::TextureSlice(), m_renderTargets->MotionVectors, nvrhi::TextureSlice());
    // Initialize all PSR buffers to 0; FinalShading reads from them only when PSRDiffuseAlbedo or PSRSpecularF0 > 0, else uses default G-buffer
    m_commandList->clearTextureFloat(m_renderTargets->PSRDepth, nvrhi::AllSubresources, nvrhi::Color(0.f));
    m_commandList->clearTextureFloat(m_renderTargets->PSRNormalRoughness, nvrhi::AllSubresources, nvrhi::Color(0.f));
    m_commandList->clearTextureFloat(m_renderTargets->PSRHitT, nvrhi::AllSubresources, nvrhi::Color(0.f));
    m_commandList->clearTextureUInt(m_renderTargets->PSRDiffuseAlbedo, nvrhi::AllSubresources, 0u);
    m_commandList->clearTextureUInt(m_renderTargets->PSRSpecularF0, nvrhi::AllSubresources, 0u);
    m_commandList->clearTextureUInt(m_renderTargets->PSRLightDir, nvrhi::AllSubresources, 0u);
    m_commandList->endMarker();
}

void SceneRenderer::UpdateRtxdiFrameIndex(uint effectiveFrameIndex)
{
    m_isContext->GetReSTIRDIContext().SetFrameIndex(effectiveFrameIndex);
    m_isContext->GetReSTIRGIContext().SetFrameIndex(effectiveFrameIndex);
    m_isContext->GetReSTIRPTContext().SetFrameIndex(effectiveFrameIndex);
}

void SceneRenderer::PrepareLights()
{
    ProfilerScope scope(*m_profiler, m_commandList, ProfilerSection::MeshProcessing);
    auto& restirDIContext = m_isContext->GetReSTIRDIContext();
    RTXDI_LightBufferParameters lightBufferParams = m_prepareLightsPass->Process(
        m_commandList,
        restirDIContext,
        m_scene->GetSceneGraph()->GetLights(),
        m_environmentMapPdfMipmapPass != nullptr && m_ui.environmentMapImportanceSampling);
    m_isContext->SetLightBufferParams(lightBufferParams);

    auto initialSamplingParams = restirDIContext.GetInitialSamplingParameters();
    initialSamplingParams.environmentMapImportanceSampling = lightBufferParams.environmentLightParams.lightPresent;
    m_ui.restirDI.initialSamplingParams.environmentMapImportanceSampling = initialSamplingParams.environmentMapImportanceSampling;
    restirDIContext.SetInitialSamplingParameters(initialSamplingParams);
}

void SceneRenderer::ProcessLocalLightPdfMipMap()
{
    if (IsLocalLightPowerRISEnabled())
    {
        ProfilerScope scope(*m_profiler, m_commandList, ProfilerSection::LocalLightPdfMap);

        m_localLightPdfMipmapPass->Process(m_commandList);
    }
}

void SceneRenderer::ApplyNrdCheckerboardSettings()
{
#if WITH_NRD
    auto& restirDIContext = m_isContext->GetReSTIRDIContext();
    if (restirDIContext.GetStaticParameters().CheckerboardSamplingMode != rtxdi::CheckerboardMode::Off)
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
}

LightingPasses::RenderSettings SceneRenderer::GetFullyProcessedLightingSettings(uint32_t denoiserMode, bool enableDirectReStirPass)
{
    LightingPasses::RenderSettings lightingSettings = m_ui.lightingSettings;
    lightingSettings.enablePrevTLAS &= m_ui.enableAnimations;
#if WITH_NRD
    lightingSettings.reblurHitDistanceParams = &m_ui.reblurSettings.hitDistanceParameters;
    lightingSettings.denoiserMode = denoiserMode;
#else
    lightingSettings.denoiserMode = DENOISER_MODE_OFF;
#endif
    if (lightingSettings.denoiserMode == DENOISER_MODE_OFF)
        lightingSettings.enableGradients = false;

    if (!enableDirectReStirPass)
    {
        // Secondary resampling can only be done as a post-process of ReSTIR direct lighting
        lightingSettings.brdfptParams.enableSecondaryResampling = false;

        // Gradients are only produced by the direct ReSTIR pass
        lightingSettings.enableGradients = false;
    }

    return lightingSettings;
}

void SceneRenderer::PresampleLights(bool enableDirectReStirPass, bool enableIndirect, const LightingPasses::RenderSettings& lightingSettings)
{
    if (enableDirectReStirPass || enableIndirect)
    {
        m_lightingPasses->PrepareForLightSampling(m_commandList,
            *m_isContext,
            m_view, m_viewPrevious,
            lightingSettings,
            /* enableAccumulation = */ m_ui.aaMode == AntiAliasingMode::Accumulation);
    }
}

void SceneRenderer::RenderDirectLighting(bool enableDirectReStirPass, bool checkerboard, const LightingPasses::RenderSettings& lightingSettings)
{
    if (enableDirectReStirPass)
    {
        m_commandList->clearTextureFloat(m_renderTargets->Gradients, nvrhi::AllSubresources, nvrhi::Color(0.f));

        m_lightingPasses->RenderDirectLighting(m_commandList,
            m_isContext->GetReSTIRDIContext(),
            m_view,
            lightingSettings);

        // Post-process the gradients into a confidence buffer usable by NRD
        if (lightingSettings.enableGradients)
        {
            m_filterGradientsPass->Render(m_commandList, m_view, checkerboard);
            m_confidencePass->Render(m_commandList, m_view, lightingSettings.gradientLogDarknessBias, lightingSettings.gradientSensitivity, lightingSettings.confidenceHistoryLength, checkerboard);
        }
    }
}

void SceneRenderer::RenderIndirectLighting(bool enableBrdfAndIndirectPass, bool enableDirectReStirPass, const LightingPasses::RenderSettings& lightingSettings)
{
    if (enableBrdfAndIndirectPass)
    {
        auto restirDIShadingParams = m_isContext->GetReSTIRDIContext().GetShadingParameters();
        restirDIShadingParams.enableDenoiserInputPacking = true;
        m_isContext->GetReSTIRDIContext().SetShadingParameters(restirDIShadingParams);

        m_lightingPasses->RenderIndirectLighting(
            m_commandList,
            *m_isContext,
            m_view, m_viewPrevious, m_viewPreviousPrevious,
            lightingSettings,
            m_ui.gbufferSettings,
            *m_environmentLight,
            m_ui.indirectLightingMode, ///* enableIndirect = */ enableIndirect,
            /* enableAdditiveBlend = */ enableDirectReStirPass,
            /* enableEmissiveSurfaces = */ m_ui.lightingSettings.directLightingMode == DirectLightingMode::Brdf,
            /* enableAccumulation = */ m_ui.aaMode == AntiAliasingMode::Accumulation
        );
    }
}

void SceneRenderer::HandleNoLightingCase(bool enableDirectReStirPass, bool enableBrdfAndIndirectPass)
{
    // If none of the passes above were executed, clear the textures to avoid stale data there.
// It's a weird mode but it can be selected from the UI.
    if (!enableDirectReStirPass && !enableBrdfAndIndirectPass)
    {
        m_commandList->clearTextureFloat(m_renderTargets->DiffuseLighting, nvrhi::AllSubresources, nvrhi::Color(0.f));
        m_commandList->clearTextureFloat(m_renderTargets->SpecularLighting, nvrhi::AllSubresources, nvrhi::Color(0.f));
    }
}

void SceneRenderer::Denoiser(const LightingPasses::RenderSettings& lightingSettings)
{
#if WITH_NRD
        if (m_ui.denoiserMode == DenoiserMode::NRD_RELAX || m_ui.denoiserMode == DenoiserMode::NRD_REBLUR)
        {
            ProfilerScope scope(*m_profiler, m_commandList, ProfilerSection::Denoising);
            m_commandList->beginMarker("Denoising - NRD");

            const void* methodSettings = (m_ui.nrdDenoisingMethod == nrd::Denoiser::RELAX_DIFFUSE_SPECULAR)
                ? (void*)&m_ui.relaxSettings
                : (void*)&m_ui.reblurSettings;

            bool psrEnabled = m_ui.lightingSettings.enableDenoiserPSR && m_ui.indirectLightingMode == IndirectLightingMode::ReStirPT;
            m_nrd->RunDenoiserPasses(m_commandList, *m_renderTargets, m_view, m_viewPrevious, GetFrameIndex(), lightingSettings.enableGradients, methodSettings, m_ui.nrdDebugSettings, psrEnabled, m_ui.debug);

            m_commandList->endMarker();
        }
#endif
}

void SceneRenderer::TransparentGeometry()
{
    if (m_ui.gbufferSettings.enableTransparentGeometry)
    {
        ProfilerScope scope(*m_profiler, m_commandList, ProfilerSection::Glass);

        m_glassPass->Render(m_commandList, m_view,
            *m_environmentLight,
            m_ui.gbufferSettings.normalMapScale,
            m_ui.gbufferSettings.enableMaterialReadback,
            m_ui.gbufferSettings.materialReadbackPosition,
            m_ui.indirectLightingMode);
    }
}

donut::render::DLSS::EvaluateParameters SceneRenderer::GetDLSSSRParams() const
{
    donut::render::DLSS::EvaluateParameters params;
    params.depthTexture = m_ui.rasterizeGBuffer ? m_renderTargets->DeviceDepth : m_renderTargets->DeviceDepthUAV;
    params.motionVectorsTexture = m_renderTargets->PSRMotionVectors;
    params.inputColorTexture = m_renderTargets->HdrColor;
    params.outputColorTexture = m_renderTargets->ResolvedColor;
    params.exposureBuffer = m_toneMappingPass->GetExposureBuffer();
    params.exposureScale = m_ui.dlssExposureScale;
    params.sharpness = m_ui.dlssSharpness;
    params.resetHistory = m_ui.resetAccumulation;
    return params;
}

donut::render::DLSS::EvaluateParameters SceneRenderer::GetDLSSRRParams() const
{
    donut::render::DLSS::EvaluateParameters params = GetDLSSSRParams();
    params.diffuseAlbedo = m_renderTargets->PSRDiffuseAlbedo_RR;
    params.normalRoughness = m_renderTargets->PSRNormalRoughness;
    params.specularAlbedo = m_renderTargets->PSRSpecularF0_RR;
    return params;
}

void SceneRenderer::ResolveAA(nvrhi::ICommandList* commandList, float accumulationWeight) const
{
    ProfilerScope scope(*m_profiler, commandList, ProfilerSection::Resolve);

#if DONUT_WITH_DLSS
    // DLSS-RR is a denoiser mode that also performs the post-processing resolve (denoise + upscale),
    // so when it's selected it takes over here regardless of the AA mode.
    if (m_ui.denoiserMode == DenoiserMode::DLSS_RR)
    {
        donut::render::DLSS::EvaluateParameters params = GetDLSSRRParams();
        m_dlssRR->Evaluate(commandList, params, m_view);
        return;
    }
#endif

    auto aaMode = m_ui.aaMode;

    switch (aaMode)
    {
    case AntiAliasingMode::None: {
        engine::BlitParameters blitParams;
        blitParams.sourceTexture = m_renderTargets->HdrColor;
        blitParams.sourceBox.m_maxs.x = m_view.GetViewport().width() / m_upscaledView.GetViewport().width();
        blitParams.sourceBox.m_maxs.y = m_view.GetViewport().height() / m_upscaledView.GetViewport().height();
        blitParams.targetFramebuffer = m_renderTargets->ResolvedFramebuffer->GetFramebuffer(m_upscaledView);
        m_CommonPasses->BlitTexture(commandList, blitParams);
        break;
    }

    case AntiAliasingMode::Accumulation: {
        m_accumulationPass->Render(commandList, m_view, m_upscaledView, accumulationWeight);
        m_CommonPasses->BlitTexture(commandList, m_renderTargets->ResolvedFramebuffer->GetFramebuffer(m_upscaledView), m_renderTargets->AccumulatedColor);
        break;
    }

    case AntiAliasingMode::TAA: {
        auto taaParams = m_ui.taaParams;
        if (m_ui.resetAccumulation)
            taaParams.newFrameWeight = 1.f;

        m_temporalAntiAliasingPass->TemporalResolve(commandList, taaParams, m_prevViewValid, m_view, m_upscaledView);
        break;
    }

#if DONUT_WITH_DLSS
    case AntiAliasingMode::DLSS_SR: {
        donut::render::DLSS::EvaluateParameters params = GetDLSSSRParams();
        m_dlssSR->Evaluate(commandList, params, m_view);
        break;
    }
#endif
    }
}

void SceneRenderer::Bloom()
{
    if (m_ui.enableBloom)
    {
#if DONUT_WITH_DLSS
        // Use the unresolved image for bloom when DLSS is active because DLSS can modify HDR values significantly and add bloom flicker.
        const bool useUnresolvedForBloom = (m_ui.aaMode == AntiAliasingMode::DLSS_SR)
            && (m_ui.resolutionScale == 1.f)
            && (m_ui.denoiserMode != DenoiserMode::DLSS_RR);
        nvrhi::ITexture* bloomSource = useUnresolvedForBloom
            ? m_renderTargets->HdrColor
            : m_renderTargets->ResolvedColor;
#else
        nvrhi::ITexture* bloomSource = m_renderTargets->ResolvedColor;
#endif

        m_bloomPass->Render(m_commandList, m_renderTargets->ResolvedFramebuffer, m_upscaledView, bloomSource, 32.f, 0.005f);
    }
}

void SceneRenderer::ReferenceImage(bool cameraIsStatic)
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
        m_commandList->copyTexture(m_renderTargets->ReferenceColor, nvrhi::TextureSlice(), m_renderTargets->ResolvedColor, nvrhi::TextureSlice());
        m_ui.storeReferenceImage = false;
        m_ui.referenceImageCaptured = true;
    }

    // When the "Split Display" parameter is nonzero, show a portion of the previously stored
    // ReferenceColor texture on the left side of the screen by copying it into the ResolvedColor texture.
    if (m_ui.referenceImageSplit > 0.f)
    {
        engine::BlitParameters blitParams;
        blitParams.sourceTexture = m_renderTargets->ReferenceColor;
        blitParams.sourceBox.m_maxs = float2(m_ui.referenceImageSplit, 1.f);
        blitParams.targetFramebuffer = m_renderTargets->ResolvedFramebuffer->GetFramebuffer(nvrhi::AllSubresources);
        blitParams.targetBox = blitParams.sourceBox;
        blitParams.sampler = engine::BlitSampler::Point;
        m_CommonPasses->BlitTexture(m_commandList, blitParams, &m_bindingCache);
    }
}

void SceneRenderer::ToneMapping(bool exposureResetRequired)
{
    if (m_ui.enableToneMapping)
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

        m_toneMappingPass->SimpleRender(m_commandList, ToneMappingParams, m_upscaledView, m_renderTargets->ResolvedColor);
    }
    else
    {
        m_CommonPasses->BlitTexture(m_commandList, m_renderTargets->LdrFramebuffer->GetFramebuffer(m_upscaledView), m_renderTargets->ResolvedColor, &m_bindingCache);
    }
}

void SceneRenderer::DebugPathViz()
{
    if (m_ui.debugOutputSettings.ptPathViz.enabled)
    {
        m_ptPathVizPass->EnableCameraToPrimarySurfaceRay(m_ui.debugOutputSettings.ptPathViz.cameraVertexEnabled);
        m_ptPathVizPass->EnableDepthTest(m_ui.debugOutputSettings.ptPathViz.enableDepth);
        m_ptPathVizPass->SetPathsData(m_ui.debugOutputSettings.ptPathViz.paths);
        m_shouldUpdatePtViz = m_ui.debugOutputSettings.ptPathViz.updateOnClickOrAlways ? m_shouldUpdatePtViz : true;
        if (m_shouldUpdatePtViz)
        {
            m_ptPathVizPass->Update(m_commandList);
            if (m_ui.debugOutputSettings.ptPathViz.updateOnClickOrAlways)
            {
                m_shouldUpdatePtViz = false;
            }
        }
        m_ptPathVizPass->Render(m_commandList, m_view, *m_renderTargets);
    }
}

void SceneRenderer::DebugVisualizationOverlayPass(nvrhi::IFramebuffer* framebuffer)
{
    if (m_ui.debugOutputSettings.visualizationOverlayMode != VisualizationOverlayMode::VISUALIZATION_OVERLAY_MODE_NONE)
    {
        bool haveSignal = true;
        uint32_t inputBufferIndex = 0;
        switch (m_ui.debugOutputSettings.visualizationOverlayMode)
        {
        case VisualizationOverlayMode::VISUALIZATION_OVERLAY_MODE_DENOISED_DIFFUSE:
        case VisualizationOverlayMode::VISUALIZATION_OVERLAY_MODE_DENOISED_SPECULAR:
            // These come from NRD; DLSS-RR does not produce them.
            haveSignal = (m_ui.denoiserMode == DenoiserMode::NRD_RELAX || m_ui.denoiserMode == DenoiserMode::NRD_REBLUR);
            break;

        case VisualizationOverlayMode::VISUALIZATION_OVERLAY_MODE_DIFFUSE_CONFIDENCE:
        case VisualizationOverlayMode::VISUALIZATION_OVERLAY_MODE_SPECULAR_CONFIDENCE:
            haveSignal = m_ui.lightingSettings.enableGradients && (m_ui.denoiserMode == DenoiserMode::NRD_RELAX || m_ui.denoiserMode == DenoiserMode::NRD_REBLUR);
            break;

        case VisualizationOverlayMode::VISUALIZATION_OVERLAY_MODE_RESERVOIR_WEIGHT:
        case VisualizationOverlayMode::VISUALIZATION_OVERLAY_MODE_RESERVOIR_M:
            inputBufferIndex = m_lightingPasses->GetOutputReservoirBufferIndex();
            haveSignal = m_ui.lightingSettings.directLightingMode == DirectLightingMode::ReStir;
            break;

        case VisualizationOverlayMode::VISUALIZATION_OVERLAY_MODE_GI_WEIGHT:
        case VisualizationOverlayMode::VISUALIZATION_OVERLAY_MODE_GI_M:
            inputBufferIndex = m_lightingPasses->GetGIOutputReservoirBufferIndex();
            haveSignal = m_ui.indirectLightingMode == IndirectLightingMode::ReStirGI;
            break;
        }

        if (haveSignal)
        {
            m_visualizationPass->Render(
                m_commandList,
                m_renderTargets->LdrFramebuffer->GetFramebuffer(m_upscaledView),
                m_view,
                m_upscaledView,
                *m_isContext,
                inputBufferIndex,
                m_ui.debugOutputSettings.visualizationOverlayMode,
                m_ui.aaMode == AntiAliasingMode::Accumulation);
        }
    }
    m_CommonPasses->BlitTexture(m_commandList, framebuffer, m_renderTargets->LdrColor, &m_bindingCache);
}

void SceneRenderer::DebugReservoirSubfieldVizPass(nvrhi::IFramebuffer* framebuffer)
{
    switch (m_ui.debugOutputSettings.reservoirSubfieldVizMode)
    {
    case ReservoirSubfieldVizMode::DIReservoir:
    {
        auto& diContext = m_isContext->GetReSTIRDIContext();
        m_diReservoirVizPass->Render(m_commandList, m_view, m_isContext->GetReSTIRDIContext().GetRuntimeParams(), diContext.GetReservoirBufferParameters(), diContext.GetBufferIndices(), m_ui.debugOutputSettings.diReservoirVizField, m_prepareLightsPass->GetMaxLightsInBuffer());
    }
    break;
    case ReservoirSubfieldVizMode::GIReservoir:
    {
        auto& giContext = m_isContext->GetReSTIRGIContext();
        m_giReservoirVizPass->Render(m_commandList, m_view, m_isContext->GetReSTIRDIContext().GetRuntimeParams(), giContext.GetReservoirBufferParameters(), giContext.GetBufferIndices(), m_ui.debugOutputSettings.giReservoirVizField);
    }
    break;
    case ReservoirSubfieldVizMode::PTReservoir:
    {
        auto& ptContext = m_isContext->GetReSTIRPTContext();
        m_ptReservoirVizPass->Render(m_commandList, m_view, m_isContext->GetReSTIRDIContext().GetRuntimeParams(), ptContext.GetReservoirBufferParameters(), ptContext.GetBufferIndices(), m_ui.debugOutputSettings.ptReservoirVizField);
    }
    break;
    }
    m_CommonPasses->BlitTexture(m_commandList, framebuffer, m_renderTargets->DebugColor, &m_bindingCache);
}

void SceneRenderer::DebugTextureBlit(nvrhi::IFramebuffer* framebuffer)
{
    switch (m_ui.debugOutputSettings.textureBlitMode)
    {
    case DebugTextureBlitMode::LDRColor:
        m_CommonPasses->BlitTexture(m_commandList, framebuffer, m_renderTargets->LdrColor, &m_bindingCache);
        break;
    case DebugTextureBlitMode::Depth:
        m_CommonPasses->BlitTexture(m_commandList, framebuffer, m_renderTargets->Depth, &m_bindingCache);
        break;
    case DebugTextureBlitMode::PSRDepth:
        m_CommonPasses->BlitTexture(m_commandList, framebuffer, m_renderTargets->PSRDepth, &m_bindingCache);
        break;
    case DebugTextureBlitMode::GBufferDiffuseAlbedo:
        m_debugVizPasses->RenderUnpackedDiffuseAlbeo(m_commandList, m_upscaledView);
        m_CommonPasses->BlitTexture(m_commandList, framebuffer, m_renderTargets->DebugColor, &m_bindingCache);
        break;
    case DebugTextureBlitMode::GBufferSpecularRough:
        m_debugVizPasses->RenderUnpackedSpecularRoughness(m_commandList, m_upscaledView);
        m_CommonPasses->BlitTexture(m_commandList, framebuffer, m_renderTargets->DebugColor, &m_bindingCache);
        break;
    case DebugTextureBlitMode::GBufferNormals:
        m_debugVizPasses->RenderUnpackedNormals(m_commandList, m_upscaledView);
        m_CommonPasses->BlitTexture(m_commandList, framebuffer, m_renderTargets->DebugColor, &m_bindingCache);
        break;
    case DebugTextureBlitMode::GBufferGeoNormals:
        m_debugVizPasses->RenderUnpackedGeoNormals(m_commandList, m_upscaledView);
        m_CommonPasses->BlitTexture(m_commandList, framebuffer, m_renderTargets->DebugColor, &m_bindingCache);
        break;
    case DebugTextureBlitMode::GBufferEmissive:
        m_CommonPasses->BlitTexture(m_commandList, framebuffer, m_renderTargets->GBufferEmissive, &m_bindingCache);
        break;
    case DebugTextureBlitMode::DiffuseLighting:
        m_CommonPasses->BlitTexture(m_commandList, framebuffer, m_renderTargets->DiffuseLighting, &m_bindingCache);
        break;
    case DebugTextureBlitMode::SpecularLighting:
        m_CommonPasses->BlitTexture(m_commandList, framebuffer, m_renderTargets->SpecularLighting, &m_bindingCache);
        break;
    case DebugTextureBlitMode::DenoisedDiffuseLighting:
        m_CommonPasses->BlitTexture(m_commandList, framebuffer, m_renderTargets->DenoisedDiffuseLighting, &m_bindingCache);
        break;
    case DebugTextureBlitMode::DenoisedSpecularLighting:
        m_CommonPasses->BlitTexture(m_commandList, framebuffer, m_renderTargets->DenoisedSpecularLighting, &m_bindingCache);
        break;
    case DebugTextureBlitMode::RestirLuminance:
        m_CommonPasses->BlitTexture(m_commandList, framebuffer, m_renderTargets->RestirLuminance, &m_bindingCache);
        break;
    case DebugTextureBlitMode::PrevRestirLuminance:
        m_CommonPasses->BlitTexture(m_commandList, framebuffer, m_renderTargets->PrevRestirLuminance, &m_bindingCache);
        break;
    case DebugTextureBlitMode::DiffuseConfidence:
        m_CommonPasses->BlitTexture(m_commandList, framebuffer, m_renderTargets->DiffuseConfidence, &m_bindingCache);
        break;
    case DebugTextureBlitMode::SpecularConfidence:
        m_CommonPasses->BlitTexture(m_commandList, framebuffer, m_renderTargets->SpecularConfidence, &m_bindingCache);
        break;
    case DebugTextureBlitMode::MotionVectors:
        m_CommonPasses->BlitTexture(m_commandList, framebuffer, m_renderTargets->MotionVectors, &m_bindingCache);
        break;
    case DebugTextureBlitMode::PSRMotionVectors:
        m_CommonPasses->BlitTexture(m_commandList, framebuffer, m_renderTargets->PSRMotionVectors, &m_bindingCache);
        break;
    case DebugTextureBlitMode::DirectLightingRaw:
        m_CommonPasses->BlitTexture(m_commandList, framebuffer, m_renderTargets->DirectLightingRaw, &m_bindingCache);
        break;
    case DebugTextureBlitMode::IndirectLightingRaw:
        m_CommonPasses->BlitTexture(m_commandList, framebuffer, m_renderTargets->IndirectLightingRaw, &m_bindingCache);
        break;
    case DebugTextureBlitMode::PSRNormalRoughness:
        m_CommonPasses->BlitTexture(m_commandList, framebuffer, m_renderTargets->PSRNormalRoughness, &m_bindingCache);
        break;
    case DebugTextureBlitMode::PSRHitT:
        m_CommonPasses->BlitTexture(m_commandList, framebuffer, m_renderTargets->PSRHitT, &m_bindingCache);
        break;
    case DebugTextureBlitMode::PSRDiffuseAlbedo:
        m_debugVizPasses->RenderUnpackedPSRDiffuseAlbedo(m_commandList, m_upscaledView);
        m_CommonPasses->BlitTexture(m_commandList, framebuffer, m_renderTargets->DebugColor, &m_bindingCache);
        break;
    case DebugTextureBlitMode::PSRSpecularF0:
        m_debugVizPasses->RenderUnpackedPSRSpecularF0(m_commandList, m_upscaledView);
        m_CommonPasses->BlitTexture(m_commandList, framebuffer, m_renderTargets->DebugColor, &m_bindingCache);
        break;
    case DebugTextureBlitMode::PTDuplicationMap:
        m_commandList->setTextureState(m_renderTargets->PTDuplicationMap, nvrhi::AllSubresources, nvrhi::ResourceStates::ShaderResource);
        m_debugVizPasses->RenderPTDuplicationMap(m_commandList, m_view);
        m_CommonPasses->BlitTexture(m_commandList, framebuffer, m_renderTargets->DebugColor, &m_bindingCache);
        break;
    case DebugTextureBlitMode::SmoothedPTDuplicationMap:
        m_commandList->setTextureState(m_renderTargets->SmoothedPTDuplicationMap, nvrhi::AllSubresources, nvrhi::ResourceStates::ShaderResource);
        m_debugVizPasses->RenderSmoothedPTDuplicationMap(m_commandList, m_view);
        m_CommonPasses->BlitTexture(m_commandList, framebuffer, m_renderTargets->DebugColor, &m_bindingCache);
        break;
    case DebugTextureBlitMode::PTDecorrelationFactor:
        m_commandList->setTextureState(m_renderTargets->PTDecorrelationFactor, nvrhi::AllSubresources, nvrhi::ResourceStates::ShaderResource);
        m_debugVizPasses->RenderPTDecorrelationFactor(m_commandList, m_view);
        m_CommonPasses->BlitTexture(m_commandList, framebuffer, m_renderTargets->DebugColor, &m_bindingCache);
        break;
    case DebugTextureBlitMode::PTSampleID:
        m_commandList->setTextureState(m_renderTargets->PTSampleIDTexture, nvrhi::AllSubresources, nvrhi::ResourceStates::ShaderResource);
        m_debugVizPasses->RenderPTSampleID(m_commandList, m_view);
        m_CommonPasses->BlitTexture(m_commandList, framebuffer, m_renderTargets->DebugColor, &m_bindingCache);
        break;
    case DebugTextureBlitMode::NeighborSelectionGBuffer:
        m_CommonPasses->BlitTexture(m_commandList, framebuffer, m_renderTargets->NeighborSelectionGBuffer, &m_bindingCache);
        break;
    }
}

void SceneRenderer::DebugOutputPass(nvrhi::IFramebuffer* framebuffer)
{
    switch (m_ui.debugOutputSettings.renderOutputMode)
    {
    default:
    case DebugRenderOutputMode::Off:
        m_CommonPasses->BlitTexture(m_commandList, framebuffer, m_renderTargets->LdrColor, &m_bindingCache);
        break;

    case DebugRenderOutputMode::VisualizationOverlay:
        DebugVisualizationOverlayPass(framebuffer);
        break;

    case DebugRenderOutputMode::TextureBlit:
        DebugTextureBlit(framebuffer);
        break;

    case DebugRenderOutputMode::ReservoirSubfield:
        DebugReservoirSubfieldVizPass(framebuffer);
        break;
    case DebugRenderOutputMode::NrdValidation:
        if(m_ui.nrdDebugSettings.overlayOnFinalImage)
        {
            m_CommonPasses->BlitTexture(m_commandList, framebuffer, m_renderTargets->LdrColor, &m_bindingCache);
            donut::engine::BlitParameters blitParams = {};
            blitParams.targetFramebuffer = framebuffer;
            blitParams.targetViewport = m_view.GetViewportState().viewports[0];// viewportState.viewports[0];
            blitParams.sourceTexture = m_renderTargets->NrdValidation;
            blitParams.blendState.setBlendEnable(true)
                .setSrcBlend(nvrhi::BlendFactor::SrcAlpha)
                .setDestBlend(nvrhi::BlendFactor::InvSrcAlpha)
                .setSrcBlendAlpha(nvrhi::BlendFactor::Zero)
                .setDestBlendAlpha(nvrhi::BlendFactor::One);
            m_CommonPasses->BlitTexture(m_commandList, blitParams, &m_bindingCache);
        }
        else
        {
            m_CommonPasses->BlitTexture(m_commandList, framebuffer, m_renderTargets->NrdValidation, &m_bindingCache);
        }
        break;
    }
}

void SceneRenderer::FinalFramebufferOutput(nvrhi::IFramebuffer* framebuffer)
{
    if (m_ui.debugOutputSettings.renderOutputMode == DebugRenderOutputMode::Off)
    {
        m_CommonPasses->BlitTexture(m_commandList, framebuffer, m_renderTargets->LdrColor, &m_bindingCache);
    }
    else
    {
        DebugOutputPass(framebuffer);
    }
}

void SceneRenderer::ProcessScreenshotFrame()
{
    if (!m_args.saveFrameFileName.empty() && m_renderFrameIndex == m_args.saveFrameIndex)
    {
        bool success = SaveScreenshot(GetDevice(), m_renderTargets->LdrColor, m_args.saveFrameFileName.c_str());

        //g_ExitCode = success ? 0 : 1;

        glfwSetWindowShouldClose(GetDeviceManager()->GetWindow(), 1);
    }
}

void SceneRenderer::OutputShaderMessages()
{
    std::vector<std::string> lines = m_shaderDebugPrintPass->GetLines();
    if (lines.size() < 1)
        return;
    donut::log::debug("----------------------------------------------------------------");
    std::string header = "Shader debug print frame: " + std::to_string(m_renderFrameIndex);
    donut::log::debug(header.c_str());
    for (int i = 0; i < lines.size(); i++)
    {
        donut::log::debug(lines[i].c_str());
    }
}
