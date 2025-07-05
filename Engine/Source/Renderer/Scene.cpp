#include "Renderer/Scene.h"
#include "Renderer/Device/Direct12/D3D12Device.h"
#include "Resource//ResourceLoader.h"
#include "Renderer/Camera.h"

namespace MRenderer{
    SceneObject::SceneObject()
        :mScale(1.0, 1.0, 1.0)
    {
        mConstantBuffer = GD3D12Device->CreateConstBuffer(sizeof(ConstantBufferInstance));
    }

    SceneObject::SceneObject(std::string_view name)
        :SceneObject()
    {
        mName = name;
    }

    SceneObject::SceneObject(SceneObject&& other)
        : SceneObject()
    {
        Swap(*this, other);
    }

    SceneObject& SceneObject::operator=(SceneObject other)
    {
        Swap(*this, other);
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
            num += model->GetModel()->GetMeshResource()->mMeshData.GetSubMeshCount();
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
}