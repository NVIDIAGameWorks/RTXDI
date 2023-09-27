/***************************************************************************
 # Copyright (c) 2021-2023, NVIDIA CORPORATION.  All rights reserved.
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

constexpr int LightType_Environment = 1000;
constexpr int LightType_Cylinder = 1001;
constexpr int LightType_Disk = 1002;
constexpr int LightType_Rect = 1003;

class SpotLightWithProfile : public donut::engine::SpotLight
{
public:
    std::string profileName;
    int profileTextureIndex = -1;

    void Load(const Json::Value& node) override;
    void Store(Json::Value& node) const override;
    [[nodiscard]] std::shared_ptr<SceneGraphLeaf> Clone() override;
};

class EnvironmentLight : public donut::engine::Light
{
public:
    dm::float3 radianceScale = 1.f;
    int textureIndex = -1;
    float rotation = 0.f;
    dm::uint2 textureSize = 0u;

    [[nodiscard]] int GetLightType() const override { return LightType_Environment; }
    [[nodiscard]] std::shared_ptr<SceneGraphLeaf> Clone() override;
};

class CylinderLight : public donut::engine::Light
{
public:
    float length = 1.f;
    float radius = 1.f;
    float flux = 1.f;

    void Load(const Json::Value& node) override;
    void Store(Json::Value& node) const override;
    [[nodiscard]] int GetLightType() const override { return LightType_Cylinder; }
    [[nodiscard]] std::shared_ptr<SceneGraphLeaf> Clone() override;
};

class DiskLight : public donut::engine::Light
{
public:
    float radius = 1.f;
    float flux = 1.f;

    void Load(const Json::Value& node) override;
    void Store(Json::Value& node) const override;
    [[nodiscard]] int GetLightType() const override { return LightType_Disk; }
    [[nodiscard]] std::shared_ptr<SceneGraphLeaf> Clone() override;
};

class RectLight : public donut::engine::Light
{
public:
    float width = 1.f;
    float height = 1.f;
    float flux = 1.f;

    void Load(const Json::Value& node) override;
    void Store(Json::Value& node) const override;
    [[nodiscard]] int GetLightType() const override { return LightType_Rect; }
    [[nodiscard]] std::shared_ptr<SceneGraphLeaf> Clone() override;
};

class SampleMesh : public donut::engine::MeshInfo
{
public:
    using MeshInfo::MeshInfo;

    nvrhi::rt::AccelStructHandle prevAccelStruct;
};

class SampleSceneTypeFactory : public donut::engine::SceneTypeFactory
{
public:
    std::shared_ptr<donut::engine::SceneGraphLeaf> CreateLeaf(const std::string& type) override;
    std::shared_ptr<donut::engine::MeshInfo> CreateMesh() override;
};

class SampleScene : public donut::engine::Scene
{
private:
    nvrhi::rt::AccelStructHandle m_TopLevelAS;
    nvrhi::rt::AccelStructHandle m_PrevTopLevelAS;
    std::vector<nvrhi::rt::InstanceDesc> m_TlasInstances;
    std::shared_ptr<donut::engine::SceneGraphAnimation> m_BenchmarkAnimation;
    std::shared_ptr<donut::engine::PerspectiveCamera> m_BenchmarkCamera;
    
    bool m_CanUpdateTLAS = false;
    bool m_CanUpdatePrevTLAS = false;

    double m_WallclockTime = 0;

    std::vector<std::string> m_EnvironmentMaps;

public:
    using Scene::Scene;

    bool LoadWithExecutor(const std::filesystem::path& jsonFileName, tf::Executor* executor) override;

    const donut::engine::SceneGraphAnimation* GetBenchmarkAnimation() const { return m_BenchmarkAnimation.get(); }
    const donut::engine::PerspectiveCamera* GetBenchmarkCamera() const { return m_BenchmarkCamera.get(); }
    
    void BuildMeshBLASes(nvrhi::IDevice* device);
    void UpdateSkinnedMeshBLASes(nvrhi::ICommandList* commandList, uint32_t frameIndex);
    void BuildTopLevelAccelStruct(nvrhi::ICommandList* commandList);
    void NextFrame();
    void Animate(float  fElapsedTimeSeconds);

    nvrhi::rt::IAccelStruct* GetTopLevelAS() const { return m_TopLevelAS; }
    nvrhi::rt::IAccelStruct* GetPrevTopLevelAS() const { return m_PrevTopLevelAS; }

    std::vector<std::string>& GetEnvironmentMaps() { return m_EnvironmentMaps; }
};
