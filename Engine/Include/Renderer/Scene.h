#pragma once
#include <vector>

#include "Resource/ResourceDef.h"
#include "Utils/MathLib.h"
#include "Utils/LooseOctree.h"

namespace MRenderer 
{
    class SceneObject
    {
        friend class Scene;
    public:
        SceneObject();
        SceneObject(std::string_view name);
        virtual ~SceneObject() = default;

        SceneObject(const SceneObject&) = delete;
        SceneObject& operator=(const SceneObject&) = delete;
        SceneObject(SceneObject&& other);
        SceneObject& operator=(SceneObject other);

        inline const Matrix4x4& GetWorldMatrix() const { return mModelMatrix;}
        inline DeviceConstantBuffer* GetConstantBuffer() { return mConstantBuffer.get(); }
        inline Vector3 GetTranslation() const { return mTranslation; }
        inline Vector3 GetRotation() const { return mRotation; }
        inline Vector3 GetScale() const { return mScale; }
        inline AABB GetLocalBound() const { return mLocalBound; }
        inline AABB GetWorldBound() const { return GetWorldMatrix() * mLocalBound; }

        void SetWorldMatrix(const Matrix4x4& matrix)
        {
            mModelMatrix = matrix;
            mTranslation = matrix.GetTranslation();
            mRotation = matrix.GetRotation().GetEulerAngle();
            mScale = matrix.GetScale();
        }

        void SetTranslation(const Vector3& translation)
        {
            mTranslation = translation;
            mModelMatrix.SetTranslation(translation);

            mOnTransformChanged.Broadcast(translation);
        }

        void SetRotation(const Vector3& rotation)
        {
            mRotation = rotation;
            mModelMatrix.SetRotation(rotation.x, rotation.y, rotation.z);
        }

        void SetScale(const Vector3& scale)
        {
            mScale = scale;
            mModelMatrix.SetScale(scale);
        }

        void PostDeserialized();

        friend void swap(SceneObject& lhs, SceneObject& rhs) 
        {
            using std::swap;
            swap(lhs.mName, rhs.mName);
            swap(lhs.mModelMatrix, rhs.mModelMatrix);
            swap(lhs.mConstantBuffer, rhs.mConstantBuffer);
        }

    public:
        // serializable member
        std::string mName;
        Vector3 mTranslation; // split the matrix into these components for convenient editing
        Vector3 mRotation;  
        Vector3 mScale;

        // runtime member
        AABB mLocalBound;
        Matrix4x4 mModelMatrix = Matrix4x4::Identity();

        Event<Vector3> mOnTransformChanged;
        std::shared_ptr<DeviceConstantBuffer> mConstantBuffer;
    };

    class SceneModel : public SceneObject 
    {
    public:
        SceneModel() = default;

        SceneModel(std::string_view name, std::shared_ptr<ModelResource> model)
            : SceneObject(name)
        {
            SetModel(model);
        }

        inline ModelResource* GetModel() { return mModel.get(); }

        void SetModel(const std::shared_ptr<ModelResource>& res);
        void PostDeserialized();

        friend void swap(SceneModel& lhs, SceneModel& rhs) 
        {
            swap(static_cast<SceneObject&>(lhs), static_cast<SceneObject&>(rhs));
            std::swap(lhs.mModel, rhs.mModel);
            std::swap(lhs.mLocalBound, rhs.mLocalBound);
        }

    public:
        // serializable member
        std::string mModelFilePath;

        // runtime member
        std::shared_ptr<ModelResource> mModel;
    };


    class SceneLight : public SceneObject
    {
    public:
        SceneLight() 
            : mColor(1, 1, 1), mIntensity(1.0f)
        {
            SetRadius(mRadius);
        }

        SceneLight(std::string_view name, float radius)
            : SceneObject(name)
        {
            SetRadius(mRadius);
        }

        inline void SetRadius(float radius) 
        { 
            mRadius = radius; 
            mLocalBound = AABB(Vector3(-1, -1, -1) * mRadius, Vector3(1, 1, 1) * mRadius);
        }

        inline void SetColor(const Vector3& color) { mColor = color; }
        inline void SetIntensity(float intensity) { mIntensity = intensity; }

        inline float GetRadius() const { return mRadius; }
        inline const Vector3& GetColor() const { return mColor; }
        inline float GetIntensity() const { return mIntensity; }

        void PostDeserialized();

    public:
        // serializable member
        Vector3 mColor;
        float mRadius;
        float mIntensity;
    };


    class Scene : public IResource
    {
    public:
        template<typename T>
        using ContainCallback = std::function<void(const T&)>;

        constexpr static EResourceFormat ResourceFormat = EResourceFormat_Json;
        constexpr static float WorldBound = 1000.0f;

    public:
        Scene();
        Scene(std::string_view repo_path);
        
        inline uint32 GetModelCount() const { return static_cast<uint32>(mSceneModel.size()); }
        inline uint32 GetLightCount() const { return static_cast<uint32>(mSceneLight.size()); }

        uint32 GetMeshCount() const;

        template<typename... Args>
        SceneModel* AddSceneModel(std::string_view name, Args&&... args)
        {
            return AddObjectInternal<SceneModel>(mSceneModel, mOctreeSceneModel, name, std::forward<Args>(args)...);
        }

        template<typename... Args>
        SceneLight* AddSceneLight(std::string_view name, Args&&... args)
        {
            return AddObjectInternal<SceneLight>(mSceneLight, mOctreeSceneLight, name, std::forward<Args>(args)...);
        }

        // call @func for each model in the scene that intersects with the frustum volume
        void CullModel(const FrustumVolume& volume, ContainCallback<SceneModel*> func)
        {
            mOctreeSceneModel.FrustumCull(volume, 
                [&](int index) 
                {
                    func(mSceneModel[index].get());
                }
            );
        }

        // call @func for each light in the scene that intersects with the frustum volume
        void CullLight(const FrustumVolume& volume, ContainCallback<SceneLight*> func)
        {
            mOctreeSceneLight.FrustumCull(volume, 
                [&](int index)
                {
                    func(mSceneLight[index].get());
                }
            );
        }

        inline CubeMapResource* GetSkyBox() { return mSkyBox.get();}
        void SetSkyBox(const std::shared_ptr<CubeMapResource>& res);

        void PostDeserialized();

    protected:
        template<typename T, typename... Args>
        T* AddObjectInternal(std::vector<std::unique_ptr<T>>& container, LooseOctree<int>& octree, std::string_view name, Args... args)
        {
            auto& obj = container.emplace_back(std::make_unique<T>(name, std::forward<Args>(args)...));
            AddOctreeElementInternal(octree, *obj, container.size() - 1);
            return obj.get();
        }

        template<typename T, typename... Args>
        void AddOctreeElementInternal(LooseOctree<int>& octree, T& obj, int index) 
        {
            const AABB& bound = obj.GetWorldBound();
            LooseOctree<int>::OctreeElement* element = octree.AddObject(bound, index);

            obj.mOnTransformChanged.AddFunc(
                [&, element](Vector3 translation) mutable
                {
                    AABB world_aabb = obj.GetWorldBound();
                    octree.UpdateElement(element, world_aabb);
                }
            );
        }

    public:
        // serializable member
        std::string mSkyBoxPath;

        // the reflection and serialization code doesn't support polymorphic class for now.
        // as a compromise, scene objects are divided into different std::vector
        std::vector<std::unique_ptr<SceneModel>> mSceneModel;
        std::vector<std::unique_ptr<SceneLight>> mSceneLight;

        // runtime member
        std::shared_ptr<CubeMapResource> mSkyBox;
        LooseOctree<int> mOctreeSceneModel; // for fast intersection check, @int is the index of the object in @mSceneModel
        LooseOctree<int> mOctreeSceneLight; // same as above
    };
}