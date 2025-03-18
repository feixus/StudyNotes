1. Plannar Reflection  
   一般可用于water, glass, polished surface  
  
2. Screen Space Denoising for  
   shadowVisibilityMasks  
   polychromatic penumbra: denoise one lighting harmonic when denoising multiple light's penumbra  
   reflections/water reflections: denoise first bounce specular  
   AmbientOcclusion  
   DiffuseIndirect  
   SkyLight diffuse indirect  
   reflected skylight diffuse indirect  
   spherical harmonic for diffuse indirect: denoise first bounce diffuse as spherical harmonic
   SSGI(diffuse indirect)/
   diffuse indirect probe hierarchy  
  
     
3. Velocity Pass(velocity buffer/motion vectors)  
   存储前一帧和当前帧的每像素变换差值的buffer, 可用于TAA/MotionBlur/Temporal upscaling/Temporal denoising... 对于成为高品质实时渲染基础的temporal techniques必不可少  

4. HZB occlusion  

5. stencil dither(stipple pattern) for LOD transition, eg. foliage/trss. used like for masked translucency and anisotropic material with DitherTemproalAA  
  
6. 如何在手机设备上获取Tile-Base Deferred Rendering的 Tile size, 可以进行调整么?  
7. Tiled-Based Deferred Rendering/Clustered Deferred Rendering/Deferred Adaptive Computing Shading  
8. Forward+ Rendering(Tiles Forward Rendering)/Cluster Forward Rendering/Volume Tiled Forward Rendering  
9. GPU Skin Cache  
    在GPU侧缓存skeletal vertices. 每个world/scene默认配置的最大缓存为128M. 可选每帧更新tangent. 将bone transform在compute shader计算并缓存  
10. ispc(https://github.com/ispc/ispc) 
  

  


   