////////////////////////////////////////////////////////////////////////////////////////////////////
//
// PLAY CHESS ENGINE
//
// Author: Stewart Tunbridge, Pi Micros
// Email:  stewarttunbridge@gmail.com
// Copyright (c) 2024, 2025 Stewart Tunbridge, Pi Micros
//
////////////////////////////////////////////////////////////////////////////////////////////////////


typedef int _Coord;   // x << 3 | y

#define BI(x,y)  (x*8+y)    // Board Index

typedef enum {pEmpty, pKing, pQueen, pRook, pBishop, pKnight, pPawn, pWhite = 0x08, pChecked = 0x10, pPawn2 = 0x20, pMoveID = 0x100} _Piece;
  // Bits 0-2 specifies piece, Bit 3 => piece white, Bit 4 =>King in Check, Bit 5 => double move (Pawn only)
  // Bits 8..31 Move ID: 0 => never moved.  (needed for castling & en passant

typedef _Piece _Board [64] ;   // Board [x << 3 | y]
_Board Board;   // This is THE BOARD

int BoardTotalScore;   // Keeps a running total of the values

int MoveID;   // ID incremented every move

_Coord MoveForbidenFrom = {-1};
_Coord MoveForbidenTo = {-1};


typedef enum {aPiecesOnly, aMoves} _Analysis;

typedef struct
  {
    int DepthPlay = 4;
    _Analysis Analysis = aPiecesOnly; //aMoves;
    int AnalysisScorePiece = 1000;   // Value of a Pawn
    int PlaceWeighting = 0;   // Value added to Piece if in centre 4 squares
    int AnalysisScoreMove = 10;
    int AnalysisScoreAttack = 25;   // * Piece Value / 1000
    int AnalysisRandomize = 0;
  } _Engine;

int PlaceWeightingValues [64] =
  {
    0,0,0,0,0,0,0,0,
    0,1,1,1,1,1,1,0,
    0,1,2,2,2,2,1,0,
    0,1,2,3,3,2,1,0,
    0,1,2,3,3,2,1,0,
    0,1,2,2,2,2,1,0,
    0,1,1,1,1,1,1,0,
    0,0,0,0,0,0,0,0
  };

_Engine Engine;

bool BestMoveAbort = false;

#define DepthMax 10

#define Piece(p)       (_Piece) (p & (pWhite-1))
#define PieceWhite(p)  ((p&pWhite) != 0)
#define PieceFrom(BasePiece,White) ((_Piece)(White ? (BasePiece | pWhite) : BasePiece))

bool InCheck (bool PlayWhite);

unsigned int Rand;


////////////////////////////////////////////////////////////////////////////////////////////////////
//
// BoardInit: Initialize the Board
//

_Piece BoardStart [8] = {pRook, pKnight, pBishop, pQueen, pKing, pBishop, pKnight, pRook};

void BoardInit ()
  {
    int xy;
    //
    MoveID = 0;
    for (xy = 0; xy < 64; xy++)
      if ((xy & 7) == 0)   // y = 0
        Board [xy] = PieceFrom (BoardStart [xy >> 3], true);
      else if ((xy & 7) == 1)
        Board [xy] = PieceFrom (pPawn, true);
      else if ((xy & 7) == 6)
        Board [xy] = pPawn;
      else if ((xy & 7) == 7)
        Board [xy] = BoardStart [xy >> 3];
      else
        Board [xy] = pEmpty;
    BoardTotalScore = 0;
    Rand = time (NULL);   // Initialize random number generator
  }


////////////////////////////////////////////////////////////////////////////////////////////////////
//
// PieceMove: Return all possible legal moves for piece at From.
//
// Resuls given in To. One at a time.
// Return false when no legal moves left

#define End 99999

_Coord PieceMovesKing [] = {BI (2, 0), BI (-2, 0), BI (0, 1), BI (1, 1), BI (1, 0), BI (1, -1), BI (0, -1), BI (-1, -1), BI (-1, 0), BI (-1, 1), End};

_Coord PieceMovesAllDir [] = {{0*8+1}, {1*8+1}, {1*8+0}, {1*8-1}, {0*8-1}, {-1*8-1}, {-1*8+0}, {-1*8+1}, {End}};

_Coord PieceMovesRook [] = {{0*8+1}, {1*8+0}, {0*8-1}, {-1*8+0}, {End}};

_Coord PieceMovesBishop [] = {{1*8+1}, {1*8-1}, {-1*8-1}, {-1*8+1}, {End}};

_Coord PieceMovesKnight [] = {{1*8+2}, {2*8+1}, {2*8-1}, {1*8-2}, {-1*8-2}, {-2*8-1}, {-2*8+1}, {-1*8+2}, {End}};

_Coord PieceMovesPawn  [] = {{0*8+2}, {0*8+1}, {1*8+1}, {-1*8+1}, {End}};   // White Pawn
_Coord PieceMovesPawn_ [] = {{0*8-2}, {0*8-1}, {1*8-1}, {-1*8-1}, {End}};   // Black Pawn

//                          pEmpty, pKing,          pQueen,           pRook,          pBishop,          pKnight,          pPawn
int  PieceMovesRepeat [] = {false,  false,          true,             true,           true,             false,            false};
_Coord *PieceMoves  [] =    {NULL,   PieceMovesKing, PieceMovesAllDir, PieceMovesRook, PieceMovesBishop, PieceMovesKnight, PieceMovesPawn};
_Coord *PieceMoves_ [] =    {NULL,   PieceMovesKing, PieceMovesAllDir, PieceMovesRook, PieceMovesBishop, PieceMovesKnight, PieceMovesPawn_};

int PawnFirstY [] = {6, 1};
int FirstY [] = {7, 0};

void GetPieceMoves (_Coord From, _Coord *Res) //, _Analysis Analysis = aMoves)   // Analysis set for BoardScore
  {
    int Index;
    _Piece FromPce, ToPce, p_;
    _Coord *Moves;
    _Coord To, To_;
    bool Repeat;
    bool Bad, PieceTaken;
    bool NextDir;
    //
    FromPce = Board [From];
    if (PieceWhite (FromPce))
      Moves = PieceMoves [Piece (FromPce)];
    else
      Moves = PieceMoves_ [Piece (FromPce)];
    Repeat = PieceMovesRepeat [Piece (FromPce)];
    Index = 0;
    To = From;
    // Build list of all possible legal moves
    while (true)
      {
        if (!Moves)
          {
            DebugAdd ("****** Moves == NULL");
            break;
          }
        if (Moves [Index] == End)
          break;
        To_ = To;
        To += Moves [Index];
        Bad = false;
        PieceTaken = false;
        if ((To >= 64) || (To < 0))   // off the board
          Bad = true;
        else if (Abs ((To & 0b000111) - (To_ & 0b000111)) > 2)   // Wrapping around
          Bad = true;
        else if (From == MoveForbidenFrom && To == MoveForbidenTo)
          Bad = true;
        else
          {
            ToPce = (_Piece) Board [To];
            if (Piece (ToPce) != pEmpty)   // destination NOT empty AND is
              if (PieceWhite (ToPce) == PieceWhite (FromPce)) // && (Analysis != aDefend))   // my own colour
                Bad = true;
              else
                PieceTaken = true;   // takes oponents piece
            if (Piece (FromPce) == pPawn)   // Pawn has more limitations
              switch (Index)
                {
                  case 0: // straight, double step
                          if ((From & 0b000111) != PawnFirstY [PieceWhite (FromPce)])   // Only allow  from start row
                            Bad = true;
                          else if (Piece (Board [(From + To) / 2]) != pEmpty)   // Only if intermediate square is empty
                            Bad = true;
                          // continue into Index == 1
                  case 1: // straight, single step OR double step
                          if (Piece (ToPce) != pEmpty)
                            Bad = true;
                          break;
                  case 2: // diagonal attacks
                  case 3: //
                          if (Piece (ToPce) == pEmpty)   // Attacking empty square. En Passon?
                            {
                              // Find square behind your attack
                              if (PieceWhite (FromPce))
                                p_ = Board [To - 1];
                              else
                                p_ = Board [To + 1];
                              if ( (Piece (p_) != pPawn) ||   // - not opponents pawn
                                   (PieceWhite (p_) == PieceWhite (FromPce)) ||
                                   ((p_ & pPawn2) == 0) ||   // - not there by double move
                                   (p_ / pMoveID != MoveID) )   // - not the last move
                                Bad = true;   // not en passan
                            }
                }
            else if (Piece (FromPce) == pKing)
              if (Index < 2)   // Castling
                if ((FromPce / pMoveID > 0) || (FromPce & pChecked))   // moved OR been in check?
                  Bad = true;
                else
                  {
                    p_ = PieceFrom (pRook, PieceWhite (FromPce));   // corresponding Rook for the castle: pRook, Colour, no moves
                    if (Piece (Board [(From + To) / 2]) != pEmpty)   // passing square must be empty
                      Bad = true;
                    else if (Piece (Board [To]) != pEmpty)
                      Bad = true;
                    else
                      if (Index == 0)   // Kingside castle
                        {
                          if (Board [To | 0b111000] != p_)   // Right side edge must be Rook
                            Bad = true;
                        }
                      else   // Index == 1   Queenside castle
                        if (Board [To & 0b000111] != p_)   // Left side edge must be Rook
                          Bad = true;
                  }
          }
        if (Bad)   // no good, try next option
          {
            Index++;
            To = From;
          }
        else
          {
            *Res = To;
            Res++;
            NextDir = true;
            if (Repeat)
              {
                NextDir = false;
                if (PieceTaken)
                  {
                    NextDir = true;
                    /*if (Analysis == aExtend)
                      if (PieceWhite (FromPce) != PieceWhite (ToPce))
                        NextDir = false;*/
                  }
              }
            //if (!Repeat || PieceTaken)
            if (NextDir)
              {
                Index++;
                To = From;
              }
          }
      }
    *Res = -1;
  }


////////////////////////////////////////////////////////////////////////////////////////////////////
//
// BoardScore: Calculate Value of the Board for specified player
//
// 0 => Equal position both sides
// BoardScore (Player) = - BoardScore (!Player)

const int Mate = (100000000 - 16);   // This or higher => checkmate

#define iSqr(i) ((i)*(i))

int PieceValue [] = {0,      10000, 8800,   5100,  3330,    3200,    1000};
//                   pEmpty, pKing, pQueen, pRook, pBishop, pKnight, pPawn
//                   Hans Berliners system (based on experience and computer experiments):

inline int PieceValue_ (_Piece Pce, _Coord xy)
  {
    _Piece p;
    int Res;
    //
    p = Piece (Pce);
    Res = PieceValue [p];
    if (p == pPawn)   // Pawn: add 1/8 its value per step across Board
      {
        if (PieceWhite (Pce))
          Res = Res * (8 + (xy & 0b000111)) / 8;
        else
          Res = Res * (8 + (7 - (xy & 0b000111))) / 8;
      }
    else   // Non-Pawn: add value re Placement
      if (Engine.PlaceWeighting > 0)
        Res += PlaceWeightingValues [xy] * Engine.PlaceWeighting / 3;
    return Res;
  }

/* EXPERIMENT
// Try scoring for checking your own pieces
// Try taking turn into account
//   Code for hitting the king %%%%

int BoardScore (bool PlayWhite)
  {
    int Score, ds;
    byte Threatened [64];   // [x>>3 | y], Black 0x01 White 0x02
    int i;
    bool Safe;
    _Coord Moves [64], *m;
    _Piece p, p_;
    //
    // Set bits in Threatened
    memset (Threatened, 0, sizeof (Threatened));
    for (i = 0; i < 64; i++)
      {
        p = ((_Piece *) Board) [i];
        if (Piece (p) != pEmpty)
          {
            GetPieceMoves ({i >> 3, i & 7}, Moves, aMoves);
            m = Moves;
            while (m->x >= 0)
              {
                Threatened [(m->x << 3) | m->y] |= PieceWhite (p) ? 0x02 : 0x01;
                m++;
              }
          }
      }
    Score = 0;
    for (i = 0; i < 64; i++)
      {
        p = ((_Piece *) Board) [i];
        if (Piece (p) != pEmpty)
          {
            ds = PieceValue [Piece (p)];   // Score piece value
            GetPieceMoves ({i >> 3, i & 7}, Moves, aMoves);
            m = Moves;
            while (m->x >= 0)
              {
                Safe = ((Threatened [(m->x << 3) | m->y] & (PieceWhite (p) ? 0x01 : 0x02)) == 0);
                if (Safe)
                  ds += AnalysisScoreMove;
                p_ = Piece (Board [m->x][m->y]);
                if (p_ != pEmpty)   // taking a piece
                  {
                    ds += PieceValue [p_] * AnalysisScoreAttack / 1000;   // Score an attack
                    if (!Safe)
                      ds -= PieceValue [p] * AnalysisScoreAttack / 1000;   // Assume a piece swap
                  }
                m++;
              }
            if (PieceWhite (p) == PlayWhite)
              Score += ds;
            else
              Score -= ds;
          }
      }
    if (AnalysisRandomize)
      Score += rand () % (AnalysisRandomize + AnalysisRandomize + 1) - AnalysisRandomize;
    return Score;
  }
*/

int BoardScore (void)
  {
    int Score;
    _Coord From;
    _Piece p, p_;
    _Coord Moves [64], *m;
    int ds;
    //
    if (Engine.Analysis == aPiecesOnly)
      return BoardTotalScore;
    //
    Score = 0;
    for (From = 0; From < 64; From++)
      {
        p = (_Piece) Board [From];
        if (Piece (p) != pEmpty)
          {
            ds = PieceValue_ (p, From) * Engine.AnalysisScorePiece / 1000;   // Score piece value
            if (Engine.Analysis > aPiecesOnly)
              {
                // Add points for every available move
                GetPieceMoves (From, Moves); //, Engine.Analysis);
                m = Moves;
                while (*m >= 0)
                  {
                    // Score for each available move, more if attacking
                    ds += Engine.AnalysisScoreMove;   // Score for each available move into a new square
                    p_ = Piece (Board [*m]);
                    if (p_ != pEmpty)   // Add points for every Piece that you can attack/defend
                      ds += PieceValue_ (p_, *m) * Engine.AnalysisScoreAttack / 1000;   // Direct attack
                    m++;
                  }
              }
            if (PieceWhite (p)) // == PlayWhite)
              Score += ds;
            else
              Score -= ds;
          }
      }
    if (Score != BoardTotalScore)
      DebugAddP ("Score Mismatch", {BoardTotalScore, Score});
    return Score;
  }

void BoardTotalScoreRecalculate (void)   // call this when Board is changed outside of MovePiece
  {
    int xy;
    _Piece p;
    int v;
    //
    BoardTotalScore = 0;
    for (xy = 0; xy < 64; xy++)
      {
        p = Board [xy];
        if (Piece (p) != pEmpty)
          {
            v = PieceValue_ (Board [xy], xy) * Engine.AnalysisScorePiece / 1000;
            if (PieceWhite (p))
              BoardTotalScore += v;
            else
              BoardTotalScore -= v;
          }
      }
  }


////////////////////////////////////////////////////////////////////////////////////////////////////
//
// BestMove: Calculate best move recursively
//
// Returns Score of best move

typedef enum {smNone, smCrown, smCastle, smEnPassant} _SpecialMove;

int LastRow [] = {0, 7};
int PawnSkipRow [] = {5, 2};   // Row missed when pawns start with a double

inline void BoardTotalScoreAdjust (int xy, bool Add)
  {
    _Piece Pce = Board [xy];
    if (Piece (Pce) != pEmpty)
      if (PieceWhite (Pce) == Add)
        BoardTotalScore += PieceValue_ (Pce, xy);
      else
        BoardTotalScore -= PieceValue_ (Pce, xy);
  }

// Move a piece allowing for special moves

_SpecialMove MovePiece (_Coord From, _Coord To)
  {
    _SpecialMove Res;
    _Piece Pce, PceTaken;
    _Coord i;
    //
    Res = smNone;
    MoveID++;   // Next ID
    Pce = (_Piece) ((Board [From] & (pMoveID - 1 - pPawn2)) | (MoveID * pMoveID));   // Set new MoveID and clear Pawn double move flag
    BoardTotalScoreAdjust (From, false);
    Board [From] = pEmpty;
    PceTaken = (_Piece) Board [To];
    BoardTotalScoreAdjust (To, false);
    // Pawn special moves
    if (Piece (Pce) == pPawn)
      {
        // Check for double first move
        if (Abs ((From & 0b000111) - (To & 0b000111)) > 1)
          Pce = (_Piece) (Pce | pPawn2);   // set Pawn double move flag
        // Check for pawn reaching the far end of the board
        if ((To & 0b000111) == LastRow [PieceWhite (Pce)])   // Last row reached
          {
            Res = smCrown;
            Pce = (_Piece) (Pce - pPawn + pQueen);   // upgradde from Pawn to Queen
          }
        // Check for en-passant
        else if ((To & 0b000111) == PawnSkipRow [!PieceWhite (Pce)])   // Up to skip row of opponent
          if ((From & 0b111000) != (To & 0b111000))   // x changes => diagonal move
            if (Piece (PceTaken) == pEmpty)   // to an empty square. EN-PASSANT
              {
                Res = smEnPassant;
                // take piece (pawn) behind
                if (PieceWhite (Pce))
                  i = To - 1;
                else
                  i = To + 1;
                PceTaken = Board [i];
                BoardTotalScoreAdjust (i, false);
                Board [i] = pEmpty;
              }
      }
    // Check for Castling
    else if (Piece (Pce) == pKing)
      {
        if (To == From + 2*8)   // kingside castle
          {
            Res = smCastle;
            Board [5*8 + (To & 0b000111)] = Board [7*8 + (To & 0b000111)];   // jump Rook
            Board [7*8 + (To & 0b000111)] = pEmpty;
          }
        else if (To == From - 2*8)   // queenside castle
          {
            Res = smCastle;
            Board [3*8 + (To & 0b000111)] = Board [0*8 + (To & 0b000111)];   // jump Rook
            Board [0*8 + (To & 0b000111)] = pEmpty;
          }
      }
    Board [To] = Pce;   // Place the move
    BoardTotalScoreAdjust (To, true);
    return Res;
  }

// undo the above

void UnmovePiece (_Coord From, _Coord To, _Piece OldFrom, _Piece OldTo, _SpecialMove SpecialMove)
  {
    int xy;
    //
    Board [From] = OldFrom;
    BoardTotalScoreAdjust (From, true);
    BoardTotalScoreAdjust (To, false);
    Board [To] = OldTo;
    BoardTotalScoreAdjust (To, true);
    // Special moves
    MoveID--;   // Next ID
    if (SpecialMove == smCastle)   // return Rook
      {
        if ((To >> 3) == 6)   // kingside castle
          {
            Board [7*8 + (To & 0b000111)] = Board [5*8 + (To & 0b000111)];
            Board [5*8 + (To & 0b000111)] = pEmpty;
          }
        else   // queenside castle
          {
            Board [0*8 + (To & 0b000111)] = Board [3*8 + (To & 0b000111)];
            Board [3*8 + (To & 0b000111)] = pEmpty;
          }
      }
    else if (SpecialMove == smEnPassant)   // reinstate opponents pawn with the flags it would have had
      {
        xy = (To & 0b111000) + (From & 0b000111);
        Board [xy] = (_Piece) (PieceFrom (pPawn, !PieceWhite (OldFrom)) | pPawn2 | (MoveID * pMoveID));
        BoardTotalScoreAdjust (xy, true);
      }
  }

longint MovesConsidered;
_Coord BestA [DepthMax], BestB [DepthMax];
//_Coord BestA_ [DepthMax], BestB_ [DepthMax];   // copy best move sequence

int BestMove (bool PlayWhite, int Depth)
  {
    _Coord Moves [64], *m;
    _Piece p, p_;
    _Coord From;
    _SpecialMove sm;
    int Score;
    int BestScore;
    //
    BestScore = IntMin;
    if (!BestMoveAbort)
      // Go thru all my pieces and all their moves
      for (From = 0; From < 64; From++)   // for every square
        {
          p = Board [From];
          if ((Piece (p) != pEmpty) && (PieceWhite (p) == PlayWhite))   // if my piece
            {
              GetPieceMoves (From, Moves);
              m = Moves;
              while (*m >= 0)   // for all moves
                {
                  MovesConsidered++;
                  Rand = (Rand * 0xED563) ^ *m;
                  p_ = (_Piece) Board [*m];   // piece being taken (or Empty)
                  if (Piece (p_) == pKing)   // This would end in victory
                    Score = Mate + 16 - Depth;   // Score for a Mate
                  else
                    {
                      sm = MovePiece (From, *m);
                      if (Depth >= Engine.DepthPlay)   // Reached the limit of look-ahead
                        if (PlayWhite)
                          Score = BoardScore ();   // evaluate resulting board
                        else
                          Score = -BoardScore ();   // score for black play
                      else   // otherwise find the reply move
                        Score = -BestMove (!PlayWhite, Depth + 1);
                      UnmovePiece (From, *m, p, p_, sm);
                    }
                  if (Abs (Score) < Mate)
                    Score += Rand % (2 * Engine.AnalysisRandomize + 1) - Engine.AnalysisRandomize >= BestScore;
                  if (Score >= BestScore)
                    {
                      BestScore = Score;
                      BestA [Depth] = From;
                      BestB [Depth] = *m;
                    }
                  m++;
                }   // no more moves
            }
        }   // no more pieces
    if (BestScore <= -Mate)   // no moves available (without losing the king)
      if (!InCheck (PlayWhite))   // stalemate
        BestScore = PieceValue [pKing];   // was Mate. that makes this the best move
    return BestScore;
  }


////////////////////////////////////////////////////////////////////////////////////////////////////
//
// MoveValid: Returns true if move is legal
//

bool MoveValid (_Coord From, _Coord To)
  {
    _Piece p;
    _Coord Moves [64], *m;
    //
    p = (_Piece) Board [From];
    if (Piece (p) != pEmpty)
      {
        GetPieceMoves (From, Moves);
        m = Moves;
        while (*m >= 0)
          {
            if (*m == To)   // destination found
              return true;
            m++;
          }
      }
    return false;
  }

// Returns true if selected colour is in Check. Also sets / clears pChecked

bool InCheck (bool PlayWhite)
  {
    _Coord From, Moves [64], *m;
    _Piece p, p_;
    //
    for (From = 0; From < 64; From++)   // for every square
        {
          p = (_Piece) Board [From];
          if ((Piece (p) != pEmpty) && (PieceWhite (p) != PlayWhite))   // if not my piece
            {
              GetPieceMoves (From, Moves);
              m = Moves;
              while (*m >= 0)   // for all moves
                {
                  p_ = Board [*m];
                  if (Piece (p_) == pKing)   // and it can take my king
                    {
                      Board [*m] = (_Piece) (p_ | pChecked);
                      return true;
                    }
                  m++;
                }
            }
        }
    // Untag pChecked
    for (From = 0; From < 64; From++)   // for every square
      {
        p_ = Board [From];
        if (PieceWhite (p_) == PlayWhite)
          Board [From] = (_Piece) (p_ & ~pChecked);
      }
    return false;
  }
