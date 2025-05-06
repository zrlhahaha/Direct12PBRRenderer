#include "global.hlsli"
#define MaxLightsPerCluster 1024

struct Cluster
{
    float3 MinBound;
    float Padding;
    float3 MaxBound;
    int NumLights;
    int LightIndex[MaxLightsPerCluster];
}

struct PointLight
{
    float3 Position;
    float Radius;
    float3 Color;
    float Intensity;
}

cbuffer ShaderConstant
{
    int ClusterX;
    int ClusterY;
    int ClusterZ;
    int NumLight;
}