/***************************************************************************
 # Copyright (c) 2021-2022, NVIDIA CORPORATION.  All rights reserved.
 #
 # NVIDIA CORPORATION and its licensors retain all intellectual property
 # and proprietary rights in and to this software, related documentation
 # and any modifications thereto.  Any use, reproduction, disclosure or
 # distribution of this software and related documentation without an express
 # license agreement from NVIDIA CORPORATION is strictly prohibited.
 **************************************************************************/

#pragma once

#include <donut/engine/Scene.h>
#include <donut/app/imgui_renderer.h>
#include "RenderPass.h"


struct UIData
{
    bool reloadShaders = false;
    bool showUI = true;
    bool isLoading = true;
    
    RenderPass::Settings lightingSettings;
};


class UserInterface : public donut::app::ImGui_Renderer
{
private:
    UIData& m_ui;
    ImFont* m_FontOpenSans = nullptr;

protected:
    void buildUI() override;

public:
    UserInterface(donut::app::DeviceManager* deviceManager, donut::vfs::IFileSystem& rootFS, UIData& ui);
};
