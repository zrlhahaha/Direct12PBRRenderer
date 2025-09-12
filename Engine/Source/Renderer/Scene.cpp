#include "Renderer/Scene.h"
#include "Renderer/Device/Direct12/D3D12Device.h"
#include "Resource//ResourceLoader.h"
#include "Renderer/Camera.h"

namespace MRenderer{
    SceneObject::SceneObject()
        :mScale(1.0, 1.0, 1.0)
    {
        mConstantBuffer = GD3D12ResourceAllocator->CreateConstBuffer(sizeof(ConstantBufferInstance));
    }

    SceneObject::SceneObject(std::string_view name)
        :SceneObject()
    {
        mName = name;
    }

    SceneObject::SceneObject(SceneObject&& other)
        : SceneObject()
    {
        swap(*this, other);
    }

    SceneObject& SceneObject::operator=(SceneObject other)
    {
        swap(*this, other);
        return *this;
    }

    void SceneObject::PostDeserialized()
    {
        mModelMatrix.SetRotation(mRotation.x * Deg2Rad, mRotation.y * Deg2Rad, mRotation.z * Deg2Rad);
        mModelMatrix.SetTranslation(mTranslation);
        mModelMatrix.SetScale(mScale);
    }

    void SceneModel::SetModel(const std::shared_ptr<ModelResource>& res)
    {
        mModel = res;
        mLocalBound = res->GetBound();
        mModelFilePath = res->GetRepoPath();
    }

    void SceneModel::PostDeserialized()
    {
        SceneObject::PostDeserialized();

        if (!mModel)
        {
            SetModel(ResourceLoader::Instance().LoadResource<ModelResource>(mModelFilePath));
        }
    }

    Scene::Scene() 
        :IResource(), mOctreeSceneModel(WorldBound), mOctreeSceneLight(WorldBound)
    {
    }

    Scene::Scene(std::string_view repo_path)
        :Scene()
    {
        SetRepoPath(repo_path);
    }

    uint32 Scene::GetMeshCount() const
    {
        uint32 num = 0;
        for (const auto& model : mSceneModel)
        {
            size_t mesh_num = model->GetModel()->GetMeshResource()->GetSubMeshes().size();
            num += static_cast<uint32>(mesh_num);
        }
        return num;
    }

    void Scene::SetSkyBox(const std::shared_ptr<CubeMapResource>& res)
    {
        mSkyBox = res;
        mSkyBoxPath = res->GetRepoPath();
    }

    void Scene::PostDeserialized()
    {
        if (!mSkyBoxPath.empty()) 
        {
            mSkyBox = ResourceLoader::Instance().LoadResource<CubeMapResource>(mSkyBoxPath);
        }

        for (int i = 0; i < mSceneModel.size(); i++) 
        {
            AddOctreeElementInternal(mOctreeSceneModel, *mSceneModel[i], i);
        }

        for (int i = 0; i < mSceneLight.size(); i++)
        {
            AddOctreeElementInternal(mOctreeSceneLight, *mSceneLight[i], i);
        }
    }

    void SceneLight::PostDeserialized()
    {
        SceneObject::PostDeserialized();

        SetRadius(mRadius);
    }

    PointLightAttenuation MRenderer::SceneLight::CaclAttenuationCoefficients(float radius)
    {
        for (uint32 i = 0; i < std::size(PointLightAttenuationPresets) - 1; i++) 
        {
            const PointLightAttenuation& lower = PointLightAttenuationPresets[i];
            const PointLightAttenuation& upper = PointLightAttenuationPresets[i + 1];

            if (radius < PointLightAttenuationPresets[i].Radius) 
            {
                return PointLightAttenuation
                {
                    .Radius = radius,
                    .CullingRadius = lower.CullingRadius,
                    .ConstantCoefficent = lower.ConstantCoefficent,
                    .LinearCoefficent = lower.LinearCoefficent,
                    .QuadraticCoefficent = lower.QuadraticCoefficent,
                };
            }

            if (radius >= PointLightAttenuationPresets[i].Radius && radius <= PointLightAttenuationPresets[i].Radius)
            {
                float k = (radius - lower.Radius) / (upper.Radius - lower.Radius);

                return PointLightAttenuation
                {
                    .Radius = radius,
                    .CullingRadius = Lerp(lower.CullingRadius, upper.CullingRadius, k),
                    .ConstantCoefficent = Lerp(lower.ConstantCoefficent, upper.ConstantCoefficent, k),
                    .LinearCoefficent = Lerp(lower.LinearCoefficent, upper.LinearCoefficent, k),
                    .QuadraticCoefficent = Lerp(lower.QuadraticCoefficent, upper.QuadraticCoefficent, k),
                };
            }
        }

        return PointLightAttenuationPresets[std::size(PointLightAttenuationPresets) - 1];
    }
}