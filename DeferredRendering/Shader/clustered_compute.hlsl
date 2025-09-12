#include "clustered.hlsli"

RWStructuredBuffer<Cluster> Clusters : register(u0);

// intersection between the ray (from viewpoint to near plane NDC point) 
// and the plane z = view_z (which is parallel to the near plane)
// ndc =>[-1, 1], view_z=>[0, infinate]
float3 zplane_intersection(float2 ndc, float view_z)
{
    // view space ray direction for given ndc coordinate
    float3 ray = float3(ndc.x * Ratio * tan(Fov / 2), ndc.y * tan(Fov / 2), 1) * Near;

    float t = view_z / ray.z; // simplified ray and z-plane intersection
    return ray * t;
}


[numthreads(ClusterX, ClusterY, 1)]
void cs_main(int3 dtid: SV_DispatchThreadID)
{
    for(int z = 0; z < ClusterZ; z++)
    {
        int cluster_index = ClusterIndex(dtid.x, dtid.y, z);

        // cluster near and far plane
        float znear = Near * pow((Far / Near), z / (float)ClusterZ);
        float zfar = Near * pow((Far / Near), (z + 1) / (float)ClusterZ);

        // ndc coordinate
        float2 tile_min_ndc = 2 * float2(dtid.x, dtid.y) / float2(ClusterX, ClusterY) - 1;
        float2 tile_max_ndc = 2 * float2(dtid.x + 1, dtid.y + 1) / float2(ClusterX, ClusterY) - 1;
        
        float3 min_near = zplane_intersection(tile_min_ndc, znear);
        float3 min_far = zplane_intersection(tile_min_ndc, zfar);
        float3 max_near = zplane_intersection(tile_max_ndc, znear);
        float3 max_far = zplane_intersection(tile_max_ndc, zfar);
        
        Clusters[cluster_index].MinBound = min(min_near, min_far);
        Clusters[cluster_index].MaxBound = max(max_near, max_far);
        Clusters[cluster_index].NumLights = 0;
    }
}
