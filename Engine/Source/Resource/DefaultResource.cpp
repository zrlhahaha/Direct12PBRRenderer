#include "Resource/DefaultResource.h"

namespace MRenderer{
	MeshData DefaultResource::BoxMesh(float width, float height, float depth)
	{
		using Vertex = StandardVertex;

		std::vector<Vertex> v;
		std::vector<uint32> i;

		v.resize(24);
		i.resize(36);

		float hw = 0.5f * width;
		float hh = 0.5f * height;
		float hd = 0.5f * depth;

		// Fill in the front face vertex data.
		v[0] = Vertex({ -hw, -hh, -hd }, { 0.0f, 0.0f, -1.0f }, { 1.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f }, { 0.0f, 1.0f });
		v[1] = Vertex({ -hw, +hh, -hd }, { 0.0f, 0.0f, -1.0f }, { 1.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f }, { 0.0f, 0.0f });
		v[2] = Vertex({ +hw, +hh, -hd }, { 0.0f, 0.0f, -1.0f }, { 1.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f }, { 1.0f, 0.0f });
		v[3] = Vertex({ +hw, -hh, -hd }, { 0.0f, 0.0f, -1.0f }, { 1.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f }, { 1.0f, 1.0f });

		// Fill in the back face vertex data.
		v[4] = Vertex({ -hw, -hh, +hd }, { 0.0f, 0.0f, 1.0f }, { -1.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f }, { 1.0f, 1.0f });
		v[5] = Vertex({ +hw, -hh, +hd }, { 0.0f, 0.0f, 1.0f }, { -1.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f }, { 0.0f, 1.0f });
		v[6] = Vertex({ +hw, +hh, +hd }, { 0.0f, 0.0f, 1.0f }, { -1.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f }, { 0.0f, 0.0f });
		v[7] = Vertex({ -hw, +hh, +hd }, { 0.0f, 0.0f, 1.0f }, { -1.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f }, { 1.0f, 0.0f });

		// Fill in the top face vertex data.
		v[8] = Vertex({ -hw, +hh, -hd }, { 0.0f, 1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f }, { 0.0f, 1.0f });
		v[9] = Vertex({ -hw, +hh, +hd }, { 0.0f, 1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f }, { 0.0f, 0.0f });
		v[10] = Vertex({ +hw, +hh, +hd }, { 0.0f, 1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f }, { 1.0f, 0.0f });
		v[11] = Vertex({ +hw, +hh, -hd }, { 0.0f, 1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f }, { 1.0f, 1.0f });

		// Fill in the bottom face vertex data.
		v[12] = Vertex({ -hw, -hh, -hd }, { 0.0f, -1.0f, 0.0f }, { -1.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f }, { 1.0f, 1.0f });
		v[13] = Vertex({ +hw, -hh, -hd }, { 0.0f, -1.0f, 0.0f }, { -1.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f }, { 0.0f, 1.0f });
		v[14] = Vertex({ +hw, -hh, +hd }, { 0.0f, -1.0f, 0.0f }, { -1.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f }, { 0.0f, 0.0f });
		v[15] = Vertex({ -hw, -hh, +hd }, { 0.0f, -1.0f, 0.0f }, { -1.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f }, { 1.0f, 0.0f });

		// Fill in the left face vertex data.
		v[16] = Vertex({ -hw, -hh, +hd }, { -1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, -1.0f }, { 1.0f, 1.0f, 1.0f }, { 0.0f, 1.0f });
		v[17] = Vertex({ -hw, +hh, +hd }, { -1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, -1.0f }, { 1.0f, 1.0f, 1.0f }, { 0.0f, 0.0f });
		v[18] = Vertex({ -hw, +hh, -hd }, { -1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, -1.0f }, { 1.0f, 1.0f, 1.0f }, { 1.0f, 0.0f });
		v[19] = Vertex({ -hw, -hh, -hd }, { -1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, -1.0f }, { 1.0f, 1.0f, 1.0f }, { 1.0f, 1.0f });

		// Fill in the right face vertex data.
		v[20] = Vertex({ +hw, -hh, -hd }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, { 1.0f, 1.0f, 1.0f }, { 0.0f, 1.0f });
		v[21] = Vertex({ +hw, +hh, -hd }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, { 1.0f, 1.0f, 1.0f }, { 0.0f, 0.0f });
		v[22] = Vertex({ +hw, +hh, +hd }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, { 1.0f, 1.0f, 1.0f }, { 1.0f, 0.0f });
		v[23] = Vertex({ +hw, -hh, +hd }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, { 1.0f, 1.0f, 1.0f }, { 1.0f, 1.0f });

		// Fill in the front face index data
		i[0] = 0; i[1] = 1; i[2] = 2;
		i[3] = 0; i[4] = 2; i[5] = 3;

		// Fill in the back face index data
		i[6] = 4; i[7] = 5; i[8] = 6;
		i[9] = 4; i[10] = 6; i[11] = 7;

		// Fill in the top face index data
		i[12] = 8; i[13] = 9; i[14] = 10;
		i[15] = 8; i[16] = 10; i[17] = 11;

		// Fill in the bottom face index data
		i[18] = 12; i[19] = 13; i[20] = 14;
		i[21] = 12; i[22] = 14; i[23] = 15;

		// Fill in the left face index data
		i[24] = 16; i[25] = 17; i[26] = 18;
		i[27] = 16; i[28] = 18; i[29] = 19;

		// Fill in the right face index data
		i[30] = 20; i[31] = 21; i[32] = 22;
		i[33] = 20; i[34] = 22; i[35] = 23;

        Vector3 min = Vector3(-hw, -hh, -hd);
        Vector3 max = Vector3(hw, hh, hd);

		return MeshData(StandardVertexFormat, v, i, AABB(min, max));
	}
	MeshData DefaultResource::SphereMesh(float radius, uint32 longtitude_slice, uint32 latitude_slice)
	{
		using SphereVertex = Vertex<EVertexFormat_P3F_N3F_T3F_C3F_T2F>;

		std::vector<SphereVertex> vertices;
		std::vector<IndexType> indicies;

		// code snipet from dx12 book
		// Compute the vertices stating at the top pole and moving down the stacks.
		//

		// Poles: note that there will be texture coordinate distortion as there is
		// not a unique point on the texture map to assign to the pole when mapping
		// a rectangular texture onto a sphere.
		SphereVertex topVertex{
			.Position		= Vector3(0.0f, +radius, 0.0f),
			.Normal		= Vector3(0.0f, +1.0f, 0.0f),
			.Tangent	= Vector3(1.0f, 0.0f, 0.0f),
			.Color		= Vector3(0.0f, 0.0f, 0.0f),
			.TexCoord0	= Vector2(0.0f, 0.0f)
		};

		SphereVertex bottomVertex{
			.Position = Vector3(0.0f, -radius, 0.0f),
			.Normal = Vector3(0.0f, -1.0f, 0.0f),
			.Tangent = Vector3(1.0f, 0.0f, 0.0f),
			.Color = Vector3(0.0f, 0.0f, 0.0f),
			.TexCoord0 = Vector2(0.0f, 1.0f)
		};

		vertices.push_back(topVertex);

		float phiStep = PI / latitude_slice;
		float thetaStep = 2.0f * PI / longtitude_slice;

		// Compute vertices for each stack ring (do not count the poles as rings).
		for (uint32 i = 1; i <= latitude_slice - 1; ++i)
		{
			float phi = i * phiStep;

			// Vertices of ring.
			for (uint32 j = 0; j <= longtitude_slice; ++j)
			{
				float theta = j * thetaStep;

				SphereVertex v;

				// spherical to cartesian
				v.Position = Vector3(
					radius * sinf(phi) * cosf(theta),
					radius * cosf(phi),
					radius * sinf(phi) * sinf(theta)
				);

				// Partial derivative of P with respect to theta
				v.Tangent = Vector3(
					-radius * sinf(phi) * sinf(theta),
					0.0f,
					radius * sinf(phi) * cosf(theta)
				);
				v.Tangent.Normalize();
				v.Normal = v.Position.GetNormalized();

				v.Color = Vector3(0.0f, 0.0f, 0.0f);
				v.TexCoord0 = Vector2(theta / (2 * PI), phi / PI);

				vertices.push_back(v);
			}
		}

		vertices.push_back(bottomVertex);

		//
		// Compute indices for top stack.  The top stack was written first to the vertex buffer
		// and connects the top pole to the first ring.
		//

		for (uint32 i = 1; i <= longtitude_slice; ++i)
		{
			indicies.push_back(0);
			indicies.push_back(i + 1);
			indicies.push_back(i);
		}

		//
		// Compute indices for inner stacks (not connected to poles).
		//

		// Offset the indices to the index of the first vertex in the first ring.
		// This is just skipping the top pole vertex.
		uint32 baseIndex = 1;
		uint32 ringVertexCount = longtitude_slice + 1;
		for (uint32 i = 0; i < latitude_slice - 2; ++i)
		{
			for (uint32 j = 0; j < longtitude_slice; ++j)
			{
				indicies.push_back(baseIndex + i * ringVertexCount + j);
				indicies.push_back(baseIndex + i * ringVertexCount + j + 1);
				indicies.push_back(baseIndex + (i + 1) * ringVertexCount + j);
				
				indicies.push_back(baseIndex + (i + 1) * ringVertexCount + j);
				indicies.push_back(baseIndex + i * ringVertexCount + j + 1);
				indicies.push_back(baseIndex + (i + 1) * ringVertexCount + j + 1);
			}
		}

		//
		// Compute indices for bottom stack.  The bottom stack was written last to the vertex buffer
		// and connects the bottom pole to the bottom ring.
		//

		// South pole vertex was added last.
		uint32 southPoleIndex = static_cast<uint32>(vertices.size() - 1);

		// Offset the indices to the index of the first vertex in the last ring.
		baseIndex = southPoleIndex - ringVertexCount;

		for (uint32 i = 0; i < longtitude_slice; ++i)
		{
			indicies.push_back(southPoleIndex);
			indicies.push_back(baseIndex + i);
			indicies.push_back(baseIndex + i + 1);
		}

		// calculate AABB
        Vector3 min = Vector3(radius, radius, radius);
        Vector3 max = Vector3(-radius, -radius, -radius);

		return MeshData(EVertexFormat_P3F_N3F_T3F_C3F_T2F, vertices, indicies, AABB(min, max));
	}
}