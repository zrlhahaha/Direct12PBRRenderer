#pragma once
#include "Reflection.h"
#include "Resource/ResourceDef.h"
#include "Renderer/Scene.h"

namespace MRenderer
{
    BEGIN_REFLEFCT_ENUM(EVertexFormat)
        REFLECT_ENUM_VALUE(EVertexFormat_P3F_N3F_T3F_C3F_T2F),
        REFLECT_ENUM_VALUE(EVertexFormat_P3F_N3F_T3F_C3F_T2F)
    END_REFLECT_ENUM

    BEGIN_REFLEFCT_ENUM(ETextureFormat)
        REFLECT_ENUM_VALUE(ETextureFormat_None),
        REFLECT_ENUM_VALUE(ETextureFormat_R16G16B16A16_UNORM)
    END_REFLECT_ENUM

    BEGIN_REFLECT_CLASS(Vector2, void)
        REFLECT_FIELD(x, true),
        REFLECT_FIELD(y, true)
    END_REFLECT_CLASS

    BEGIN_REFLECT_CLASS(Vector3, void)
        REFLECT_FIELD(x, true),
        REFLECT_FIELD(y, true),
        REFLECT_FIELD(z, true)
    END_REFLECT_CLASS

    BEGIN_REFLECT_CLASS(Vector4, void)
        REFLECT_FIELD(x, true),
        REFLECT_FIELD(y, true),
        REFLECT_FIELD(z, true),
        REFLECT_FIELD(w, true)
    END_REFLECT_CLASS

    BEGIN_REFLECT_CLASS(AABB, void)
        REFLECT_FIELD(Min, true),
        REFLECT_FIELD(Max, true)
    END_REFLECT_CLASS

    BEGIN_REFLECT_CLASS(SH2Coefficients, void)
        REFLECT_FIELD(Data, true)
    END_REFLECT_CLASS

    BEGIN_REFLECT_CLASS(SH2CoefficientsPack, void)
        REFLECT_FIELD(sha_r, true),
        REFLECT_FIELD(shb_r, true),
        REFLECT_FIELD(sha_g, true),
        REFLECT_FIELD(shb_g, true),
        REFLECT_FIELD(sha_b, true),
        REFLECT_FIELD(shb_b, true),
        REFLECT_FIELD(shc, true)
    END_REFLECT_CLASS

    BEGIN_REFLECT_CLASS(SubMeshData, void)
        REFLECT_FIELD(Index, true),
        REFLECT_FIELD(IndicesCount, true)
    END_REFLECT_CLASS

    BEGIN_REFLECT_CLASS(MeshData, void)
        REFLECT_FIELD(mVertexFormat, true),
        REFLECT_FIELD(mBound, true),
        REFLECT_FIELD(mVertices, true),
        REFLECT_FIELD(mIndicies, true),
        REFLECT_FIELD(mSubMeshes, true)
    END_REFLECT_CLASS

    BEGIN_REFLECT_CLASS(TextureInfo, void)
        REFLECT_FIELD(Width, true),
        REFLECT_FIELD(Height, true),
        REFLECT_FIELD(Depth, true),
        REFLECT_FIELD(MipLevels, true),
        REFLECT_FIELD(Format, true)
    END_REFLECT_CLASS

    BEGIN_REFLECT_CLASS(TextureData, void)
        REFLECT_FIELD(mInfo, true),
        REFLECT_FIELD(mData, true)
    END_REFLECT_CLASS

    BEGIN_REFLECT_CLASS(CubeMapTextureData, void)
        REFLECT_FIELD(mData, true),
        REFLECT_FIELD(mSHCoefficients, true)
    END_REFLECT_CLASS

    BEGIN_REFLECT_CLASS(IResource, void)
        REFLECT_FIELD(mRepoPath, false)
    END_REFLECT_CLASS

    BEGIN_REFLECT_CLASS(MeshResource, IResource)
        REFLECT_FIELD(mMeshPath, true),
        REFLECT_FIELD(mDeviceVertexBuffer, false),
        REFLECT_FIELD(mDeviceIndexBuffer, false),
        REFLECT_FIELD(mBound, false)
    END_REFLECT_CLASS

    BEGIN_REFLECT_CLASS(TextureResource, IResource)
        REFLECT_FIELD(mTexturePath, true),
        REFLECT_FIELD(mDeviceTexture, false)
    END_REFLECT_CLASS

    BEGIN_REFLECT_CLASS(CubeMapResource, IResource)
        REFLECT_FIELD(mTexturePath, true),
        REFLECT_FIELD(mSHCoefficients, false),
        REFLECT_FIELD(mDeviceTexture2DArray, false)
    END_REFLECT_CLASS

    BEGIN_REFLECT_CLASS(MaterialResource, IResource)
        REFLECT_FIELD(mShaderPath, true),
        REFLECT_FIELD(mTexturePath, true),
        REFLECT_FIELD(mParameterTable, true),
        REFLECT_FIELD(mTextureRefs, false),
        REFLECT_FIELD(mShadingState, false)
    END_REFLECT_CLASS

    BEGIN_REFLECT_CLASS(ModelResource, IResource)
        REFLECT_FIELD(mMeshPath, true),
        REFLECT_FIELD(mMaterialPath, true),
        REFLECT_FIELD(mMeshResource, false),
        REFLECT_FIELD(mMaterials, false)
    END_REFLECT_CLASS

    BEGIN_REFLECT_CLASS(SceneObject, void)
        REFLECT_FIELD(mName, true),
        REFLECT_FIELD(mTranslation, true),
        REFLECT_FIELD(mRotation, true),
        REFLECT_FIELD(mScale, true),
        REFLECT_FIELD(mModelMatrix, false),
        REFLECT_FIELD(mConstantBuffer, false)
    END_REFLECT_CLASS

    BEGIN_REFLECT_CLASS(SceneLight, SceneObject)
        REFLECT_FIELD(mRadius, true)
    END_REFLECT_CLASS

    BEGIN_REFLECT_CLASS(SceneModel, SceneObject)
        REFLECT_FIELD(mModelFilePath, true)
    END_REFLECT_CLASS

    BEGIN_REFLECT_CLASS(Scene, IResource)
        REFLECT_FIELD(mSkyBoxPath, true),
        REFLECT_FIELD(mSceneModel, true),
        REFLECT_FIELD(mSceneLight, true),
        REFLECT_FIELD(mSkyBox, false),
        REFLECT_FIELD(mOctreeSceneModel, false),
        REFLECT_FIELD(mOctreeSceneLight, false)
    END_REFLECT_CLASS
}
