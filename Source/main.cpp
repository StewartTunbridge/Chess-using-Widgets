///////////////////////////////////////////////////////////////////////////////////////////////////
//
// STEWARTS CHESS PROGRAMME
//
// Author: Stewart Tunbridge, Pi Micros
// Email:  stewarttunbridge@gmail.com
// Copyright (c) 2024,2026 Stewart Tunbridge, Pi Micros
//
//
// TODO:
// / Check: Undo when playing Black
// / Stats => [White/Black] Stats .... Score = ####{relative to the Stats subject}
// Castle: Still allows castle thru threatened square
//
///////////////////////////////////////////////////////////////////////////////////////////////////


const char Revision [] = "1.96";

#include <stdio.h>
#include <unistd.h>
#include <string.h>

#include "../Widgets/Widgets.hpp"
#include "../Widgets/WidgetsGrid.hpp"
#include "../Widgets/FileSelect.hpp"
#include "../Widgets/ColourSelect.hpp"
#include "../Widgets/MessageBox.hpp"
#include "../Widgets/MessageResponseBox.hpp"

#include "Chess.c"

/*void DebugAdd (const char* Message)
  {
    static int Time0 = ClockMS ();
    static int Time1 = Time0;
    char *s = (char *) malloc (StrLen (Message) + 32);
    char *ps = s;
    int t;
    //
    t = ClockMS ();
    IntToStrDecimals (&ps, t - Time0, 3);
    StrCat (&ps, " [");
    IntToStrDecimals (&ps, t - Time1, 3);
    Time1 = t;
    StrCat (&ps, "] ");
    StrCat (&ps, Message);
    *ps = 0;
    //
    puts (s);
    //Log ("Log", s);
    free (s);
  }*/

void CoordToStr (char **s, _Coord Pos)
  {
    *(*s)++ = (Pos >> 3) + 'a';
    *(*s)++ = (Pos & 0b000111) + '1';
  }

bool Refresh;
bool Exit;
bool Load;
bool Save;
bool SaveLog;

bool PlayThreadWhite;
bool PlayThreadStarted;
bool PlayThreadFinished;
int PlayThreadScore;
int PlayThreadTime;

int PlayThread (void *PlayWhite)
  {
    PlayThreadWhite = (bool) PlayWhite;
    PlayThreadStarted = true;
    PlayThreadFinished = false;
    MovesConsidered = 0;
    BestMoveAbort = false;
    PlayThreadTime = ClockMS ();
    PlayThreadScore = BestMove (PlayThreadWhite, 0);
    PlayThreadTime = ClockMS () - PlayThreadTime;
    PlayThreadFinished = true;
    PlayThreadStarted = false;
    return 0;
  }


////////////////////////////////////////////////////////////////////////////////////////////////////
//
// BOARD VISUAL COMPONENT
//
////////////////////////////////////////////////////////////////////////////////////////////////////

_DirItem *Fonts;   // Directory List of all Font files
int FontPiecesSelect = 0;   // Index into Fonts
char *FontPieces = nullptr;   // Points to Name in Fonts. This never owns the data
int FontPiecesMap = 0;   // Specifies Mapping table (below) to convert _Piece to "character" for FontPieces

// Maps _Piece to character. Multiple mapps apply
int BlackPieceToUChar [3][8] = {
                                 {' ', 'l', 'w', 't', 'n', 'j', 'o'},   // Common map
                                 {' ', 'l', 'w', 't', 'v', 'm', 'o'},   // another "
                                 {' ', 0x265a, 0x265b, 0x265c, 0x265d, 0x265e, 0x265f}   // Unicode standard chess pieces
                               };
int WhitePieceToUChar [3][8] = {
                                 {' ', 'k', 'q', 'r', 'b', 'h', 'p'},
                                 {' ', 'k', 'q', 'r', 'b', 'n', 'p'},
                                 {' ', 0x2654, 0x2655, 0x2656, 0x2657, 0x2658, 0x2659}
                               };

void PieceToChar (char **Dest, _Piece Pce)
  {
    if (PieceWhite (Pce))
      UTF32ToStr (Dest, WhitePieceToUChar [FontPiecesMap][Piece (Pce)]);
    else
      UTF32ToStr (Dest, BlackPieceToUChar [FontPiecesMap][Piece (Pce)]);
  }

void AllPieces (char *Res)
  {
    int p;
    char *r;
    //
    r = Res;
    PieceToChar (&r, PieceFrom (pEmpty, false));
    StrCat (&r, '\t');
    for (p = pEmpty + 1; p <= pPawn; p++)
      {
        PieceToChar (&r, PieceFrom (p, false));
        StrCat (&r, '\t');
      }
    for (p = pEmpty + 1; p <= pPawn; p++)
      {
        PieceToChar (&r, PieceFrom (p, true));
        StrCat (&r, '\t');
      }
    *r = 0;
  }

bool FontsCallBack (_DirItem *Item)
  {
    if (!Item->Directory)
      if (StrPos (Item->Name, ".ttf"))
        return true;
    return false;
  }

int FontsRead (void)
  {
    DirRead (NULL, &Fonts, FontsCallBack);
    Fonts = ListSort (Fonts, DirItemsCompare);
    if (Fonts)
      FontPieces = Fonts->Name;
    return ListLength (Fonts);
  }

class _ChessBoard: public _Container
  {
    public:
      bool Rotate;
      int Colours [3];   // Odd, Even, Pieces
      bool Filled;
      bool MoveStart, MoveComplete;
      bool MoveWhite;
      _Coord Move [2][2];   // [Colour]  [From/To]
      _Coord SquareSpecial;
      bool LegalMovesShow;
      bool LegalMoves [64];
      //
      _ChessBoard (_Container *Parent, _Rect Rect);
      void DrawCustom (void);
      bool ProcessEventCustom (_Event *Event, _Point Offset);
      void TranslateCoord (_Coord *Pos);
      void BoardSquare (_Coord Pos, _Rect *Res);
      _Point BoardSquareCenter (_Coord Pos);
      void DrawVector (_Coord Move [], int Colour);
  };

_ChessBoard::_ChessBoard (_Container *Parent, _Rect Rect) : _Container (Parent, Rect)
  {
    Rotate = false;
    Colours [0] = ColourAdjust (cBrown, 140);
    Colours [1] = ColourAdjust (cBrown, 180);
    Colours [2] = cBlack;
    Filled = false;
    MoveStart = MoveComplete = false;
    MoveWhite = false;
    for (int c = 0; c < 2; c++)
      Move [c][0] = Move [c][1] = -1;
    SquareSpecial = -1;
    LegalMovesShow = true;
    memset (LegalMoves, false, sizeof (LegalMoves));
  }

const int cGreenGray = 0x9cf09c;
const int cBlueGray = 0xffaaaa;

void _ChessBoard::TranslateCoord (_Coord *Pos)
  {
    if (Rotate)
      *Pos = 63 - *Pos;
  }

void _ChessBoard::BoardSquare (_Coord Pos, _Rect *Res)
  {
    int dx = Rect.Width / 8;
    int dy = Rect.Height / 8;
    TranslateCoord (&Pos);
    *Res = {(Pos >> 3) * dx, (7 - (Pos & 0b000111)) * dy, dx, dy};
  }

_Point _ChessBoard::BoardSquareCenter (_Coord Pos)
  {
    int dx = Rect.Width / 8;
    int dy = Rect.Height / 8;
    TranslateCoord (&Pos);
    return {(Pos >> 3) * dx + dx / 2, (7 - (Pos & 0b000111)) * dy + dy / 2};
  }

void _ChessBoard::DrawVector (_Coord Move [], int Colour)
  {
    _Point p0, p1;
    int Width;
    //
    if (Move [0] >= 0)
      {
        p0 = BoardSquareCenter (Move [0]);
        p1 = BoardSquareCenter (Move [1]);
        Width = Max (1, Rect.Width / 8 / 40);
        DrawLine (p0.x, p0.y, p1.x, p1.y, Colour, Width);
        DrawCircle (p0.x, p0.y, 3 * Width, -1, Colour, 0);
      }
  }

void _ChessBoard::DrawCustom (void)
  {
    _Rect Square, s;
    _Point Center;
    int xy, Pass;
    _Piece p;
    char St [8], *c;
    int WidthLine;
    int WidthCross;
    //
    WidthLine = Max (1, Rect.Width / 8 / 24);
    WidthCross = Rect.Width / 8 / 8;   // Half width of cross
    FontSet (FontPieces, (Rect.Height / 8) -2);//* 5 / 6);
    for (Pass = 0; Pass < 2; Pass++)
      {
        if (Pass == 1)
          FontSet (NULL, (Rect.Height / 8) * 3 / 10, fsBold);
        for (xy = 0; xy < 64; xy++)   // for all squares
          {
            BoardSquare (xy, &Square);
            Center = BoardSquareCenter (xy);
            ColourGrad = Colour = Colours [(xy ^ (xy >> 3)) & 1];
            if (MoveStart)
              if (xy == Move [MoveWhite][0] || xy == Move [MoveWhite][1])
                Colour = ColourAverage (Colours [0], Colours [1]); // cGray; //cWhite;
            if (Pass == 0)
              {
                //DrawRectangle (Square, cBlack, cBlack, Colour);   // Background
                DrawRectangle (Square, -1, -1, Colour);   // Background
                p = Board [xy];
                if (Piece (p) != pEmpty)
                  {
                    Font->Colour = Colours [2];
                    c = St;   // Piece
                    if (Filled)
                      {
                        if (PieceWhite (p))
                          Font->Colour = cWhite; // ColourAdjust (Font->Colour, 185); //;
                        StrCat (&c, "\ao");
                        PieceToChar (&c, Piece (p));
                        StrCat (&c, "\ao");
                      }
                    else
                      PieceToChar (&c, p);
                    *c = 0;
                    TextOutAligned (Square, St, aCenter, aCenter);
                  }
                if (p & pChecked || ((p & pPawn2) && (p / pMoveID == MoveID)))
                  DrawCircle (Center.x, Center.y, Max (Rect.Width / 8 / 16, 1), cRed, ColourAdjust (cRed, 150), 1);
                if (LegalMovesShow && LegalMoves [xy])   // Legal moves
                  {
                    DrawLine (Center.x - WidthCross, Center.y - WidthCross, Center.x + WidthCross, Center.y + WidthCross, MoveWhite ? cWhite : cBlack, WidthLine);
                    DrawLine (Center.x + WidthCross, Center.y - WidthCross, Center.x - WidthCross, Center.y + WidthCross, MoveWhite ? cWhite : cBlack, WidthLine);
                  }
              }
            else   // Pass == 1  => Draw Column / Row
              if (Square.Height > 40)   // Large enough for Column / Row markers?
                {
                  s = AddMargin (Square, {2, 1});
                  Font->Colour = Colours [((xy ^ (xy >> 3)) & 1) ^ 1];   // Write in the alt square colour
                  St [1] = 0;   // terminate string before it's made
                  if ((xy & 0b111000) == 0)
                    {
                      St [0] = (xy & 0b000111) + '1';
                      TextOutAligned (s, St, Rotate ? aRight : aLeft, Rotate ? aBottom : aTop);
                    }
                  if ((xy & 0b000111) == 0)
                    {
                      St [0] = (xy >> 3) + 'a';
                      TextOutAligned (s, St, Rotate ? aLeft : aRight, Rotate ? aTop : aBottom);
                    }
                  /*if (Controlled [x][y] & 0x01)   // Controlled by Black
                    DrawCircle (Square.x + 4, Square.y + 4, 4, -1, cBlack);
                  if (Controlled [x][y] & 0x02)   // Controlled by White
                    DrawCircle (Square.x + 4, Square.y + 8, 4, -1, cWhite);*/
                }
          }
      }
    DrawRectangle ({0, 0, Rect.Width & ~7, Rect.Height & ~7}, cBlack, cBlack);
    DrawVector (Move [0], cBlack);
    DrawVector (Move [1], cWhite);
    Colour = -1;   // Revert to transparent
  }

bool _ChessBoard::ProcessEventCustom (_Event *Event, _Point Offset)
  {
    _Coord Sel, Moves [64], *m;
    bool Res;
    _Piece p;
    //
    Res = false;
    //if (Event->Type == etMouseDown)//####
    //  DebugAddHex ("etMouseDown", Event->MouseKeys);//####
    Sel = ((Event->X - Offset.x) / (Rect.Width / 8)) << 3;
    Sel += 7 - ((Event->Y - Offset.y) / (Rect.Height / 8));
    TranslateCoord (&Sel);
    p = Board [Sel];
    if (Event->Type == etMouseDown && Event->Key == KeyMouseLeft && !PlayThreadStarted)
      {
        BestMoveAbort = true;
        if (Piece (p) != pEmpty)
          {
            MoveStart = true;
            MoveWhite = PieceWhite (p);
            Move [MoveWhite][0] = Sel;
            Move [MoveWhite][1] = Sel;
            // Store possible moves in MovePos []
            memset (LegalMoves, false, sizeof (LegalMoves));
            if (Piece (Board [Sel]) != pEmpty)
              {
                GetPieceMoves (Sel, Moves); //, Engine.Analysis);
                m = Moves;
                while (*m >= 0)   // for all moves
                  {
                    LegalMoves [*m] = true;
                    m++;
                  }
              }
            Invalidate ();
          }
        Res = true;
      }
    else if (Event->Type == etMouseDown && Event->Key == KeyMouseMiddle)
      SquareSpecial = Sel;
    else if (Event->Type == etMouseMove && Event->MouseKeys == Bit [KeyMouseMiddle - 1])
      SquareSpecial = Sel;
    else if (Event->Type == etMouseMove && MoveStart)
      {
        if (Sel != Move [MoveWhite][1])
          {
            Move [MoveWhite][1] = Sel;
            Invalidate ();
          }
        Res = true;
      }
    else if (Event->Type == etMouseUp && Event->Key == KeyMouseLeft)
      if (MoveStart)
        {
          if (Move [MoveWhite][0] == Move [MoveWhite][1])   // we haven't moved
            MoveStart = false;
          else
            MoveComplete = true;
          memset (LegalMoves, false, sizeof (LegalMoves));
          Invalidate ();
          Res = true;
        }
    return Res;
  }


////////////////////////////////////////////////////////////////////////////////////////////////////
//
// GLOBALS
//
////////////////////////////////////////////////////////////////////////////////////////////////////

class _FormMain: public _Form
  {
    public:
      const int LogsSize = 4000;
      const int BoardWidth = 500;
      char *Logs, *sLogs;
      _Piece Graveyard [2][64];
      int ColourBG [2];
      //
      _Container *Toolbar;
      //_Button *bProperties, *bUndo, *bPlay, *bRestart, *bEdit, *bHelp, *bQuit;
      _Button *bPlay, *bUndo, *bEdit, *bProperties, *bHelp;
      _Button *bPCPlayBlack, *bPCPlayWhite;
      _Label *lMessage;
      _Label *lGraveyard;
      _ChessBoard *cBoard;
      _Label *lLogs;
      _Label *lPCStats;
      //
      _Wait *Wait;
      _MenuPopup *Menu;
      _MenuPopup *mPieces;
      //
      _FormMain (char *Title);
     ~_FormMain ();
  };

_FormMain *fMain;

enum {pPause, pPlay, pPlayManual} PCPlay;
bool Undo;
bool Analyse;
bool Restart;

char *Path;

_Engine EngineBW [2];   // Analysis settings: Black, White

typedef struct
  {
    _Coord From;
    _Coord To;
    _Piece OldFrom;
    _Piece OldTo;
    _SpecialMove SpecMov;
    char *Log;
  } _UndoItem;

_UndoItem UndoStack [1000];

int UndoStackSize;

int StrChangeChar (char *p, char c1, char c2)
  {
    int n;
    //
    n = 0;
    while (*p)
      {
        if (*p == c1)
          {
            *p = c2;
            n++;
          }
        p++;
      }
    return n;
  }

char *RemoveTextFormats (char *St)
  {
    char *Res, *r;
    //
    Res = NULL;
    StrAssignCopy (&Res, St);
    r = Res;
    while (true)
      {
        r = StrPos (r, '\a');
        if (r == NULL)
          break;
        StrDelete (r, 2);
      }
    r = Res;
    while (true)
      {
        r = StrPos (r, '\v');
        if (r == NULL)
          break;
        *r = '\t';
      }
    return Res;
  }

bool FileWriteLine_ (int f, char *Line)
  {
    bool Res;
    //
    StrChangeChar (Line, '\n', ',');
    Res = FileWriteLine (f, Line);
    StrChangeChar (Line, ',', '\n');
    return Res;
  }

void GameSave (char *Name, void *Parameter)
  {
    int f;   // file ID
    char *Line, *l;
    _Coord xy;
    int i;
    _Piece *p;
    //
    FileBackup (Name);
    Line = (char *) malloc (fMain->LogsSize + 100);
    f = FileOpen (Name, foWrite);
    if (f >= 0)
      {
        l = Line;
        StrCat (&l, "Board\t");
        for (xy = 0; xy < 64; xy++)
          {
            NumToStrBase (&l, Board [xy], 0, 16);
            StrCat (&l, ',');
          }
        *l = 0;
        FileWriteLine_ (f, Line);
        //
        l = Line;
        StrCat (&l, "Graveyard\t");
        for (i = 0; i <= 1; i++)
          {
            p = fMain->Graveyard [i];
            while (*p != pEmpty)
              {
                NumToHex (&l, *p++);
                StrCat (&l, ',');
              }
            StrCat (&l, '\t');
          }
        *l = 0;
        FileWriteLine_ (f, Line);
        //
        l = Line;
        StrCat (&l, "Logs\t");
        StrCat (&l, fMain->Logs);
        *l = 0;
        FileWriteLine_ (f, Line);
        //
        l = Line;
        StrCat (&l, "Moves\t");
        NumToStr (&l, MoveID);
        *l = 0;
        FileWriteLine_ (f, Line);
        FileClose (f);
      }
    else
      StrCat (&fMain->sLogs, "*** ERROR SAVING FILE ***\n");
    free (Line);
  }

void GameLoad (char *Name, void *Parameter)
  {
    int f;   // file ID
    int Size;
    char *Data, *dp, *dp_;
    _Coord xy;
    _Piece *p;
    int n;
    //
    f = FileOpen (Name, foRead);
    if (f >= 0)
      {
        fMain->Graveyard [0][0] = fMain->Graveyard [1][0] = pEmpty;
        Size = FileSize (f);
        Data = (char *) malloc (Size + 1);
        FileRead (f, (byte *) Data, Size);
        Data [Size] = 0;   // Terminate String
        dp = Data;
        while (*dp)
          {
            // Break off a line
            dp_ = StrPos (dp, '\n');
            *dp_++ = 0;
            // Look for a Data Name
            if (StrMatch (&dp, "Board\t"))
              {
                xy = 0;
                while (true)
                  {
                    if (*dp == 0)
                      break;
                    if (xy >= 64)
                      break;
                    Board [xy] = (_Piece) StrGetHex (&dp);
                    dp++;   // step past separator
                    xy++;
                  }
              }
            else if (StrMatch (&dp, "Graveyard\t"))
              {
                p = fMain->Graveyard [0];
                n = 0;
                while (*dp && n < SIZEARRAY (fMain->Graveyard [0]))
                  {
                    if (*dp == '\t')
                      {
                        p = fMain->Graveyard [1];
                        n = 0;
                        dp++;
                      }
                    if (*dp)
                      {
                        p [n++] = (_Piece) StrGetHex (&dp);
                        p [n] = pEmpty;
                      }
                    if (*dp == ',')
                      dp++;
                  }
              }
            else if (StrMatch (&dp, "Logs\t"))
              {
                strcpy (fMain->Logs, dp);
                StrChangeChar (fMain->Logs, ',', '\n');
              }
            else if (StrMatch (&dp, "Moves\t"))
              MoveID = StrGetNum (&dp);
            else
              break;
            dp = dp_;
          }
        FileClose (f);
        free (Data);
        fMain->sLogs = StrPos (fMain->Logs, (char) 0);
        UndoStackSize = 0;
        fMain->cBoard->Move [0][0] = -1;
        fMain->cBoard->Move [1][0] = -1;
        fMain->lMessage->TextSet (NULL);
        fMain->lMessage->VisibleSet (false);
        Analyse = true;
        Refresh = true;
      }
    else
      StrCat (&fMain->sLogs, "*** ERROR LOADING FILE ***\n");
  }

void LogSave (char *Name, void *Parameter)
  {
    int f;   // file ID
    char *l;
    //
    f = FileOpen (Name, foWrite);
    if (f >= 0)
      {
        l = RemoveTextFormats (fMain->Logs);
        FileWrite (f, (byte *) l, StrLen (l));
        FileClose (f);
        free (l);
      }
    else
      StrCat (&fMain->sLogs, "*** ERROR SAVING FILE ***\n");
  }

#define FileSettings ".Chess"

bool SettingsSave (void)
  {
    int f;   // file ID
    char *Line, *l;
    //
    Line = (char *) malloc (250);
    f = FileOpen_ (FileSettings, foWrite);
    if (f >= 0)
      {
        l = Line;
        StrCat (&l, "Depth\t");
        IntToStr (&l, Engine.DepthPlay);
        *l = 0;
        FileWriteLine_ (f, Line);
        //
        l = Line;
        StrCat (&l, "Analysis\t");
        IntToStr (&l, Engine.Analysis);
        StrCat (&l, '\t');
        IntToStr (&l, Engine.AnalysisScorePiece);
        StrCat (&l, '\t');
        IntToStr (&l, Engine.AnalysisScoreMove);
        StrCat (&l, '\t');
        IntToStr (&l, Engine.AnalysisScoreAttack);
        //StrCat (&l, '\t');
        //IntToStr (&l, AnalysisScoreAttackInd);
        *l = 0;
        FileWriteLine_ (f, Line);
        //
        l = Line;
        StrCat (&l, "Randomize\t");
        IntToStr (&l, Engine.AnalysisRandomize);
        *l = 0;
        FileWriteLine_ (f, Line);
        //
        l = Line;
        StrCat (&l, "Font\t");
        StrCat (&l, FontPieces);
        *l = 0;
        FileWriteLine_ (f, Line);
        //
        l = Line;
        StrCat (&l, "FontMap\t");
        IntToStr (&l, FontPiecesMap);
        *l = 0;
        FileWriteLine_ (f, Line);
        // Colours
        l = Line;
        StrCat (&l, "Colours\t");
        NumToHex (&l, fMain->cBoard->Colours [0]); StrCat (&l, ',');
        NumToHex (&l, fMain->cBoard->Colours [1]); StrCat (&l, ',');
        NumToHex (&l, fMain->cBoard->Colours [2]); StrCat (&l, ',');
        NumToHex (&l, fMain->ColourBG [0]); StrCat (&l, ',');
        NumToHex (&l, fMain->ColourBG [1]); StrCat (&l, ',');
        NumToHex (&l, fMain->cBoard->Filled);
        *l = 0;
        FileWriteLine_ (f, Line);
        // Done Close file & exit
        FileClose (f);
      }
    free (Line);
    return f >= 0;
  }

int StrGetHex_ (char **St)
  {
    int Val;
    //
    Val = StrGetHex (St);
    if (**St);
      (*St)++;
    return Val;
  }

bool SettingsLoad (void)
  {
    int f;   // file ID
    int Size;
    char *Data, *dp, *dp_;
    _DirItem *d;
    //
    f = FileOpen_ (FileSettings, foRead);
    if (f >= 0)
      {
        Size = FileSize (f);
        Data = (char *) malloc (Size + 1);
        Data [Size] = 0;   // Terminate String
        FileRead (f, (byte *) Data, Size);
        dp = Data;
        while (*dp)
          {
            // Break off a line
            dp_ = StrPos (dp, '\n');
            *dp_++ = 0;
            // Look for a Data Name
            if (StrMatch (&dp, "Depth\t"))
              Engine.DepthPlay = StrGetNum (&dp);
            else if (StrMatch (&dp, "Analysis\t"))
              {
                if (*dp)
                  Engine.Analysis = (_Analysis) StrGetNum (&dp);
                if (*dp)
                  Engine.AnalysisScorePiece = StrGetNum (&dp);
                if (*dp)
                  Engine.AnalysisScoreMove = StrGetNum (&dp);
                if (*dp)
                  Engine.AnalysisScoreAttack = StrGetNum (&dp);
                //if (*dp)
                //  AnalysisScoreAttackInd = StrGetNum (&dp);
              }
            else if (StrMatch (&dp, "Randomize\t"))
              Engine.AnalysisRandomize = StrGetNum (&dp);
            else if (StrMatch (&dp, "Font\t"))
              {
                // find font in Fonts
                d = Fonts;
                FontPiecesSelect = 0;
                while (d)
                  if (StrSame (d->Name, dp))   // Font found
                    {
                      FontPieces = d->Name;
                      d = NULL;
                    }
                  else
                    {
                      d = d->Next;
                      FontPiecesSelect++;
                    }
              }
            else if (StrMatch (&dp, "FontMap\t"))
              FontPiecesMap = StrGetNum (&dp);
            else if (StrMatch (&dp, "Colours\t"))
              {
                fMain->cBoard->Colours [0] = StrGetHex_ (&dp);
                fMain->cBoard->Colours [1] = StrGetHex_ (&dp);
                fMain->cBoard->Colours [2] = StrGetHex_ (&dp);
                fMain->ColourBG [0] = StrGetHex_ (&dp);
                fMain->ColourBG [1] = StrGetHex_ (&dp);
                fMain->cBoard->Filled = StrGetHex_ (&dp);
              }
            else
              DebugAddS ("Properties File: Bad Item:", dp);
            dp = dp_;
          }
        FileClose (f);
        free (Data);
        return true;
      }
    return false;
  }

void GraveyardUpdate (void)
  {
    int c;
    _Piece *p;
    char St [256], *s;
    //
    s = St;
    for (c = 0; c <= 1; c++)   // Go through both "sides" of the Graveyard
      {
        p = fMain->Graveyard [c];
        while (*p != pEmpty)
          PieceToChar (&s, *p++);
        if (!c)
          StrCat (&s, '\n');
      }
    *s = 0;
    if (fMain->lGraveyard->Font)
      fMain->lGraveyard->FontSet (FontPieces, fMain->lGraveyard->Font->Size);
    fMain->lGraveyard->TextSet (St);
  }

void GraveyardAddPiece (_Piece Piece)
  {
    _Piece *p;
    //
    p = fMain->Graveyard [PieceWhite (Piece)];   // Appropriate Graveyard "side"
    while (*p != pEmpty)
      p++;
    *p++ = Piece;
    *p = pEmpty;
    Refresh = true;
  }

void GraveyardRemovePiece (_Piece Piece)
  {
    _Piece *p, *q;
    //
    p = fMain->Graveyard [PieceWhite (Piece)];   // Select Grave "side"
    q = NULL;
    while (*p != pEmpty)   // Find end
      {
        q = p;   // note last item (this will be deleted)
        p++;
      }
    if (q)   // Is there an item here
      *q = pEmpty;   // delete it
    Refresh = true;
  }

void MovePiece_ (_Coord From, _Coord To)
  {
    _UndoItem *ui;
    //
    ui = &UndoStack [UndoStackSize];
    ui->From = From;
    ui->To = To;
    ui->OldFrom = Board [From];
    ui->OldTo = Board [To];
    ui->Log = fMain->sLogs;
    //
    if ((MoveID & 1) == 0)   // new White move
      {
        //StrCat (&fMain->sLogs, "\a00\a11\a22\a33\a44\a55\a66\a77\a88\a99 ");
        StrCat (&fMain->sLogs, "\a8");
        NumToStr (&fMain->sLogs, (MoveID >> 1) + 1);
        StrCat (&fMain->sLogs, ".  \a7");
      }
    else   // new black move
      StrCat (&fMain->sLogs, "\a0");
    // Log move and update Graveyard
    CoordToStr (&fMain->sLogs, From);
    if (Piece (ui->OldTo) != pEmpty)   // taking piece
      {
        StrCat (&fMain->sLogs, "\abx\ab");   // show an x
        GraveyardAddPiece (ui->OldTo);   // Add to Graveyard
      }
    else
      StrCat (&fMain->sLogs, ' ');
    CoordToStr (&fMain->sLogs, To);
    ui->SpecMov = MovePiece (From, To);
    if (ui->SpecMov != smNone)
      StrCat (&fMain->sLogs, '*');
    //else
    //  StrCat (&fMain->sLogs, ' ');
    if (ui->SpecMov == smEnPassant)
      GraveyardAddPiece (PieceFrom (pPawn, !PieceWhite (ui->OldFrom)));
    if (MoveID & 1)   // make ready for a Black Move
      StrCat (&fMain->sLogs, '\v');
    else
      StrCat (&fMain->sLogs, '\n');
    //
    if (UndoStackSize  + 1 < SIZEARRAY (UndoStack))   // just in case
      UndoStackSize++;
  }

bool UnmovePiece_ (void)
  {
    _UndoItem *ui;
    //
    if (UndoStackSize == 0)
      return false;
    UndoStackSize--;
    ui = &UndoStack [UndoStackSize];
    UnmovePiece (ui->From, ui->To, ui->OldFrom, ui->OldTo, ui->SpecMov);
    if (Piece (ui->OldTo) != pEmpty)   // replacing taken piece
      GraveyardRemovePiece (ui->OldTo);
    if (ui->SpecMov == smEnPassant)
      GraveyardRemovePiece (PieceFrom (pPawn, !PieceWhite (ui->OldFrom)));
    fMain->sLogs = ui->Log;
    return true;
  }

bool PrevPlayer (void)
  {
    _UndoItem *ui;
    //
    ui = &UndoStack [UndoStackSize];
    return PieceWhite (ui->OldFrom);
  }

bool NoDraws = true;

bool UndoStackSame (_UndoItem *m1, _UndoItem *m2)
  {
    if (m1->From == m2->From)
      if (m1->To == m2->To)
        return true;
    return false;
  }

bool CheckRepeat (void)
  {
    int n, i;
    //
    MoveForbidenFrom = -1;
    MoveForbidenTo = -1;
    if (!NoDraws)
      return false;
    n = 4;
    while (true)
      {
        if (n >= UndoStackSize / 2)
          return false;
        i = 0;
        while (true)
          {
            if (UndoStackSize - i - n - 1 < 0)
              break;
            if (!UndoStackSame (&UndoStack [UndoStackSize - i - n - 1], &UndoStack [UndoStackSize - i - 1]))
              break;
            if (i >= n)
              {
                MoveForbidenFrom = UndoStack [UndoStackSize - n].From;
                MoveForbidenTo = UndoStack [UndoStackSize - n].To;
                return true;
              }
            i++;
          }
        n += 2;
      }
  }


////////////////////////////////////////////////////////////////////////////////////////////////////
//
// PROPERTIES FORM
//
////////////////////////////////////////////////////////////////////////////////////////////////////

class _FormProperties: public _Form
  {
    public:
      int TabOld;
      _Tabs *tPage;
      _Container *cPageAppearence, *cPageChessEngine;
      // PageAppearence
      _CheckBox *cbRotate;
      _Label *lTheme;
      _DropList *dlTheme;
      _Label *lBoard;
      _Button *bColourB0, *bColourB1;
      _Label *lPieces;
      _Button *bColourP;
      _Label *lBackground;
      _Button *bColourBG0, *bColourBG1;
      _Label *lFont;
      _DropList *dlFont;
      _CheckBox *cbFilled;
      _Label *lFontMap;
      _DropList *dlFontMap;
      // PageChessEngine
      _Label *lDepth;
      _ButtonArrow *bDepthUp, *bDepthDown;
      int Depth;
      _CheckBox *cbNoDraws;
      _Label *lAnalysis;
      _DropList *dlAnalysis;
      _Label *lScorePiece, *lScoreMove, *lScoreAttack, *lScoreAttackInd, *lPlaceWeight;
      _EditNumber *eScorePiece, *eScoreMove, *eScoreAttack, *ePlaceWeight;
      _Label *lRandomize;
      _Slider *sRandomize;
      _FormProperties (char *Title, _Point Position);
     ~_FormProperties (void);
  };

_FormProperties *fProperties;

_FormProperties::~_FormProperties (void)
  {
    fProperties = NULL;
  }

int RandomizeValues [12] = {0, 1, 2, 5, 10, 25, 100, 200, 1000, 2000, 10000, 100000};
                        // {0, 50, 100, 250, 500, 1000, 2000, 4000, 8000, 20000, 50000, 100000};

void StrDepth (char *St, int Depth)
  {
    StrCat (&St, "Depth: ");
    IntToStr (&St, Depth);
    *St = 0;
  }

void StrRandomize (void)
  {
    char St [64], *s;
    //
    s = St;
    StrCat (&s, "Randomize: ");
    NumToStr (&s, RandomizeValues [fProperties->sRandomize->ValueGet()]);
    *s = 0;
    fProperties->lRandomize->TextSet (St);
  }

int RandomizeValueFind (int Value)
  {
    int v;
    //
    v = 0;
    while (RandomizeValues [v] < Value)
      v++;
    return v;
  }

void EngineRead (_Engine *Engine)
  {
    NoDraws = fProperties->cbNoDraws->Down;
    Engine->Analysis = (_Analysis) fProperties->dlAnalysis->Selected;
    Engine->DepthPlay = fProperties->Depth;
    Engine->AnalysisScorePiece = fProperties->eScorePiece->Value;
    Engine->AnalysisScoreMove = fProperties->eScoreMove->Value;
    Engine->AnalysisScoreAttack = fProperties->eScoreAttack->Value;
    Engine->PlaceWeighting = fProperties->ePlaceWeight->Value;
    Engine->AnalysisRandomize = RandomizeValues [fProperties->sRandomize->ValueGet ()];
  }

void EngineFill (_Engine *Engine)
  {
    char S [32];
    //
    fProperties->cbNoDraws->Down = NoDraws;
    fProperties->dlAnalysis->Selected = Engine->Analysis;
    //
    fProperties->Depth = Engine->DepthPlay;
    StrDepth (S, fProperties->Depth);
    fProperties->lDepth->TextSet (S);
    //
    fProperties->eScorePiece->ValueSet (Engine->AnalysisScorePiece);
    fProperties->eScoreMove->ValueSet (Engine->AnalysisScoreMove);
    fProperties->eScoreAttack->ValueSet (Engine->AnalysisScoreAttack);
    fProperties->ePlaceWeight->ValueSet (Engine->PlaceWeighting);
    fProperties->sRandomize->ValueSet (RandomizeValueFind (Engine->AnalysisRandomize));
    StrRandomize ();
    fProperties->cPageChessEngine->InvalidateAll (true);
  }

void ActionPropertiesTab (_Tabs *Tabs)
  {
    fProperties->cPageAppearence->VisibleSet (Tabs->Selected == 0);
    fProperties->cPageChessEngine->VisibleSet (Tabs->Selected >= 1);
    if (fProperties->TabOld > 0)
      EngineRead (&EngineBW [fProperties->TabOld - 1]);
    if (Tabs->Selected > 0)
      EngineFill (&EngineBW [Tabs->Selected - 1]);
    fProperties->TabOld = Tabs->Selected;
  }

void ActionDepth (_Container *Container)
  {
    _ButtonArrow *b;
    char St [16];
    //
    b = (_ButtonArrow *) Container;
    if (b->Direction == dUp)
      fProperties->Depth = Min (fProperties->Depth + 1, 9);
    else if (b->Direction == dDown)
      fProperties->Depth = Max (fProperties->Depth - 1, 0);
    // update label
    StrDepth (St, fProperties->Depth);
    fProperties->lDepth->TextSet (St);
    EngineRead (&EngineBW [fProperties->tPage->Selected - 1]);
  }

void ActionAnalysis (_Container *Container)
  {
    NoDraws = fProperties->cbNoDraws->Down;
    EngineRead (&EngineBW [fProperties->tPage->Selected - 1]);
    Analyse = true;
  }

void ActionAnalysisScore (_Container *Container)
  {
    EngineRead (&EngineBW [fProperties->tPage->Selected - 1]);
    Analyse = true;
  }

void ActionRandomize (_Slider *Slider)
  {
    EngineRead (&EngineBW [fProperties->tPage->Selected - 1]);
    Analyse = true;
    StrRandomize ();
  }

void ActionRotate (_CheckBox *CheckBox)
  {
    fMain->cBoard->Rotate = CheckBox->Down;
    fMain->cBoard->Invalidate ();
  }

void ColoursApply (void)
  {
    fMain->cBoard->Colours [0] = fProperties->bColourB0->Colour;
    fMain->cBoard->Colours [1] = fProperties->bColourB1->Colour;
    fMain->cBoard->Colours [2] = fProperties->bColourP->Colour;
    fMain->ColourBG [0] = fProperties->bColourBG0->Colour;
    fMain->ColourBG [1] = fProperties->bColourBG1->Colour;
    fMain->Container->InvalidateAll (true);
  }

int Colour0 [] = {0x6696BC, 0x549375, 0x008FE5, 0x00C0FF};
int Colour1 [] = {0xCCDCE8, 0xCEEAEA, 0x99D9FF, 0x00FCFF};
int Colour2 [] = {0x00A0FF, 0x5C5C5C, 0x6EFFFF, 0xFFFF00};
int Colour3 [] = {0x0080CC, 0x7B7B7B, 0x002EC9, 0xFF4949};

void ActionPalette (_DropList *List)
  {
    if (List->Selected >= 0)
      {
        fProperties->bColourB0->Colour = Colour0 [List->Selected]; fProperties->bColourB0->Invalidate ();
        fProperties->bColourB1->Colour = Colour1 [List->Selected]; fProperties->bColourB1->Invalidate ();
        fProperties->bColourBG0->Colour = Colour2 [List->Selected]; fProperties->bColourBG0->Invalidate ();
        fProperties->bColourBG1->Colour = Colour3 [List->Selected]; fProperties->bColourBG1->Invalidate ();
        fProperties->bColourP->Colour = cBlack; fProperties->bColourP->Invalidate ();
        ColoursApply ();
      }
    Refresh = true;
  }

void ActionColourSelected (int Result, void *Parameter)
  {
    _Button *bColour;
    //
    if (Parameter)
      {
        bColour = (_Button *) Parameter;
        bColour->Colour = Result;
        bColour->Invalidate ();
        ColoursApply ();
      }
  }

void ActionColourBG (_Button *Button)
  {
    ColourSelect ("Adjust Colour", Button->Colour, ActionColourSelected, Button);
  }

void ActionColourPieces(_Button *Button)
  {
    ColourSelect ("Piece Colour", Button->Colour, ActionColourSelected, Button);
  }

void ActionFilled (_CheckBox *cb)
  {
    fMain->cBoard->Filled = cb->Down;
    fMain->cBoard->Invalidate ();
  }

void ActionFont (_DropList *dl)
  {
    _DirItem *di;
    int i;
    //
    i = 0;
    di = Fonts;
    if (dl->Selected >= 0)
      if (ListGoto (Fonts, dl->Selected, &i, &di))
        {
          FontPiecesSelect = dl->Selected;
          FontPieces = di->Name;
          fMain->cBoard->Invalidate ();
          GraveyardUpdate ();
        }
  }

void ActionFontMap (_DropList *dl)
  {
    if (dl->Selected >= 0)
      {
        FontPiecesMap = dl->Selected;
        fMain->cBoard->Invalidate ();
        GraveyardUpdate ();
      }
  }

_FormProperties::_FormProperties (char *Title, _Point Position): _Form (Title, {Position.x, Position.y, 250, 260}, waAlwaysOnTop)
  {
    const int Th = 28;
    const int Bdr = 4;
    const int Ht = 28;
    const int Col1 = 100;
    const int Col2 = 120;
    const int bw = Ht * 2 / 3;
    int x, y;
    _DirItem *f;
    char *St, *s;
    //
    Container->Colour = cBlueLight;
    Container->ColourGrad = cGreenLight;
    //
    St = (char *) malloc (MaxPath);
    Container->FontSet (NULL, 12);
    tPage = new _Tabs (Container, {0, 0, 0, Th}, "\aB\a2Appearence\aB\t\aB\a0Engine\aB\t\aB\a7Engine\aB", (_Action) ActionPropertiesTab);
    tPage->Colour = Container->Colour;
    TabOld = -1;
    // Page Appearence
    cPageAppearence = new _Container (Container, {0, Th, 0, 0});
    x = y = Bdr;
    cbRotate = new _CheckBox (cPageAppearence, {x, y, -Bdr, Ht},"Rotate Board", (_Action) ActionRotate);
    cbRotate->Down = fMain->cBoard->Rotate;
    y += Ht + Bdr;
    lTheme = new _Label (cPageAppearence, {x, y, Col1, Ht},"Colour Theme");
    x += Col1;
    dlTheme = new _DropList (cPageAppearence, {x, y, -Bdr, Ht}, "Oak\tTeal\tSunset\tNeon", (_Action) ActionPalette);
    x = Bdr;
    y += Ht + Bdr;
    lBoard = new _Label (cPageAppearence, {x, y, Col1, Ht}, "Colour Board");
    x += Col1;
    bColourB0 = new _Button (cPageAppearence, {x, y, Ht, Ht}, NULL, (_Action) ActionColourBG);
    bColourB0->Colour = fMain->cBoard->Colours [0]; bColourB0->ColourGrad = -1;
    bColourB0->DataInt = 1;
    x += Ht + Bdr;
    bColourB1 = new _Button (cPageAppearence, {x, y, Ht, Ht}, NULL, (_Action) ActionColourBG);
    bColourB1->Colour = fMain->cBoard->Colours [1]; bColourB1->ColourGrad = -1;
    bColourB1->DataInt = 2;
    x = Bdr;
    y += Ht + Bdr;
    lPieces = new _Label (cPageAppearence, {x, y, Col1, Ht}, "Colour Pieces");
    x += Col1;
    bColourP = new _Button (cPageAppearence, {x, y, Ht, Ht}, NULL, (_Action) ActionColourPieces);
    bColourP->Colour = fMain->cBoard->Colours [2]; bColourP->ColourGrad = -1;
    cbFilled = new _CheckBox (cPageAppearence, {x + Ht + 4, y, 0, Ht}, "Solid", (_Action) ActionFilled);
    cbFilled->Align = aLeft;
    cbFilled->Down = fMain->cBoard->Filled;
    x = Bdr;
    y += Ht + Bdr;
    lBackground = new _Label (cPageAppearence, {x, y, Col1, Ht}, "Background");
    x += Col1;
    bColourBG0 = new _Button (cPageAppearence, {x, y, Ht, Ht}, NULL, (_Action) ActionColourBG);
    bColourBG0->Colour = fMain->Container->Colour; bColourBG0->ColourGrad = -1;
    bColourBG0->DataInt = 3;
    x += Ht + Bdr;
    bColourBG1 = new _Button (cPageAppearence, {x, y, Ht, Ht}, NULL, (_Action) ActionColourBG);
    bColourBG1->Colour = fMain->Container->ColourGrad; bColourBG1->ColourGrad = -1;
    bColourBG1->DataInt = 4;
    x = Bdr;
    y += Ht + Bdr;
    lFont = new _Label (cPageAppearence, {x, y, Col1, Ht}, "Font (Pieces)"); x += Col1;
    s = St;
    f = Fonts;
    while (true)
      {
        StrCat (&s, f->Name);
        f = f->Next;
        if (f)
          StrCat (&s, '\t');
        else
          break;
      }
    *s = 0;
    dlFont = new _DropList (cPageAppearence, {x, y, -Bdr, Ht}, St, (_Action) ActionFont);
    dlFont->Selected = FontPiecesSelect;
    x = Bdr;
    y += Ht + Bdr;
    lFontMap = new _Label (cPageAppearence, {x, y, Col1, Ht}, "Font Map"); x += Col1;
    dlFontMap = new _DropList (cPageAppearence, {x, y, -Bdr, Ht}, "Map A\tMap B\tUnicode", (_Action) ActionFontMap);
    dlFontMap->Selected = FontPiecesMap;
    //
    // Page Chess Engine
    cPageChessEngine = new _Container (Container, {0, Th, 0, 0});
    cPageChessEngine->VisibleSet (false);
    x = y = Bdr;
    //StrDepth (St);
    lDepth = new _Label (cPageChessEngine, {x, y, Col2, Ht}, St, aLeft);
    x += Col2;
    bDepthUp = new _ButtonArrow (cPageChessEngine, {x, y, bw, Ht / 2}, NULL, ActionDepth, dUp);
    bDepthDown = new _ButtonArrow (cPageChessEngine, {x, y + Ht / 2, bw, Ht / 2}, NULL, ActionDepth, dDown);
    x += bw + bw;
    cbNoDraws = new _CheckBox (cPageChessEngine, {x, y, -Bdr, Ht}, "No Draws", ActionAnalysis);
    //cbNoDraws->Down = NoDraws;
    x = Bdr;  //x += Bdr + Ht;
    y += Ht + Bdr;
    lAnalysis = new _Label (cPageChessEngine, {x, y, 0, Ht}, "Analysis");
    x += Col2;
    dlAnalysis = new _DropList (cPageChessEngine, {x, y, -Bdr, Ht}, "Pieces Only\tPieces + Moves", ActionAnalysis);
    //dlAnalysis->Selected = Engine.Analysis;
    x = Bdr;
    y += Ht + Bdr;
    lScorePiece = new _Label (cPageChessEngine, {x, y, 0, Ht}, "Score / Piece (Pawn)"); x += Col2;
    eScorePiece = new _EditNumber (cPageChessEngine, {x, y, 64, Ht}, NULL, ActionAnalysisScore, 0, 1000);
    //eScorePiece->Value = Engine.AnalysisScorePiece;
    y += Ht + Bdr;
    x = Bdr;
    lPlaceWeight = new _Label (cPageChessEngine, {x, y, 0, Ht}, "Score Board Centre"); x += Col2;
    ePlaceWeight = new _EditNumber (cPageChessEngine, {x, y, 64, Ht}, NULL, ActionAnalysis, 0, 2500);
    y += Ht + Bdr;
    x = Bdr;
    lScoreMove = new _Label (cPageChessEngine, {x, y, 0, Ht}, "Score / Move"); x += Col2;
    eScoreMove = new _EditNumber (cPageChessEngine, {x, y, 64, Ht}, NULL, ActionAnalysisScore, 0, 1000);
    //eScoreMove->Value = Engine.AnalysisScoreMove;
    x = Bdr;
    y += Ht + Bdr;
    lScoreAttack = new _Label (cPageChessEngine, {x, y, 0, Ht}, "Score / Attack (Pawn)"); x += Col2;
    eScoreAttack = new _EditNumber (cPageChessEngine, {x, y, 64, Ht}, NULL, ActionAnalysisScore, 0, 1000);
    //eScoreAttack->Value = Engine.AnalysisScoreAttack;
    x = Bdr;
    y += Ht + Bdr;
    lRandomize = new _Label (cPageChessEngine, {x, y, 0, Ht}, NULL); x += Col2; //"Randomize"
    sRandomize = new _Slider (cPageChessEngine, {x, y, -Bdr, Ht}, NULL, (_Action) ActionRandomize, 0, SIZEARRAY (RandomizeValues) - 1);
    free (St);
  }

void ActionProperties (_Container *Container)
  {
    if (!fProperties)
      fProperties = new _FormProperties ("Setup", MousePos ());
  }


////////////////////////////////////////////////////////////////////////////////////////////////////
//
// MAIN FORM
//
////////////////////////////////////////////////////////////////////////////////////////////////////

char *Help = "Help\v\a+\a+\ab\au\a6Stewy's Chess\ab\au\a0\a-\a-\v\e5  \n"
             "\nDrag a piece to move.\n"
             "Castling, Crowning & En-Passant allowed.\n"
             "PC plays to win and avoid a Draw.\n"
             "\n"
             "\e9 \eA \aBPC Play Black & White toggles:\ab\n"
             "  Choose which colour(s) are played by the PC. This allows:\n"
             "    -A human player to play either colour against the PC,\n"
             "    -2 humans to play each other,\n"
             "    -The PC to verse itself.\n"
             "\n"
             "\e2 \aBPlay:\aB The PC will play the next move.\n"
             "  This happens automatically when it is the PC's turn.\n"
             "\n"
             "\e1 \aBUndo:\aB Take back moves.\n"
             "\n"
             //"\auRestart\au: Start again and choose your colour OR play a friend.\n"
             //"\n"
             "\e4 \aBEdit:\aB\n"
             "  Move any pieces anywhere.\n"
             "  Right-click for a new piece (or empty).\n"
             "  Middle-click/drag to clear a squares.\n"
             "\n"
             "\e0 \aBSetup:\aB\n"
             "  \auAppearance\au: Change Colours & Piece Fonts etc.\n"
             "  \auChess Engine\au: Change how the PC Plays:\n"
             "    \aiDepth\ai: Number of moves to look ahead.\n"
             "    \aiAnalysis\ai: method used to score a board:\n"
             "      -Score Pieces only\n"
             "      -Score Pieces AND Moves AND Attacks.\n"
             "    \aiScore / Piece\ai: the value of surviving pieces (relative to a Pawn).\n"
             "    \aiScore / Move\ai: the value of each available move.\n"
             "    \aiScore / Attack\ai: the value of each piece you can attack (\").\n"
             //"    \aiScore / Attack'\ai: the value of each piece you guard (\").\n"
             "    \aiRandomize\ai: adds a random element.\n"
             "\n"
             "\abLog:\ab\n"
             "  Lists all the previous moves.\n"
             "  Shows a \aiprogress circle\ai while thinking about a move.\n"
             "  Click here to abort a move calculation\n"
             "\n"
             "\aBStats:\aB From previous PC move:\n"
             "  Number of Moves considered\n"
             "  Time taken\n"
             "  Projected Board Score (and Current Score), in \aiPawns\ai.\n"
             "\n"
             "Right click anywhere for:\n"
             "  game save etc.\n"
             "  Reset Board, Quit\n"
             "\n"
             "\au\a4github.com/StewartTunbridge\n"
             "\a4www.pimicros.com.au\a0\au";

void ActionPCPlay (_Container *Container)
  {
    PCPlay = pPause;
  }

void ActionUndo (_Container *Container)
  {
    Undo = true;
  }

void ActionPlay (_Container *Container)
  {
    PCPlay = pPlayManual;
  }

void ActionEdit (_Button *Button)
  {
    fMain->mPieces->EnabledSet (Button->Down);
    fMain->bPlay->EnabledSet (!Button->Down);
    fMain->bUndo->EnabledSet (!Button->Down);
    UndoStackSize = 0;
    Analyse = true;
  }

void ActionHelp (_Button *Button)
  {
    MessageBox ({500, 400}, Help, 14);
    fMessageBox->Container->Bitmap = BitmapLoad (fMessageBox->Window, "Chess.bmp");
    fMessageBox->lMessage->AlignVert = aTop;
    fMessageBox->lMessage->Align = aLeft;
    fMessageBox->lMessage->Margin = 4;
    fMessageBox->Container->Colour = ColourAdjust (cBlue, 160);
    fMessageBox->Container->ColourGrad = cWhite;
  }

void ActionMenu (_Container *Container)
  {
    _Menu *Menu;
    //
    Menu = (_Menu *) Container;
    if (!PlayThreadStarted)
      if (Menu->Selected == 0)   // Load Game
        Load = true;
      else if (Menu->Selected == 1)   // Save Game
        Save = true;
      else if (Menu->Selected == 2)   // Save Log
        SaveLog = true;
      else if (Menu->Selected == 3)   // Reset Board
        Restart = true;
      else if (Menu->Selected == 4)   // Quit
        Exit = true;
  }

void ActionPieces (_MenuPopup *mp)
  {
    _ChessBoard *cb;
    int dx, dy;
    _Coord Sel;
    //
    if (mp->Selected >= 0)
      {
        cb = (_ChessBoard *) mp->Parent;
        dx = cb->Rect.Width / 8;
        dy = cb->Rect.Height / 8;
        Sel = (mp->Mouse.x / dx) << 3;
        Sel += 7 - mp->Mouse.y / dy;
        cb->TranslateCoord (&Sel);
        if (mp->Selected <= pPawn)
          Board [Sel] = PieceFrom (mp->Selected, false);
        else
          Board [Sel] = PieceFrom (mp->Selected - pPawn, true);
        cb->Move [0][0] = -1;
        cb->Move [1][0] = -1;
        cb->Invalidate ();
        Analyse = true;
      }
  }

bool FormMainPreview (_Form *Form, _Event *Event)
  {
    if (FileSelectActive)
      return true;
    return false;
  }

const _Point FormSize = {630, 600};

_FormMain::_FormMain (char *Title): _Form (Title, {100, 100, FormSize.x, FormSize.y}, waResizable)
  {
    int x, y;
    int bx = 56;
    int by = 44;
    _Container *c;
    // Make the Main Form
    ColourBG [0] = cOrange;
    ColourBG [1] = ColourAdjust (cOrange, 80);
    Logs = (char *) malloc (LogsSize);
    sLogs = Logs;
    Container->FontSet (NULL, 14);//, fsNoGrayscale);Ubuntu-R.ttf
    Container->Bitmap = BitmapLoad (Window, "Chess.bmp");
    y = 0;
    // build Toolbar
    Toolbar = new _Container (Container, {0, y, 0, by});
    Toolbar->ColourText = cWhite;
    x = 0;
    bPCPlayBlack = new _Button (Toolbar, {x, 0, by/2, by/2}, "\e9", ActionPCPlay);
    bPCPlayBlack->Toggle = true;
    bPCPlayBlack->DownSet (true);
    bPCPlayWhite = new _Button (Toolbar, {x, by/2, by/2, 0}, "\eA", ActionPCPlay);
    bPCPlayWhite->Toggle = true;
    x += by/2;
    bPlay = new _Button (Toolbar, {x, 0, bx, 0}, "\e2\nPlay", (_Action) ActionPlay); x += bx;
    bUndo = new _Button (Toolbar, {x, 0, bx, 0}, "\e1\nUndo", (_Action) ActionUndo); x += bx;
    //bRestart = new _Button (Toolbar, {x, 0, btn, 0}, "\e3\nRestart", (_Action) ActionRestart); x += btn;
    bEdit = new _Button (Toolbar, {x, 0, bx, 0}, "\e4\nEdit", (_Action) ActionEdit); x += bx;
    bProperties = new _Button (Toolbar, {x, 0, bx, 0}, "\e0\nSetup", (_Action) ActionProperties); x += bx;
    bEdit->Toggle = true;
    bHelp = new _Button (Toolbar, {x, 0, bx, 0}, "\e5\nHelp", (_Action) ActionHelp); x += bx;
    //bQuit = new _Button (Toolbar, {x, 0, btn, 0}, "\e6\nQuit", (_Action) ActionQuit); x += btn;
    //x += 8;
    // Make Buttons Transparent
    c = Toolbar->Children;
    while (c)
      {
        c->Colour = -1;
        c = c->Next;
      }
    lMessage = new _Label (Toolbar, {x, 0, 0, 0}, NULL, aCenter);//, bLowered);
    lMessage->FontSet (NULL, 22, fsBold);
    lMessage->ColourText = cWhite;
    lMessage->Colour = cRed;
    lMessage->VisibleSet (false);
    y += Toolbar->Rect.Height;
    // Dead Pieces go to the Graveyard
    lGraveyard = new _Label (Container, {0, y, BoardWidth, 60}, NULL, aLeft);
    lGraveyard->FontSet (FontPieces, lGraveyard->Rect.Height / 2 - 1);
    lGraveyard->RectLock |= rlRight;
    y += lGraveyard->Rect.Height;
    //
    x = 2;
    cBoard = new _ChessBoard (Container, {x, y, BoardWidth, - 20});
    cBoard->RectLock |= rlRight + rlBottom;
    char s [128];
    AllPieces (s);
    mPieces = new _MenuPopup (cBoard, {}, s, (_Action) ActionPieces);
    mPieces->FontSet (FontPieces, 18);
    mPieces->EnabledSet (false);
    x += BoardWidth + 2;
    //
    lLogs = new _Label (Container, {x, Toolbar->Rect.Height, -2, -20}, NULL, aLeft, bNone);
    lLogs->RectLock = rlLeft | rlBottom;
    lLogs->AlignVert = aBottom;
    lLogs->FontSet (NULL, 14);//, fsBold);
    y += cBoard->Rect.Height;
    //
    lPCStats = new _Label (Container, {0, y, 0, 0}, NULL, aLeft);
    lPCStats->RectLock |= rlTop;
    //
    Wait = new _Wait (lLogs, {0, 0, 0, 0});
    Wait->VisibleSet (false);
    Wait->Period = 500; //mS
    //
    Menu = new _MenuPopup (Container, {0, 0, 0, 0}, "\e7Load Game\t\e8Save Game\t_\e8Save Log\t\e3Reset Board\t\e6Quit", ActionMenu);
    //
    EventPreview = FormMainPreview;
  }

_FormMain::~_FormMain ()
  {
    free (Logs);
    SettingsSave ();   // and save settings there
  }


////////////////////////////////////////////////////////////////////////////////////////////////////
//
// MAIN LOOP
//
////////////////////////////////////////////////////////////////////////////////////////////////////

int main_ (int argc, char *argv [])
  {
    char St [200], *s;
    _Bitmap *Icon;
    //
    DebugAddS ("===========Start Chess", Revision);
    chdir (PathExe);
    DebugAddS ("Exe Path: ", PathExe);
    BoardInit ();
    Restart = true;
    PCPlay = pPause;
    Analyse = false;
    UndoStackSize = 0;
    s = St;   // Build the form Title
    StrCat (&s, "Stewy's Chess Programme - ");
    StrCat (&s, Revision);
    *s = 0;
    if (FontsRead () == 0)
      DebugAdd ("**** NO CHESS FONTS");
    fMain = new _FormMain (St);
    Icon = BitmapLoad (fMain->Window, "Icon.bmp");
    WindowSetIcon (fMain->Window, Icon);
    BitmapDestroy (Icon);
    SettingsLoad ();
    EngineBW [0] = Engine;
    EngineBW [1] = Engine;
    // Main Loop
    while (!Exit)
      {
        usleep (1000);
        if (fMain->Wait->Clicked)
          {
            fMain->Wait->Clicked = false;
            BestMoveAbort = true;
            PCPlay = pPause;
          }
        //fMain->Container->EnabledSet (!FileSelectActive);
        fMain->bUndo->EnabledSet (UndoStackSize > 0);
        if (fMain->sLogs - fMain->Logs + 50 > fMain->LogsSize)   // the problem of c strings is ...
          fMain->sLogs = fMain->Logs;
        if (Restart)
          {
            Restart = false;
            BoardInit ();
            //BoardScoreWhite = 0;
            fMain->cBoard->Move [0][0] = -1;
            fMain->cBoard->Move [1][0] = -1;
            fMain->Graveyard [0][0] = pEmpty;
            fMain->Graveyard [1][0] = pEmpty;
            GraveyardUpdate ();
            fMain->sLogs = fMain->Logs;
            fMain->lMessage->VisibleSet (false);
            fMain->lMessage->TextSet (NULL);
            fMain->lPCStats->TextSet (NULL);
            Refresh = true;
            UndoStackSize = 0;
          }
        else if (Load)
          {
            Load = false;
            FileSelect ("Load Game", ".chess", false, GameLoad);
          }
        else if (Save)
          {
            Save = false;
            FileSelect ("Save Game", ".chess", true, GameSave);
          }
        else if (SaveLog)
          {
            SaveLog = false;
            FileSelect ("Save Log", ".log", true, LogSave);
          }
        else if (Undo)
          {
            Undo = false;
            if (UndoStackSize)
              UnmovePiece_();   // so undo a move
            fMain->cBoard->Move [0][0] = -1;
            fMain->cBoard->Move [1][0] = -1;
            fMain->lMessage->VisibleSet (false);
            fMain->lMessage->TextSet (NULL);
            if (MoveID == 0)   // starting again
              Restart = true;
            PCPlay = pPause;
            //Analyse = true;
            Refresh = true;
          }
        else if (PlayThreadFinished)
          {
            PlayThreadFinished = false;
            fMain->bPlay->EnabledSet (true);
            fMain->bEdit->EnabledSet (true);
            if (BestMoveAbort)
              fMain->lPCStats->TextSet ("\a1\abABORTED\ab");
            else if (PlayThreadScore <= -Mate)   // no moves possible without losing King
              {
                //if (InCheck (PlayThreadWhite))   // Mate
                  fMain->lMessage->TextSet ("No Moves, It\'s Over");
                //else   // Stalemate
                //  fMain->lMessage->TextSet ("Stalemate");
                fMain->lMessage->VisibleSet (true);
                Beep ();
                PCPlay = pPause;
              }
            else
              {
                // Build / show stats message
                s = St;
                StrCat (&s, "  Score ");
                IntToStrDecimals (&s, BoardScore (), 3);
                StrCat (&s, ".  ");
                if (PlayThreadWhite)
                  StrCat (&s, "\a7");
                else
                  StrCat (&s, "\a0");
                IntToStr (&s, MovesConsidered, DigitsCommas);
                StrCat (&s, " moves considered in ");
                IntToStrDecimals (&s, PlayThreadTime, 3);
                StrCat (&s, " sec");
                if (PlayThreadTime)
                  {
                    StrCat (&s, " (");
                    IntToStrDecimals (&s, MovesConsidered / PlayThreadTime, 3);
                    StrCat (&s, " M/s. ");
                  }
                IntToStrDecimals (&s, PlayThreadScore, 3);
                StrCat (&s, ')');
                *s = 0;
                fMain->lPCStats->TextSet (St);
                //
                MovePiece_ (BestA [0], BestB [0]);   // Update Board [][] ...
                fMain->cBoard->Move [PlayThreadWhite][0] = BestA [0];
                fMain->cBoard->Move [PlayThreadWhite][1] = BestB [0];
                if (InCheck (PlayThreadWhite))   // PC just moved into check
                  {
                    fMain->lMessage->TextSet ("STALEMATE");
                    fMain->lMessage->VisibleSet (true);
                    Beep ();
                    PCPlay = pPause;
                  }
                else if (InCheck (!PlayThreadWhite))
                  {
                    fMain->lMessage->TextSet ("IN CHECK");
                    fMain->lMessage->VisibleSet (true);
                    Beep ();
                  }
                else
                  fMain->lMessage->VisibleSet (false);
                Refresh = true;
              }
            fMain->Wait->VisibleSet (false);
          }
        else if (Analyse)
          {
            Analyse = false;
            BoardTotalScoreRecalculate ();
            Engine = EngineBW [1];
            s = St;
            StrCat (&s, "\a7White Board Score ");
            IntToStrDecimals (&s, BoardScore (), 3);
            *s = 0;
            fMain->lPCStats->TextSet (St);
          }
        else if (fMain->cBoard->SquareSpecial >= 0)
          {
            if (fMain->bEdit->Down)   // Editing
              {
                Board [fMain->cBoard->SquareSpecial] = pEmpty;
                Refresh = true;
                Analyse = true;
              }
            fMain->cBoard->SquareSpecial = -1;
          }
        else if (fMain->cBoard->MoveComplete)
          {
            fMain->cBoard->MoveStart = false;
            fMain->cBoard->MoveComplete = false;
            fMain->lMessage->VisibleSet (false);
            if (fMain->bEdit->Down)   // Editing
              {
                // Copy Piece to new square AND Clear old square
                Board [fMain->cBoard->Move [fMain->cBoard->MoveWhite][1]] = Board [fMain->cBoard->Move [fMain->cBoard->MoveWhite][0]];
                Board [fMain->cBoard->Move [fMain->cBoard->MoveWhite][0]] = pEmpty;
                Analyse = true;
              }
            else
              {
                if (fMain->cBoard->MoveWhite == (MoveID & 1))  // Wrong Player moved
                  {
                    fMain->lMessage->TextSet ("Not that Colour\'s turn");
                    fMain->lMessage->VisibleSet (true);
                    Beep ();
                    fMain->cBoard->Move [fMain->cBoard->MoveWhite][0] = -1;
                  }
                else
                  {
                    if (MoveValid (fMain->cBoard->Move [fMain->cBoard->MoveWhite][0], fMain->cBoard->Move [fMain->cBoard->MoveWhite][1]))
                      {
                        MovePiece_ (fMain->cBoard->Move [fMain->cBoard->MoveWhite][0], fMain->cBoard->Move [fMain->cBoard->MoveWhite][1]);
                        //Analyse = true;
                        if (InCheck (MoveID & 1))   // You moved into / stayed in Check
                          {
                            UnmovePiece_ ();   // undo move
                            fMain->lMessage->TextSet ("Save The King");
                            fMain->lMessage->VisibleSet (true);
                            Beep ();
                          }
                        else
                          PCPlay = pPlay;
                      }
                    else
                      {
                        fMain->lMessage->TextSet ("Not a move");
                        fMain->lMessage->VisibleSet (true);
                        Beep ();
                        fMain->cBoard->Move [fMain->cBoard->MoveWhite][0] = -1;
                      }
                  }
              }
            Refresh = true;
          }
        else if (!PlayThreadStarted && PCPlay != pPause)   // Paused or already generating a move
          if (PCPlay == pPlayManual || ((MoveID & 1) ? fMain->bPCPlayBlack->Down : fMain->bPCPlayWhite->Down))
            {
              PCPlay = pPlay;
              bool Player = (~MoveID & 1);   // Play for whoever's turn it is
              fMain->bPlay->EnabledSet (false);
              fMain->bEdit->EnabledSet (false);
              InCheck (Player);   // Mark King if in check
              if (CheckRepeat ())
                fMain->lPCStats->TextSet ("\ab\a1Avoiding Loop\ab");
              Engine = EngineBW [Player];
              StartThread (PlayThread, (void *) Player);
              fMain->Wait->ColourText = Player ? cWhite : cBlack;
              fMain->Wait->Invalidate ();
              fMain->Wait->VisibleSet (true);
            }
        //
        if (fMain->ColourBG [0] != fMain->Container->Colour || fMain->ColourBG [1] != fMain->Container->ColourGrad)
          {
            fMain->Container->Colour = fMain->ColourBG [0];
            fMain->Container->ColourGrad = fMain->ColourBG [1];
            fMain->Toolbar->Colour = fMain->ColourBG [0];
            fMain->Toolbar->ColourGrad = fMain->ColourBG [1];
            fMain->lPCStats->Colour = fMain->ColourBG [0];
            fMain->lPCStats->ColourGrad = fMain->ColourBG [1];
            fMain->Container->InvalidateAll (true);
          }
        if (Refresh)
          {
            Refresh = false;
            fMain->cBoard->Invalidate ();
            *fMain->sLogs = 0;
            fMain->lLogs->TextSet (fMain->Logs);
            GraveyardUpdate ();
          }
        if (FormsUpdate ())
          break;
      }
    while (FormList)
      delete (FormList);
    DirFree (Fonts);
    return 0;
  }

