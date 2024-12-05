[Physically Based Shading on Mobile-BrianKaris-2014](https://www.unrealengine.com/en-US/blog/physically-based-shading-on-mobile)
<br>


https://cdn2.unrealengine.com/Resources/files/GDC2014_Next_Generation_Mobile_Rendering-2033767592.pdf

<br>

- removed Disney's "hotness" modification to the geometry term.

- with Eric Heitz's [recent work](http://jcgt.org/published/0003/02/03/paper.pdf), to add a correlation between the shadowing and masking.

- the Cavity parameter never made it in, found uses for controllable dielectric specular reflectance. Instead use the Specular parameter as defined in Disney's model.

- the same material model (BaseColor, Metallic, Specular, Roughness) appears to behave the same on PC, consoles and mobile devices.

- only support a single directional sun light that is calculated dynamically on mobile device.
- the shadows are precomputed and stored as signed distance fields in texture space.
- All other lights are baked into lightmaps. the lightmaps use a similar HDR encoded SH directional representation as high end but adapted to better compress with PVRTC which have separate, uncorrelated, alpha compression.  (PVRTC is older, now is ASTC? ETC2?)
- like high end, precomputed specular uses preconvolved environment maps. the exact same preconvolution is used and they are similarly normalized and scaled by the lightmap. 
- for mobile, only one environment map is fetched per pixel.


- Environment BRDF<br>
  Environment BRDF which for high end is precomputed with Monte Carlo integration and stored in a 2D LUT.
  Dependent texture fetches are really expensive on some mobile hardward but even worse is the extremely limiting 8 sampler limit of OpenGL ES2(now most device is OpenGL3/Vulkan/Metal).
  an approximate analytic version based on [Dimitar Lazarov's work](http://blog.selfshadow.com/publications/s2013-shading-course/lazarov/s2013_pbs_black_ops_2_notes.pdf):<br>


  ```HLSL
    half3 EnvBRDFApprox( half3 SpecularColor, half Roughness, half NoV )
    {
        const half4 c0 = { -1, -0.0275, -0.572, 0.022 };
        const half4 c1 = { 1, 0.0425, 1.04, -0.04 };
        half4 r = Roughness * c0 + c1;
        half a004 = min( r.x * r.x, exp2( -9.28 * NoV ) ) * r.x + r.y;
        half2 AB = half2( -1.04, 1.04 ) * a004 + r.zw;
        return SpecularColor * AB.x + AB.y;
    }

  ```

  when Metallic and Specular material represent as nonmetals, can further optimize the function:

  ```HLSL
    half EnvBRDFApproxNonmetal( half Roughness, half NoV )
    {
        // Same as EnvBRDFApprox( 0.04, Roughness, NoV )
        const half2 c0 = { -1, -0.0275 };
        const half2 c1 = { 1, 0.0425 };
        half2 r = Roughness * c0 + c1;
        return min( r.x * r.x, exp2( -9.28 * NoV ) ) * r.x + r.y;
    }

  ```

  some games optimize for no specular but that isn't energy conserving. we instead have a "Full rough" flag which sets Roughness=1 and optimizes out constant factors:

  EnvBRDFApprox( SpecularColor, 1, 1) = SpecularColor * 0.4524 - 0.0024

  from there i make the simplification:

  DiffuseColor += SpecularColor * 0.45  
  SpecularColor = 0

  we only use the flag on select objects that really need the extra performance.


- Directional Light<br>
  just calculated a portion of it in an approximate preintegrated form with the EnvBRDF though. the reflection vector which was used to sample the environment map.
  the idea is to analytically evaluate the radially symmetric lobe that is used to prefilter the environment map and then multiply the result with EnvBRDF just like we do for IBL. think of this as anylytically integrating the lobe against the incoming light direction instead of numerically integrating like we do with the environment map.

  first, replace the GGX NDF with Blinn. Blinn is then approximated with a radially symmetric Phong lobe.

  $$D_{Blinn}(h) = \frac{1}{ \pi \alpha^2 } (n\cdot h)^{ \left( \frac{2}{ \alpha^2 } - 2 \right) } \approx \frac{1}{ \pi \alpha^2 } (r\cdot l)^{ \left( \frac{1}{ 2 \alpha^2 } - \frac{1}{2} \right) } $$

  Where $r$ is the reflection direction $r = 2(n \cdot v)n - v$

  Phong is further [approximated with a Spherical Gaussian](http://seblagarde.wordpress.com/2012/06/03/spherical-gaussien-approximation-for-blinn-phong-phong-and-fresnel/):

  $$x^n \approx e^{ (n + 0.775) (x - 1) } $$

    ```HLSL
    half D_Approx( half Roughness, half RoL )
    {
        half a = Roughness * Roughness;
        half a2 = a * a;
        float rcp_a2 = rcp(a2);
        // 0.5 / ln(2), 0.275 / ln(2)
        half c = 0.72134752 * rcp_a2 + 0.39674113;
        return rcp_a2 * exp2( c * RoL - c );
    }
    ```
    Where $\frac{1}{\pi}$ is factored into the light's color.
