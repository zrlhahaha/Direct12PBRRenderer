#include "clustered.hlsli"

RWStructuredBuffer<Cluster> Clusters : register(u0);

float3 zplane_intersection(float2 uv, float z)
{
    // screen_coord ray direction
    float2 ndc = 2 * uv - 1;
    float3 ray = float3(ndc.x * Ratio * tan(Fov / 2), ndc.y * tan(Fov / 2), 1) * Near;

    float t = z / ray.z; // simplified ray and z-plane intersection
    return ray * t;
}


[numthreads(ClusterX, ClusterY, 1)]
void cs_main(int3 dtid: SV_DispatchThreadID)
{
    for(int z = 0; z < ClusterZ; z++)
    {
        int cluster_index = ClusterIndex(dtid.x, dtid.y, z);
        
        float znear = Near * pow((Far / Near), z / (float)ClusterZ);
        float zfar = Near * pow((Far / Near), (z + 1) / (float)ClusterZ);
        
        float2 tile_size = 1 / float2(ClusterX, ClusterY);
        float2 tile_min = float2(dtid.x, dtid.y) * tile_size;
        float2 tile_max = tile_min + tile_size;
        
        float3 min_near = zplane_intersection(tile_min, znear);
        float3 min_far = zplane_intersection(tile_min, zfar);
        float3 max_near = zplane_intersection(tile_max, znear);
        float3 max_far = zplane_intersection(tile_max, zfar);
        
        Clusters[cluster_index].MinBound = min(min_near, max_far);
        Clusters[cluster_index].MaxBound = max(max_near, max_far);
        Clusters[cluster_index].NumLights = 0;
    }
}
