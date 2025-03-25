extern void DotProduct(uniform float a[], uniform float b[], uniform float* uniform result, uniform int count);

export void Normalize(uniform float v[], const uniform int count)
{
    uniform float length;
    DotProduct(v, v, &length, count);
    length = 1.0f / sqrt(length);
    foreach(index = 0 ... count)
    {
        v[index] *= length;
    }
}