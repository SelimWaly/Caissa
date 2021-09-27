#pragma once

#include "Square.hpp"
#include "Piece.hpp"

#include <iostream>

struct PackedMove
{
    UNNAMED_STRUCT union
    {
        UNNAMED_STRUCT struct
        {
            uint16_t fromSquare : 6;
            uint16_t toSquare : 6;
            uint16_t promoteTo : 4;
        };

        uint16_t value;
    };

    INLINE constexpr PackedMove() : value(0u) { }
    INLINE constexpr PackedMove(const PackedMove&) = default;
    INLINE constexpr PackedMove& operator = (const PackedMove&) = default;

    // make from regular move
    PackedMove(const Move rhs);

    INLINE Square FromSquare() const { return fromSquare; }
    INLINE Square ToSquare() const { return toSquare; }
    INLINE Piece GetPromoteTo() const { return (Piece)promoteTo; }

    // valid move does not mean it's a legal move for a given position
    // use Position::IsMoveLegal() to fully validate a move
    bool constexpr IsValid() const
    {
        return value != 0u;
    }

    INLINE constexpr bool operator == (const PackedMove& rhs) const
    {
        return value == rhs.value;
    }

    std::string ToString() const;
};

static_assert(sizeof(PackedMove) == 2, "Invalid PackedMove size");

struct Move
{
    INLINE const Square FromSquare() const      { return value & 0b111111; }
    INLINE const Square ToSquare() const        { return (value >> 6) & 0b111111; }
    INLINE const Piece GetPromoteTo() const     { return (Piece)((value >> 12) & 0b1111); }
    INLINE const Piece GetPiece() const         { return (Piece)((value >> 16) & 0b1111); }
    INLINE constexpr bool IsCapture() const     { return (value >> 20) & 1; }
    INLINE constexpr bool IsEnPassant() const   { return (value >> 21) & 1; }
    INLINE constexpr bool IsCastling() const    { return (value >> 22) & 1; }

    // data layout is following:
    //
    // [type]   [property]  [bits]
    // 
    // Square   fromSquare  : 6
    // Square   toSquare    : 6
    // Piece    promoteTo   : 4     target piece after promotion (only valid is piece is pawn)
    // Piece    piece       : 4
    // bool     isCapture   : 1
    // bool     isEnPassant : 1     (is en passant capture)
    // bool     isCastling  : 1     (only valid if piece is king)
    //
    uint32_t value;

    static constexpr uint32_t mask = (1 << 23) - 1;

    INLINE static constexpr Move Make(
        Square fromSquare, Square toSquare, Piece piece, Piece promoteTo = Piece::None,
        bool isCapture = false, bool isEnPassant = false, bool isCastling = false)
    {
        return
        {
            ((uint32_t)fromSquare.mIndex) |
            ((uint32_t)toSquare.mIndex << 6) |
            ((uint32_t)promoteTo << 12) |
            ((uint32_t)piece << 16) |
            ((uint32_t)isCapture << 20) |
            ((uint32_t)isEnPassant << 21) |
            ((uint32_t)isCastling << 22)
        };
    }

    INLINE static constexpr const Move Invalid()
    {
        return { 0 };
    }

    INLINE constexpr bool operator == (const Move rhs) const
    {
        return (value & mask) == (rhs.value & mask);
    }

    INLINE constexpr bool operator != (const Move rhs) const
    {
        return (value & mask) != (rhs.value & mask);
    }

    INLINE bool operator == (const PackedMove rhs) const
    {
        return (value & 0xFFFFu) == rhs.value;
    }

    INLINE bool operator != (const PackedMove rhs) const
    {
        return (value & 0xFFFFu) != rhs.value;
    }

    // valid move does not mean it's a legal move for a given position
    // use Position::IsMoveLegal() to fully validate a move
    bool constexpr IsValid() const
    {
        return value != 0u;
    }

    bool IsQuiet() const
    {
        return !IsCapture() && GetPromoteTo() == Piece::None;
    }

    std::string ToString() const;
};

INLINE PackedMove::PackedMove(const Move rhs)
{
    value = static_cast<uint16_t>(rhs.value);
}

static_assert(sizeof(Move) <= 4, "Invalid Move size");
