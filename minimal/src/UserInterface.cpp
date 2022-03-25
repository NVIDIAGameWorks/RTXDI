/***************************************************************************
 # Copyright (c) 2021-2022, NVIDIA CORPORATION.  All rights reserved.
 #
 # NVIDIA CORPORATION and its licensors retain all intellectual property
 # and proprietary rights in and to this software, related documentation
 # and any modifications thereto.  Any use, reproduction, disclosure or
 # distribution of this software and related documentation without an express
 # license agreement from NVIDIA CORPORATION is strictly prohibited.
 **************************************************************************/

 /*
 License for Dear ImGui

 Copyright (c) 2014-2019 Omar Cornut

 Permission is hereby granted, free of charge, to any person obtaining a copy
 of this software and associated documentation files (the "Software"), to deal
 in the Software without restriction, including without limitation the rights
 to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 copies of the Software, and to permit persons to whom the Software is
 furnished to do so, subject to the following conditions:

 The above copyright notice and this permission notice shall be included in all
 copies or substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 SOFTWARE.
 */

#include "UserInterface.h"
#include "SampleScene.h"

using namespace donut;

UserInterface::UserInterface(app::DeviceManager* deviceManager, vfs::IFileSystem& rootFS, UIData& ui)
    : ImGui_Renderer(deviceManager)
    , m_ui(ui)
{
    m_FontOpenSans = LoadFont(rootFS, "/media/fonts/OpenSans/OpenSans-Regular.ttf", 17.f);
}

void UserInterface::buildUI()
{
    if (!m_ui.showUI)
        return;
    
    int width, height;
    GetDeviceManager()->GetWindowDimensions(width, height);

    if (m_ui.isLoading)
    {
        BeginFullScreenWindow();
        DrawScreenCenteredText("Loading the scene, please wait...");
        EndFullScreenWindow();

        return;
    }

    ImGui::SetNextWindowPos(ImVec2(10.f, 10.f), 0, ImVec2(0.f, 0.f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(375.f, 0.f), ImVec2(float(width) - 20.f, float(height) - 20.f));
    if (ImGui::Begin("Settings (Tilde key to hide)", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::PushItemWidth(100.f);

        if (ImGui::Button("Reload Shaders (Ctrl+R)"))
        {
            m_ui.reloadShaders = true;
        }
        
        ImGui::Separator();

        ImGui::Checkbox("Enable Resampling", &m_ui.lightingSettings.enableResampling);
        ImGui::Checkbox("Unbiased Mode", &m_ui.lightingSettings.unbiasedMode);

        ImGui::SliderInt("Initial Samples", (int*)&m_ui.lightingSettings.numInitialSamples, 1, 32);
        ImGui::SliderInt("Spatial Samples", (int*)&m_ui.lightingSettings.numSpatialSamples, 0, 4);
        ImGui::SliderInt("Initial BRDF Samples", (int*)&m_ui.lightingSettings.numInitialBRDFSamples, 0, 8);
        ImGui::SliderFloat("BRDF Cutoff", (float*)&m_ui.lightingSettings.brdfCutoff, 0.0f, 1.0f);

        ImGui::Separator();

        double frameTime = GetDeviceManager()->GetAverageFrameTimeSeconds();
        ImGui::Text("%05.2f ms/frame (%05.1f FPS)", frameTime * 1e3f, (frameTime > 0.0) ? 1.0 / frameTime : 0.0);
        
        ImGui::PopItemWidth();
    }
    ImGui::End();
}
