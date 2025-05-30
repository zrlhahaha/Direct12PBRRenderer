#pragma once
#include <vector>
#include "Resource/ResourceDef.h"

namespace MRenderer 
{
	class DefaultResource
	{
	public:
		static MeshData StandardBoxMesh()
		{
			return BoxMesh(1, 1, 1);
		}

		static MeshData StandardSphereMesh() 
		{
			return SphereMesh(1, 32, 24);
		}

		static std::shared_ptr<ModelResource> CreateStandardSphereModel(std::string_view repo_path);

	protected:
		static MeshData BoxMesh(float width, float height, float depth);
		static MeshData SphereMesh(float radius, uint32 sliceCount, uint32 stackCount);

	};
}
 