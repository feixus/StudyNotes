// hit information, aka ray payload
// this sample only carries a shading color and hit distance
// not that the payload should be kept as small as possible
// and that its size must be declared in the corresponding D3D12_RAYTRACING_SHADER_CONFIG pipeline subobject
struct HitInfo
{
    float hit;
};

// attributes output by the raytracing when hitting a surface, here the barycentric coordinates
struct Attributes
{
    float2 bary;
};