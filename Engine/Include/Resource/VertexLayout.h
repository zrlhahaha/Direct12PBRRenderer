#pragma once    
#include <array>

#include "d3d12.h"
#include "Utils/MathLib.h"

namespace MRenderer
{
    enum EVertexFormat : uint8
    {
        EVertexFormat_None = 0,
        EVertexFormat_P3F_T2F = 1,
        EVertexFormat_P3F_N3F_T3F_C3F_T2F = 2,
    };


    template<EVertexFormat Format>
    struct Vertex;

    template<>
    struct Vertex<EVertexFormat_P3F_T2F>
    {
        static constexpr D3D12_INPUT_ELEMENT_DESC VertexLayout[] =
        {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12 , D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
        };

        static constexpr uint32 NumLayoutElements = std::size(VertexLayout);

        Vector3 Pos;
        Vector2 TexCoord0;
    };

    template<>
    struct Vertex<EVertexFormat_P3F_N3F_T3F_C3F_T2F>
    {
        static constexpr D3D12_INPUT_ELEMENT_DESC VertexLayout[] =
        {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "COLOR", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 36, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 48 , D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
        };

        static constexpr uint32 NumLayoutElements = std::size(VertexLayout);

        Vector3 Position;
        Vector3 Normal;
        Vector3 Tangent;
        Vector3 Color;
        Vector2 TexCoord0;
    };

    struct VertexDefination
    {
        EVertexFormat Format;
        uint32 VertexSize;
        const D3D12_INPUT_ELEMENT_DESC* VertexLayout;
        uint32 NumVertexElements;
    };

    template<EVertexFormat Format>
    constexpr VertexDefination DeclareVertex()
    {
        using Vertex = Vertex<Format>;

        return VertexDefination{
            .Format = Format,
            .VertexSize = sizeof(Vertex),
            .VertexLayout = Vertex::VertexLayout,
            .NumVertexElements = Vertex::NumLayoutElements
        };
    }

    constexpr EVertexFormat StandardVertexFormat = EVertexFormat_P3F_N3F_T3F_C3F_T2F;
    using StandardVertex = Vertex<StandardVertexFormat>;

    // look-up table for convert EVertexFormat to VertexDeclaration
    constexpr std::array VertexDeclarationsTable = {
        VertexDefination(), // for EVertexFormat_None
        DeclareVertex<EVertexFormat_P3F_T2F>(),
        DeclareVertex<EVertexFormat_P3F_N3F_T3F_C3F_T2F>()
    };

    inline const VertexDefination& GetVertexLayout(EVertexFormat format)
    {
        return VertexDeclarationsTable[format];
    }
}