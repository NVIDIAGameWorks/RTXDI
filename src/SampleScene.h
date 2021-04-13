/***************************************************************************
 # Copyright (c) 2021, NVIDIA CORPORATION.  All rights reserved.
 #
 # NVIDIA CORPORATION and its licensors retain all intellectual property
 # and proprietary rights in and to this software, related documentation
 # and any modifications thereto.  Any use, reproduction, disclosure or
 # distribution of this software and related documentation without an express
 # license agreement from NVIDIA CORPORATION is strictly prohibited.
 **************************************************************************/

#pragma once

#include <donut/engine/Scene.h>
#include <donut/engine/KeyframeAnimation.h>

namespace donut::engine {
    class BindlessScene;
}

constexpr int LightType_Environment = 1000;
constexpr int LightType_Cylinder = 1001;
constexpr int LightType_Disk = 1002;
constexpr int LightType_Rect = 1003;

class SpotLightWithProfile : public donut::engine::SpotLight
{
public:
    std::string profileName;
    int profileTextureIndex = -1;

    void Load(Json::Value& node) override;
    void Store(Json::Value& node) const override;
};

class EnvironmentLight : public donut::engine::Light
{
public:
    dm::float3 radianceScale = 1.f;
    int textureIndex = -1;
    float rotation = 0.f;

    [[nodiscard]] int GetLightType() const override { return LightType_Environment; }
};

class CylinderLight : public donut::engine::Light
{
public:
    dm::float3 center = 0.f;
    dm::float3 axis = dm::float3(0.f, 0.f, 1.f);
    float length = 1.f;
    float radius = 1.f;
    float flux = 1.f;

    void Load(Json::Value& node) override;
    void Store(Json::Value& node) const override;
    [[nodiscard]] int GetLightType() const override { return LightType_Cylinder; }
};

class DiskLight : public donut::engine::Light
{
public:
    dm::float3 center = 0.f;
    dm::float3 normal = dm::float3(0.f, 0.f, 1.f);
    float radius = 1.f;
    float flux = 1.f;

    void Load(Json::Value& node) override;
    void Store(Json::Value& node) const override;
    [[nodiscard]] int GetLightType() const override { return LightType_Disk; }
};

class RectLight : public donut::engine::Light
{
public:
    dm::float3 center = 0.f;
    dm::float3 normal = dm::float3(0.f, 0.f, 1.f);
    float rotation = 0.f;
    float width = 1.f;
    float height = 1.f;
    float flux = 1.f;

    void Load(Json::Value& node) override;
    void Store(Json::Value& node) const override;
    [[nodiscard]] int GetLightType() const override { return LightType_Rect; }
};

class SampleScene : public donut::engine::Scene
{
private:
    donut::engine::animation::Track<donut::engine::animation::View> m_CameraPath;

    nvrhi::rt::AccelStructHandle m_TopLevelAS;
    nvrhi::rt::AccelStructHandle m_PrevTopLevelAS;
    nvrhi::rt::TopLevelAccelStructDesc m_TlasDesc;

    struct AnimatedMaterial
    {
        donut::engine::Material* material = nullptr;
        dm::float3 radiance{};
    };

    std::vector<AnimatedMaterial> m_AnimatedMaterials;
    std::vector<donut::engine::MeshInstance*> m_RotatingFans;
    std::vector<donut::engine::MeshInstance*> m_RotatingSphereParts;
    int m_WallclockTimeMs = 0;

    std::vector<std::string> m_EnvironmentMaps;

protected:
    std::shared_ptr<donut::engine::Light> CreateLight(const std::string& type) override;
    void LoadCustomData(Json::Value& rootNode, donut::engine::TextureCache& textureCache, concurrency::task_group& taskGroup, bool& success) override;

public:
    using Scene::Scene;

    bool Load(const std::filesystem::path& jsonFileName, donut::engine::TextureCache& textureCache) override;

    [[nodiscard]] float GetCameraPathDuration() const;
    bool InterpolateCameraPath(float time, dm::float3& position, dm::float3& direction);

    void BuildMeshBLASes(nvrhi::IDevice* device);
    void BuildTopLevelAccelStruct(nvrhi::ICommandList* commandList);
    void NextFrame();
    void Animate(float  fElapsedTimeSeconds, bool animateLights, bool animateMeshes, donut::engine::BindlessScene& bindlessScene);

    nvrhi::rt::IAccelStruct* GetTopLevelAS() const { return m_TopLevelAS; }
    nvrhi::rt::IAccelStruct* GetPrevTopLevelAS() const { return m_PrevTopLevelAS; }

    std::vector<std::string>& GetEnvironmentMaps() { return m_EnvironmentMaps; }
};
