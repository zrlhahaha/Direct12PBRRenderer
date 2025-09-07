#ifndef H_CLUSTERED
#define H_CLUSTERED

#include "global.hlsli"
#define MaxLightsPerCluster 128
#define ClusterX 24
#define ClusterY 16
#define ClusterZ 9


struct Cluster
{
    float3 MinBound;
    float Padding;
    float3 MaxBound;
    int NumLights;
    int LightIndex[MaxLightsPerCluster];
};

struct PointLight
{
    float3 Position;
    float Radius;
    float3 Color;
    float Intensity;
};

// make cluster in z direction contiguous in memory
int ClusterIndex(int x, int y, int z)
{
    return z + x * ClusterZ + y * ClusterX * ClusterZ;
}

int ClusterIndex(float2 uv, float z)
{
    int slice_x = max(min(int(uv.x * ClusterX), ClusterX - 1), 0);
    int slice_y = max(min(int(uv.x * ClusterY), ClusterY - 1), 0);

    // inverse function of the function compute tile znear and zfar in clustered_compute.hlsl
    uint slice_z = uint(ClusterZ * log(max(z, 0.0f) / Near) / log(Far / Near));
    return ClusterIndex(slice_x, slice_y, slice_z);
}
#endif