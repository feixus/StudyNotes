extern void MulByElement(uniform const float a[], uniform const float b[], uniform float result[], uniform const int count);

export void DotProduct(uniform const float a[], uniform const float b[], uniform float* uniform result, uniform const int count)
{
    // 每次执行的结果都不一致: 移除 dynamic memory allocation, foreach替换foreach_active, reduction operation: reduce_add
    foreach (index = 0 ... count) {
        float _a = a[index];
        float _b = b[index];
        *result = reduce_add(_a * _b);
    }
}