#include "clustered.hlsli"

RWStructuredBuffer<Cluster> Clusters : register(u0);
RWStructuredBuffer<PointLight> PointLights : register(u1);

cbuffer CONSTANT_BUFFER_SHADER : register(b0)
{
    int NumLight;
};

bool sphere_aabb_intersection(float3 center, float radius, float3 aabb_min, float3 aabb_max)
{
    float3 closest = clamp(center, aabb_min, aabb_max);
    float3 distance = center - closest;
    return dot(distance, distance) < radius * radius;
}

[numthreads(ClusterX, ClusterY, 1)]
void cs_main(int3 dtid: SV_DispatchThreadID)
{
    for(int z = 0; z < ClusterZ; z++)
    {
        int cluster_index = ClusterIndex(dtid.x, dtid.y, z);

        for (int i = 0; i < NumLight && Clusters[cluster_index].NumLights < MaxLightsPerCluster; i++)
        {
            PointLight light = PointLights[i];

            // TODO: we can store the view space light position into group shared memory to avoid redundant matrix multiplication
            float3 pos_view = mul(View, float4(light.Position, 1)).xyz;

            // I/(1 + 4.5*d/r + 75*d/r^2) = 1/256 => 1.814 * r * sqrt(I)
            float culling_radius = light.Attenuation.Radius * CullingRadiusCoefficient * sqrt(light.Intensity);
            if(sphere_aabb_intersection(pos_view, culling_radius, Clusters[cluster_index].MinBound, Clusters[cluster_index].MaxBound))
            {
                int light_index = Clusters[cluster_index].NumLights++;
                Clusters[cluster_index].LightIndex[light_index] = i;
            }
        }
    }
}