#include "clustered.hlsli"

RWStructuredBuffer<Cluster> Clusters : register(t0);

float3 zplane_intersection(float2 screen_coord, float z)
{
    float2 ndc = 2 * screen_coord / Resolution - 1;
    float3 ray = float3(ndc.x * Aspect * tan(Fov / 2), ndc.y * tan(Fov / 2), Near);

    float t = z / ray.z; // simplified ray and z-plane intersection
    return ray * t;
}


[numthreads(ClusterX, ClusterY, ClusterZ)]
void cs_main(int3 dtid: SV_DispatchThreadID)
{
    int clusterIndex = dtid.x + dtid.y * ClusterX + dtid.z * ClusterX * ClusterY;
    
    float znear = Near * pow((Far / Near), dtid.z / (float)ClusterZ);
    float zfar = Near * pow((Far / Near), (dtid.z + 1) / (float)ClusterZ);

    float2 tile_size = Resolution / float2(ClusterX, ClusterY);
    float2 tile_min = float2(dtid.x, dtid.y) * tile_size;
    float2 tile_max = tile_min + tile_size;

    float3 min_near = zplane_intersection(tile_min, znear);
    float3 min_far = zplane_intersection(tile_min, zfar);
    float3 max_near = zplane_intersection(tile_max, znear);
    float3 max_far = zplane_intersection(tile_max, zfar);

    Clusters[clusterIndex].MinBound = min(min_near, max_far);
    Clusters[clusterIndex].MaxBound = max(max_near, max_far)
}
