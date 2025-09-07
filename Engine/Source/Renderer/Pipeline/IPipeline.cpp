#include "Renderer/Device/Direct12/D3D12Device.h"
#include "Renderer/Pipeline/IPipeline.h"
#include "Resource/ResourceLoader.h"
#include "Renderer/Device/Direct12/D3D12CommandList.h"
#include "Renderer/FrameGraph.h"


namespace MRenderer
{
    ShadingState::ShadingState()
        :mShaderProgram(nullptr), mShaderConstantBuffer(nullptr)
    {
    }

    ShadingState::ShadingState(ShadingState&& other)
        :ShadingState()
    {
        swap(*this, other);
    }

    ShadingState& ShadingState::operator=(ShadingState other)
    {
        swap(*this, other);
        return *this;
    }

    void ShadingState::SetShader(std::string_view filename, bool is_compute)
    {
        if (mShaderProgram && filename == mShaderProgram->mFilePath)
        {
            return;
        }

        // compile shader
        mShaderProgram = ShaderLibrary::Instance().ComplieShader(filename, is_compute);
        mIsCompute = is_compute;

        // update shader constant buffer size
        const ShaderConstantBufferAttribute* attr = mShaderProgram->GetPrimaryShader()->FindConstantBufferAttribute(ConstantBufferShader::SemanticName);
        uint32 size = attr ? attr->mSize : 0; // 0 means create samllest buffer, which is 256 bytes in dx12
        
        if (!mShaderConstantBuffer || mShaderConstantBuffer->BufferSize() != size)
        {
            mShaderConstantBuffer = GD3D12ResourceAllocator->CreateConstBuffer(size);
        }
    }

    bool ShadingState::SetTexture(std::string_view semantic_name, DeviceTexture* texture)
    {
        ASSERT(mShaderProgram);
        ASSERT(texture);

        const ShaderAttribute* attr = FindShaderAttribute(EShaderAttrType_Texture, semantic_name);

        if (attr) 
        {
            mResourceBinding.SRVs[attr->mBindPoint] = texture->GetShaderResourceView();
            return true;
        }
        else 
        {
            return false;
        }
    }

    bool ShadingState::SetTexture(std::string_view semantic_name, DeviceTexture2D* texture, uint32 mip_slice)
    {
        ASSERT(mShaderProgram);
        ASSERT(texture);

        const ShaderAttribute* attr = FindShaderAttribute(EShaderAttrType_Texture, semantic_name);

        if (attr)
        {
            mResourceBinding.SRVs[attr->mBindPoint] = texture->GetMipSliceSRV(mip_slice);
            return true;
        }
        else
        {
            return false;
        }
    }

    bool ShadingState::SetRWTexture(std::string_view semantic_name, DeviceTexture2D* texture)
    {
        ASSERT(mShaderProgram && mIsCompute);
        const ShaderAttribute* attr = FindShaderAttribute(EShaderAttrType_RWTexture, semantic_name);

        if (attr) 
        {
            ASSERT(attr->mBindCount == 1);

            mResourceBinding.UAVs[attr->mBindPoint] = texture->GetUnorderedResourceView();
            return true;
        }
        else 
        {
            return false;
        }
    }

    bool ShadingState::SetRWTexture(std::string_view semantic_name, DeviceTexture2D* texture, uint32 mip_slice)
    {
        ASSERT(mShaderProgram && mIsCompute);
        const ShaderAttribute* attr = FindShaderAttribute(EShaderAttrType_RWTexture, semantic_name);

        if (attr)
        {
            ASSERT(attr->mBindCount == 1);

            mResourceBinding.UAVs[attr->mBindPoint] = texture->GetMipSliceUAV(mip_slice);
            return true;
        }
        else
        {
            return false;
        }
    }

    bool ShadingState::SetRWTextureArray(std::string_view semantic_name, DeviceTexture2DArray* texture_array)
    {
        ASSERT(mShaderProgram && mIsCompute);
        const ShaderAttribute* attr = FindShaderAttribute(EShaderAttrType_RWTexture, semantic_name); //RWTextureArray is the RWTexture, just the bind count are different
        
        if (attr) 
        {
            //e.g shader code: "RWTexture2DArray PrefilterEnvMap[5]" require array has at least 5 mip levels
            ASSERT(attr->mBindCount <= texture_array->MipLevels());

            // assign each mip slice to the corresponding texture array
            for (uint32 mip_level = 0; mip_level < texture_array->MipLevels(); mip_level++)
            {
                mResourceBinding.UAVs[attr->mBindPoint + mip_level] = texture_array->GetMipSliceUAV(mip_level);
            }
            return true;
        }
        else
        {
            return false;
        }
    }

    bool ShadingState::SetStructuredBuffer(std::string_view semantic_name, DeviceStructuredBuffer* buffer)
    {
        const ShaderAttribute* attr = FindShaderAttribute(EShaderAttrType_StructuredBuffer, semantic_name);

        if (attr) 
        {
            mResourceBinding.SRVs[attr->mBindPoint] = buffer->GetShaderResourceView();
            return true;
        }
        else 
        {
            return false;
        }
    }

    bool ShadingState::SetRWStructuredBuffer(std::string_view semantic_name, DeviceStructuredBuffer* buffer)
    {
        const ShaderAttribute* attr = FindShaderAttribute(EShaderAttrType_RWStructuredBuffer, semantic_name);

        if (attr) 
        {
            mResourceBinding.UAVs[attr->mBindPoint] = buffer->GetUnorderedAccessView();
            return true;
        }
        else 
        {
            return false;
        }
    }

    void ShadingState::ClearResourceBinding()
    {
        mResourceBinding = {};
    }

    const ResourceBinding* ShadingState::GetResourceBinding() const
    {
        return &mResourceBinding;
    }

    DeviceConstantBuffer* ShadingState::GetConstantBuffer()
    {
        return mShaderConstantBuffer.get();
    }

    const ShaderAttribute* ShadingState::FindShaderAttribute(EShaderAttrType type, std::string_view semantic_name)
    {
        const ShaderAttribute* attr = mShaderProgram->GetPrimaryShader()->FindAttribute(type, semantic_name);

        if (!attr)
        {
            Log("Try to assign undefined or unused shader attribute:", semantic_name, "to shader:", mShaderProgram->GetFilePath());
            return nullptr;
        }

        return attr;
    }

    D3D12ShaderProgram* ShadingState::GetShader()
    {
        return mShaderProgram;
    }

    void ShaderParameter::JsonSerialize(nlohmann::json& json, const ShaderParameter& t)
    {
        Overload overloads{
            [&](bool val) {json = val; },
            [&](float val) {json = val; },
            [&](Vector2 vec) {json = std::array<float, 2>{vec.x, vec.y}; },
            [&](Vector3 vec) {json = std::array<float, 3>{vec.x, vec.y, vec.z}; },
            [&](Vector4 vec) {json = std::array<float, 4>{vec.x, vec.y, vec.z, vec.w}; },
        };

        std::visit(overloads, t.mData);
    }

    void ShaderParameter::JsonDeserialize(nlohmann::json& json, ShaderParameter& t)
    {
        Overload overloads
        {
            [&](bool val) { t.mData = val; },
            [&](float val) { t.mData = val; },
            [&](const std::array<float, 2>& vec) { t.mData = Vector2(vec[0], vec[1]); },
            [&](const std::array<float, 3>& vec) { t.mData = Vector3(vec[0], vec[1], vec[2]); },
            [&](const std::array<float, 4>& vec) { t.mData = Vector4(vec[0], vec[1], vec[2], vec[3]); },
        };

        if (json.is_number())
        {
            overloads(json.get<float>());
        }
        else if (json.is_boolean())
        {
            overloads(json.get<bool>());
        }
        else if (json.is_array())
        {
            if (json.size() == 2)
                overloads(json.get<std::array<float, 2>>());
            else if (json.size() == 3)
                overloads(json.get<std::array<float, 3>>());
            else if (json.size() == 4)
                overloads(json.get<std::array<float, 4>>());
        }
    }

    void PresentPass::Execute(FGContext* context)
    {
        ASSERT(mFinalTexture != InvalidFGResourceId);

        DeviceTexture2D* tex = dynamic_cast<DeviceTexture2D*>(GetTransientResource(context, mFinalTexture));
        context->CommandList->Present(tex);
    }

    void PresentPass::SetFinalTexture(FGResourceId id)
    {
        ASSERT(mFinalTexture == InvalidFGResourceId);
        ReadResource(id);
        mFinalTexture = id;
    }

    inline IDeviceResource* IRenderPass::GetTransientResource(FGContext* context, FGResourceId id)
    {
        return context->FrameGraph->GetFGResource(this, id);
    }
}