#include "Renderer/Device/Direct12/D3D12Device.h"
#include "Renderer/Pipeline/IPipeline.h"
#include "Resource/ResourceLoader.h"
#include "Renderer/Device/Direct12/D3D12CommandList.h"

namespace MRenderer
{
    void ShaderParameter::JsonSerialize(nlohmann::json& json, const ShaderParameter& t)
    {
        uint32 index = static_cast<uint32>(t.mType);
        ASSERT(index <= static_cast<uint32>(ShaderParameter::EShaderParameter_Total) && index >= 0);

        // float or vector will all be considered as a list in json
        json = std::vector<float>(&t.mData.vec1, &t.mData.vec1 + index);
    }

    void ShaderParameter::JsonDeserialize(nlohmann::json& json, ShaderParameter& t)
    {
        std::vector<float> staging = json;

        // json list to vector or float
        memset(&t.mData, 0, sizeof(t.mData));
        memcpy(&t.mData, staging.data(), staging.size());

        // size of the list = type of the element (EShaderParameter)
        t.mType = static_cast<ShaderParameter::EShaderParameter>(staging.size());
    }

    ShadingState::ShadingState()
        :mShaderProgram(nullptr), mShaderConstantBuffer(nullptr)
    {
    }

    ShadingState::ShadingState(ShadingState&& other)
        :ShadingState()
    {
        Swap(*this, other);
    }

    ShadingState& ShadingState::operator=(ShadingState other)
    {
        Swap(*this, other);
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
        uint32 size = mShaderProgram->GetConstantBufferSize(is_compute ? EShaderType_Compute : EShaderType_Pixel);
        if (!mShaderConstantBuffer || mShaderConstantBuffer->BufferSize() != size)
        {
            mShaderConstantBuffer = GD3D12Device->CreateConstBuffer(size);
        }
    }

    bool ShadingState::SetTexture(std::string_view semantic_name, DeviceTexture* texture)
    {
        ASSERT(mShaderProgram);
        ASSERT(texture);

        const ShaderAttribute* attr;
        if (mIsCompute) 
        {
            attr = mShaderProgram->mCS->FindAttribute(EShaderAttrType_Texture, semantic_name);
        }
        else 
        {
            attr = mShaderProgram->mPS->FindAttribute(EShaderAttrType_Texture, semantic_name);
        }

        if (!attr)
        {
            Log("Try To Assign Undefined Or Unused Texture ", semantic_name, "To ", mShaderProgram->GetFilePath());
            return false;
        }

        mResourceBinding.SRVs[attr->mBindPoint] = texture->GetShaderResourceView();
        return true;
    }

    bool ShadingState::SetRWTexture(std::string_view semantic_name, DeviceTexture2D* texture)
    {
        ASSERT(mShaderProgram && mIsCompute);
        const ShaderAttribute* attr = mShaderProgram->mCS->FindAttribute(EShaderAttrType_RWTexture, semantic_name);

        if (!attr)
        {
            Log("Try To Assign Undefined Or Unused RWTexture ", semantic_name, "To ", mShaderProgram->GetFilePath());
            return false;
        }
        ASSERT(attr->mBindCount == 1);

        mResourceBinding.UAVs[attr->mBindPoint] = texture->GetUnorderedResourceView();
        return true;
    }

    bool ShadingState::SetRWTextureArray(std::string_view semantic_name, DeviceTexture2DArray* texture_array)
    {
        ASSERT(mShaderProgram && mIsCompute);
        const ShaderAttribute* attr = mShaderProgram->mCS->FindAttribute(EShaderAttrType_RWTexture, semantic_name);

        if (!attr)
        {
            Log("Try To Assign Undefined Or Unused RWTextureArray ", semantic_name, "To ", mShaderProgram->GetFilePath());
            return false;
        }

        //e.g shader code: "RWTexture2DArray PrefilterEnvMap[5]" require array has at least 5 mip levels
        ASSERT(attr->mBindCount <= texture_array->MipSize());

        // assign each mip slice to the corresponding texture array
        for (uint32 mip_level = 0; mip_level < texture_array->MipSize(); mip_level++)
        {
            mResourceBinding.UAVs[attr->mBindPoint + mip_level] = texture_array->GetUnorderedAccessView(mip_level);
        }
        return true;
    }

    bool ShadingState::SetStructuredBuffer(std::string_view semantic_name, DeviceStructuredBuffer* buffer)
    {
        const ShaderAttribute* attr;
        if (mIsCompute)
        {
            attr = mShaderProgram->mCS->FindAttribute(EShaderAttrType_Texture, semantic_name);
        }
        else
        {
            attr = mShaderProgram->mPS->FindAttribute(EShaderAttrType_Texture, semantic_name);
        }

        if (!attr)
        {
            Log("Try To Assign Undefined Or Unused StructuredBuffer", semantic_name, "To ", mShaderProgram->GetFilePath());
            return false;
        }

        mResourceBinding.SRVs[attr->mBindPoint] = buffer->GetShaderResourceView();
        return true;
    }

    bool ShadingState::SetRWStructuredBuffer(std::string_view semantic_name, DeviceStructuredBuffer* buffer)
    {
        ASSERT(mShaderProgram && mIsCompute);
        const ShaderAttribute* attr = mShaderProgram->mCS->FindAttribute(EShaderAttrType_RWStructuredBuffer, semantic_name);

        if (!attr)
        {
            Log("Try To Assign Undefined Or Unused RWStructuredBuffer", semantic_name, "To ", mShaderProgram->GetFilePath());
            return false;
        }

        mResourceBinding.UAVs[attr->mBindPoint] = buffer->GetUnorderedAccessView();
        return true;
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

    const D3D12ShaderProgram* ShadingState::GetShader() const
    {
        return mShaderProgram;
    }


    RenderPassNode* IRenderPass::SampleTexture(RenderPassNode* node)
    {
        ASSERT(node);
        mInputNodes.push_back(node);
        return node;
    }

    RenderPassNode* IRenderPass::SampleTexture(IRenderPass* pass, std::string_view output_name)
    {
        ASSERT(pass);
        RenderPassNode* node = pass->FindOutput(output_name);
        ASSERT(node);
        mInputNodes.push_back(node);
        return node;
    }

    void IRenderPass::WriteRenderTarget(std::string_view name, TextureFormatKey key)
    {
        ASSERT(mNumRenderTargets != MaxRenderTargets);
        mOutputNodes.push_back(RenderPassNode::TransientPassResource(name, this, key));
        mRenderTargets[mNumRenderTargets++] = mOutputNodes.back().get();
    }

    void IRenderPass::WriteRenderTarget(std::string_view name, RenderPassNode* node)
    {
        ASSERT(mNumRenderTargets != MaxRenderTargets);
        mOutputNodes.push_back(RenderPassNode::TransientPassResourceReference(name, this, node));
        mRenderTargets[mNumRenderTargets++] = mOutputNodes.back().get();
    }

    void IRenderPass::WriteDepthStencil(uint32 width, uint32 height)
    {
        ASSERT(!mDepthStencil && "caon only write to one depth-stencil at a time");
        mOutputNodes.push_back(RenderPassNode::TransientPassResource("DepthStencil", this, TextureFormatKey(width, height, ETextureFormat_DepthStencil)));
        mDepthStencil = mOutputNodes.back().get();
    }

    void IRenderPass::WriteDepthStencil(RenderPassNode* node)
    {
        ASSERT(!mDepthStencil && "caon only write to one depth-stencil at a time");
        mOutputNodes.push_back(RenderPassNode::TransientPassResourceReference("DepthStencil", this, node));
        mDepthStencil = mOutputNodes.back().get();
    }

    void IRenderPass::WritePersistent(std::string_view name, IDeviceResource* persisten_resource)
    {
        mOutputNodes.push_back(RenderPassNode::PersistentPassResource(name, this, persisten_resource));
    }

    RenderPassNode* IRenderPass::FindOutput(std::string_view name)
    {
        auto ret = std::find_if(mOutputNodes.begin(), mOutputNodes.end(), [&](std::unique_ptr<RenderPassNode>& node) { return node->Name == name; });
        ASSERT(ret != mOutputNodes.end());

        return (ret != mOutputNodes.end()) ? ret->get() : nullptr;
    }

    RenderPassNode* IRenderPass::FindInput(std::string_view name)
    {
        auto ret = std::find_if(mInputNodes.begin(), mInputNodes.end(), [&](RenderPassNode* node) { return node->Name == name; });
        ASSERT(ret != mInputNodes.end());

        return (ret != mInputNodes.end()) ? *ret : nullptr;
    }

    void IRenderPass::UpdatePassStateDesc()
    {
        mPassState.DepthStencilFormat = static_cast<ETextureFormat>(mDepthStencil ? mDepthStencil->GetTextureFormatKey().Info.Format : ETextureFormat_None);
        mPassState.NumRenderTarget = GetRenderTargetsSize();

        for (uint32 i = 0; i < mPassState.NumRenderTarget; i++)
        {
            mPassState.RenderTargetFormats[i] = static_cast<ETextureFormat>(mRenderTargets[i]->GetTextureFormatKey().Info.Format);
        }
    }

    const RenderPassStateDesc* IRenderPass::GetPassStateDesc() const
    {
        return &mPassState;
    }
    
    TextureFormatKey RenderPassNode::GetTextureFormatKey()
    {
        if (Type == ERenderPassNodeType_Transient)
        {
            return TransientResource.RenderTargetKey;
        }
        else if (Type == ERenderPassNodeType_Persisten)
        {
            DeviceTexture* texture = dynamic_cast<DeviceTexture*>(PersistentResource);
            ASSERT(texture);

            return TextureFormatKey
            {
                static_cast<uint16>(texture->TextureWidth()),
                static_cast<uint16>(texture->TextureHeight()),
                texture->Resource()->Format(),
            };
        }
        else
        {
            return PassNodeReference->GetTextureFormatKey();
        }
    }

    IDeviceResource* RenderPassNode::GetResource()
    {
        if (Type == ERenderPassNodeType_Transient)
        {
            ASSERT(TransientResource.Resource);
            return TransientResource.Resource;
        }
        else if (Type == ERenderPassNodeType_Persisten)
        {
            return PersistentResource;
        }
        else if (Type == ERenderPassNodeType_Reference)
        {
            return PassNodeReference->GetResource();
        }
        else 
        {
            ASSERT(false);
            return nullptr;
        }
    }

    // find the RenderPassNode that holds the actual resource
    RenderPassNode* RenderPassNode::GetActualPassNode()
    {
        if (Type == ERenderPassNodeType_Reference) 
        {
            return PassNodeReference->GetActualPassNode();
        }
        else 
        {
            return this;
        }
    }
}