#include "PackedNeuralNetwork.hpp"
#include "Memory.hpp"
#include "Math.hpp"
#include "Bitboard.hpp"
#include "Square.hpp"
#include "Piece.hpp"
#include "NeuralNetworkEvaluator.hpp"

#include <cassert>
#include <cmath>
#include <algorithm>
#include <iostream>
#include <vector>

#if defined(PLATFORM_LINUX)
    #include <fcntl.h>
    #include <unistd.h>
    #include <sys/mman.h>
    #include <sys/stat.h>
#endif // PLATFORM_LINUX

namespace nn {

static_assert(sizeof(PackedNeuralNetwork::Header) % CACHELINE_SIZE == 0, "Network header size must be multiple of cacheline size");

#ifdef USE_AVX2
INLINE static int32_t m256_hadd(__m256i a)
{
    const __m256i sum1 = _mm256_hadd_epi32(a, a);
    const __m256i sum2 = _mm256_hadd_epi32(sum1, sum1);
    const __m128i sum3 = _mm256_extracti128_si256(sum2, 1);
    return _mm_cvtsi128_si32(_mm_add_epi32(_mm256_castsi256_si128(sum2), sum3));
}
#endif // USE_AVX2

#ifdef USE_AVX512
INLINE static int32_t m512_hadd(__m512i v)
{
    const __m256i sum256 = _mm256_add_epi32(
        _mm512_castsi512_si256(v),
        _mm512_extracti64x4_epi64(v, 1));
    return m256_hadd(sum256);
}
#endif // USE_AVX512

#ifdef USE_SSE4
INLINE static int32_t m128_hadd(__m128i a)
{
    a = _mm_hadd_epi32(a, a);
    a = _mm_hadd_epi32(a, a);
    return _mm_cvtsi128_si32(a);
}
#endif // USE_SSE4

INLINE static int32_t LinearLayer_Accum_SingleOutput(
    const LastLayerWeightType* weights,
    const AccumulatorType* inputA, const AccumulatorType* inputB)
{
    int32_t val = 0;

#if defined(NN_USE_AVX512)
    constexpr uint32_t registerWidth = 32;
    ASSERT((size_t)weights % (2 * registerWidth) == 0);

    // unroll 2x so two sums can be calculated independently
    __m512i sumA = _mm512_setzero_si512();
    __m512i sumB = _mm512_setzero_si512();
    for (uint32_t j = 0; j < AccumulatorSize; j += registerWidth)
    {
        __m512i inA = Int16VecLoad(inputA + j);
        __m512i inB = Int16VecLoad(inputB + j);

        // apply clipped-ReLU
        inA = _mm512_min_epi16(_mm512_max_epi16(inA, _mm512_setzero_si512()), _mm512_set1_epi16(127));
        inB = _mm512_min_epi16(_mm512_max_epi16(inB, _mm512_setzero_si512()), _mm512_set1_epi16(127));

        // perform 16bit x 16bit multiplication and accumulate to 32bit registers
        const __m512i wA = Int16VecLoad(weights + j);
        const __m512i wB = Int16VecLoad(weights + j + AccumulatorSize);
        sumA = _mm512_add_epi32(sumA, _mm512_madd_epi16(inA, wA));
        sumB = _mm512_add_epi32(sumB, _mm512_madd_epi16(inB, wB));
    }

    // add 16 int32s horizontally
    val += m512_hadd(_mm512_add_epi32(sumA, sumB));

#elif defined(NN_USE_AVX2)
    constexpr uint32_t registerWidth = 16;
    ASSERT((size_t)weights % (2 * registerWidth) == 0);

    // unroll 2x so two sums can be calculated independently
    __m256i sumA = _mm256_setzero_si256();
    __m256i sumB = _mm256_setzero_si256();
    for (uint32_t j = 0; j < AccumulatorSize; j += registerWidth)
    {
        __m256i inA = _mm256_load_si256(reinterpret_cast<const __m256i*>(inputA + j));
        __m256i inB = _mm256_load_si256(reinterpret_cast<const __m256i*>(inputB + j));

        // apply clipped-ReLU
        inA = _mm256_min_epi16(_mm256_max_epi16(inA, _mm256_setzero_si256()), _mm256_set1_epi16(127));
        inB = _mm256_min_epi16(_mm256_max_epi16(inB, _mm256_setzero_si256()), _mm256_set1_epi16(127));

        // perform 16bit x 16bit multiplication and accumulate to 32bit registers
        const __m256i wA = _mm256_load_si256(reinterpret_cast<const __m256i*>(weights + j));
        const __m256i wB = _mm256_load_si256(reinterpret_cast<const __m256i*>(weights + j + AccumulatorSize));
#ifdef NN_USE_VNNI
        sumA = _mm256_dpwssd_epi32(sumA, inA, wA);
        sumB = _mm256_dpwssd_epi32(sumB, inB, wB);
#else
        sumA = _mm256_add_epi32(sumA, _mm256_madd_epi16(inA, wA));
        sumB = _mm256_add_epi32(sumB, _mm256_madd_epi16(inB, wB));
#endif // NN_USE_VNNI
    }

    // add 8 int32s horizontally
    val += m256_hadd(_mm256_add_epi32(sumA, sumB));

#elif defined(NN_USE_SSE4)
    constexpr uint32_t registerWidth = 8;
    static_assert(AccumulatorSize % registerWidth == 0, "");
    ASSERT((size_t)weights % (2 * registerWidth) == 0);

    // unroll 2x so two sums can be calculated independently
    __m128i sumA = _mm_setzero_si128();
    __m128i sumB = _mm_setzero_si128();
    for (uint32_t j = 0; j < AccumulatorSize; j += registerWidth)
    {
        __m128i inA = _mm_load_si128(reinterpret_cast<const __m128i*>(inputA + j));
        __m128i inB = _mm_load_si128(reinterpret_cast<const __m128i*>(inputB + j));

        // apply clipped-ReLU
        inA = _mm_min_epi16(_mm_max_epi16(inA, _mm_setzero_si128()), _mm_set1_epi16(127));
        inB = _mm_min_epi16(_mm_max_epi16(inB, _mm_setzero_si128()), _mm_set1_epi16(127));

        // perform 16bit x 16bit multiplication and accumulate to 32bit registers
        const __m128i wA = _mm_load_si128(reinterpret_cast<const __m128i*>(weights + j));
        const __m128i wB = _mm_load_si128(reinterpret_cast<const __m128i*>(weights + j + AccumulatorSize));
        sumA = _mm_add_epi32(sumA, _mm_madd_epi16(inA, wA));
        sumB = _mm_add_epi32(sumB, _mm_madd_epi16(inB, wB));
    }

    // add 8 int32s horizontally
    val += m128_hadd(_mm_add_epi32(sumA, sumB));

#elif defined(NN_USE_ARM_NEON)

    constexpr uint32_t registerWidth = 8;
    static_assert(AccumulatorSize % registerWidth == 0, "");
    ASSERT((size_t)weights % (2 * registerWidth) == 0);

    int32x4_t sumA = vdupq_n_s32(0);
    int32x4_t sumB = vdupq_n_s32(0);
    int32x4_t sumC = vdupq_n_s32(0);
    int32x4_t sumD = vdupq_n_s32(0);
    for (uint32_t j = 0; j < AccumulatorSize; j += registerWidth)
    {
        // load 8 16bit inputs
        int16x8_t inA = vld1q_s16(inputA + j);
        int16x8_t inB = vld1q_s16(inputB + j);

        // apply clipped-ReLU
        inA = vminq_s16(vmaxq_s16(inA, vdupq_n_s16(0)), vdupq_n_s16(127));
        inB = vminq_s16(vmaxq_s16(inB, vdupq_n_s16(0)), vdupq_n_s16(127));

        // load 8 16bit weights
        const int16x8_t wA = vld1q_s16(weights + j);
        const int16x8_t wB = vld1q_s16(weights + j + AccumulatorSize);

        // perform 16bit x 16bit multiplication and accumulate to 32bit registers
        sumA = vaddq_s32(sumA, vmull_s16(vget_low_s16(wA), vget_low_s16(inA)));
        sumB = vaddq_s32(sumB, vmull_high_s16(wA, inA));
        sumC = vaddq_s32(sumC, vmull_s16(vget_low_s16(wB), vget_low_s16(inB)));
        sumD = vaddq_s32(sumD, vmull_high_s16(wB, inB));
    }

    // add int32s horizontally
    val += vaddvq_s32(vaddq_s32(vaddq_s32(sumA, sumB), vaddq_s32(sumC, sumD)));

#else
    for (uint32_t i = 0; i < AccumulatorSize; ++i)
    {
        const AccumulatorType in = std::clamp<AccumulatorType>(inputA[i], 0, std::numeric_limits<IntermediateType>::max());
        val += (int32_t)in * (int32_t)weights[i];
    }
    for (uint32_t i = 0; i < AccumulatorSize; ++i)
    {
        const AccumulatorType in = std::clamp<AccumulatorType>(inputB[i], 0, std::numeric_limits<IntermediateType>::max());
        val += (int32_t)in * (int32_t)weights[i + AccumulatorSize];
    }
#endif

    return val;
}

///

bool PackedNeuralNetwork::Save(const char* filePath) const
{
    FILE* file = fopen(filePath, "wb");
    if (!file)
    {
        std::cerr << "Failed to save neural network: " << "cannot open file" << std::endl;
        return false;
    }

    Header header; // TODO remove

    if (1 != fwrite(&header, sizeof(Header), 1, file))
    {
        fclose(file);
        std::cerr << "Failed to save neural network: " << "cannot write header" << std::endl;
        return false;
    }

    // TODO write weights
    //if (1 != fwrite(weightsBuffer, GetWeightsBufferSize(), 1, file))
    //{
    //    fclose(file);
    //    std::cerr << "Failed to save neural network: " << "cannot write weights" << std::endl;
    //    return false;
    //}

    fclose(file);
    return true;
}

bool PackedNeuralNetwork::LoadFromFile(const char* filePath)
{
    bool success = false;
    uint32_t numActiveLayers = 0;

    Header header;

    // file mapping
#if defined(PLATFORM_WINDOWS)
    HANDLE fileHandle = INVALID_HANDLE_VALUE;
    HANDLE fileMapping = INVALID_HANDLE_VALUE;
#else
    int fileDesc = -1;
#endif // PLATFORM_WINDOWS

    uint64_t mappedSize = 0;
    void* mappedData = nullptr;

#if defined(PLATFORM_WINDOWS)

    DWORD sizeLow = 0, sizeHigh = 0;
    
    // open file
    {
#ifdef _UNICODE
        wchar_t wideFilePath[4096];
        size_t len = 0;
        mbstowcs_s(&len, wideFilePath, 4096, filePath, _TRUNCATE);
        fileHandle = ::CreateFile(wideFilePath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
#else
        fileHandle = ::CreateFile(filePath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
#endif

        if (fileHandle == INVALID_HANDLE_VALUE)
        {
            fprintf(stderr, "CreateFile() failed, error = %lu.\n", GetLastError());
            goto onError;
        }
    }
    
    sizeLow = ::GetFileSize(fileHandle, &sizeHigh);
    fileMapping = ::CreateFileMapping(fileHandle, NULL, PAGE_READONLY, sizeHigh, sizeLow, NULL);
    if (fileMapping == INVALID_HANDLE_VALUE)
    {
        fprintf(stderr, "CreateFileMapping() failed, error = %lu.\n", GetLastError());
        goto onError;
    }

    mappedSize = (uint64_t)sizeLow + ((uint64_t)sizeHigh << 32);
    mappedData = (void*)::MapViewOfFile(fileMapping, FILE_MAP_READ, 0, 0, 0);
    if (mappedData == nullptr)
    {
        fprintf(stderr, "MapViewOfFile() failed, error = %lu.\n", GetLastError());
        goto onError;
    }

#else

    fileDesc = open(filePath, O_RDONLY);
    if (fileDesc == -1)
    {
        perror("open");
        goto onError;
    }

    struct stat statbuf;
    if (fstat(fileDesc, &statbuf))
    {
        perror("fstat");
        goto onError;
    }

    mappedSize = statbuf.st_size;
    mappedData = mmap(NULL, statbuf.st_size, PROT_READ, MAP_SHARED, fileDesc, 0);
    if (mappedData == MAP_FAILED)
    {
        perror("mmap");
        goto onError;
    }

#endif // PLATFORM_WINDOWS

    memcpy(&header, mappedData, sizeof(Header));

    if (header.magic != MagicNumber)
    {
        std::cerr << "Failed to load neural network: " << "invalid magic" << std::endl;
        goto onError;
    }

    if (header.version != CurrentVersion)
    {
        std::cerr << "Failed to load neural network: " << "unsupported version" << std::endl;
        goto onError;
    }

    if (header.layerSizes[0] == 0 || header.layerSizes[0] > MaxInputs)
    {
        std::cerr << "Failed to load neural network: " << "invalid number of inputs" << std::endl;
        goto onError;
    }

    if (header.layerSizes[1] == 0 || header.layerSizes[1] / 2 != AccumulatorSize)
    {
        std::cerr << "Failed to load neural network: " << "invalid first layer size" << std::endl;
        goto onError;
    }

    for (uint32_t i = 0; i < MaxNumLayers; ++i)
    {
        if (header.layerSizes[i] == 0) break;

        // handle pre-variants format
        if (header.layerVariants[i] == 0) header.layerVariants[i] = 1;

        if (header.layerVariants[i] != 1 && header.layerVariants[i] != NumVariants)
        {
            std::cerr << "Failed to load neural network: " << "unexpected number of variants" << std::endl;
            goto onError;
        }

        numActiveLayers = i + 1;
    }

    if (numActiveLayers < 2)
    {
        std::cerr << "Failed to load neural network: " << "invalid number of layers" << std::endl;
        goto onError;
    }

    LoadFromMemory(mappedData);

    success = true;

onError:

    if (mappedData)
    {
#if defined(PLATFORM_WINDOWS)
        if (fileMapping == INVALID_HANDLE_VALUE)
        {
            ::CloseHandle(fileMapping);
            fileMapping = INVALID_HANDLE_VALUE;
        }
        if (fileHandle == INVALID_HANDLE_VALUE)
        {
            ::CloseHandle(fileHandle);
            fileHandle = INVALID_HANDLE_VALUE;
        }
#else
        if (mappedData)
        {
            if (0 != munmap(mappedData, mappedSize))
            {
                perror("munmap");
            }
        }
        if (fileDesc != -1)
        {
            close(fileDesc);
            fileDesc = -1;
        }
#endif // PLATFORM_WINDOWS
    }

    return success;
}

bool PackedNeuralNetwork::LoadFromMemory(const void* data)
{
    // TODO get rid of header
    const uint8_t* weightsBuffer = reinterpret_cast<const uint8_t*>(data) + sizeof(Header);

    // copy accumulator weights and biases
    memcpy(&accumulatorWeights, weightsBuffer, sizeof(Accumulator) * NumNetworkInputs);
    memcpy(&accumulatorBiases, weightsBuffer + sizeof(Accumulator) * NumNetworkInputs, sizeof(Accumulator));

    // copy last layer weights and biases
    memcpy(&lastLayerWeights, weightsBuffer + sizeof(Accumulator) * (NumNetworkInputs + 1), sizeof(LastLayerWeightsBlock) * NumVariants);

    InitAccumulatorDeltas();

    return true;
}

int32_t PackedNeuralNetwork::Run(const Accumulator& stmAccum, const Accumulator& nstmAccum, uint32_t variant) const
{
    const LastLayerWeightsBlock& weightsBlock = lastLayerWeights[variant];
    return weightsBlock.bias + LinearLayer_Accum_SingleOutput(weightsBlock.weights, stmAccum.values, nstmAccum.values);
}

int32_t PackedNeuralNetwork::Run(const uint16_t* stmFeatures, const uint32_t stmNumFeatures, const uint16_t* nstmFeatures, const uint32_t nstmNumFeatures, uint32_t variant) const
{
    Accumulator stmAccum;
    stmAccum.Refresh(accumulatorWeights, accumulatorBiases, stmNumFeatures, stmFeatures);

    Accumulator nstmAccum;
    nstmAccum.Refresh(accumulatorWeights, accumulatorBiases, nstmNumFeatures, nstmFeatures);

    return Run(stmAccum, nstmAccum, variant);
}

void PackedNeuralNetwork::InitAccumulatorDeltas()
{
    memset(accumDeltaIndexTable, -1, sizeof(accumDeltaIndexTable));

    Accumulator zeroAccum;
    memset(zeroAccum.values, 0, sizeof(Accumulator));

    uint32_t count = 0;
    for (const Color color : {White, Black})
    {
        for (uint32_t pieceIdx = 0; pieceIdx < 6; ++pieceIdx)
        {
            const Piece piece = static_cast<Piece>(pieceIdx + 1);
            for (uint32_t squareA = 0; squareA < 64; ++squareA)
            {
                Bitboard attacks;
                switch (piece)
                {
                case Piece::Pawn:
                    attacks = Bitboard::GetPawnAttacks(Square(squareA), color);
                    if (color == White)
                    {
                        attacks |= Square(squareA).GetBitboard().North(); // single push
                        attacks |= Square(squareA).GetBitboard().North().North() & Bitboard::RankBitboard<3>(); // double push
                    }
                    else
                    {
                        attacks |= Square(squareA).GetBitboard().South(); // single push
                        attacks |= Square(squareA).GetBitboard().South().South() & Bitboard::RankBitboard<4>(); // double push
                    }
                    break;
                case Piece::Knight:
                    attacks = Bitboard::GetKnightAttacks(Square(squareA));
                    break;
                case Piece::Bishop:
                    attacks = Bitboard::GetBishopAttacks(Square(squareA));
                    break;
                case Piece::Rook:
                    attacks = Bitboard::GetRookAttacks(Square(squareA));
                    break;
                case Piece::Queen:
                    attacks = Bitboard::GetQueenAttacks(Square(squareA));
                    break;
                case Piece::King:
                    // TODO filter out "from" squares on right side of board
                    // TODO filter out king moves that change bucket
                    attacks = Bitboard::GetKingAttacks(Square(squareA));
                    // handle castling moves
                    // Note: adding weird castling targets due to horizontal symmetry
                    if (color == White && Square(squareA).Rank() == 0)
                    {
                        attacks |= Square(Square_b1).GetBitboard();
                        attacks |= Square(Square_c1).GetBitboard();
                        attacks |= Square(Square_f1).GetBitboard();
                        attacks |= Square(Square_g1).GetBitboard();
                    }
                    else if (color == Black && Square(squareA).Rank() == 7)
                    {
                        attacks |= Square(Square_b8).GetBitboard();
                        attacks |= Square(Square_c8).GetBitboard();
                        attacks |= Square(Square_f8).GetBitboard();
                        attacks |= Square(Square_g8).GetBitboard();
                    }
                    break;
                }

                attacks.Iterate([&](const Square square)
                {
                    const uint32_t squareB = square.Index();

                    const uint16_t accumDeltaIndex = static_cast<uint16_t>(count++);
                    accumDeltaIndexTable[color][pieceIdx][squareA][squareB] = accumDeltaIndex;

                    // compute accumulator deltas for each king bucket
                    for (uint32_t kingBucket = 0; kingBucket < NumKingBuckets; ++kingBucket)
                    {
                        const uint16_t removedFeature = GetFeatureIndexInBucket(pieceIdx, squareA, color, kingBucket);
                        const uint16_t addedFeature = GetFeatureIndexInBucket(pieceIdx, squareB, color, kingBucket);

                        accumulatorWeights[NumNetworkInputs + kingBucket * NumAccumDeltas + accumDeltaIndex].Update(
                            zeroAccum, accumulatorWeights,
                            1, &addedFeature, 1, &removedFeature);
                    }
                });
            }
        }
    }
    ASSERT(count == NumAccumDeltas);
    UNUSED(count);
}

} // namespace nn
