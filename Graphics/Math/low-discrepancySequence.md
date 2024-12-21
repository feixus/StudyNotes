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

  <br>