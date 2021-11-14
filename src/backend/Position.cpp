#include "Position.hpp"
#include "MoveList.hpp"
#include "Bitboard.hpp"

#include <random>

static_assert(sizeof(MaterialKey) == sizeof(uint64_t), "Invalid material key size");
static_assert(sizeof(PackedPosition) == 32, "Invalid packed position size");

const char* Position::InitPositionFEN = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

static uint64_t s_BlackToMoveHash;
static uint64_t s_PiecePositionHash[2][6][64];
static uint64_t s_CastlingRightsHash[2][2];
static uint64_t s_EnPassantFileHash[8];

void InitZobristHash()
{
    std::mt19937_64 mt(0x06db3aa64a37b526LLU);
    std::uniform_int_distribution<uint64_t> distr;

    s_BlackToMoveHash = distr(mt);

    for (uint32_t color = 0; color < 2; ++color)
    {
        for (uint32_t piece = 0; piece < 6; ++piece)
        {
            for (uint32_t square = 0; square < 64; ++square)
            {
                s_PiecePositionHash[color][piece][square] = distr(mt);
            }
        }
    }

    for (uint32_t file = 0; file < 8; ++file)
    {
        s_EnPassantFileHash[file] = distr(mt);
    }

    s_CastlingRightsHash[0][0] = distr(mt);
    s_CastlingRightsHash[0][1] = distr(mt);
    s_CastlingRightsHash[1][0] = distr(mt);
    s_CastlingRightsHash[1][1] = distr(mt);
}

uint64_t Position::ComputeHash() const
{
    uint64_t hash = mSideToMove == Color::Black ? s_BlackToMoveHash : 0llu;

    for (uint32_t color = 0; color < 2; ++color)
    {
        const SidePosition& pos = mColors[color];

        pos.pawns.Iterate([&](uint32_t square)   INLINE_LAMBDA { hash ^= s_PiecePositionHash[color][0][square]; });
        pos.knights.Iterate([&](uint32_t square) INLINE_LAMBDA { hash ^= s_PiecePositionHash[color][1][square]; });
        pos.bishops.Iterate([&](uint32_t square) INLINE_LAMBDA { hash ^= s_PiecePositionHash[color][2][square]; });
        pos.rooks.Iterate([&](uint32_t square)   INLINE_LAMBDA { hash ^= s_PiecePositionHash[color][3][square]; });
        pos.queens.Iterate([&](uint32_t square)  INLINE_LAMBDA { hash ^= s_PiecePositionHash[color][4][square]; });
        pos.king.Iterate([&](uint32_t square)    INLINE_LAMBDA { hash ^= s_PiecePositionHash[color][5][square]; });
    }

    if (mWhitesCastlingRights & CastlingRights_ShortCastleAllowed)  hash ^= s_CastlingRightsHash[0][0];
    if (mWhitesCastlingRights & CastlingRights_LongCastleAllowed)   hash ^= s_CastlingRightsHash[0][1];
    if (mBlacksCastlingRights & CastlingRights_ShortCastleAllowed)  hash ^= s_CastlingRightsHash[1][0];
    if (mBlacksCastlingRights & CastlingRights_LongCastleAllowed)   hash ^= s_CastlingRightsHash[1][1];

    if (mEnPassantSquare.IsValid()) hash ^= s_EnPassantFileHash[mEnPassantSquare.File()];

    return hash;
}

Piece SidePosition::GetPieceAtSquare(const Square square) const
{
    ASSERT(square.IsValid());

    const Bitboard squareBitboard = square.GetBitboard();

    Piece piece = Piece::None;

    if (pawns & squareBitboard)     piece = Piece::Pawn;
    if (knights & squareBitboard)   piece = Piece::Knight;
    if (bishops & squareBitboard)   piece = Piece::Bishop;
    if (rooks & squareBitboard)     piece = Piece::Rook;
    if (queens & squareBitboard)    piece = Piece::Queen;
    if (king & squareBitboard)      piece = Piece::King;

    return piece;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////

Position::Position()
    : mSideToMove(Color::White)
    , mEnPassantSquare(Square::Invalid())
    , mWhitesCastlingRights(CastlingRights(0))
    , mBlacksCastlingRights(CastlingRights(0))
    , mHalfMoveCount(0u)
    , mMoveCount(1u)
    , mHash(0u)
{}

void Position::SetPiece(const Square square, const Piece piece, const Color color)
{
    const Bitboard mask = square.GetBitboard();
    SidePosition& pos = mColors[(uint8_t)color];

    ASSERT((pos.pawns & mask) == 0);
    ASSERT((pos.knights & mask) == 0);
    ASSERT((pos.bishops & mask) == 0);
    ASSERT((pos.rooks & mask) == 0);
    ASSERT((pos.queens & mask) == 0);
    ASSERT((pos.king & mask) == 0);

    const uint32_t colorIndex = (uint32_t)color;
    const uint32_t pieceIndex = (uint32_t)piece - 1;
    mHash ^= s_PiecePositionHash[colorIndex][pieceIndex][square.Index()];

    pos.GetPieceBitBoard(piece) |= mask;
}

void Position::RemovePiece(const Square square, const Piece piece, const Color color)
{
    const Bitboard mask = square.GetBitboard();
    SidePosition& pos = mColors[(uint8_t)color];
    Bitboard& targetBitboard = pos.GetPieceBitBoard(piece);

    ASSERT((targetBitboard & mask) == mask);
    targetBitboard &= ~mask;

    const uint32_t colorIndex = (uint32_t)color;
    const uint32_t pieceIndex = (uint32_t)piece - 1;
    mHash ^= s_PiecePositionHash[colorIndex][pieceIndex][square.Index()];
}

void Position::SetEnPassantSquare(const Square square)
{
    if (mEnPassantSquare.IsValid())
    {
        mHash ^= s_EnPassantFileHash[mEnPassantSquare.File()];
    }
    if (square.IsValid())
    {
        mHash ^= s_EnPassantFileHash[square.File()];
    }

    mEnPassantSquare = square;
}

void Position::ClearEnPassantSquare()
{
    if (mEnPassantSquare.IsValid())
    {
        mHash ^= s_EnPassantFileHash[mEnPassantSquare.File()];
    }

    mEnPassantSquare = Square::Invalid();
}

Bitboard Position::GetAttackedSquares(Color side) const
{
    const SidePosition& currentSide = mColors[(uint8_t)side];
    const Bitboard occupiedSquares = Whites().Occupied() | Blacks().Occupied();

    Bitboard bitboard{ 0 };

    if (currentSide.pawns)
    {
        if (side == Color::White)
        {
            bitboard |= (currentSide.pawns & ~Bitboard::FileBitboard<0u>()) << 7u;
            bitboard |= (currentSide.pawns & ~Bitboard::FileBitboard<7u>()) << 9u;
        }
        else
        {
            bitboard |= (currentSide.pawns & ~Bitboard::FileBitboard<0u>()) >> 9u;
            bitboard |= (currentSide.pawns & ~Bitboard::FileBitboard<7u>()) >> 7u;
        }
    }

    currentSide.knights.Iterate([&](uint32_t fromIndex) INLINE_LAMBDA
    {
        bitboard |= Bitboard::GetKnightAttacks(Square(fromIndex));
    });

    const Bitboard rooks = currentSide.rooks | currentSide.queens;
    const Bitboard bishops = currentSide.bishops | currentSide.queens;

    rooks.Iterate([&](uint32_t fromIndex) INLINE_LAMBDA
    {
        bitboard |= Bitboard::GenerateRookAttacks(Square(fromIndex), occupiedSquares);
    });

    bishops.Iterate([&](uint32_t fromIndex) INLINE_LAMBDA
    {
        bitboard |= Bitboard::GenerateBishopAttacks(Square(fromIndex), occupiedSquares);
    });

    bitboard |= Bitboard::GetKingAttacks(Square(FirstBitSet(currentSide.king)));

    return bitboard;
}

void Position::GenerateMoveList(MoveList& outMoveList, uint32_t flags) const
{
    outMoveList.numMoves = 0u;

    GeneratePawnMoveList(outMoveList, flags);
    GenerateKnightMoveList(outMoveList, flags);
    GenerateRookMoveList(outMoveList, flags);
    GenerateBishopMoveList(outMoveList, flags);
    GenerateQueenMoveList(outMoveList, flags);
    GenerateKingMoveList(outMoveList, flags);
}

void Position::GeneratePawnMoveList(MoveList& outMoveList, uint32_t flags) const
{
    const bool onlyTactical = flags & MOVE_GEN_ONLY_TACTICAL;
    const int32_t pawnDirection = mSideToMove == Color::White ? 1 : -1;
    const SidePosition& currentSide = GetCurrentSide();
    const SidePosition& opponentSide = GetOpponentSide();
    const uint32_t pawnStartingRank = mSideToMove == Color::White ? 1u : 6u;
    const uint32_t enPassantRank = mSideToMove == Color::White ? 5u : 2u;
    const uint32_t pawnFinalRank = mSideToMove == Color::White ? 6u : 1u;

    const Bitboard occupiedSquares = Whites().Occupied() | Blacks().Occupied();

    const auto generatePawnMove = [&](const Square fromSquare, const Square toSquare, bool isCapture, bool enPassant)
    {
        if (fromSquare.Rank() == pawnFinalRank) // pawn promotion
        {
            const Piece promotionList[] = { Piece::Queen, Piece::Knight, Piece::Rook, Piece::Bishop };
            for (const Piece promoteTo : promotionList)
            {
                outMoveList.Push(Move::Make(fromSquare, toSquare, Piece::Pawn, promoteTo, isCapture, enPassant));
            }
        }
        else if (!onlyTactical || isCapture)
        {
            outMoveList.Push(Move::Make(fromSquare, toSquare, Piece::Pawn, Piece::None, isCapture, enPassant));
        }
    };

    currentSide.pawns.Iterate([&](uint32_t fromIndex) INLINE_LAMBDA
    {
        const Square fromSquare(fromIndex);
        const Square squareForward(fromSquare.Index() + pawnDirection * 8); // next rank

        // there should be no pawn in first or last rank
        ASSERT(fromSquare.Rank() > 0u && fromSquare.Rank() < 7u);

        // capture on the left
        if (fromSquare.File() > 0u)
        {
            const Square toSquare(fromSquare.Index() + pawnDirection * 8 - 1);
            if (toSquare.GetBitboard() & opponentSide.OccupiedExcludingKing())
            {
                generatePawnMove(fromSquare, toSquare, true, false);
            }
            if (toSquare == mEnPassantSquare && toSquare.Rank() == enPassantRank)
            {
                generatePawnMove(fromSquare, toSquare, true, true);
            }
        }

        // capture on the right
        if (fromSquare.File() < 7u)
        {
            const Square toSquare(fromSquare.Index() + pawnDirection * 8 + 1);
            if (toSquare.GetBitboard() & opponentSide.OccupiedExcludingKing())
            {
                generatePawnMove(fromSquare, toSquare, true, false);
            }
            if (toSquare == mEnPassantSquare && toSquare.Rank() == enPassantRank)
            {
                generatePawnMove(fromSquare, toSquare, true, true);
            }
        }

        // can move forward only to non-occupied squares
        if ((occupiedSquares & squareForward.GetBitboard()) == 0u)
        {
            generatePawnMove(fromSquare, squareForward, false, false);

            if (fromSquare.Rank() == pawnStartingRank && !onlyTactical) // move by two ranks
            {
                const Square twoSquaresForward(fromSquare.Index() + pawnDirection * 16); // two ranks up

                // can move forward only to non-occupied squares
                if ((occupiedSquares & twoSquaresForward.GetBitboard()) == 0u)
                {
                    outMoveList.Push(Move::Make(fromSquare, twoSquaresForward, Piece::Pawn, Piece::None));
                }
            }
        }
    });
}

void Position::GenerateKnightMoveList(MoveList& outMoveList, uint32_t flags) const
{
    const SidePosition& currentSide = GetCurrentSide();
    const SidePosition& opponentSide = GetOpponentSide();

    const Bitboard occupiedByOpponent = opponentSide.Occupied();
    Bitboard filter = ~currentSide.Occupied(); // can't capture own piece
    if (flags & MOVE_GEN_ONLY_TACTICAL) filter &= occupiedByOpponent;
    filter &= ~opponentSide.king; // can't capture king

    currentSide.knights.Iterate([&](uint32_t fromIndex) INLINE_LAMBDA
    {
        const Square square(fromIndex);

        const Bitboard attackBitboard = Bitboard::GetKnightAttacks(square) & filter;

        attackBitboard.Iterate([&](uint32_t toIndex) INLINE_LAMBDA
        {
            const Square targetSquare(toIndex);
            const bool isCapture = occupiedByOpponent & targetSquare.GetBitboard();

            outMoveList.Push(Move::Make(square, targetSquare, Piece::Knight, Piece::None, isCapture));
        });
    });
}

void Position::GenerateRookMoveList(MoveList& outMoveList, uint32_t flags) const
{
    const SidePosition& currentSide = GetCurrentSide();
    const SidePosition& opponentSide = GetOpponentSide();

    const Bitboard occupiedByOpponent = opponentSide.Occupied();
    const Bitboard occupiedSquares = Whites().Occupied() | Blacks().Occupied();

    currentSide.rooks.Iterate([&](uint32_t fromIndex) INLINE_LAMBDA
    {
        const Square square(fromIndex);

        Bitboard attackBitboard = Bitboard::GenerateRookAttacks(square, occupiedSquares);
        attackBitboard &= ~currentSide.Occupied(); // can't capture own piece
        if (flags & MOVE_GEN_ONLY_TACTICAL) attackBitboard &= occupiedByOpponent;

        attackBitboard.Iterate([&](uint32_t toIndex) INLINE_LAMBDA
        {
            const Square targetSquare(toIndex);
            const bool isCapture = occupiedByOpponent & targetSquare.GetBitboard();

            outMoveList.Push(Move::Make(square, targetSquare, Piece::Rook, Piece::None, isCapture));
        });
    });
}

void Position::GenerateBishopMoveList(MoveList& outMoveList, uint32_t flags) const
{
    const SidePosition& currentSide = GetCurrentSide();
    const SidePosition& opponentSide = GetOpponentSide();

    const Bitboard occupiedSquares = Whites().Occupied() | Blacks().Occupied();

    currentSide.bishops.Iterate([&](uint32_t fromIndex) INLINE_LAMBDA
    {
        const Square square(fromIndex);

        Bitboard attackBitboard = Bitboard::GenerateBishopAttacks(square, occupiedSquares);
        attackBitboard &= ~currentSide.Occupied(); // can't capture own piece
        if (flags & MOVE_GEN_ONLY_TACTICAL) attackBitboard &= opponentSide.OccupiedExcludingKing();

        attackBitboard.Iterate([&](uint32_t toIndex) INLINE_LAMBDA
        {
            const Square targetSquare(toIndex);
            const bool isCapture = opponentSide.OccupiedExcludingKing() & targetSquare.GetBitboard();

            outMoveList.Push(Move::Make(square, targetSquare, Piece::Bishop, Piece::None, isCapture));
        });
    });
}

void Position::GenerateQueenMoveList(MoveList& outMoveList, uint32_t flags) const
{
    const SidePosition& currentSide = GetCurrentSide();
    const SidePosition& opponentSide = GetOpponentSide();

    const Bitboard occupiedSquares = Whites().Occupied() | Blacks().Occupied();

    currentSide.queens.Iterate([&](uint32_t fromIndex) INLINE_LAMBDA
    {
        const Square square(fromIndex);

        Bitboard attackBitboard =
            Bitboard::GenerateRookAttacks(square, occupiedSquares) |
            Bitboard::GenerateBishopAttacks(square, occupiedSquares);
        attackBitboard &= ~currentSide.Occupied(); // can't capture own piece
        if (flags & MOVE_GEN_ONLY_TACTICAL)
        {
            attackBitboard &= opponentSide.OccupiedExcludingKing();
        }

        attackBitboard.Iterate([&](uint32_t toIndex) INLINE_LAMBDA
        {
            const Square targetSquare(toIndex);
            const bool isCapture = opponentSide.OccupiedExcludingKing() & targetSquare.GetBitboard();

            outMoveList.Push(Move::Make(square, targetSquare, Piece::Queen, Piece::None, isCapture));
        });
    });
}

void Position::GenerateKingMoveList(MoveList& outMoveList, uint32_t flags) const
{
    const bool onlyTactical = flags & MOVE_GEN_ONLY_TACTICAL;

    const CastlingRights& currentSideCastlingRights = (mSideToMove == Color::White) ? mWhitesCastlingRights : mBlacksCastlingRights;
    const SidePosition& currentSide = GetCurrentSide();
    const SidePosition& opponentSide = GetOpponentSide();

    const Bitboard occupiedSquares = Whites().Occupied() | Blacks().Occupied();

    ASSERT(currentSide.king);
    const uint32_t kingSquareIndex = FirstBitSet(currentSide.king);
    const Square kingSquare(kingSquareIndex);
    const Square opponentKingSquare(FirstBitSet(opponentSide.king));

    Bitboard attackBitboard = Bitboard::GetKingAttacks(kingSquare);
    attackBitboard &= ~currentSide.Occupied(); // can't capture own piece
    attackBitboard &= ~Bitboard::GetKingAttacks(opponentKingSquare); // can't move to piece controlled by opponent's king
    if (onlyTactical)
    {
        attackBitboard &= opponentSide.OccupiedExcludingKing();
    }

    attackBitboard.Iterate([&](uint32_t toIndex) INLINE_LAMBDA
    {
        const Square targetSquare(toIndex);
        const bool isCapture = opponentSide.OccupiedExcludingKing() & targetSquare.GetBitboard();

        outMoveList.Push(Move::Make(kingSquare, targetSquare, Piece::King, Piece::None, isCapture));
    });

    if (!onlyTactical && (currentSideCastlingRights & CastlingRights_All))
    {
        const Bitboard opponentAttacks = GetAttackedSquares(GetOppositeColor(mSideToMove));

        // TODO simplify this
        const Bitboard longCastleKingCrossedSquares = (1ull << (kingSquareIndex - 1)) | (1ull << (kingSquareIndex - 2));
        const Bitboard shortCastleKingCrossedSquares = (1ull << (kingSquareIndex + 1)) | (1ull << (kingSquareIndex + 2));
        const Bitboard longCastleCrossedSquares = longCastleKingCrossedSquares | Bitboard(1ull << (kingSquareIndex - 3));
        const Bitboard shortCastleCrossedSquares = shortCastleKingCrossedSquares;

        // king can't be in check
        if ((currentSide.king & opponentAttacks) == 0u)
        {
            if ((currentSideCastlingRights & CastlingRights_LongCastleAllowed) &&
                ((occupiedSquares & longCastleCrossedSquares) == 0u) &&
                ((opponentAttacks & longCastleKingCrossedSquares) == 0u))
            {
                // TODO Chess960 support?
                outMoveList.Push(Move::Make(kingSquare, Square(2u, kingSquare.Rank()), Piece::King, Piece::None, false, false, true));
            }

            if ((currentSideCastlingRights & CastlingRights_ShortCastleAllowed) &&
                ((occupiedSquares & shortCastleCrossedSquares) == 0u) &&
                ((opponentAttacks & shortCastleKingCrossedSquares) == 0u))
            {
                // TODO Chess960 support?
                outMoveList.Push(Move::Make(kingSquare, Square(6u, kingSquare.Rank()), Piece::King, Piece::None, false, false, true));
            }
        }
    }
}

const Bitboard Position::GetAttackers(const Square square) const
{
    const Bitboard pawns    = Whites().pawns | Blacks().pawns;
    const Bitboard knights  = Whites().knights | Blacks().knights;
    const Bitboard bishops  = Whites().bishops | Blacks().bishops;
    const Bitboard rooks    = Whites().rooks | Blacks().rooks;
    const Bitboard queens   = Whites().queens | Blacks().queens;
    const Bitboard kings    = Whites().king | Blacks().king;
    const Bitboard occupiedSquares = pawns | knights | bishops | rooks | queens | kings;

    Bitboard bitboard = Bitboard::GetKingAttacks(square) & kings;

    if (knights)
    {
        bitboard |= Bitboard::GetKnightAttacks(square) & knights;
    }

    if (rooks | queens)
    {
        bitboard |= Bitboard::GenerateRookAttacks(square, occupiedSquares) & (rooks | queens);
    }

    if (bishops | queens)
    {
        bitboard |= Bitboard::GenerateBishopAttacks(square, occupiedSquares) & (bishops | queens);
    }

    if (Whites().pawns)
    {
        bitboard |= Bitboard::GetPawnAttacks(square, Color::Black) & Whites().pawns;
    }

    if (Blacks().pawns)
    {
        bitboard |= Bitboard::GetPawnAttacks(square, Color::White) & Blacks().pawns;
    }

    return bitboard;
}

const Bitboard Position::GetAttackers(const Square square, const Color sideColor) const
{
    const SidePosition& side = mColors[(uint8_t)sideColor];
    const Bitboard occupiedSquares = Whites().Occupied() | Blacks().Occupied();

    Bitboard bitboard = Bitboard::GetKingAttacks(square) & side.king;

    if (side.knights)
    {
        bitboard |= Bitboard::GetKnightAttacks(square) & side.knights;
    }

    if (side.rooks | side.queens)
    {
        bitboard |= Bitboard::GenerateRookAttacks(square, occupiedSquares) & (side.rooks | side.queens);
    }

    if (side.bishops | side.queens)
    {
        bitboard |= Bitboard::GenerateBishopAttacks(square, occupiedSquares) & (side.bishops | side.queens);
    }

    if (side.pawns)
    {
        bitboard |= Bitboard::GetPawnAttacks(square, GetOppositeColor(sideColor)) & side.pawns;
    }

    return bitboard;
}

NO_INLINE
bool Position::IsSquareVisible(const Square square, const Color sideColor) const
{
    const SidePosition& side = mColors[(uint8_t)sideColor];
    const Bitboard occupiedSquares = Whites().Occupied() | Blacks().Occupied();

    if (Bitboard::GetKingAttacks(square) & side.king)   return true;

    if (side.knights)
    {
        if (Bitboard::GetKnightAttacks(square) & side.knights) return true;
    }

    if (side.pawns)
    {
        if (Bitboard::GetPawnAttacks(square, GetOppositeColor(sideColor)) & side.pawns) return true;
    }

    if (side.bishops | side.queens)
    {
        if (Bitboard::GenerateBishopAttacks(square, occupiedSquares) & (side.bishops | side.queens)) return true;
    }

    if (side.rooks | side.queens)
    {
        if (Bitboard::GenerateRookAttacks(square, occupiedSquares) & (side.rooks | side.queens)) return true;
    }

    return false;
}

bool Position::IsInCheck(Color sideColor) const
{
    const SidePosition& currentSide = mColors[(uint8_t)sideColor];

    const uint32_t kingSquareIndex = FirstBitSet(currentSide.king);
    return IsSquareVisible(Square(kingSquareIndex), GetOppositeColor(sideColor));
}

uint32_t Position::GetNumLegalMoves(std::vector<Move>* outMoves) const
{
    MoveList moves;
    GenerateMoveList(moves);

    if (moves.numMoves == 0)
    {
        return 0;
    }

    uint32_t numLegalMoves = 0u;
    for (uint32_t i = 0; i < moves.Size(); ++i)
    {
        const Move move = moves[i].move;
        ASSERT(move.IsValid());

        Position childPosition = *this;
        if (childPosition.DoMove(move))
        {
            numLegalMoves++;

            if (outMoves)
            {
                outMoves->push_back(move);
            }
        }
    }

    return numLegalMoves;
}

bool Position::IsMate() const
{
    return GetNumLegalMoves() == 0u && IsInCheck(mSideToMove);
}

bool Position::IsStalemate() const
{
    return GetNumLegalMoves() == 0u && !IsInCheck(mSideToMove);
}

bool Position::IsMoveLegal(const Move& move) const
{
    ASSERT(IsMoveValid(move));

    Position positionAfterMove{ *this };
    return positionAfterMove.DoMove(move);
}

static Square ExtractEnPassantSquareFromMove(const Move& move)
{
    ASSERT(move.GetPiece() == Piece::Pawn);

    if (move.FromSquare().Rank() == 1u && move.ToSquare().Rank() == 3u)
    {
        ASSERT(move.FromSquare().File() == move.ToSquare().File());
        return Square(move.FromSquare().File(), 2u);
    }

    if (move.FromSquare().Rank() == 6u && move.ToSquare().Rank() == 4u)
    {
        ASSERT(move.FromSquare().File() == move.ToSquare().File());
        return Square(move.FromSquare().File(), 5u);
    }

    return Square::Invalid();
}

void Position::ClearRookCastlingRights(const Square affectedSquare)
{
    switch (affectedSquare.mIndex)
    {
    case Square_h1:
        if (mWhitesCastlingRights & CastlingRights_ShortCastleAllowed) mHash ^= s_CastlingRightsHash[0][0];
        mWhitesCastlingRights = CastlingRights(mWhitesCastlingRights & ~CastlingRights_ShortCastleAllowed);
        break;
    case Square_a1:
        if (mWhitesCastlingRights & CastlingRights_LongCastleAllowed) mHash ^= s_CastlingRightsHash[0][1];
        mWhitesCastlingRights = CastlingRights(mWhitesCastlingRights & ~CastlingRights_LongCastleAllowed);
        break;
    case Square_h8:
        if (mBlacksCastlingRights & CastlingRights_ShortCastleAllowed) mHash ^= s_CastlingRightsHash[1][0];
        mBlacksCastlingRights = CastlingRights(mBlacksCastlingRights & ~CastlingRights_ShortCastleAllowed);
        break;
    case Square_a8:
        if (mBlacksCastlingRights & CastlingRights_LongCastleAllowed) mHash ^= s_CastlingRightsHash[1][1];
        mBlacksCastlingRights = CastlingRights(mBlacksCastlingRights & ~CastlingRights_LongCastleAllowed);
        break;
    };
}

bool Position::DoMove(const Move& move)
{
    ASSERT(IsMoveValid(move));  // move must be valid
    ASSERT(IsValid());          // board position must be valid

    SidePosition& opponentSide = GetOpponentSide();

    // move piece
    RemovePiece(move.FromSquare(), move.GetPiece(), mSideToMove);

    if (move.IsCapture())
    {
        if (!move.IsEnPassant())
        {
            const Piece capturedPiece = opponentSide.GetPieceAtSquare(move.ToSquare());
            RemovePiece(move.ToSquare(), capturedPiece, GetOppositeColor(mSideToMove));
        }

        // clear specific castling right after capturing a rook
        ClearRookCastlingRights(move.ToSquare());
    }

    // move piece
    const bool isPromotion = move.GetPiece() == Piece::Pawn && move.GetPromoteTo() != Piece::None;
    SetPiece(move.ToSquare(), isPromotion ? move.GetPromoteTo() : move.GetPiece(), mSideToMove);

    if (move.IsEnPassant())
    {
        Square captureSquare = Square::Invalid();
        if (move.ToSquare().Rank() == 5)  captureSquare = Square(move.ToSquare().File(), 4u);
        if (move.ToSquare().Rank() == 2)  captureSquare = Square(move.ToSquare().File(), 3u);
        ASSERT(captureSquare.IsValid());

        RemovePiece(captureSquare, Piece::Pawn, GetOppositeColor(mSideToMove));
    }

    SetEnPassantSquare(move.GetPiece() == Piece::Pawn ? ExtractEnPassantSquareFromMove(move) : Square::Invalid());

    if (move.GetPiece() == Piece::King)
    {
        if (move.IsCastling())
        {
            ASSERT(move.FromSquare().Rank() == 0 || move.FromSquare().Rank() == 7);
            ASSERT(move.FromSquare().Rank() == move.ToSquare().Rank());

            Square oldRookSquare, newRookSquare;

            // short castle
            if (move.FromSquare().File() == 4u && move.ToSquare().File() == 6u)
            {
                oldRookSquare = Square(7u, move.FromSquare().Rank());
                newRookSquare = Square(5u, move.FromSquare().Rank());
            }
            // long castle
            else if (move.FromSquare().File() == 4u && move.ToSquare().File() == 2u)
            {
                oldRookSquare = Square(0u, move.FromSquare().Rank());
                newRookSquare = Square(3u, move.FromSquare().Rank());
            }
            else // invalid castle
            {
                ASSERT(false);
            }

            RemovePiece(oldRookSquare, Piece::Rook, mSideToMove);
            SetPiece(newRookSquare, Piece::Rook, mSideToMove);
        }

        // clear all castling rights after moving a king
        CastlingRights& currentSideCastlingRights = (mSideToMove == Color::White) ? mWhitesCastlingRights : mBlacksCastlingRights;
        if (currentSideCastlingRights & CastlingRights_ShortCastleAllowed)  mHash ^= s_CastlingRightsHash[(uint32_t)mSideToMove][0];
        if (currentSideCastlingRights & CastlingRights_LongCastleAllowed)   mHash ^= s_CastlingRightsHash[(uint32_t)mSideToMove][1];
        currentSideCastlingRights = CastlingRights(0);
    }

    // clear specific castling right after moving a rook
    if (move.GetPiece() == Piece::Rook)
    {
        ClearRookCastlingRights(move.FromSquare());
    }

    if (mSideToMove == Color::Black)
    {
        mMoveCount++;
    }

    if (move.GetPiece() == Piece::Pawn || move.IsCapture())
    {
        mHalfMoveCount = 0;
    }
    else
    {
        mHalfMoveCount++;
    }

    const Color prevToMove = mSideToMove;

    mSideToMove = GetOppositeColor(mSideToMove);
    mHash ^= s_BlackToMoveHash;

    ASSERT(IsValid());  // board position after the move must be valid

    // validate hash
    ASSERT(ComputeHash() == GetHash());

    // can't be in check after move
    return !IsInCheck(prevToMove);
}

bool Position::DoNullMove()
{
    ASSERT(IsValid());          // board position must be valid
    ASSERT(!IsInCheck(mSideToMove));

    SetEnPassantSquare(Square::Invalid());

    if (mSideToMove == Color::Black)
    {
        mMoveCount++;
    }

    mHalfMoveCount++;

    mSideToMove = GetOppositeColor(mSideToMove);
    mHash ^= s_BlackToMoveHash;

    ASSERT(IsValid());  // board position after the move must be valid

    // validate hash
    ASSERT(ComputeHash() == GetHash());

    return true;
}

Position Position::SwappedColors() const
{
    Position result;
    result.mColors[0].king     = mColors[1].king.MirroredVertically();
    result.mColors[0].queens   = mColors[1].queens.MirroredVertically();
    result.mColors[0].rooks    = mColors[1].rooks.MirroredVertically();
    result.mColors[0].bishops  = mColors[1].bishops.MirroredVertically();
    result.mColors[0].knights  = mColors[1].knights.MirroredVertically();
    result.mColors[0].pawns    = mColors[1].pawns.MirroredVertically();
    result.mColors[1].king     = mColors[0].king.MirroredVertically();
    result.mColors[1].queens   = mColors[0].queens.MirroredVertically();
    result.mColors[1].rooks    = mColors[0].rooks.MirroredVertically();
    result.mColors[1].bishops  = mColors[0].bishops.MirroredVertically();
    result.mColors[1].knights  = mColors[0].knights.MirroredVertically();
    result.mColors[1].pawns    = mColors[0].pawns.MirroredVertically();
    result.mSideToMove = GetOppositeColor(mSideToMove);
    result.mMoveCount = mMoveCount;
    result.mHalfMoveCount = mHalfMoveCount;
    result.mHash = result.ComputeHash();
    return result;
}

void Position::MirrorVertically()
{
    mColors[0].king     = mColors[0].king.MirroredVertically();
    mColors[0].queens   = mColors[0].queens.MirroredVertically();
    mColors[0].rooks    = mColors[0].rooks.MirroredVertically();
    mColors[0].bishops  = mColors[0].bishops.MirroredVertically();
    mColors[0].knights  = mColors[0].knights.MirroredVertically();
    mColors[0].pawns    = mColors[0].pawns.MirroredVertically();

    mColors[1].king     = mColors[1].king.MirroredVertically();
    mColors[1].queens   = mColors[1].queens.MirroredVertically();
    mColors[1].rooks    = mColors[1].rooks.MirroredVertically();
    mColors[1].bishops  = mColors[1].bishops.MirroredVertically();
    mColors[1].knights  = mColors[1].knights.MirroredVertically();
    mColors[1].pawns    = mColors[1].pawns.MirroredVertically();

    mHash = ComputeHash();
}

void Position::MirrorHorizontally()
{
    mColors[0].king     = mColors[0].king.MirroredHorizontally();
    mColors[0].queens   = mColors[0].queens.MirroredHorizontally();
    mColors[0].rooks    = mColors[0].rooks.MirroredHorizontally();
    mColors[0].bishops  = mColors[0].bishops.MirroredHorizontally();
    mColors[0].knights  = mColors[0].knights.MirroredHorizontally();
    mColors[0].pawns    = mColors[0].pawns.MirroredHorizontally();

    mColors[1].king     = mColors[1].king.MirroredHorizontally();
    mColors[1].queens   = mColors[1].queens.MirroredHorizontally();
    mColors[1].rooks    = mColors[1].rooks.MirroredHorizontally();
    mColors[1].bishops  = mColors[1].bishops.MirroredHorizontally();
    mColors[1].knights  = mColors[1].knights.MirroredHorizontally();
    mColors[1].pawns    = mColors[1].pawns.MirroredHorizontally();

    mHash = ComputeHash();
}

bool Position::HasNonPawnMaterial(Color color) const
{
    return
        mColors[(uint32_t)color].queens     != 0 ||
        mColors[(uint32_t)color].rooks      != 0 ||
        mColors[(uint32_t)color].bishops    != 0 ||
        mColors[(uint32_t)color].knights    != 0;
}

const MaterialKey Position::GetMaterialKey() const
{
    MaterialKey key;

    key.numWhiteQueens = mColors[0].queens.Count();
    key.numWhiteRooks = mColors[0].rooks.Count();
    key.numWhiteBishops = mColors[0].bishops.Count();
    key.numWhiteKnights = mColors[0].knights.Count();
    key.numWhitePawns = mColors[0].pawns.Count();

    key.numBlackQueens = mColors[1].queens.Count();
    key.numBlackRooks = mColors[1].rooks.Count();
    key.numBlackBishops = mColors[1].bishops.Count();
    key.numBlackKnights = mColors[1].knights.Count();
    key.numBlackPawns = mColors[1].pawns.Count();

    return key;
}

/*
uint32_t Position::ComputeNetworkInputs() const
{
    uint32_t inputs = 0;

    if (key.numWhitePawns > 0 || key.numBlackPawns > 0)
    {
        // has pawns, so can't exploit vertical/diagonal symmetry
        inputs += 32; // white king on left files
        inputs += 64; // black king on any file
    }
    else
    {
        // pawnless position, can exploit vertical/horizonal/diagonal symmetry
        inputs += 16; // white king on files A-D, ranks 1-4
        inputs += 36; // black king on bottom-right triangle (a1, b1, b2, c1, c2, c3, ...)
    }

    // knights/bishops/rooks/queens on any square
    if (key.numWhiteQueens)     inputs += 64;
    if (key.numBlackQueens)     inputs += 64;
    if (key.numWhiteRooks)      inputs += 64;
    if (key.numBlackRooks)      inputs += 64;
    if (key.numWhiteBishops)    inputs += 64;
    if (key.numBlackBishops)    inputs += 64;
    if (key.numWhiteKnights)    inputs += 64;
    if (key.numBlackKnights)    inputs += 64;

    // pawns on ranks 2-7
    if (key.numWhitePawns)      inputs += 48;
    if (key.numBlackPawns)      inputs += 48;

    return inputs;
}
*/

uint32_t Position::ToFeaturesVector(uint32_t* outFeatures) const
{
    Square whiteKingSquare = Square(FirstBitSet(Whites().king));
    Square blackKingSquare = Square(FirstBitSet(Blacks().king));

    Bitboard whiteQueens = Whites().queens;
    Bitboard blackQueens = Blacks().queens;
    Bitboard whiteRooks = Whites().rooks;
    Bitboard blackRooks = Blacks().rooks;
    Bitboard whiteBishops = Whites().bishops;
    Bitboard blackBishops = Blacks().bishops;
    Bitboard whiteKnights = Whites().knights;
    Bitboard blackKnights = Blacks().knights;
    Bitboard whitePawns = Whites().pawns;
    Bitboard blackPawns = Blacks().pawns;

    if (whiteKingSquare.File() >= 4)
    {
        whiteKingSquare = whiteKingSquare.FlippedFile();
        blackKingSquare = blackKingSquare.FlippedFile();

        whiteQueens = whiteQueens.MirroredHorizontally();
        blackQueens = blackQueens.MirroredHorizontally();

        whiteRooks = whiteRooks.MirroredHorizontally();
        blackRooks = blackRooks.MirroredHorizontally();

        whiteBishops = whiteBishops.MirroredHorizontally();
        blackBishops = blackBishops.MirroredHorizontally();

        whiteKnights = whiteKnights.MirroredHorizontally();
        blackKnights = blackKnights.MirroredHorizontally();

        whitePawns = whitePawns.MirroredHorizontally();
        blackPawns = blackPawns.MirroredHorizontally();
    }

    uint32_t numFeatures = 0;
    uint32_t inputOffset = 0;

    // white king
    {
        const uint32_t whiteKingIndex = 4 * whiteKingSquare.Rank() + whiteKingSquare.File();
        outFeatures[numFeatures++] = whiteKingIndex;
        inputOffset += 32;
    }

    // black king
    {
        outFeatures[numFeatures++] = inputOffset + blackKingSquare.Index();
        inputOffset += 64;
    }

    if (whiteQueens)
    {
        for (uint32_t i = 0; i < 64u; ++i)
        {
            if ((whiteQueens >> i) & 1) outFeatures[numFeatures++] = inputOffset + i;
        }
        inputOffset += 64;
    }

    if (blackQueens)
    {
        for (uint32_t i = 0; i < 64u; ++i)
        {
            if ((blackQueens >> i) & 1) outFeatures[numFeatures++] = inputOffset + i;
        }
        inputOffset += 64;
    }

    if (whiteRooks)
    {
        for (uint32_t i = 0; i < 64u; ++i)
        {
            if ((whiteRooks >> i) & 1) outFeatures[numFeatures++] = inputOffset + i;
        }
        inputOffset += 64;
    }

    if (blackRooks)
    {
        for (uint32_t i = 0; i < 64u; ++i)
        {
            if ((blackRooks >> i) & 1) outFeatures[numFeatures++] = inputOffset + i;
        }
        inputOffset += 64;
    }

    if (whiteBishops)
    {
        for (uint32_t i = 0; i < 64u; ++i)
        {
            if ((whiteBishops >> i) & 1) outFeatures[numFeatures++] = inputOffset + i;
        }
        inputOffset += 64;
    }

    if (blackBishops)
    {
        for (uint32_t i = 0; i < 64u; ++i)
        {
            if ((blackBishops >> i) & 1) outFeatures[numFeatures++] = inputOffset + i;
        }
        inputOffset += 64;
    }

    if (whiteKnights)
    {
        for (uint32_t i = 0; i < 64u; ++i)
        {
            if ((whiteKnights >> i) & 1) outFeatures[numFeatures++] = inputOffset + i;
        }
        inputOffset += 64;
    }

    if (blackKnights)
    {
        for (uint32_t i = 0; i < 64u; ++i)
        {
            if ((blackKnights >> i) & 1) outFeatures[numFeatures++] = inputOffset + i;
        }
        inputOffset += 64;
    }

    if (whitePawns)
    {
        for (uint32_t i = 0; i < 48u; ++i)
        {
            const uint32_t squreIndex = i + 8u;
            if ((whitePawns >> squreIndex) & 1) outFeatures[numFeatures++] = inputOffset + i;
        }
        inputOffset += 48;
    }

    if (blackPawns)
    {
        for (uint32_t i = 0; i < 48u; ++i)
        {
            const uint32_t squreIndex = i + 8u;
            if ((blackPawns >> squreIndex) & 1) outFeatures[numFeatures++] = inputOffset + i;
        }
        inputOffset += 48;
    }

    return numFeatures;
}

static const int32_t pawnValue = 100;
static const int32_t knightValue = 300;
static const int32_t bishopValue = 300;
static const int32_t rookValue = 500;
static const int32_t queenValue = 900;
static const int32_t kingValue = INT32_MAX;

int32_t Position::BestPossibleMoveValue() const
{
    int32_t value = 0;

    const SidePosition& side = GetOpponentSide();

    // can capture most valuable piece
         if (side.queens)   value = queenValue;
    else if (side.rooks)    value = rookValue;
    else if (side.knights)  value = knightValue;
    else if (side.bishops)  value = bishopValue;
    else if (side.pawns)    value = pawnValue;

    // can promote to queen
    if (GetCurrentSide().pawns & (mSideToMove == Color::White ? Bitboard::RankBitboard<6>() : Bitboard::RankBitboard<1>()))
    {
        value += queenValue - pawnValue;
    }

    return value;
}

bool Position::StaticExchangeEvaluation(const Move& move, int32_t treshold) const
{
    const Square toSquare = move.ToSquare();
    const Square fromSquare = move.FromSquare();

    const int32_t c_seePieceValues[] =
    {
        0, // none
        pawnValue,
        knightValue,
        bishopValue,
        rookValue,
        queenValue,
        kingValue,
    };

    Bitboard occupied = Whites().Occupied() | Blacks().Occupied();
    Bitboard allAttackers = GetAttackers(toSquare);

    int32_t balance = 0;

    {
        const Piece capturedPiece = GetOpponentSide().GetPieceAtSquare(toSquare);
        balance = c_seePieceValues[(uint32_t)capturedPiece] - treshold;
        if (balance < 0) return false;
    }

    {
        const Piece movedPiece = GetCurrentSide().GetPieceAtSquare(fromSquare);
        balance = c_seePieceValues[(uint32_t)movedPiece] - balance;
        if (balance <= 0) return true;
    }

    // "do" move
    occupied &= ~fromSquare.GetBitboard();
    occupied |= toSquare.GetBitboard();

    Color sideToMove = mSideToMove;
    int32_t result = 1;

    for (;;)
    {
        sideToMove = GetOppositeColor(sideToMove);
        allAttackers &= occupied;

        const SidePosition& side = mColors[(uint8_t)sideToMove];
        const Bitboard ourAttackers = allAttackers & side.Occupied();

        // no more attackers - side to move loses
        if (ourAttackers == 0) break;

        result ^= 1;

        // TODO filter out pinned pieces

        Bitboard pieceBitboard;

        if (pieceBitboard = ourAttackers & side.pawns)
        {
            // remove attacker from occupied squares
            const uint32_t attackerSquare = FirstBitSet(pieceBitboard.value);
            const Bitboard mask = 1ull << attackerSquare;
            ASSERT((occupied & mask) != 0);
            occupied ^= mask;

            // update diagonal attackers
            allAttackers |= Bitboard::GenerateBishopAttacks(move.ToSquare(), occupied) & (Whites().bishops | Blacks().bishops | Whites().queens | Blacks().queens);

            balance = pawnValue - balance;
            if (balance < result) break;
        }
        else if (pieceBitboard = ourAttackers & side.knights)
        {
            // remove attacker from occupied squares
            const uint32_t attackerSquare = FirstBitSet(pieceBitboard.value);
            const Bitboard mask = 1ull << attackerSquare;
            ASSERT((occupied & mask) != 0);
            occupied ^= mask;

            balance = knightValue - balance;
            if (balance < result) break;
        }
        else if (pieceBitboard = ourAttackers & side.bishops)
        {
            // remove attacker from occupied squares
            const uint32_t attackerSquare = FirstBitSet(pieceBitboard.value);
            const Bitboard mask = 1ull << attackerSquare;
            ASSERT((occupied & mask) != 0);
            occupied ^= mask;

            // update diagonal attackers
            allAttackers |= Bitboard::GenerateBishopAttacks(toSquare, occupied) & (Whites().bishops | Blacks().bishops | Whites().queens | Blacks().queens);

            balance = bishopValue - balance;
            if (balance < result) break;
        }
        else if (pieceBitboard = ourAttackers & side.rooks)
        {
            // remove attacker from occupied squares
            const uint32_t attackerSquare = FirstBitSet(pieceBitboard.value);
            const Bitboard mask = 1ull << attackerSquare;
            ASSERT((occupied & mask) != 0);
            occupied ^= mask;

            // update hirozontal/vertical attackers
            allAttackers |= Bitboard::GenerateRookAttacks(toSquare, occupied) & (Whites().rooks | Blacks().rooks | Whites().queens | Blacks().queens);

            balance = rookValue - balance;
            if (balance < result) break;
        }
        else if (pieceBitboard = ourAttackers & side.queens)
        {
            // remove attacker from occupied squares
            const uint32_t attackerSquare = FirstBitSet(pieceBitboard.value);
            const Bitboard mask = 1ull << attackerSquare;
            ASSERT((occupied & mask) != 0);
            occupied ^= mask;

            // update hirozontal/vertical/diagonal attackers
            allAttackers |= Bitboard::GenerateBishopAttacks(toSquare, occupied) & (Whites().bishops | Blacks().bishops | Whites().queens | Blacks().queens);
            allAttackers |= Bitboard::GenerateRookAttacks(toSquare, occupied) & (Whites().rooks | Blacks().rooks | Whites().queens | Blacks().queens);

            balance = rookValue - balance;
            if (balance < result) break;
        }
        else // king
        {
            if (allAttackers & GetOpponentSide().Occupied())
            {
                result ^= 1;
            }

            break;
        }

        /*
        // find weakest attacker
        for (uint32_t pieceType = 1; pieceType <= 6u; ++pieceType)
        {
            const Bitboard pieceBitboard = ourAttackers & side.GetPieceBitBoard((Piece)pieceType);
            if (Bitboard pieceBitboard = ourAttackers & side.pawns)
            {
                uint32_t attackerSquare = UINT32_MAX;
                pieceBitboard.BitScanForward(attackerSquare);
                ASSERT(attackerSquare != UINT32_MAX);

                // remove attacker from occupied squares
                const Bitboard mask = 1ull << attackerSquare;
                ASSERT((occupied & mask) != 0);
                occupied ^= mask;

                // TODO update diagonal/vertical attackers

                balance = c_seePieceValues[pieceType] - balance;
                if (balance < result) return result != 0;

                break;
            }
        }
        */
    }

    return result != 0;
}