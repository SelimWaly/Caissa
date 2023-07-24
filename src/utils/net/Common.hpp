#pragma once

#include "../Common.hpp"
#include "../../backend/Memory.hpp"

#include <vector>

namespace nn {

    class Layer;
    class NeuralNetwork;

using Values = std::vector<float, AlignmentAllocator<float, 32>>;

struct ActiveFeature
{
    uint32_t index;
    float value;
};


inline float Sigmoid(float x)
{
    return 1.0f / (1.0f + expf(-x));
}
inline float SigmoidDerivative(float x)
{
    float s = Sigmoid(x);
    return s * (1.0f - s);
}


inline float ReLU(float x)
{
    if (x <= 0.0f) return 0.0f;
    return x;
}
inline float ReLUDerivative(float x)
{
    if (x <= 0.0f) return 0.0f;
    return 1.0f;
}


inline float CReLU(float x)
{
    if (x <= 0.0f) return 0.0f;
    if (x >= 1.0f) return 1.0f;
    return x;
}
inline float CReLUDerivative(float x)
{
    if (x <= 0.0f) return 0.0f;
    if (x >= 1.0f) return 0.0f;
    return 1.0f;
}


inline float SqrCReLU(float x)
{
    if (x <= 0.0f) return 0.0f;
    if (x >= 1.0f) return 1.0f;
    return x * x;
}
inline float SqrCReLUDerivative(float x)
{
    if (x <= 0.0f) return 0.0f;
    if (x >= 1.0f) return 0.0f;
    return 2.0f * x;
}

#ifdef USE_AVX

inline __m256 ReLU(const __m256 x)
{
    return _mm256_max_ps(_mm256_setzero_ps(), x);
}
inline __m256 ReLUDerivative(const __m256 x, const __m256 coeff)
{
    return _mm256_and_ps(coeff,
                         _mm256_cmp_ps(x, _mm256_setzero_ps(), _CMP_GT_OQ));
}


inline __m256 CReLU(const __m256 x)
{
    return _mm256_min_ps(_mm256_set1_ps(1.0f), _mm256_max_ps(_mm256_setzero_ps(), x));
}
inline __m256 CReLUDerivative(const __m256 x, const __m256 coeff)
{
    return _mm256_and_ps(coeff,
                         _mm256_and_ps(_mm256_cmp_ps(x, _mm256_setzero_ps(),  _CMP_GT_OQ),
                                       _mm256_cmp_ps(x, _mm256_set1_ps(1.0f), _CMP_LT_OQ)));
}


inline __m256 SqrCReLU(const __m256 x)
{
    return _mm256_min_ps(_mm256_set1_ps(1.0f),
                         _mm256_max_ps(_mm256_setzero_ps(), _mm256_mul_ps(x, x)));
}
inline __m256 SqrCReLUDerivative(const __m256 x, const __m256 coeff)
{
    return _mm256_mul_ps(coeff,
        _mm256_and_ps(_mm256_add_ps(x, x),
                      _mm256_and_ps(_mm256_cmp_ps(x, _mm256_setzero_ps(),  _CMP_GT_OQ),
                                    _mm256_cmp_ps(x, _mm256_set1_ps(1.0f), _CMP_LT_OQ))));
}

#endif // USE_AVX

} // namespace nn