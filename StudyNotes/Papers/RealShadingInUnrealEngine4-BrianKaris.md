[UE4 PBR -- Brian Karis](https://de45xmedrsdbp.cloudfront.net/Resources/files/2013SiggraphPresentationsNotes-26915738.pdf)

# Real Shading in UE4

- 目标
  - 实时性能: 同一时间许多可见光的使用是高效的.
  - 减少复杂度: 尽可能少的参数; 为了切换使用 image-based lighting 和 analytic light sources, 参数在横跨所有光照类型时必须行为一致.
  - 直观的接口: 更喜欢简单理解的值, 而不是物理值如折射率.
  - 线性的感知: 希望支持 layering through masks, 但仅提供每像素一次着色. 则参数混合的着色必须尽可能接近着色结果的混合.
  - 易于掌控: 避免对dielectrics和conductors的技术理解, 最小的努力需求来创建基础的物理令人满意的材质.
  - Robust: 很难错误创建物理不满意的材质, 所有参数的组合尽可能是鲁棒的和满意的.
  - 富于表现的: 延迟着色限制了shading model的数量,因此基础shading model需尽可能覆盖真实世界的材质; 分层材质需共享同一组参数,为了能混合.
  - 灵活的: 允许non-photorealistic rendering

    <br/>
- Shading Model
  
  - Diffuse BRDF
  评估Burley的diffuse model, 相较于Lambertian diffuse(1), 仅有较小的差异. 此外,任何更复杂的diffuse model都很难更有效率的用于image-base或spherical harmonic lighting.
  
  $$ f(l, v) = \frac{c_{diff}}{\pi}   \qquad   (1) $$

    ${c_{diff}} 是材质的diffuse \ albedo. \\  
    除\pi是因为漫反射部分从BRDF中剥离出来时,需抵消积分计算结果值中的\pi   $
<br/>
        
  - Microfacet Specular BRDF
    Cook-Torrance microfacet specular shading model:
    $$ f(l, v) = \frac{D(h)F(v, h)G(l,v,h)}{4(n{\cdot}l)(n{\cdot}v)} \qquad (2) $$
<br/>

  - Specular D
    normal distribution function(NDF)采用Disney选择的 GGX/Trowbridge-Reitz, 与Blinn-Phong相比, 额外费用相当小. 外观有很长的拖尾(tail). 也采纳Disnery的重新参数化 ${\alpha} = {Roughness^2} $.

    $$ D(h) = \frac{\alpha^2}{\pi((n\cdot h)^2(\alpha^2 - 1) + 1)^2} \qquad (3) $$

  - Specular G
  specular geometric attenuation选择了Schlick model. 但 $k = \alpha / 2$,以更好的适应GGX的Smith model. 在此修改下, Schlick model可在$\alpha = 1$时精确匹配Smith, 并在[0,1]范围内相当接近.
  Disney通过重新映射roughness为$ \frac{Roughness + 1}{2} $ 来减少"hotness". 但此调节仅用于 analytic light sources. 若应用于image-based lighting, 则在glancing angles的结果会十分暗淡.   <br/>

    $$k = \frac{(Roughness + 1)^2}{8} (for \ analytic \ light \ sources / direct \  lighting) $$  
    $$k = \frac{Roughness^2}{2} (for \ image-based \ lighting) $$
    $$ G_1(v) = \frac{\vec{n} \cdot \vec{v}}{(\vec{n} \cdot \vec{v})(l - k) + k} $$
    $$ G(l,v,h) = G_1(l)G_1(v)  \qquad  (4) $$
    <br/>

  - Specular F
  Fresnel 使用Schlick's approximation. 但有细微的修改: 使用 Spherical Gaussian approximation来替换power( $(1 - (h \cdot v))^5$ ). 计算效率更高一些,差别也看不出来.
  $$ F(v,h) = F_0 + (1 - F_0)2^{(-5.55473(\vec{v} \cdot \vec{h}) - 6.98316)(\vec{v} \cdot \vec{h})} \qquad (5) $$
  $F_0 是 the \ specular \ reflectance \ at \ normal \ incidence$
    <br/>

  - Image-Based Lighting
    为了将shading model和image-based lighting一起使用, radiance integral需要被解析, 一般使用importance sampling. 以下方程式描述此数值积分:
    $$ \int_H L_{i} f(l,v) cos\theta_l dl \approx \frac{1}{N} \sum_{k=1}^{N}  \frac{L_i(l_k)f(l_k,v)cos\theta_{l_k}}{p(l_k,v)} \qquad (6) $$

    (import sampling 体现在只有视角方向正好是入射光方向的反射方向,才会通过采样. 根据GGX分布方程式,使用球面坐标来生成需要的法线H)
    通过使用mip maps可以显著减少采样数. 但采样数仍需要大于16以确保质量.但ue为了local reflections,混合了许多environment maps,因此实际仅每一个单个采样.
  <br/>

  - Split Sum Approximation
  将上述的近似求和分割成两部分.每部分的求和可以预计算.此近似对于constant $L_i(l)$是精确的,对于common environment十分精确.
  $$ \frac{1}{N} \sum_{k=1}^{N} \frac{L_i(l_k)f(l_k,v) cos \theta_{l_k}}{p(l_k, v)} \approx (\frac{1}{N} \sum_{k=1}^{N}L_i(l_k)) (\frac{1}{N} \sum_{k=1}^{N} \frac{f(l_k, v)cos \theta_{l_k}}{p(l_k, v)}) \qquad (7) $$
  <br/>

  - Pre-Filtered Environment Map
