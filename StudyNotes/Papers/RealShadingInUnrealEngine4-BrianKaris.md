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

    $$f(l, v) = \frac{c_{diff}}{\pi}   \qquad   (1) $$

    ${c_{diff}} 是材质的diffuse \ albedo.$ <br>
    $除\pi是因为漫反射部分从BRDF中剥离出来时,需抵消积分计算结果值中的\pi$
    <br>

  - Microfacet Specular BRDF
  
    Cook-Torrance microfacet specular shading model:

    $$f(l, v) = \frac{D(h)F(v, h)G(l,v,h)}{4(n{\cdot}l)(n{\cdot}v)} \qquad (2) $$
    <br>

  - Specular D
  
    normal distribution function(NDF)采用Disney选择的 GGX/Trowbridge-Reitz, 与Blinn-Phong相比, 额外费用相当小. 外观有很长的拖尾(tail). 也采纳Disnery的重新参数化 ${\alpha} = {Roughness^2} $.

    $$D(h) = \frac{\alpha^2}{\pi((n\cdot h)^2(\alpha^2 - 1) + 1)^2} \qquad (3) $$
    <br>

  - Specular G
  
    specular geometric attenuation选择了Schlick model. 但 $k = \alpha / 2$,以更好的适应GGX的Smith model. 在此修改下, Schlick model可在 $\alpha = 1$ 时精确匹配Smith, 并在[0,1]范围内相当接近.
    Disney通过重新映射roughness为 $\frac{Roughness + 1}{2}$ 来减少"hotness". 但此调节仅用于 analytic light sources. 若应用于image-based lighting, 则在glancing angles的结果会十分暗淡.   <br/>

    $$k = \frac{(Roughness + 1)^2}{8} (for \ analytic \ light \ sources / direct \  lighting) $$  

    $$k = \frac{Roughness^2}{2} (for \ image-based \ lighting) $$

    $$G_1(v) = \frac{\vec{n} \cdot \vec{v}}{(\vec{n} \cdot \vec{v})(l - k) + k} $$

    $$G(l,v,h) = G_1(l)G_1(v)  \qquad  (4) $$

    <br/>
    
    ![alt text](images/schlickToSmith.png)

    <br/>

  - Specular F
  
    Fresnel 使用Schlick's approximation. 但有细微的修改: 使用 Spherical Gaussian approximation来替换power( $(1 - (h \cdot v))^5$ ). 计算效率更高一些,差别也看不出来.
    
    $$F(v,h) = F_0 + (1 - F_0)2^{(-5.55473(\vec{v} \cdot \vec{h}) - 6.98316)(\vec{v} \cdot \vec{h})} \qquad (5) $$
    
    $F_0 是 the \ specular \ reflectance \ at \ normal \ incidence$
    <br/>

  - Image-Based Lighting
  
    为了将shading model和image-based lighting一起使用, radiance integral需要被解析, 一般使用importance sampling. 以下方程式描述此数值积分:
  
    $$\int_H L_{i} f(l,v) cos\theta_l dl \approx \frac{1}{N} \sum_{k=1}^{N}  \frac{L_i(l_k)f(l_k,v)cos\theta_{l_k}}{p(l_k,v)} \qquad (6) $$

    (import sampling 体现在只有视角方向正好是入射光方向的反射方向,才会通过采样. 根据GGX分布方程式,使用球面坐标来生成需要的法线H)<br>
    通过使用mip maps可以显著减少采样数. 但采样数仍需要大于16以确保质量.但ue为了local reflections,混合了许多environment maps,因此实际仅每一个单个采样.
    <br/>

    ```c++
    float3 ImportanceSampleGGX( float2 Xi, float Roughness, float3 N )
    {
        float a = Roughness * Roughness;

        float Phi = 2 * PI * Xi.x;
        // 如何推导出costheta的呢???
        float CosTheta = sqrt( (1 - Xi.y) / (1 + (a*a - 1) * Xi.y ));
        float SinTheta = sqrt( 1 - CosTheta * Costheta );

        //左手坐标系 球面坐标系->笛卡尔坐标系
        float3 H;
        H.x = SinTheta * cos(Phi);
        H.y = SinTheta * sin(Phi);
        H.z = CosTheta;

        float3 UpVector = abs(N.z) < 0.999 ? float3(0,0,1) : float3(1,0,0);
        float3 TangentX = normalize( cross(UpVector, N) );
        float3 TangentY = cross( N, TangentX );

        //tanget to world space
        return TangentX * H.x + TangentY * H.y + N * H.z;
    }

    float3 SpecularIBL( float3 SpecularColor, float Roughness, float3 N, float3 V )
    {
        float3 SpecularLighting = 0;

        const uint NumSamples = 1024;
        for ( uint i = 0; i < NumSamples; i++ )
        {
            float2 Xi = Hammersley( i, NumSamples );
            float3 H = ImportanceSampleGGX( Xi, Roughness, N );
            float3 L = 2 * dot( V, H ) * H - V;

            float NoV = saturate( dot( N, V ) );
            float NoL = saturate( dot( N, L ) );
            float NoH = saturate( dot( N, H ) );
            float VoH = saturate( dot( V, H ) );

            if ( NoL > 0 )
            {
              float3 SampleColor = EnvMap.SampleLevel( EnvMapSampler, L, 0 ).rgb;

              float G = G_Smith( Roughness, NoV, NoL );
              float Fc = pow( 1 - VoH, 5 );
              float3 F = (1 - Fc) * SpecularColor + Fc;

              // Incident light = SampleColor * NoL
              // Microfacet specular = D*F*G / (4*NoL*NoV)
              // pdf = D * NoH / (4* VoH) (这是如何推导的???)
              SpecularLighting += SampleColor * F * G * VoH / (NoH * NoV);
            }
        }
    }
    ```

    ![alt text](images/hammersley-1.png)  ![alt text](images/hammersley-2.png)

    <br/>

  - Split Sum Approximation
  
    将上述的近似求和分割成两部分.每部分的求和可以预计算.此近似对于constant $L_i(l)$ 是精确的,对于common environment十分精确.

    $$\frac{1}{N} \sum_{k=1}^{N} \frac{L_i(l_k)f(l_k,v) cos \theta_{l_k}}{p(l_k, v)} \approx (\frac{1}{N} \sum_{k=1}^{N}L_i(l_k)) (\frac{1}{N} \sum_{k=1}^{N} \frac{f(l_k, v)cos \theta_{l_k}}{p(l_k, v)}) \qquad (7) $$
    <br/>

  - Pre-Filtered Environment Map
  
    为不同的roughness来预计算第一个求和,结果存储在mip-map levels of a cubemap.相对其他游戏工业所使用的此方法,有个小的区别.即使用importance sampling将着色模型的GGX分布和environment map进行卷积.<br>
    既然这是个微面元模型,分布的形状变化是基于到表面的viewing angle,因此假定此角度为零,即 n = v = r. 此 isotropic假设是近似的第二个来源. 这样做意味着无法获得lengthy reflections at grazing angles. 相比较split sum approximation, 这实际上是IBL解决方案的很大错误来源. 以下的代码可发现 $cos \theta_{l_k}$的加权可获取更好的结果.

    ```HLSL
    float3 PrefilterEnvMap( float Roughness, float3 R )
    {
      float3 N = R;
      float3 V = R;

      float3 PrefilteredColor = 0;

      const uint NumSamplers = 1024;
      for ( uint i = 0; i < NumSamples; i++ )
      {
        float2 Xi = Hammersley( i, NumSamplers );
        float3 H = ImportanceSampleGGX( Xi, Roughness, N );
        float3 L = 2 * dot( V, H ) * H - V;

        float NoL = saturate( dot( N, L ) );
        if ( NoL > 0 )
        {
          PrefilteredColor += Envmap.SampleLevel( EnvMapSampler, L, 0 ).rgb * NoL;
          TotalWeight += NoL;
        }
      }

      return PrefilteredColor / TotalWeight;
    }

    ```

    <br>

  - Environment BRDF
  
    第二个求和和带有solid-white environment的specular BRDF积分一样. 即 $L_i(l_k) = 1$. 通过代入 Schlick's Fresnel: $F(v, h) = F_{0} + (1 - F_0)(1 - v·h)^5 $, $F_0$可以分离出此积分.

    $$\int_H f(l, v) cos\theta_l dl = F_0 \int_H \frac{f(l, v)}{F(v, h)} (1 - (1 - v·h)^5) cos\theta_l dl + \int_H \frac{f(l, v)}{F(v, h)} (1 - v·h)^5 cos\theta_l dl \qquad  (8)$$

    此积分有两个输入(Roughness | $cos \theta_v$)和两个输出(scale and bias to $F_0$), 每个的范围都在[0, 1]内. 预计算此函数的结果并存储在2D look-up texture(LUT)中.

    ```HLSL
    float2 IntegrateBRDF( float Roughness, float NoV )
    {
      float3 V;
      V.x = sqrt( 1.0f - Nov * NoV ); //sin
      V.y = 0;
      V.z = NoV;                      // cos

      float A = 0;
      float B = 0;

      const uint NumSamples = 1024;
      for ( uint i = 0; i < NumSamples; i++ )
      {
        float2 Xi = Hammersley( i, NumSamples );
        float3 H = ImportanceSampleGGX( Xi, Roughness, N );
        float3 L = 2 * dot( V, H ) * H - V;

        float NoL = saturate( L.z );
        float NoH = saturate( H.z );
        float VoH = saturate( dot( V, H ) );

        if (NoL > 0)
        {
          float G = G_Smith( Roughness, NoV, NoL );

          float G_Vis = G * VoH / (NoH * NoV);
          float Fc = pow( 1 - VoH, 5 );
          A += (1 - Fc) * G_Vis;
          B += Fc * G_Vis;
        }
      }

      return float2( A, B ) / NumSamples;
    }

    ```

    ```HLSL
    float3 ApproximateSpecularIBL( float3 SpecularColor, float Roughness, float3 N, float3 V )
    {
      float NoV = saturate( dot( N, V ) );
      float3 R = 2 * dot( V, N ) * N - V;

      float3 PrefilteredColor = PrefilterEnvMap( Roughness, R );
      float2 EnvBRDF = IntegrateBRDF( Roughness, NoV );

      return PrefilteredColor * ( SpecularColor * EnvBRDF.x + EnvBRDF.y );
    }

    ```

# low-discrepancy sequence(quasi-random sequence)

https://www.youtube.com/watch?v=N6xZvrLusPI <br>
http://holger.dammertz.org/stuff/notes_HammersleyOnHemisphere.html <br>
https://en.wikipedia.org/wiki/Low-discrepancy_sequence <br>

<br>

in sampling, random sampling can result in noise, regular grid can result in aliasing with high frequencies inputs.

finding the characteristic function of a probability density function
finding the derivative function of a deterministic function with a small amount of noise.


- Halton Sequence

  $$h_{Halton}(n) = (h_2(n) \quad h_3(n) \quad h_5(n) \quad h_7(n) \quad ... \quad h_b(n)) $$

  $h_b(n)$ is computed by radical inverse function, mirroring the numerical value of n(to the prime base b) at the decimal point.

  |Index n| Numerical value(Base 2) | Mirrored | $h_2(n)$ |
  |--- | --- | --- | --- |
  | 1 | 1 |0.1 = 1/2| 1/2 |
  | 2 | 10 |0.01 = 0/2 + 1/4| 1/4 |
  | 3 | 11 |0.11 = 1/2 + 1/4| 3/4 |
  | 4 | 100 |0.001 = 0/2 + 0/4 + 1/8| 1/8 |
  | 5 | 101 |0.101 = 1/2 + 0/4 + 1/8| 5/8 |
  | 6 | 110 |0.011 = 0/2 + 1/4 + 1/8| 3/8 |
  | 7 | 111 |0.111 = 1/2 + 1/4 + 1/8| 7/8 |

  <br>

  |Index n| Numerical value(Base 3) | Mirrored | $h_3(n)$ |
  |--- | --- | --- | --- |
  | 1 | 1 |0.1 = 1/3| 1/3 |
  | 2 | 2 |0.2 = 2/3| 2/3 |
  | 3 | 10 |0.01 = 0/3 + 1/9| 1/9 |
  | 4 | 11 |0.11 = 1/3 + 1/9| 4/9 |
  | 5 | 12 |0.21 = 2/3 + 1/9| 7/9 |
  | 6 | 20 |0.02 = 0/3 + 2/9| 2/9 |
  | 7 | 21 |0.12 = 1/3 + 2/9| 3/9 |
  | 7 | 22 |0.22 = 2/3 + 2/9| 8/9 |

  <br>

  ```GLSL
  float halton(uint base, uint index) {
    float result = 0.0;
    float digitWeight = 1.0;
    while(index > 0) {
      digitWeight = digitWeight / float(base);
      uint nominator = index % base;
      result += float(nominator) * digitWeight;
      index = index / base;
    }
    return result;
  }
  ```
  $$O(n) = log_b(n) + 1$$


- Hammersley Sequence

  $$h_{Hammersley}(n) = ( \frac{n}{N} \quad h_2(n) \quad h_3(n) \quad h_5(n) \quad h_7(n) \quad ... \quad h_b(n)) $$

  ```GLSL
    // for prime base 2 only
    float radicalInverse_VdC(uint bits) {
        bits = (bits << 16u) | (bits >> 16u);
        bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
        bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
        bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
        bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
        return float(bits) * 2.3283064365386963e-10; // / 0x100000000
    }

    vec2 hammersley2d(uint i, uint N) {
      return vec2(float(i)/float(N), radicalInverse_VdC(i));
  }

  ```

- Sobel Sequence

- low-discrepancy sequences in numerical integration
  e.g. [0,1], as the average of the function evaluated at a set $\{x_1, x_2, ..., x_N \}$ in that interval:

  $$\int_{0}^{1} f(u)du \approx \frac{1}{N} \sum_{i = 1}^{N} f(x_i) $$

  if the points are chosen as $x_i= \frac{i}{N}$, this is rectangle rule.<br>
  if the points are chosen to be randomly(or pseudo-randomly) distributed, this is the Monte Carlo method.<br>
  if the points are chosen as elements of a low-discrepancy sequence, this is the quasi-Monte Carlo method.<br>
