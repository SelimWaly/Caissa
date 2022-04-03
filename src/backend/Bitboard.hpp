#pragma once

#include "Common.hpp"
#include "Color.hpp"

#include <assert.h>
#include <string>

class Square;

enum class RayDir
{
    North,
    South,
    East,
    West,
    NorthEast,
    NorthWest,
    SouthEast,
    SouthWest,
};

struct Bitboard
{
    uint64_t value;

    Bitboard() = default;
    INLINE constexpr Bitboard(uint64_t value) : value(value) {}
    INLINE constexpr Bitboard(const Bitboard& other) = default;
    INLINE constexpr Bitboard& operator = (const Bitboard& other) = default;
    INLINE constexpr Bitboard operator & (const Bitboard& rhs) const { return value & rhs.value; }
    INLINE constexpr Bitboard operator | (const Bitboard& rhs) const { return value | rhs.value; }
    INLINE constexpr Bitboard operator ^ (const Bitboard& rhs) const { return value ^ rhs.value; }
    INLINE constexpr Bitboard& operator &= (const Bitboard& rhs) { value &= rhs.value; return *this; }
    INLINE constexpr Bitboard& operator |= (const Bitboard& rhs) { value |= rhs.value; return *this; }
    INLINE constexpr Bitboard& operator ^= (const Bitboard& rhs) { value ^= rhs.value; return *this; }
    INLINE constexpr operator uint64_t() const { return value; }
    INLINE constexpr Bitboard operator ~() const { return ~value; }

    // debug print
    std::string Print() const;

    template<typename Func>
    INLINE void Iterate(const Func func) const
    {
        uint64_t mask = value;
        while (mask)
        {
            const uint32_t index = FirstBitSet(mask);
            mask &= ~(1ull << index);
            func(index);
        };
    }

    const Bitboard Rotated180() const
    {
        const uint64_t h1 = 0x5555555555555555ull;
        const uint64_t h2 = 0x3333333333333333ull;
        const uint64_t h4 = 0x0F0F0F0F0F0F0F0Full;
        const uint64_t v1 = 0x00FF00FF00FF00FFull;
        const uint64_t v2 = 0x0000FFFF0000FFFFull;

        uint64_t x = value;
        x = ((x >>  1) & h1) | ((x & h1) <<  1);
        x = ((x >>  2) & h2) | ((x & h2) <<  2);
        x = ((x >>  4) & h4) | ((x & h4) <<  4);
        x = ((x >>  8) & v1) | ((x & v1) <<  8);
        x = ((x >> 16) & v2) | ((x & v2) << 16);
        x =  (x >> 32)       | ( x       << 32);
        return x;
    }

    INLINE const Bitboard MirroredVertically() const
    {
        return SwapBytes(value);
    }

    const Bitboard MirroredHorizontally() const
    {
        const uint64_t k1 = 0x5555555555555555ull;
        const uint64_t k2 = 0x3333333333333333ull;
        const uint64_t k4 = 0x0f0f0f0f0f0f0f0full;

        uint64_t x = value;
        x = ((x >> 1) & k1) +  2u * (x & k1);
        x = ((x >> 2) & k2) +  4u * (x & k2);
        x = ((x >> 4) & k4) + 16u * (x & k4);
        return x;
    }

    INLINE static constexpr Bitboard Full()
    {
        return 0xFFFFFFFFFFFFFFFFull;
    }

    INLINE static constexpr Bitboard LightSquares()
    {
        return 0x55AA55AA55AA55AAull;
    }

    INLINE static constexpr Bitboard DarkSquares()
    {
        return 0xAA55AA55AA55AA55ull;
    }

    template<uint32_t rank>
    INLINE static constexpr Bitboard RankBitboard()
    {
        static_assert(rank < 8u, "Invalid rank");
        return 0xFFull << (8u * rank);
    }

    template<uint32_t file>
    INLINE static constexpr Bitboard FileBitboard()
    {
        static_assert(file < 8u, "Invalid file");
        return 0x0101010101010101ull << file;
    }

    INLINE static Bitboard RankBitboard(uint32_t rank)
    {
        ASSERT(rank < 8u);
        return 0xFFull << (8u * rank);
    }

    INLINE static Bitboard FileBitboard(uint32_t file)
    {
        ASSERT(file < 8u);
        return 0x0101010101010101ull << file;
    }

    INLINE static constexpr Bitboard ShiftRight(Bitboard board, uint32_t num)
    {
        for (uint32_t i = 0; i < num; i++)
        {
            board = (board << 1u) & (~FileBitboard<0>());
        }
        return board;
    }


    INLINE static constexpr Bitboard ShiftLeft(Bitboard board, uint32_t num)
    {
        for (uint32_t i = 0; i < num; i++)
        {
            board = (board >> 1u) & (~FileBitboard<7>());
        }
        return board;
    }

    INLINE uint32_t FileMask() const
    {
        uint32_t mask = (uint32_t)(value | (value >> 32));
        mask |= mask >> 16;
        mask |= mask >> 8;
        return mask & 0xFF;
    }

    INLINE uint32_t Count() const
    {
        return PopCount(value);
    }

    INLINE bool BitScanForward(uint32_t& outIndex) const
    {
        if (value)
        {
            outIndex = FirstBitSet(value);
            return true;
        }
        else
        {
            return false;
        }
    }

    INLINE bool BitScanReverse(uint32_t& outIndex) const
    {
        if (value)
        {
            outIndex = LastBitSet(value);
            return true;
        }
        else
        {
            return false;
        }
    }

    static Bitboard GetRay(const Square square, const RayDir dir);

    template<Color color>
    static Bitboard GetPawnAttacks(const Square square);

    template<Color color>
    static Bitboard GetPawnAttacks(const Bitboard pawns);

    static Bitboard GetPawnAttacks(const Square square, const Color color);
    static Bitboard GetKingAttacks(const Square square);
    static Bitboard GetKnightAttacks(const Square square);
    static Bitboard GetRookAttacks(const Square square);
    static Bitboard GetBishopAttacks(const Square square);

    static Bitboard GenerateRookAttacks(const Square square, const Bitboard blockers);
    static Bitboard GenerateBishopAttacks(const Square square, const Bitboard blockers);

    static Bitboard GenerateRookAttacks_Slow(const Square square, const Bitboard blockers);
    static Bitboard GenerateBishopAttacks_Slow(const Square square, const Bitboard blockers);
};

void InitBitboards();
