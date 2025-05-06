#include "clustered.hlsli"


RWStructuredBuffer<Cluster> Clusters : register(t0);
StructuredBuffer<PointLight> PointLights : register(u1);

bool sphere_aabb_intersection(float3 center, float3 radius, float3 aabb_min, float3 aabb_max)
{
    float3 closest = clamp(center, aabb_min, aabb_max);
    float3 distance = center - closest;
    return dot(distance, distance) < dot(radius, radius);
}

[numthreads(ClusterX, ClusterY, ClusterZ)]
void cs_main(int3 dtid: SV_DispatchThreadID)
{
    int cluster_index = dtid.x + dtid.y * ClusterX + dtid.z * ClusterX * ClusterY;

    Cluster cluster = Clusters[cluster_index];
    for (int i = 0; i < NumLight && cluster.NumLights < MaxLightsPerCluster; i++)
    {
        PointLight light = PointLights[i];

        // TODO: we can store the view space light position into group shared memory to avoid redundant matrix multiplication
        float3 pos_view = mul(float4(light.Position, 1), View).xyz;

        if(sphere_aabb_intersection(pos_view, light.Radius, cluster.MinBound, cluster.MaxBound))
        {
            cluster.LightIndex[cluster.NumLights] = i;
            cluster.NumLights++;
        }
    }
}