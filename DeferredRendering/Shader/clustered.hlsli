#ifndef H_CLUSTERED
#define H_CLUSTERED

#include "global.hlsli"
#define MaxLightsPerCluster 32
#define ClusterX 24
#define ClusterY 16
#define ClusterZ 8


struct Cluster
{
    float3 MinBound;
    float3 MaxBound;
    int NumLights;
    int LightIndex[MaxLightsPerCluster];
};

struct PointLightAttenuation
{
    float Radius;
    float CullingRadius;
    float ConstantCoefficent;
    float LinearCoefficent;
    float QuadraticCoefficent;
};

struct PointLight
{
    float3 Position;
    float3 Color;
    float Intensity;
    PointLightAttenuation Attenuation;
};

// make cluster in z direction contiguous in memory
int ClusterIndex(int x, int y, int z)
{
    return z + x * ClusterZ + y * ClusterX * ClusterZ;
}

int ClusterIndex(float2 uv, float z)
{
    // we define that the slice_x/y/z directions align with the NDC space basis axes, with the near plane's bottom-left corner as the origin.
    // since UV coordinates originate from the top-left corner (opposite Y direction), we need to use 1 - uv.y.
    int slice_x = (int)floor(uv.x * ClusterX);
    int slice_y = (int)floor((1 - uv.y) * ClusterY);

    // inverse function of the function compute tile znear and zfar in clustered_compute.hlsl
    int slice_z = int(ClusterZ * log(min(max(z, Near), Far) / Near) / log(Far / Near));
    
    return ClusterIndex(
            min(max(slice_x, 0), ClusterX),
            min(max(slice_y, 0), ClusterY),
            min(max(slice_z, 0), ClusterZ)
        );
}
#endif