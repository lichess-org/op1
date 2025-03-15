#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>

static char *Version = "7.9";

/*
 * 1.0  First version, based on ykeval version 3.23 (20191213)
 * 1.1  Include dtz > 254 (20191227)
 * 1.2  Slight format changes for PGN output to work with SCID (20200117)
 * 1.3  Fix some high DTZ behavior (20200123)
 * 1.4  Added global ZSTD decompression context for efficiency (20200208)
 * 2.0  Added 9-man endings (20200328)
 * 2.1  Small bug fixes; 7 identical pieces (20200428)
 * 2.2  Small bug fix (20200505)
 * 2.3  Explicit bishop-parity handling for odd-dimensioned boards (20200518)
 * 2.4  Fix interactive play (20200620)
 * 2.5  Add 133, 313, 331 configuration (20200726)
 * 2.6  Include databases restricted to certain bishop parities (20201025)
 * 3.0  Include pawns (20201025)
 * 3.1  Some fixes for parity handling; optionally compile without double pawn
 * steps (20201107) 3.2  Some more parity constrained endings (20210105) 3.3
 * Better en passant handling (20210223) 3.4  Fixed score output when parsing
 * PGN files (20210313) 3.5  Better display of bishop parities in PGN (20210316)
 * 3.6  Extract bad study moves directly from position file even if not merging
 * with PGN (20210328) 3.7  Note number of positions processed unless commenting
 * games or finding best line (20210421) 3.8  Slight output changes (20210509)
 * 3.9  Support reading of databases with blocked pawns (20210605)
 * 3.10 Support reading of databases for opposing pawns (20210709)
 * 4.0  Option to convert indices from MB to YK format and vice versa (20210725)
 * 4.1  Support reading of databases with pair of opposing pawns (20210822)
 * 4.2  Fix some 960 castling issues (20210825)
 * 4.3  Support reading database with 2 vs 1 pawn, with 1 opposing pair
 * (20210910) 5.0  Support YK files (20210925) 5.1  Add high DTC for YK files;
 * bug fix on YK <-> MB conversion (20211005) 5.2  Ability to insert scores
 * directly into PGN move list, including variations (20211110) 5.3  Add 2 vs 2,
 * with 1 opposing pair (20211111) 5.4  Add 3 vs 1, and 1 vs 3 with 1 opposing
 * pair (20211114) 5.5  Bug fixes; add number of pieces to EGTB comments
 * (20211130) 5.6  Add max number of pieces to EGTB in annotator field
 * (20211210) 5.7  Bug fixes for number of pieces after a result changing
 * capture; zz designation (20220109) 5.8  Increased maximum line length in PGN
 * to 8192; more tolerance for dangling symbols (20220115) 5.9  Add 4 vs 1 and 1
 * vs 4 pawn configurations with 1 opposing pair (20220126) 5.10 Add 3 vs 2 and
 * 2 vs 3 pawn configurations with 1 opposing pair (20220126) 5.11 Add 3 vs 3
 * configurations with 1 opposing pair (20220126) 5.12 Add 4 vs 2 and 2 vs 4
 * configurations with 1 opposing pair (20220212) 6.0  Add UCI wrapper
 * (conditional compilation) (20220302) 6.1  Point out errors in comments in
 * annotator (20220501) 6.2  Print out EPD for incorrectly played positions
 * (20220508) 6.3  Enforce number of pieces constraint for all PGN parsing
 * (20220517) 6.4  Handle long lines in PGN input for Chessbase (20230427)
 *      Optionally read MB or YK databases only (20230428)
 * 6.5  Fixed PGN output for long strings; correct assessment of final position
 * (20230609) 7.0  DP_22 configurations (two opposing pawn pairs, where the
 * pawns can never capture each other) (20230707) 7.1  Bug fix for large number
 * of pieces (20230708) 7.2  More info in verbose mode (20230711) 7.3 Additional
 * statistic output (20230718) 7.4  Better handling of 9-man endings (20230805)
 * 7.5  Output adjustments for chessbase (20230903)
 * 7.6  In PGN output, optionally only include games with at least one TB
 * evaluation (20231115) 7.7  Optionally always tag positions with castling as
 * unknown from EGTB (20240519) 7.8  Some chessbase PGN file bug work-arounds
 * (20240523) 7.9  Tag endings where promotions are restricted (queen only,
 * etc.) (20240529)
 */

#define NROWS 8
#define NCOLS 8

#if (NROWS == 8) && (NCOLS == 8)
#define Row(sq) ((sq) >> 3)
#define Column(sq) ((sq)&07)
#define SquareMake(row, col) (((row) << 3) | (col))
#else
#define Row(sq) ((sq) / (NCOLS))
#define Column(sq) ((sq) % (NCOLS))
#define SquareMake(row, col) ((NCOLS) * (row) + (col))
#endif

//#define NO_DOUBLE_PAWN_STEPS

#define USE_64_BIT // Needed for 8-man endings

#if !defined(NO_ZSTD)
#include "zstd.h" // ZSTD compression library
#endif

#if !defined(NO_ZLIB)
#include "zlib.h" // ZLIB compression library
#endif

#define NSQUARES ((NROWS) * (NCOLS))

enum {
    FREE_PAWNS = 0,
    BP_11_PAWNS,
    OP_11_PAWNS,
    OP_21_PAWNS,
    OP_12_PAWNS,
    OP_22_PAWNS,
    DP_22_PAWNS,
    OP_31_PAWNS,
    OP_13_PAWNS,
    OP_41_PAWNS,
    OP_14_PAWNS,
    OP_32_PAWNS,
    OP_23_PAWNS,
    OP_33_PAWNS,
    OP_42_PAWNS,
    OP_24_PAWNS
};

const char *OpExtensionName[] = {"Free Pawns", "BP_11", "OP_11", "OP_21",
                                 "OP_12",      "OP_22", "DP_22", "OP_31",
                                 "OP_13",      "OP_41", "OP_14", "OP_32",
                                 "OP_23",      "OP_33", "OP_42", "OP_24"};

enum { DTZ = 0, DTC, DTM, BIT_BASE };

const char *MetricName[] = {"DTZ", "DTC", "DTM", "BitBase"};

enum { BITMAP_FORMAT = 0, LIST_FORMAT, HIGH_DTZ_FORMAT };

enum { NO_COMPRESSION = 0, ZLIB, ZSTD, NUM_COMPRESSION_METHODS };
enum { ZLIB_YK = 0, BZIP_YK, LZMA_YK, ZSTD_YK, NO_COMPRESSION_YK };

const char *CompressionMethod[] = {"None", "ZLIB", "ZSTD"};

static bool YKFormatOnly = false;
static bool MBFormatOnly = false;

#if !defined(NO_ZSTD)
static ZSTD_DCtx *ZSTD_DecompressionContext = NULL;
#endif

#if !defined(NO_ZLIB)
#define COMPRESS_OK Z_OK
#else
#define COMPRESS_OK 0
#endif

#define COMPRESS_NOT_OK ((COMPRESS_OK) + 1)

#ifndef abs
#define abs(a) ((a) < 0 ? -(a) : (a))
#endif

/*
 * Determine the number of distinct canonical positions of the two kings, given
 * the symmetry of endings with pawns.
 *
 * N_SKINGS is the unique number in which a single king can be put on the board.
 * For a square board with opposite corners [0,0] and [NCOLS-1,NROWS-1], the
 * single king can be confined to the rectangle with corners:
 *
 * [0,0], [(NCOLS-1)/2,0], [0, NROWS-1], [(NCOLS-1)/2, NROWS-1]
 *
 * The number of king position is then the sum of the squares in that rectangle:
 *
 * N_SKINGS = NROWS*(NCOLS+1)/2,
 *
 * which is 32 on an 8x8 board.  Special cases are 2x3 (2 columns, three rows),
 * where the king can't be on a2, 3x2 (3 columns, two rows) where the king can't
 * be on b1 or b2, and 3x3, where the king can't be on b2.
 *
 * N_KINGS is the unique number in which two kings can be put on the board so
 * that they are not touching (1806 on an 8x8 board).
 */

#if (NSQUARES < 6) || (NROWS < 2) || (NCOLS < 2)
#error Board too small
#endif

#if (NCOLS % 2)
#if (NCOLS == 3)
#if (NROWS == 2)
#define N_SKINGS 2
#define N_KINGS 8
#elif (NROWS == 3)
#define N_SKINGS 5
#define N_SKINGS 17
#else
#define N_SKINGS ((NROWS) * ((NCOLS + 1) / 2))
#define N_KINGS                                                                \
    (2 * (NSQUARES - 4) + (NROWS - 2) * (NSQUARES - 6) +                       \
     (NSQUARES + NROWS - 8) + (NROWS - 2) * ((NSQUARES + NROWS) / 2 - 6))
#endif
#else
#define N_SKINGS ((NROWS) * ((NCOLS + 1) / 2))
#define N_KINGS                                                                \
    (2 * (NSQUARES - 4) + (NROWS - 2) * (NSQUARES - 6) +                       \
     2 * ((NCOLS - 3) / 2) * (NSQUARES - 6) +                                  \
     (NROWS - 2) * ((NCOLS - 3) / 2) * (NSQUARES - 9) +                        \
     (NSQUARES + NROWS - 8) + (NROWS - 2) * ((NSQUARES + NROWS) / 2 - 6))
#endif
#else // NCOLS even
#if (NCOLS == 2)
#if (NROWS == 3)
#define N_SKINGS 2
#define N_KINGS 4
#else
#define N_SKINGS NROWS
#define N_KINGS (2 * (NSQUARES - 4) + (NROWS - 2) * (NSQUARES - 6))
#endif
#else
#define N_SKINGS ((NROWS) * ((NCOLS + 1) / 2))
#define N_KINGS                                                                \
    (4 + (NROWS + NCOLS - 2) * (NSQUARES - 6) +                                \
     (NROWS - 2) * ((NCOLS + 1) / 2 - 1) * (NSQUARES - 9))
#endif
#endif

/*
 * Determine the number of distinct canonical positions of the two kings, given
 * the symmetry of pawnless pieces.  These depend on whether the board is a
 * square or a rectangle, and whether the sides are even or odd
 *
 * N_SKINGS_NOPAWNS is the unique number in which a single king can be put on
 * the board. For a square board with opposite corners [0,0] and
 * [NCOLS-1,NCOLS-1], the single king can be confined to the triangle with
 * corners:
 *
 * [0,0], [(NCOLS-1)/2,0], [(NCOLS-1)/2,(NCOLS-1)/2]
 *
 * The number of king position is then the sum of the squares in that triangle:
 *
 * N_SKINGS_NOPAWNS = ((NCOLS+1)/2 * ((NCOLS+1)/2 + 1))/2,
 *
 * which is 10 on an 8x8 board.  A special case is 3x3, where the white can't
 * be at the apex of the triangle b2, because the black king would have nowhere
 * to go. In that case, N_SKINGS_NOPAWNS = 2 (a1 and b1) rather than 3 as from
 * the formula.
 *
 * For a rectangular board with opposite corners [0,0] and [NCOLS-1,NROWS-1],
 * the single king can be confined to the rectangle with opposite corners:
 *
 * [0,0], [(NCOLS+1)/2, (NROWS+1)/2]
 *
 * The number of king positions is then obviously:
 *
 * N_SKINGS_NOPAWNS = (NCOLS+1)/2 * (NROWS+1)/2
 *
 * The only exception is for a 3x2 board, where the white king only has one
 * unique square instead of 2 from the formula.
 *
 * N_KINGS_NOPAWNS is the unique number in which two kings can be put on the
 * board so that they are not touching (462 on an 8x8 board). The formulas below
 * are rather complicated, taking into account that if the white king is on a
 * symmetry axis, such as the a1-h8 diagonal on a square board, or the d3-d7
 * vertical on a 7x7 board, the black king can be restricted to one of the sides
 * that results from reflection on that symmetry axis.
 */

#if (NCOLS >= 256) || (NROWS >= 256)
#error Board too large
#endif

#if (NCOLS < 3) || (NROWS < 2)
#error Board too small
#endif

#if (NROWS == NCOLS)
#define SQUARE
#define NSYMMETRIES 8
#if (NCOLS % 2) // square with odd sides
#define ODD_SQUARE
#if (NCOLS == 3)
#define N_KINGS_NOPAWNS 5
#define N_SKINGS_NOPAWNS 2
#else
#define N_KINGS_NOPAWNS                                                        \
    (NCOLS * (NCOLS + 1) + (NCOLS + 1) * (NCOLS + 3) / 8 - 10 +                \
     (NCOLS - 3) / 2 *                                                         \
         (2 * NSQUARES + NCOLS + (NCOLS - 5) / 2 * (NSQUARES - 9) / 2 - 18))
#define N_SKINGS_NOPAWNS (((NCOLS + 1) / 2 * ((NCOLS + 1) / 2 + 1)) / 2)
#endif
#else // square with even sides
#define N_KINGS_NOPAWNS                                                        \
    (NCOLS * (NCOLS + 1) / 2 - 3 + (NCOLS / 2 - 1) * (NSQUARES - 6) +          \
     (NCOLS / 2 - 1) * (NCOLS * (NCOLS + 1) / 2 - 6) +                         \
     (NCOLS / 2 - 2) * (NCOLS / 2 - 1) / 2 * (NSQUARES - 9))
#endif
#define N_SKINGS_NOPAWNS (((NCOLS + 1) / 2 * ((NCOLS + 1) / 2 + 1)) / 2)
#else // columns and rows not equal
#define NSYMMETRIES 4
#define RECTANGLE
#if (NROWS % 2)
#define ODD_ROWS
#if (NCOLS % 2) // both sides are odd
#define ODD_COLUMNS
#define N_KINGS_NOPAWNS                                                        \
    (NSQUARES - 4 + (NCOLS - 3) / 2 * (NSQUARES - 6) +                         \
     (NROWS - 3) / 2 * (NSQUARES - 6) + NROWS * (NCOLS + 1) / 2 - 4 +          \
     NCOLS * (NROWS + 1) / 2 - 4 +                                             \
     (NCOLS - 3) / 2 * (NCOLS * (NROWS + 1) / 2 - 6) +                         \
     (NROWS - 3) / 2 * (NROWS * (NCOLS + 1) / 2 - 6) +                         \
     (NCOLS - 3) / 2 * (NROWS - 3) / 2 * (NSQUARES - 9) +                      \
     (NCOLS + 1) / 2 * (NROWS + 1) / 2 - 4)
#else // rows odd, columns even
#define N_KINGS_NPAWNS                                                         \
    ((NCOLS / 2 - 1) *                                                         \
         ((NROWS - 1) / 2 * NSQUARES + (NCOLS / 2) * (NROWS + 1) -             \
          9 * (NROWS - 3) / 2 - 12) +                                          \
     (NROWS - 1) / 2 * NSQUARES + (NCOLS / 2) * (NROWS + 1) - 3 * NROWS + 1)
#endif
#elif (NCOLS % 2) // rows even, columns odd
#define ODD_COLUMNS
#define N_KINGS_NOPAWNS                                                        \
    ((NROWS / 2 - 1) *                                                         \
         ((NCOLS - 1) / 2 * NSQUARES + (NROWS / 2) * (NCOLS + 1) -             \
          9 * (NCOLS - 3) / 2 - 12) +                                          \
     (NCOLS - 1) / 2 * NSQUARES + (NROWS / 2) * (NCOLS + 1) - 3 * NCOLS + 1)
#else // both sides even
#define N_KINGS_NOPAWNS                                                        \
    (NSQUARES - 4 + (NROWS / 2 + NCOLS / 2 - 2) * (NSQUARES - 6) +             \
     (NROWS / 2 - 1) * (NCOLS / 2 - 1) * (NSQUARES - 9))
#endif
#if ((NCOLS == 2) && (NROWS == 3)) || ((NCOLS == 3) && (NROWS == 2))
#define N_SKINGS_NOPAWNS 1
#else
#define N_SKINGS_NOPAWNS (((NROWS + 1) / 2) * ((NCOLS + 1) / 2))
#endif
#endif

#if !defined(SQUARE) && !defined(RECTANGLE)
#error At least one of SQUARE or RECTANGLE must be defined
#endif

#if defined(SQUARE) && defined(RECTANGLE)
#error Cannot have both SQUARE and RECTANGLE defined
#endif

#if defined(ODD_SQUARE) && defined(ODD_ROWS)
#error Cannot have both ODD_SQUARE and ODD_ROWS defined
#endif

#define MAX_PIECES 32
#define MAX_PIECES_MB 9
#define MAX_PIECES_YK 7
#define MAX_PIECES_PER_SIDE 7
#define MAX_PIECES_PER_SIDE_YK 4
#define MAX_IDENT_PIECES 10
#define MAX_MOVES 256
#define MAX_PLY 1500
#define MAX_STACK 1000
#define MAX_SCORES 10000000
#define MAX_FILE_NAME 64
#define MAX_FILES 64
#define MAX_FILES_YK 16
#define MAX_FILES_HIGH_DTZ 64
#define MAX_CACHE 8

#define BLEICHER_MATED 0
#define BLEICHER_NOT_FOUND 32700
#define BLEICHER_DRAW 32701
#define BLEICHER_WON 32705
#define BLEICHER_LOST 32706

#define SWAP(a, b)                                                             \
    {                                                                          \
        int tmp = a;                                                           \
        a = b;                                                                 \
        b = tmp;                                                               \
    }

#define SORT(a, b)                                                             \
    {                                                                          \
        if (a > b) {                                                           \
            int tmp = a;                                                       \
            a = b;                                                             \
            b = tmp;                                                           \
        }                                                                      \
    }

/*
 * The following king and rook values generalize castling to
 * include Chess960, as well as larger boards
 */

#define KING_ORIG_COL_TRADITIONAL ((NCOLS) / 2)
#define CROOK_ORIG_COL_TRADITIONAL (0)
#define GROOK_ORIG_COL_TRADITIONAL ((NCOLS)-1)

static int king_orig_col = KING_ORIG_COL_TRADITIONAL;
static int crook_orig_col = CROOK_ORIG_COL_TRADITIONAL;
static int grook_orig_col = GROOK_ORIG_COL_TRADITIONAL;

#define KING_GCASTLE_DEST_COL ((NCOLS)-1 - ((NCOLS)-1 - (NCOLS) / 2) / 2)
#define ROOK_GCASTLE_DEST_COL ((KING_GCASTLE_DEST_COL)-1)

#define KING_CCASTLE_DEST_COL ((NCOLS) / 4)
#define ROOK_CCASTLE_DEST_COL ((KING_CCASTLE_DEST_COL) + 1)

#if defined(SQUARE) || defined(ODD_COLUMNS) || defined(ODD_ROWS)
#define FLIP_POSSIBLE
#else
#undef FLIP_POSSIBLE
#endif

#define KK_TABLE_LIMIT 256

/*
 * We make the bottom-right corner "white"
 */

#if defined(ODD_SQUARE) || (defined(ODD_COLUMNS) && defined(ODD_ROWS))
#define ODD_EDGES
#define MIN_BISHOPS_FOR_PARITY 1
#define NUM_WHITE_SQUARES ((NSQUARES + 1) / 2)
#define NUM_BLACK_SQUARES ((NSQUARES - 1) / 2)
#else
#undef ODD_EDGES
#define MIN_BISHOPS_FOR_PARITY 2
#define NUM_WHITE_SQUARES ((NSQUARES) / 2)
#define NUM_BLACK_SQUARES ((NSQUARES) / 2)
#endif

#define NUM_BLACK_PAIRS ((NUM_BLACK_SQUARES) * (NUM_BLACK_SQUARES - 1) / 2)
#define NUM_WHITE_PAIRS ((NUM_WHITE_SQUARES) * (NUM_WHITE_SQUARES - 1) / 2)

#define N2_ODD_PARITY ((NUM_WHITE_SQUARES) * (NUM_BLACK_SQUARES))
#define N2_EVEN_PARITY ((NUM_WHITE_SQUARES) * (NUM_WHITE_SQUARES - 1))

#define N3_ODD_PARITY                                                          \
    ((NUM_WHITE_SQUARES) * (NUM_BLACK_SQUARES) *                               \
     (NUM_WHITE_SQUARES + NUM_BLACK_SQUARES - 2) / 2)
#define N3_EVEN_PARITY                                                         \
    ((NUM_WHITE_SQUARES) * (NUM_WHITE_SQUARES - 1) * (NUM_WHITE_SQUARES - 2) / \
         6 +                                                                   \
     (NUM_BLACK_SQUARES) * (NUM_BLACK_SQUARES - 1) * (NUM_BLACK_SQUARES - 2) / \
         6)

#define N2 (NSQUARES * (NSQUARES - 1) / 2)
#define N3 (N2 * (NSQUARES - 2) / 3)
#define N4 (N3 * (NSQUARES - 3) / 4)
#define N5 (N4 * (NSQUARES - 4) / 5)
#define N6 (N5 * (NSQUARES - 5) / 6)

// compute N7 in such a way that for NSQUARES=64 no intermediate 32-bit overflow
// occurs

#if (N6 % 7)
#define N7 (N6 * ((NSQUARES - 6) / 7))
#else
#define N7 ((N6 / 7) * (NSQUARES - 6))
#endif

#define N2_1_OPPOSING                                                          \
    ((NCOLS * (NCOLS - 1) * (NROWS - 1) * (NROWS - 2) * (NROWS - 3) / 2) +     \
     (2 * (NCOLS - 1) * (NROWS - 5 + 2)) +                                     \
     (NCOLS * (NROWS - 3) * (NROWS - 2) * (2 * NROWS - 5)) / 6)

#define N1_2_OPPOSING N2_1_OPPOSING

#define N4_Opposing_NoEP                                                       \
    (NCOLS * (NCOLS - 1) / 2 * (NROWS - 2) * (NROWS - 3) / 2 * (NROWS - 2) *   \
         (NROWS - 3) / 2 +                                                     \
     (NCOLS * (NROWS - 2) * (NROWS - 3) * (NROWS - 4) * (NROWS - 5) / 12))

#define N4_Opposing_EP (2 * (NCOLS - 1) * 4 * (NROWS - 5))

#define N4_ONE_COLUMN                                                          \
    (NCOLS * (NROWS - 2) * (NROWS - 3) * (NROWS - 4) * (NROWS - 5) / 12)
#define N4_NON_ADJACENT                                                        \
    ((NCOLS - 1) * (NCOLS - 2) / 2 * (NROWS - 2) * (NROWS - 3) / 2 *           \
     (NROWS - 2) * (NROWS - 3) / 2)
#define N4_ADJACENT                                                            \
    (2 * (NCOLS - 1) * (NROWS - 1) * (NROWS - 2) * (NROWS - 3) * (NROWS - 4) / \
     24)

#define N4_OPPOSING (N4_ONE_COLUMN + N4_NON_ADJACENT + N4_ADJACENT)

#define N2_2_Opposing_3                                                        \
    ((NROWS - 2) * (NROWS - 3) / 2 * NCOLS * ((NROWS - 1) * (NROWS - 1) - 1) * \
     (NCOLS - 1) * (NCOLS - 2))
#define N2_2_Opposing_2a                                                       \
    ((NROWS - 2) * (NROWS - 3) / 2 * (NROWS * (NROWS - 1) / 2 - 1) * NCOLS *   \
     (NCOLS - 1))
#define N2_2_Opposing_2b                                                       \
    (((NROWS - 2) * (NROWS - 3) * (NROWS - 4) / 6 +                            \
      (NROWS - 1) * (NROWS - 2) * (NROWS - 3) / 6) *                           \
         (NROWS - 1) -                                                         \
     (NROWS - 2) * (NROWS - 3) / 2) *                                          \
        NCOLS *(NCOLS - 1)
#define N2_2_Opposing_1                                                        \
    ((2 * (NROWS - 1) * (NROWS - 2) * (NROWS - 3) * (NROWS - 4) / 24 +         \
      NROWS * (NROWS - 1) * (NROWS - 2) * (NROWS - 3) / 24 -                   \
      (NROWS - 2) * (NROWS - 3) / 2) *                                         \
     NCOLS)

#define N2_2_Opposing_NoEP                                                     \
    (N2_2_Opposing_3 + N2_2_Opposing_2a + 2 * N2_2_Opposing_2b +               \
     N2_2_Opposing_1 + N4_Opposing_NoEP)

#define N2_2_EP_1 ((NROWS - 2) * (NROWS - 3) / 2 * (NCOLS - 2))
#define N2_2_EP_2 (2 * (NROWS - 2) * (NCOLS - 2))
#define N2_2_EP_3 ((NROWS - 5) * (NROWS - 2) * (NCOLS - 2))
#define N2_2_EP_4 ((NROWS - 5) * (NROWS - 5))
#define N2_2_EP_5 0
#define N2_2_EP_6 ((NROWS - 3) * (NROWS - 4) / 2 + 1)
#define N2_2_EP_7 ((NROWS - 5) * (NROWS - 6))

#define N2_2_Opposing_EP                                                       \
    ((N2_2_EP_1 + N2_2_EP_2 + N2_2_EP_3 + N2_2_EP_4 + N2_2_EP_5 + N2_2_EP_6 +  \
      N2_2_EP_7) *                                                             \
         2 * 2 * (NCOLS - 1) -                                                 \
     2 * 4 * (NCOLS - 2) + N4_Opposing_EP)

#define N2_2_OPPOSING (N2_2_Opposing_NoEP + N2_2_Opposing_EP)

// Number of 3 vs 1 pawn positions that contain an opposing pair

#define N31_a1 (NCOLS * (NROWS - 2) * (NROWS - 3) / 2)
#define N31_a2                                                                 \
    ((NCOLS - 1) * (NROWS - 1) * ((NCOLS - 1) * (NROWS - 1) - 1) / 2 -         \
     (NCOLS - 1) * (NCOLS - 2) / 2)
#define N31_b1 2 * (NROWS - 2) * (NROWS - 3) * (NROWS - 4) / 6 * (NROWS - 1)
#define N31_b2 (NROWS - 2) * (NROWS - 3) / 2 * (NROWS - 2)
#define N31_c1 2 * (NROWS - 1) * (NROWS - 2) * (NROWS - 3) * (NROWS - 4) / 24
#define N31_c2 (NROWS - 2) * (NROWS - 3) * (NROWS - 4) * (NROWS - 5) / 24

#define N31_Opposing_NoEP                                                      \
    ((N31_a1) * (N31_a2) + NCOLS * (NCOLS - 1) * (N31_b1 + N31_b2) +           \
     NCOLS * (N31_c1 + N31_c2))

#define N31_ep_w_a1 (NROWS - 5) * (NROWS - 6) / 2
#define N31_ep_w_a2 (NROWS - 5) * ((NCOLS - 1) * (NROWS - 2) - 1)
#define N31_ep_w_a 2 * (N31_ep_w_a1 + N31_ep_w_a2)

#define N31_ep_w_b1 2 * (NROWS - 5) * (NROWS - 6) / 2
#define N31_ep_w_b2 (NROWS - 5) * (2 * (NCOLS - 1) * (NROWS - 2) - 3)
#define N31_ep_w_b (NCOLS - 2) * (N31_ep_w_b1 + N31_ep_w_b2)

#define N31_ep_w (N31_ep_w_a + N31_ep_w_b)

#define N31_ep_b                                                               \
    (NCOLS - 1) * 2 * (1 + 2 * (2 * (NROWS - 5) + (NCOLS - 2) * (NROWS - 2)))

#define N31_Opposing_EP (N31_ep_w + N31_ep_b)

#define N3_1_OPPOSING (N31_Opposing_NoEP + N31_Opposing_EP)

#define N1_3_OPPOSING N3_1_OPPOSING

// For index calculations on 8x8 board, ensure zone size is divisible by
// NSQUARES for backwards compatibility with Yakov Konoval's index methodology.

#if (N2 % NSQUARES) && (NROWS == 8) && (NCOLS == 8)
#define N2_Offset (N2 + NSQUARES - (N2 % NSQUARES))
#else
#define N2_Offset N2
#endif

#if (N3 % NSQUARES) && (NROWS == 8) && (NCOLS == 8)
#define N3_Offset (N3 + NSQUARES - (N3 % NSQUARES))
#else
#define N3_Offset N3
#endif

#if (N4 % NSQUARES) && (NROWS == 8) && (NCOLS == 8)
#define N4_Offset (N4 + NSQUARES - (N4 % NSQUARES))
#else
#define N4_Offset N4
#endif

#if (N5 % NSQUARES) && (NROWS == 8) && (NCOLS == 8)
#define N5_Offset (N5 + NSQUARES - (N5 % NSQUARES))
#else
#define N5_Offset N5
#endif

// for 6 or 7 identical pieces we enforce divisibility by NSQUARES*NSQUARES
// so that the zone size for 8 or 9-piece endings is always divisible by
// NSQUARES*NSQUARES

#if (N6 % (NSQUARES * NSQUARES)) && (NROWS == 8) && (NCOLS == 8)
#define N6_Offset (N6 + (NSQUARES * NSQUARES) - (N6 % (NSQUARES * NSQUARES)))
#else
#define N6_Offset N6
#endif

#if (N7 % (NSQUARES * NSQUARES)) && (NROWS == 8) && (NCOLS == 8)
#define N7_Offset (N7 + (NSQUARES * NSQUARES) - (N7 % (NSQUARES * NSQUARES)))
#else
#define N7_Offset N7
#endif

#if (N2_ODD_PARITY % NSQUARES) && (NROWS == 8) && (NCOLS == 8)
#define N2_ODD_PARITY_Offset                                                   \
    (N2_ODD_PARITY + NSQUARES - (N2_ODD_PARITY % NSQUARES))
#if N2_ODD_PARITY_Offset != (1 << 10)
#error On 8x8 board, N2_ODD_PARITY_Offset should be 1 << 10
#endif
#else
#define N2_ODD_PARITY_Offset N2_ODD_PARITY
#endif

#if (N2_EVEN_PARITY % NSQUARES) && (NROWS == 8) && (NCOLS == 8)
#define N2_EVEN_PARITY_Offset                                                  \
    (N2_EVEN_PARITY + NSQUARES - (N2_EVEN_PARITY % NSQUARES))
#if N2_EVEN_PARITY_Offset != (1 << 10)
#error On 8x8 board, N2_EVEN_PARITY_Offset should be 1 << 10
#endif
#else
#define N2_EVEN_PARITY_Offset N2_EVEN_PARITY
#endif

#if (N3_EVEN_PARITY % NSQUARES) && (NROWS == 8) && (NCOLS == 8)
#define N3_EVEN_PARITY_Offset                                                  \
    (N3_EVEN_PARITY + NSQUARES - (N3_EVEN_PARITY % NSQUARES))
#else
#define N3_EVEN_PARITY_Offset N3_EVEN_PARITY
#endif

#if (NROWS == 8) && (NCOLS == 8)
#define N3_ODD_PARITY_Offset (1 << 15)
#if (N3_ODD_PARITY_Offset < N3_ODD_PARITY)
#error on 8x8 board, number of odd triplets should be <= (1 << 15)
#endif
#else
#define N3_ODD_PARITY_Offset N3_ODD_PARITY
#endif

static char *HighDTCList[] = {
    "krrnkrr",  "kbbbnkrb", "kbbbnkrn", "kbbnnkrn", "kbnnpkqp", "knnnpkqp",
    "kqnnkqn",  "kqnnkqb",  "kqbnkqn",  "kqbnkqb",  "kqnkrbn",  "krnpkrb",
    "krbnnkqn", "krbbpkq",  "krbppkqp", "krbpkbbp", "krbpkbnp", "krnpkbbp",
    "krnppkq",  "krnnkbbn", "krakrbn",  "kcbkrnn",  "kankbnn",  "kabnkan",
    "krbnnkcb", "krbnnkqb", "krrbkcn",  "krrbnkqr", "krrnkrbb", "krrnnkqr",
    "krrpkrbp"};

#define NumHighDTC (sizeof(HighDTCList) / sizeof(HighDTCList[0]))

#define MAX_PATHS 16

static char TbPaths[MAX_PATHS][1024];
static int NumPaths = 0;

#define MAX_PGN_LINE (1024 * 1024)
#define MAX_LINE 8192
#define MAX_STRING 10000

static size_t FilesOpened = 0;
static size_t FilesClosed = 0;

static int Verbose = 0;
static bool UseEnPassant = true;
static bool IgnoreCastle = true;
static bool Chess960 = true;
static bool Chess960Game = false;
static bool SummaryStats = false;
static bool StrictPGN = false;
static bool EGFormat = false;
static bool GermanKnight = false;
static bool StopAtTransition = false;
static bool StopAtPawnMove = false;

static bool AnnotateVariations = false;
static bool CheckSyntaxOnly = false;
static bool PositionDatabase = false;
static bool PrintBadlyPlayedPositions = false;
static bool TBGamesOnly = false;
static bool FlagRestrictedPromotions = false;
static int NumRestrictedPromotionEndings = 0;
static char *RestrictedPromotionFile = NULL;
static bool OutputPGNFormat = false;
static bool UniqueMovesOnly = false;
static bool AddAnnotator = false;
static char *Annotator = "EGTB";
static bool AddEGTBDepthComments = false;
static bool AddEGTBComments = false;
static bool InsertComments = false;
static int DepthDelta = -1;
static char *ScoreFile = NULL;
static char *DBFile = NULL;
static int NumDBPositions = 0;
static int NumBadMoves = 0;
static int NumBadMovesDelta = 0;
static int NumZZPositions = 0;
static bool OverwriteAnnotations = false;
static int MinimumNumberOfPieces = 3;
static int MaximumNumberOfPieces = 9;

static int MaxLineLength = 1000;
static int LineWidth = 80;

static ULONG CacheHits = 0;
static ULONG DBHits = 0;

static bool FileExists(char *fname) {
    struct _stat64 buffer;
    return (_stat64(fname, &buffer) == 0);
}

#if defined(_WIN32) || defined(_WIN64)
#define strtok_r strtok_s
typedef unsigned __int64 INDEX;
#define DEC_INDEX_FORMAT "%I64u"
#define SEPARATOR "/"
#define DELIMITER "\\"

#if defined(USE_64_BIT)
typedef unsigned __int64 ZINDEX;
#define DEC_ZINDEX_FORMAT "%I64u"
#define DEC_ZINDEX_FORMAT_W(n) "%" #n "I64u"
#define HEX_ZINDEX_FORMAT "%016I64X"
#else
typedef unsigned long ZINDEX;
#define DEC_ZINDEX_FORMAT "%lu"
#define DEC_ZINDEX_FORMAT_W(n) "%" #n "lu"
#define HEX_ZINDEX_FORMAT "%08lx"
#endif

#define ZERO ((ZINDEX)0)
#define ONE ((ZINDEX)1)
#define ALL_ONES (~(ZERO))

#define YK_HEADER_SIZE (4096 - ((462) * 8) - 8)

typedef struct {
    BYTE unused[16];
    char basename[16];
    INDEX n_elements;
    int kk_index;
    int max_depth;
    ULONG block_size;
    ULONG num_blocks;
    BYTE nrows;
    BYTE ncols;
    BYTE side;
    BYTE metric;
    BYTE compression_method;
    BYTE index_size;
    BYTE format_type;
    BYTE list_element_size;
} HEADER;

typedef struct {
    ZINDEX index;
    int score;
} HIGH_DTZ;

typedef struct {
    char ending[32];
    int kk_index;
    INDEX offset;
    int side;
    int promos;
    int game_num;
    int move_no;
    int result;
    int score;
    int zz_type;
    char cz_type;
} POSITION_DATA;

static int high_dtz_compar(const void *a, const void *b) {
    HIGH_DTZ *x = (HIGH_DTZ *)a;
    HIGH_DTZ *y = (HIGH_DTZ *)b;

    if (x->index < y->index)
        return -1;
    else if (x->index > y->index)
        return 1;
    return 0;
}

typedef struct {
    BYTE *buffer;
    ULONG buf_size;
    ULONG block_size;
} BUFFER;

static INDEX FileReads = 0;
static INDEX FileWrites = 0;

typedef HANDLE file;

file f_open(const char *szFile, const char *szMode) {
    HANDLE h;
    h = CreateFile(
        szFile,
        GENERIC_READ |
            ((('w' == szMode[0]) || ('w' == szMode[1])) ? GENERIC_WRITE : 0),
        (('r' == szMode[0]) && ('w' != szMode[1])) ? FILE_SHARE_READ : 0, NULL,
        ('r' == szMode[0]) ? (('w' == szMode[1]) ? OPEN_ALWAYS : OPEN_EXISTING)
                           : CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL, NULL);
    if (INVALID_HANDLE_VALUE == h) {
        return NULL;
    }
    FilesOpened++;
    return h;
}

static void f_close(file f) {
    if (f == 0)
        return;
    if (0 == CloseHandle(f)) {
        printf("*** Close failed\n");
        fflush(stdout);
        exit(1);
    }
    FilesClosed++;
}

size_t f_read(void *pv, size_t cb, file fp, INDEX indStart) {
    BOOL bResult;
    DWORD cbRead;

#if defined(USE_64_BIT)
    LARGE_INTEGER s;
    s.QuadPart = indStart;
    bResult = SetFilePointerEx(fp, s, NULL, FILE_BEGIN);
    if (bResult == 0 && GetLastError() != NO_ERROR) {
        fprintf(
            stderr,
            "*** SetFilePointerEx failed in f_read: pos is %I64u code is %lx\n",
            indStart, GetLastError());
        exit(1);
        return 0;
    }
#else
    LONG lHiPos;
    DWORD dwPtr;

    if (sizeof(INDEX) == 4)
        lHiPos = 0;
    else
        lHiPos = (LONG)(indStart >> 32);
    dwPtr = SetFilePointer(fp, (LONG)indStart, &lHiPos, FILE_BEGIN);
    if (0xFFFFFFFF == dwPtr && NO_ERROR != GetLastError()) {
        printf("*** Seek failed: pos is %I64x code is %lx\n", indStart,
               GetLastError());
        fflush(stdout);
        exit(1);
        return 0;
    }
#endif
    bResult = ReadFile(fp, pv, cb, &cbRead, NULL);
    if (0 == bResult) {
        printf("*** Read failed\n");
        fflush(stdout);
        exit(1);
        return 0;
    }
    FileReads += cbRead;
    return cbRead;
}

static void f_write(void *pv, size_t cb, file fp, INDEX indStart) {
    BOOL bResult;
    DWORD cbWritten;
#if defined(USE_64_BIT)
    LARGE_INTEGER s;
    s.QuadPart = indStart;
    bResult = SetFilePointerEx(fp, s, NULL, FILE_BEGIN);
    if (bResult == 0 && GetLastError() != NO_ERROR) {
        fprintf(stderr,
                "*** SetFilePointerEx failed in f_write: pos is %I64u code is "
                "%lx\n",
                indStart, GetLastError());
        exit(1);
    }
#else
    LONG lHiPos;
    DWORD dwPtr;

    if (sizeof(INDEX) == 4)
        lHiPos = 0;
    else
        lHiPos = (LONG)(indStart >> 32);
    if (lHiPos == 0)
        dwPtr = SetFilePointer(fp, (LONG)indStart, NULL, FILE_BEGIN);
    else
        dwPtr = SetFilePointer(fp, (LONG)indStart, &lHiPos, FILE_BEGIN);
    if (0xFFFFFFFF == dwPtr && NO_ERROR != GetLastError()) {
        printf("*** Seek failed in f_write\n");
        printf("indStart " DEC_INDEX_FORMAT
               " indStart shifted 32 bits to the right %lx, lHiPos = %lx, "
               "low_pos = %ld\n",
               indStart, (ULONG)(indStart >> 32), lHiPos, (LONG)indStart);
        fflush(stdout);
        exit(1);
        return;
    }
#endif
    bResult = WriteFile(fp, pv, cb, &cbWritten, NULL);
    if (0 == bResult || cbWritten != cb) {
        printf("*** Write failed\n");
        printf("Tried to write " DEC_INDEX_FORMAT
               " actually wrote " DEC_INDEX_FORMAT "\n",
               (INDEX)cb, (INDEX)cbWritten);
        fflush(stdout);
        exit(1);
        return;
    }
    FileWrites += cbWritten;
}

#else // not Windows

typedef unsigned long long INDEX;
#define DEC_INDEX_FORMAT "%llu"
#define SEPARATOR "\\"
#if defined(__MWERKS__)
#define DELIMITER ":"
#else
#define DELIMITER "/"
#endif

static INDEX FileReads = 0;
static INDEX FileWrites = 0;

typedef FILE *file;

file f_open(const char *szFile, const char *szMode) {
    FILE *fp;

    fp = fopen(szFile, szMode);
    if (NULL == fp) {
        return NULL;
    }
    FilesOpened++;
    return fp;
}

static void f_close(file f) {
    if (f == 0)
        return;
    if (0 != fclose(f)) {
        printf("*** Close failed\n");
        fflush(stdout);
        exit(1);
    }
    FilesClosed++;
}

size_t f_read(void *pv, size_t cb, file fp, INDEX indStart) {
    BOOL bResult;
    DWORD cbRead;

#if defined(USE_64_BIT)
    LARGE_INTEGER s;
    s.QuadPart = indStart;
    bResult = SetFilePointerEx(fp, s, NULL, FILE_BEGIN);
    if (bResult == 0 && GetLastError() != NO_ERROR) {
        fprintf(
            stderr,
            "*** SetFilePointerEx failed in f_read: pos is %I64u code is %lx\n",
            indStart, GetLastError());
        exit(1);
        return 0;
    }
#else
    LONG lHiPos;
    DWORD dwPtr;

    if (sizeof(INDEX) == 4)
        lHiPos = 0;
    else
        lHiPos = (LONG)(indStart >> 32);
    dwPtr = SetFilePointer(fp, (LONG)indStart, &lHiPos, FILE_BEGIN);
    if (0xFFFFFFFF == dwPtr && NO_ERROR != GetLastError()) {
        printf("*** Seek failed: pos is %I64x code is %lx\n", indStart,
               GetLastError());
        fflush(stdout);
        exit(1);
        return 0;
    }
#endif
    bResult = ReadFile(fp, pv, cb, &cbRead, NULL);
    if (0 == bResult) {
        printf("*** Read failed\n");
        fflush(stdout);
        exit(1);
        return 0;
    }
    FileReads += cbRead;
    return cbRead;
}

static void f_write(void *pv, INDEX cb, file fp, INDEX indStart) {
    if (0 != fseek(fp, (long)indStart, SEEK_SET)) {
        printf("*** Seek failed\n");
        fflush(stdout);
        exit(1);
        return;
    }
    if (cb != fwrite(pv, 1, cb, fp)) {
        printf("*** Write failed\n");
        fflush(stdout);
        exit(1);
        return;
    }
    FileWrites += cb;
}

#endif // Windows

typedef unsigned char BYTE;
typedef unsigned long ULONG;

static FILE *flog = NULL;

static int MyPrintf(char *fmt, ...) {
    va_list args;
    int len;

    va_start(args, fmt);
    len = vprintf(fmt, args);
    if (flog != NULL)
        vfprintf(flog, fmt, args);
    return len;
}

void MyFlush() {
    fflush(stdout);
    if (flog != NULL)
        fflush(flog);
}

static size_t MemoryAllocated = 0;
static size_t MemoryFreed = 0;

static void *MyMalloc(size_t cb) {
    void *pv;

    if (Verbose > 2)
        MyPrintf("Allocating %lu bytes of memory\n", cb);
    pv = malloc(cb);
    if (pv == NULL) {
        MyPrintf("Could not allocate %lu bytes of memory\n", cb);
        MyFlush();
        exit(1);
    }
    MemoryAllocated += cb;
    return pv;
}

static void MyFree(void *pv, size_t cb) {
    MemoryFreed += cb;
    free(pv);
}

enum { WHITE = 0, BLACK, NEUTRAL };

#define OtherSide(side) ((side) ^ 1)
#define ColorName(side) ((side) == WHITE ? "white" : "black")
#define SideTm(side) ((side) == WHITE ? "wtm" : "btm")

enum { NONE = 0, EVEN, ODD };

enum {
    NO_PIECE = 0,
    PAWN = 1,
    KNIGHT = 2,
    BISHOP = 4,
    ARCHBISHOP = (KNIGHT | BISHOP),
    ROOK = 8,
    CARDINAL = (KNIGHT | ROOK),
    QUEEN = (BISHOP | ROOK),
    MAHARAJA = (KNIGHT | BISHOP | ROOK),
    KING = 16
};

#define QRBN_PROMOTIONS                                                        \
    ((1 << (KNIGHT)) | (1 << (BISHOP)) | (1 << (ROOK)) | (1 << (QUEEN)))
#define QRN_PROMOTIONS ((1 << (KNIGHT)) | (1 << (ROOK)) | (1 << (QUEEN)))
#define QN_PROMOTIONS ((1 << (KNIGHT)) | (1 << (QUEEN)))
#define Q_PROMOTIONS (1 << (QUEEN))
#define QRB_PROMOTIONS ((1 << (BISHOP)) | (1 << (ROOK)) | (1 << (QUEEN)))
#define QBN_PROMOTIONS ((1 << (KNIGHT)) | (1 << (BISHOP)) | (1 << (QUEEN)))
#define QR_PROMOTIONS ((1 << (ROOK)) | (1 << (QUEEN)))
#define QB_PROMOTIONS ((1 << (BISHOP)) | (1 << (QUEEN)))

enum { BASE = 0, EE, NE, NN, NW, WW, SW, SS, SE };

enum {
    LOST = 65000,
    DRAW,
    STALE_MATE,
    NOT_LOST,
    NOT_WON,
    HIGH_DTZ_MISSING,
    WON,
    CHECK_MATE,
    ILLEGAL,
    UNRESOLVED,
    UNKNOWN,
    PLUS_MINUS,
    PLUS_EQUAL,
    EQUAL_MINUS,
    PLUS_EQUALMINUS,
    PLUSEQUAL_MINUS,
    MINUS_EQUAL,
    MINUS_PLUS,
    MINUS_PLUSEQUAL,
    EQUAL_PLUS,
    EQUALMINUS_PLUS,
    NO_MZUG
};

enum {
    IDENTITY = 0,
    REFLECT_V,
    REFLECT_H,
    REFLECT_VH,
    REFLECT_D,
    REFLECT_DV,
    REFLECT_DH,
    REFLECT_DVH
};

enum {
    ETYPE_NOT_MAPPED = -65000,
    TOO_MANY_PIECES,
    MB_FILE_MISSING,
    YK_FILE_MISSING,
    BAD_ZONE_SIZE,
    BAD_ZONE_NUMBER,
    HEADER_READ_ERROR,
    OFFSET_ALLOC_ERROR,
    OFFSET_READ_ERROR,
    ZONE_ALLOC_ERROR,
    ZONE_READ_ERROR,
    BUF_ALLOC_ERROR,
    BUF_READ_ERROR
};

enum {
    CAPTURE = 0x1,
    PROMOTION = 0x2,
    EN_PASSANT = 0x4,
    CHECK = 0x8,
    MATE = 0x10,
    WK_CASTLE = 0x20,
    WQ_CASTLE = 0x40,
    BK_CASTLE = 0x80,
    BQ_CASTLE = 0x100,
    UNIQUE = 0x200,
    BEST = 0x400
};

typedef ZINDEX (*pt2index)(int *);

typedef struct {
    int itype;
    pt2index IndexFromPos;
} IType;

#define NUM_ENDINGS 5250

static int num_endings = 0;

typedef struct {
    unsigned int material;
    int piece_type_count[2][KING];
    bool pawns_present;
    int side;
    int npieces;
    bool checked;
    bool available;
    unsigned int promotions;
    int parity;
    bool bitbase;
    BYTE metric;
    int high_dtz;
    ULONG (*IndexFromPos)(int *pos);
    ULONG zone_size;
    char *path;
    char fname[16];
    char fname_h[16];
} Ending;

static Ending EndingTable[NUM_ENDINGS];

static int EndingCompar(const void *a, const void *b) {
    Ending *x = (Ending *)a;
    Ending *y = (Ending *)b;

    if (x->material < y->material)
        return -1;
    else if (x->material > y->material)
        return 1;

    if (x->side < y->side)
        return -1;
    else if (x->side > y->side)
        return 1;

    return 0;
}

typedef struct {
    int board[NSQUARES];
    int ep_square;
    int castle;
    int num_pieces, nwhite, nblack, strength_w, strength_b;
    int piece_type_count[2][KING];
    int piece_locations[2][KING][MAX_IDENT_PIECES];
    int wkpos, bkpos;
    int half_move, full_move;
    int promos;
    int side;
    int result;
    int score;
    int zz_type;
    unsigned int game_num;
} BOARD;

typedef struct {
    int piece_type_count[2][KING];
    int ply, kk_index, pindex;
    char result;
    ZINDEX offset;
} GINFO;

typedef struct {
    int from, to, piece_moved, piece_captured, piece_promoted;
    int save_ep_square;
    int save_castle;
    int save_half_move;
    unsigned int flag;
    int score;
    BYTE metric;
} Move;

/* Different classes of move determined by the lexical analyser. */
typedef enum {
    PAWN_MOVE,
    PAWN_MOVE_WITH_PROMOTION,
    ENPASSANT_PAWN_MOVE,
    PIECE_MOVE,
    KINGSIDE_CASTLE,
    QUEENSIDE_CASTLE,
    NULL_MOVE,
    UNKNOWN_MOVE
} MoveClass;

typedef enum {
    /* The first section of tokens contains those that are
     * returned to the parser as complete token identifications.
     */
    EOF_TOKEN,
    TAG,
    STRING,
    COMMENT,
    NAG,
    CHECK_SYMBOL,
    MOVE_NUMBER,
    RAV_START,
    RAV_END,
    MOVE,
    TERMINATING_RESULT,
    /* The remaining tokens are those that are used to
     * perform the identification.  They are not handled by
     * the parser.
     */
    WHITESPACE,
    TAG_START,
    TAG_END,
    DOUBLE_QUOTE,
    COMMENT_START,
    COMMENT_END,
    ANNOTATE,
    DOT,
    PERCENT,
    ESCAPE,
    ALPHA,
    DIGIT,
    STAR,
    EOS,
    OPERATOR,
    NO_TOKEN,
    ERROR_TOKEN
} PGNToken;

/* List the tags so that the strings that they represent
 * would be in alphabetical order. E.g. note that EVENT_TAG and
 * EVENT_DATE_TAG should be in this order because the strings are
 * "Event" and "EventDate".
 */
typedef enum {
    ANNOTATOR_TAG,
    BLACK_TAG,
    BLACK_ELO_TAG,
    BLACK_NA_TAG,
    BLACK_TITLE_TAG,
    BLACK_TYPE_TAG,
    BLACK_USCF_TAG,
    BOARD_TAG,
    DATE_TAG,
    ECO_TAG,
    /* The PSEUDO_ELO_TAG is not a real PGN one.  It is used with the -t
     * argument so that it becomes possible to indicate a rating of either
     * colour.
     */
    PSEUDO_ELO_TAG,
    EVENT_TAG,
    EVENT_DATE_TAG,
    EVENT_SPONSOR_TAG,
    FEN_TAG,
    LONG_ECO_TAG,
    MODE_TAG,
    NIC_TAG,
    OPENING_TAG,
    /* The PSEUDO_PLAYER_TAG is not a real PGN one.  It is used with the -t
     * argument so that it becomes possible to indicate a player of either
     * colour.
     */
    PSEUDO_PLAYER_TAG,
    PLY_COUNT_TAG,
    RESULT_TAG,
    ROUND_TAG,
    SECTION_TAG,
    SETUP_TAG,
    SITE_TAG,
    STAGE_TAG,
    SUB_VARIATION_TAG,
    TERMINATION_TAG,
    TIME_TAG,
    TIME_CONTROL_TAG,
    UTC_DATE_TAG,
    UTC_TIME_TAG,
    VARIANT_TAG,
    VARIATION_TAG,
    WHITE_TAG,
    WHITE_ELO_TAG,
    WHITE_NA_TAG,
    WHITE_TITLE_TAG,
    WHITE_TYPE_TAG,
    WHITE_USCF_TAG,
    /* The following should always be last. It should not be used
     * as a tag identification.
     */
    ORIGINAL_NUMBER_OF_TAGS
} TagName;

/* Define a table to hold the list of tag strings and the corresponding
 * TagName index. This is initialised in init_tag_list().
 */

#define MAX_TAGS 100

static char *TagList[MAX_TAGS];
static char *TagValues[MAX_TAGS];
static int tag_list_length = 0;

typedef struct {
    char *string_info;
    int move_number;
    int tag_index;
} ParseType;

typedef struct {
    /* start of line */
    char *line;
    /* next char to access in line, if line is not NULL */
    unsigned char *linep;
    /* what token resulted */
    PGNToken token;
} LinePair;

/* Define a character that may be used to comment a line in, e.g.
 * the variations files.
 * Use the PGN escape mechanism character, for consistency.
 */

#define COMMENT_CHAR '%'

#define MAX_CHAR 256
#define ALPHA_DIST ('a' - 'A')

static int ParityTable[NSQUARES];
static int WhiteSquare[NSQUARES / 2], BlackSquare[NSQUARES / 2];

typedef struct {
    int etype, op_type, sub_type;
    bool (*PosFromIndex)(ZINDEX index, int *pos);
    ZINDEX (*IndexFromPos)(int *pos);
} IndexType;

typedef struct {
    char ending[MAX_PIECES_MB + 1];
    int game_num, move_no, side, result, score, zz_type;
    char cz_type;
} SCORE;

static SCORE *ScoreList = NULL;
static SCORE *ScoreListDelta = NULL;
static SCORE *ZZList = NULL;

typedef struct {
    char ending[MAX_PIECES_MB + 1];
    unsigned int num_total;
    unsigned int num_bad;
} ENDING_LIST;

typedef struct {
    char ending[16];
    INDEX offset;
    int kk_index;
    int promos;
    int score;
    int side;
    int zz_type;
} POSITION_DB;

static POSITION_DB *PositionDB = NULL;

typedef struct {
    char ending[16];
    int promos;
} RESTRICTED_PROMOTION;

static RESTRICTED_PROMOTION *RestrictedPromotionList = NULL;

static int restricted_promo_compar(const void *a, const void *b) {
    RESTRICTED_PROMOTION *x = (RESTRICTED_PROMOTION *)a;
    RESTRICTED_PROMOTION *y = (RESTRICTED_PROMOTION *)b;

    return strcmp(x->ending, y->ending);
}

typedef struct {
    int piece_type_count[2][KING];
    int kk_index;
    INDEX offset;
    int promos;
    int game_num, move_no;
    int side, result, score, zz_type, cz_depth;
    char cz_type;
    bool flipped;
} POSITION;

static int score_compar(const void *a, const void *b) {
    SCORE *x = (SCORE *)a;
    SCORE *y = (SCORE *)b;

    if (x->game_num < y->game_num) {
        return -1;
    } else if (x->game_num > y->game_num) {
        return 1;
    }

    if (x->move_no < y->move_no) {
        return -1;
    } else if (x->move_no > y->move_no) {
        return 1;
    }

    if (x->side < y->side) {
        return -1;
    } else if (x->side > y->side) {
        return 1;
    }

    return 0;
}

static int game_compar(const void *a, const void *b) {
    SCORE *x = (SCORE *)a;
    SCORE *y = (SCORE *)b;

    if (x->game_num < y->game_num) {
        return -1;
    } else if (x->game_num > y->game_num) {
        return 1;
    }
    return 0;
}

static int ending_compar(const void *a, const void *b) {
    SCORE *x = (SCORE *)a;
    SCORE *y = (SCORE *)b;

    return strcmp(x->ending, y->ending);
}

static int ending_list_compar(const void *a, const void *b) {
    ENDING_LIST *x = (ENDING_LIST *)a;
    ENDING_LIST *y = (ENDING_LIST *)b;

    return strcmp(x->ending, y->ending);
}

static int move_count_compar(const void *a, const void *b) {
    ENDING_LIST *x = (ENDING_LIST *)a;
    ENDING_LIST *y = (ENDING_LIST *)b;

    if (x->num_total < y->num_total)
        return 1;
    else if (x->num_total > y->num_total)
        return -1;
    return 0;
}

static int game_pos_compar(const void *a, const void *b) {
    POSITION *x = (POSITION *)a;
    POSITION *y = (POSITION *)b;

    if (x->game_num < y->game_num) {
        return -1;
    } else if (x->game_num > y->game_num) {
        return 1;
    }

    if (x->move_no < y->move_no)
        return -1;
    if (x->move_no > y->move_no)
        return 1;

    return (y->side - x->side);
}

static int pos_compar(const void *a, const void *b) {
    POSITION *x = (POSITION *)a;
    POSITION *y = (POSITION *)b;

    for (int i = 0; i < 2; i++) {
        for (int j = KING - 1; j >= 0; j--) {
            if (x->piece_type_count[i][j] > y->piece_type_count[i][j])
                return 1;
            else if (x->piece_type_count[i][j] < y->piece_type_count[i][j])
                return -1;
        }
    }

    if (x->kk_index > y->kk_index)
        return 1;
    else if (x->kk_index < y->kk_index)
        return -1;

    if (x->offset > y->offset)
        return 1;
    else if (x->offset < y->offset)
        return -1;

    return (y->side - x->side);
}

static int db_pos_compar(const void *a, const void *b) {
    POSITION_DB *x = (POSITION_DB *)a;
    POSITION_DB *y = (POSITION_DB *)b;

    int e = strcmp(x->ending, y->ending);
    if (e > 0)
        return 1;
    if (e < 0)
        return -1;

    if (x->kk_index > y->kk_index)
        return 1;
    if (x->kk_index < y->kk_index)
        return -1;

    if (x->offset > y->offset)
        return 1;
    if (x->offset < y->offset)
        return -1;

    return (y->side - x->side);
}

#if defined(MB_INDEX)

#define N2_Index_Function(a, b)                                                \
    ((a) > (b) ? ((a) * ((a)-1) / 2 + b) : ((b) * ((b)-1) / 2 + a))

static int N3_Index_Function(int a, int b, int c) {
    if (a < b)
        SWAP(a, b);
    if (a < c)
        SWAP(a, c);
    if (b < c)
        SWAP(b, c);

    return a * (a - 1) * (a - 2) / 6 + b * (b - 1) / 2 + c;
}

static int N4_Index_Function(int a, int b, int c, int d) {
    if (a < b)
        SWAP(a, b);
    if (c < d)
        SWAP(c, d);
    if (a < c)
        SWAP(a, c);
    if (b < d)
        SWAP(b, d);
    if (b < c)
        SWAP(b, c);

    return a * (a - 1) * (a - 2) * (a - 3) / 24 + b * (b - 1) * (b - 2) / 6 +
           c * (c - 1) / 2 + d;
}

#else // !MB_INDEX (YK_INDEX)

#define N2_Index_Function(a, b)                                                \
    ((a) > (b) ? ((b) * ((2 * NSQUARES - 3) - b) / 2 + a - 1)                  \
               : ((a) * ((2 * NSQUARES - 3) - a) / 2 + b - 1))

static int N3_Index_Function(int a, int b, int c) {
    if (a < b)
        SWAP(a, b);
    if (a < c)
        SWAP(a, c);
    if (b < c)
        SWAP(b, c);

    b -= (c + 1);
    a -= (c + 1);

    return c *
               (((3 * NSQUARES * (NSQUARES - 2) + 2) - 6 * b) +
                c * (-3 * (NSQUARES - 1) + c)) /
               6 +
           b * ((2 * (NSQUARES - 1) - 3) - b) / 2 + a - 1;
}

static int N4_Index_Function(int a, int b, int c, int d) {
    if (a < b)
        SWAP(a, b);
    if (c < d)
        SWAP(c, d);
    if (a < c)
        SWAP(a, c);
    if (b < d)
        SWAP(b, d);
    if (b < c)
        SWAP(b, c);

    int b4 = d *
             ((-6 + NSQUARES * (22 + NSQUARES * (-18 + 4 * NSQUARES))) +
              d * ((-11 + NSQUARES * (18 - 6 * NSQUARES)) +
                   d * ((4 * NSQUARES - 6) - d))) /
             24;

    a -= (c + 1);
    b -= (c + 1);

    int c2 = c - (d + 1);

    int b3 = c2 *
             ((3 * (NSQUARES - d - 1) * (NSQUARES - d - 3) + 2) +
              c2 * ((-3 * (NSQUARES - d - 2)) + c2)) /
             6;

    return b4 + b3 + b * (2 * (NSQUARES - c - 1) - 3 - b) / 2 + a - 1;
}
#endif

#define N2_2_Index_Function(a, b)                                              \
    ((a) > (b) ? (((a) / 2) * ((a) / 2 - 1) / 2 + (b) / 2)                     \
               : (((b) / 2) * ((b) / 2 - 1) / 2 + (a) / 2))

// For 5 or more identical pieces, always compute index rather than creating
// lookup table

static ZINDEX k5_tab[NSQUARES + 1];

static ZINDEX N5_Index_Function(int a, int b, int c, int d, int e) {
    // Sort a >= b >= c >= d >= e

    if (a < b)
        SWAP(a, b);
    if (c < d)
        SWAP(c, d);
    if (a < c) {
        SWAP(a, c);
        SWAP(b, d);
    }
    // at this point we have a >= c >= d, and also a >= b
    if (e < c) {
        if (d < e)
            SWAP(d, e);
        if (b < d) {
            SWAP(b, c);
            SWAP(c, d);
            if (d < e)
                SWAP(d, e);
        } else {
            if (b < c)
                SWAP(b, c);
        }
    } else {
        SWAP(e, c);
        SWAP(d, e);
        if (b < c) {
            SWAP(b, c);
            if (c < d) {
                SWAP(c, d);
                if (d < e)
                    SWAP(d, e);
            }
            if (a < b)
                SWAP(a, b);
        }
    }

    return k5_tab[a] + b * (b - 1) * (b - 2) * (b - 3) / 24 +
           c * (c - 1) * (c - 2) / 6 + d * (d - 1) / 2 + e;
}

static ZINDEX k6_tab[NSQUARES + 1];

static ZINDEX N6_Index_Function(int a, int b, int c, int d, int e, int f) {
    if (b > a)
        SWAP(a, b);
    if (c > a)
        SWAP(a, c);
    if (d > a)
        SWAP(a, d);
    if (e > a)
        SWAP(a, e);
    if (f > a)
        SWAP(a, f);

    return k6_tab[a] + N5_Index_Function(b, c, d, e, f);
}

static ZINDEX k7_tab[NSQUARES + 1];

static ZINDEX N7_Index_Function(int a, int b, int c, int d, int e, int f,
                                int g) {
    if (b > a)
        SWAP(a, b);
    if (c > a)
        SWAP(a, c);
    if (d > a)
        SWAP(a, d);
    if (e > a)
        SWAP(a, e);
    if (f > a)
        SWAP(a, f);
    if (g > a)
        SWAP(a, g);

    return k7_tab[a] + N6_Index_Function(b, c, d, e, f, g);
}

#if defined(USE_PERMUTATION_FUNCTIONS)

#define N2_Index(a, b) N2_Index_Function(a, b)
#define N3_Index(a, b, c) N3_Index_Function(a, b, c)
#define N4_Index(a, b, c, d) N4_Index_Function(a, b, c, d)

#define N2_2_Index(a, b) N2_2_Index_Function(a, b)

#else // Use permutation tables

static int *k2_tab = NULL, *k3_tab = NULL, *k4_tab = NULL;
static int *p2_tab = NULL, *p3_tab = NULL, *p4_tab = NULL, *p4_tab_mb = NULL;
static int *k2_even_tab = NULL, *p2_even_tab = NULL;
static int *k2_odd_tab = NULL, *p2_odd_tab = NULL;
static int *k3_even_tab = NULL, *p3_even_tab = NULL;
static int *k3_odd_tab = NULL, *p3_odd_tab = NULL;
static int *k2_same_color_tab = NULL, *p2_same_color_tab = NULL;

static int *k2_opposing_tab = NULL, *p2_opposing_tab = NULL;
static int *k2_1_opposing_tab = NULL, *p2_1_opposing_tab = NULL;
static int *k1_2_opposing_tab = NULL, *p1_2_opposing_tab = NULL;
static int *k4_opposing_tab = NULL, *p4_opposing_tab = NULL;
static int *k2_2_opposing_tab = NULL, *p2_2_opposing_tab = NULL;
static int *k3_1_opposing_tab = NULL, *p3_1_opposing_tab = NULL;
static int *k1_3_opposing_tab = NULL, *p1_3_opposing_tab = NULL;

#define N2_2_Index(a, b)                                                       \
    (k2_same_color_tab[(a) / 2 + (NUM_BLACK_SQUARES) * ((b) / 2)])

#if (NROWS == 8) && (NCOLS == 8)

#define N2_Index(a, b) (k2_tab[(a) | ((b) << 6)])
#define N3_Index(a, b, c) (k3_tab[(a) | ((b) << 6) | ((c) << 12)])
#define N4_Index(a, b, c, d)                                                   \
    (k4_tab[(a) | ((b) << 6) | ((c) << 12) | ((d) << 18)])
#define N2_Odd_Index(a, b) (k2_odd_tab[(a) | ((b) << 6)])
#define N2_Even_Index(a, b) (k2_even_tab[(a) | ((b) << 6)])
#define N3_Odd_Index(a, b, c) (k3_odd_tab[(a) | ((b) << 6) | ((c) << 12)])
#define N3_Even_Index(a, b, c) (k3_even_tab[(a) | ((b) << 6) | ((c) << 12)])
#define N2_Opposing_Index(a, b) (k2_opposing_tab[(a) | ((b) << 6)])
#define N4_Opposing_Index(a, b, c, d)                                          \
    (k4_opposing_tab[((a) >> 3) | ((b)&070) | ((c) << 6) | ((d) << 12)])

#define N2_1_Opposing_Index(a, b, c)                                           \
    (k2_1_opposing_tab[(a) | ((b) << 6) | ((c) << 12)])
#define N1_2_Opposing_Index(a, b, c)                                           \
    (k1_2_opposing_tab[(a) | ((b) << 6) | ((c) << 12)])
#define N3_1_Opposing_Index(a, b, c, d)                                        \
    (k3_1_opposing_tab[(a) | ((b) << 6) | ((c) << 12) | ((d) << 18)])
#define N1_3_Opposing_Index(a, b, c, d)                                        \
    (k1_3_opposing_tab[(a) | ((b) << 6) | ((c) << 12) | ((d) << 18)])
#define N2_2_Opposing_Index(a, b, c, d)                                        \
    (k2_2_opposing_tab[(a) | ((b) << 6) | ((c) << 12) | ((d) << 18)])

#else // not 8x8 board

#define N2_Index(a, b) (k2_tab[a + (NSQUARES) * (b)])
#define N3_Index(a, b, c) (k3_tab[a + (NSQUARES) * (b + (NSQUARES) * (c))])
#define N4_Index(a, b, c, d)                                                   \
    (k4_tab[a + (NSQUARES) * (b + (NSQUARES) * (c + (NSQUARES) * (d)))])
#define N2_Odd_Index(a, b) (k2_odd_tab[a + (NSQUARES) * (b)])
#define N2_Even_Index(a, b) (k2_even_tab[a + (NSQUARES) * (b)])
#define N3_Odd_Index(a, b, c)                                                  \
    (k3_odd_tab[a + (NSQUARES) * (b + (NSQUARES) * (c))])
#define N3_Even_Index(a, b, c)                                                 \
    (k3_even_tab[a + (NSQUARES) * (b + (NSQUARES * (c)))])
#define N2_Opposing_Index(a, b) (k2_opposing_tab[a + (NSQUARES) * (b)])
#define N4_Opposing_Index(a, b, c, d)                                          \
    (k4_opposing_tab[Row(a) +                                                  \
                     (NROWS) * (Row(b) + (NROWS) * (c + (NSQUARES)*d))])

#define N2_1_Opposing_Index(a, b, c)                                           \
    (k2_1_opposing_tab[a + (NSQUARES) * (b + (NSQUARES)*c)])
#define N1_2_Opposing_Index(a, b, c)                                           \
    (k1_2_opposing_tab[a + (NSQUARES) * (b + (NSQUARES)*c)])
#define N3_1_Opposing_Index(a, b, c, d)                                        \
    (k3_1_opposing_tab[a + (NSQUARES) * (b + (NSQUARES) * (c + (NSQUARES)*d))])
#define N1_3_Opposing_Index(a, b, c, d)                                        \
    (k1_3_opposing_tab[a + (NSQUARES) * (b + (NSQUARES) * (c + (NSQUARES)*d))])
#define N2_2_Opposing_Index(a, b, c, d)                                        \
    (k2_2_opposing_tab[a + (NSQUARES) *                                        \
                               (b + (NSQUARES) * (c + (NSQUARES) * (d)))])
#endif // 8x8 board

#endif // Permutation tables

#define N5_Index(a, b, c, d, e) N5_Index_Function(a, b, c, d, e)
#define N6_Index(a, b, c, d, e, f) N6_Index_Function(a, b, c, d, e, f)
#define N7_Index(a, b, c, d, e, f, g) N7_Index_Function(a, b, c, d, e, f, g);

static int Identity[NSQUARES];
static int ReflectV[NSQUARES];
static int ReflectH[NSQUARES];
static int ReflectVH[NSQUARES];
static int ReflectD[NSQUARES];
static int ReflectDV[NSQUARES];
static int ReflectDH[NSQUARES];
static int ReflectDVH[NSQUARES];

typedef struct {
    int wk, bk, kk_index;
} KK_PAIR;

static int *Transforms[] = {Identity, ReflectV,  ReflectH,  ReflectVH,
                            ReflectD, ReflectDV, ReflectDH, ReflectDVH};

const char *SymmetryName[] = {"Identity",   "Reflect_V",  "Reflect_H",
                              "Reflect_VH", "Reflect_D",  "Reflect_DV",
                              "Reflect_DH", "Reflect_DVH"};

static int *InverseTransforms[] = {Identity, ReflectV,  ReflectH,  ReflectVH,
                                   ReflectD, ReflectDH, ReflectDV, ReflectDVH};

static int InverseTable[] = {IDENTITY,  REFLECT_V,  REFLECT_H,  REFLECT_VH,
                             REFLECT_D, REFLECT_DH, REFLECT_DV, REFLECT_DVH};

static void GetFlipFunctionNoPawns(int wk, int bk, bool *flipped,
                                   int **transform) {
    *flipped = false;
#if defined(SQUARE)
    if (ReflectD[wk] == wk && ReflectD[bk] == bk) {
        *flipped = true;
        *transform = ReflectD;
    }
#if defined(ODD_SQUARE)
    else if (ReflectV[wk] == wk && ReflectV[bk] == bk) {
        *flipped = true;
        *transform = ReflectV;
    }
#endif
#elif defined(RECTANGLE)
#if defined(ODD_COLUMNS)
    if (ReflectV[wk] == wk && ReflectV[bk] == bk) {
        *flipped = true;
        *transform = ReflectV;
    }
#endif
#if defined(ODD_COLUMNS) && defined(ODD_ROWS)
    else
#endif
#if defined(ODD_ROWS)
        if (ReflectH[wk] == wk && ReflectH[bk] == bk) {
        *flipped = true;
        *transform = ReflectH;
    }
#endif
#endif
}

static void GetFlipFunction(int wk, int bk, bool *flipped, int **transform) {
    *flipped = false;
#if (NCOLS % 2)
    if (ReflectV[wk] == wk && ReflectV[bk] == bk) {
        *flipped = true;
        *transform = ReflectV;
    }
#endif
}

static KK_PAIR KK_List_NoPawns[N_KINGS_NOPAWNS], KK_List[N_KINGS];

#if (NSQUARES > KK_TABLE_LIMIT)
static int *KK_Transform_Table = NULL;
static int *KK_Index_Table = NULL;
static int *KK_Transform_Table_NoPawns = NULL;
static int *KK_Index_Table_NoPawns = NULL;
#define KK_Transform(wk, bk) KK_Transform_Table[((NSQUARES) * (wk) + (bk))]
#define KK_Index(wk, bk) KK_Index_Table[((NSQUARES) * (wk) + (bk))]
#define KK_Transform_NoPawns(wk, bk)                                           \
    KK_Transform_Table_NoPawns[((NSQUARES) * (wk) + (bk))]
#define KK_Index_NoPawns(wk, bk)                                               \
    KK_Index_Table_NoPawns[((NSQUARES) * (wk) + (bk))]
#else
static int KK_Transform_Table[NSQUARES][NSQUARES];
static int KK_Index_Table[NSQUARES][NSQUARES];
static int KK_Transform_Table_NoPawns[NSQUARES][NSQUARES];
static int KK_Index_Table_NoPawns[NSQUARES][NSQUARES];
#define KK_Transform(wk, bk) KK_Transform_Table[(wk)][(bk)]
#define KK_Index(wk, bk) KK_Index_Table[(wk)][(bk)]
#define KK_Transform_NoPawns(wk, bk) KK_Transform_Table_NoPawns[(wk)][(bk)]
#define KK_Index_NoPawns(wk, bk) KK_Index_Table_NoPawns[(wk)][(bk)]
#endif

static int WhiteSquares[NUM_WHITE_SQUARES], BlackSquares[NUM_BLACK_SQUARES];
static bool IsWhiteSquare[NSQUARES];

static void InitN2Tables(int *tab, int *pos) {
    int index = 0, score;

#if defined(MB_INDEX)
    for (int p2 = 1; p2 < NSQUARES; p2++) {
        for (int p1 = 0; p1 <= p2; p1++) {
#else // index used by Konoval
    for (int p1 = 0; p1 < NSQUARES; p1++) {
        for (int p2 = p1; p2 < NSQUARES; p2++) {
#endif
            if (p1 == p2)
                score = -1;
            else {
                if (pos != NULL)
                    pos[index] = p2 + NSQUARES * p1;
                int g_index = N2_Index_Function(p2, p1);
                if (index != g_index) {
                    fprintf(stderr,
                            "Bad pair index: index=%d computed index=%d p1=%d "
                            "p2=%d\n",
                            index, g_index, p1, p2);
                    exit(1);
                }
                score = index++;
            }
            if (tab != NULL) {
                tab[p1 + NSQUARES * p2] = score;
                tab[p2 + NSQUARES * p1] = score;
            }
        }
    }

    if (index != N2) {
        fprintf(stderr, "Got %d doublets, expected %d\n", index, N2);
        exit(1);
    }
}

static void InitN2OddTables(int *tab, int *pos) {
    int index = 0, score;

    for (int p1 = 0; p1 < NSQUARES; p1++)
        for (int p2 = 0; p2 < NSQUARES; p2++)
            tab[p2 + NSQUARES * p1] = -1;

    for (int p1 = 0; p1 < NSQUARES; p1++) {
        int col1 = Column(p1);
        int row1 = Row(p1);
        int parity1 = (row1 & 1) ^ (col1 & 1);
        for (int p2 = p1 + 1; p2 < NSQUARES; p2++) {
            int col2 = Column(p2);
            int row2 = Row(p2);
            int parity2 = (row2 & 1) ^ (col2 & 1);
            if (parity1 == parity2)
                continue;
            if (pos != NULL) {
                pos[index] = p2 + NSQUARES * p1;
                score = index++;
            }
            if (tab != NULL) {
                tab[p1 + NSQUARES * p2] = score;
                tab[p2 + NSQUARES * p1] = score;
            }
        }
    }

    if (index != N2_ODD_PARITY) {
        fprintf(stderr, "Got %d odd doublets, expected %d\n", index,
                N2_ODD_PARITY);
        exit(1);
    }
}

static void InitN2EvenTables(int *tab, int *pos) {
    int index = 0, score;

    for (int p1 = 0; p1 < NSQUARES; p1++)
        for (int p2 = 0; p2 < NSQUARES; p2++)
            tab[p2 + NSQUARES * p1] = -1;

    for (int p1 = 0; p1 < NSQUARES; p1++) {
        int col1 = Column(p1);
        int row1 = Row(p1);
        int parity1 = (row1 & 1) ^ (col1 & 1);
        for (int p2 = p1 + 1; p2 < NSQUARES; p2++) {
            int col2 = Column(p2);
            int row2 = Row(p2);
            int parity2 = (row2 & 1) ^ (col2 & 1);
            if (parity1 != parity2)
                continue;
            if (pos != NULL) {
                pos[index] = p2 + NSQUARES * p1;
                score = index++;
            }
            if (tab != NULL) {
                tab[p1 + NSQUARES * p2] = score;
                tab[p2 + NSQUARES * p1] = score;
            }
        }
    }

    if (index != N2_EVEN_PARITY) {
        fprintf(stderr, "Got %d even doublets, expected %d\n", index,
                N2_EVEN_PARITY);
        exit(1);
    }
}

static void InitN3EvenTables(int *tab, int *pos) {
    int index = 0, score;

    for (int p1 = 0; p1 < NSQUARES; p1++)
        for (int p2 = 0; p2 < NSQUARES; p2++)
            for (int p3 = 0; p3 < NSQUARES; p3++)
                tab[p3 + NSQUARES * (p2 + NSQUARES * p1)] = -1;

    for (int p1 = 0; p1 < NSQUARES; p1++) {
        int col1 = Column(p1);
        int row1 = Row(p1);
        int parity1 = (row1 & 1) ^ (col1 & 1);
        for (int p2 = p1 + 1; p2 < NSQUARES; p2++) {
            int col2 = Column(p2);
            int row2 = Row(p2);
            int parity2 = (row2 & 1) ^ (col2 & 1);
            if (parity1 != parity2)
                continue;
            for (int p3 = p2 + 1; p3 < NSQUARES; p3++) {
                int col3 = Column(p3);
                int row3 = Row(p3);
                int parity3 = (row3 & 1) ^ (col3 & 1);
                if (parity3 != parity1)
                    continue;
                if (pos != NULL) {
                    pos[index] = p3 + NSQUARES * (p2 + NSQUARES * p1);
                    score = index++;
                }
                if (tab != NULL) {
                    tab[p1 + NSQUARES * (p2 + NSQUARES * p3)] = score;
                    tab[p1 + NSQUARES * (p3 + NSQUARES * p2)] = score;
                    tab[p2 + NSQUARES * (p1 + NSQUARES * p3)] = score;
                    tab[p2 + NSQUARES * (p3 + NSQUARES * p1)] = score;
                    tab[p3 + NSQUARES * (p1 + NSQUARES * p2)] = score;
                    tab[p3 + NSQUARES * (p2 + NSQUARES * p1)] = score;
                }
            }
        }
    }

    if (index != N3_EVEN_PARITY) {
        fprintf(stderr, "Got %d even triplets, expected %d\n", index,
                N3_EVEN_PARITY);
        exit(1);
    }
}

static void InitN3OddTables(int *tab, int *pos) {
    int index = 0, score;

    for (int p1 = 0; p1 < NSQUARES; p1++)
        for (int p2 = 0; p2 < NSQUARES; p2++)
            for (int p3 = 0; p3 < NSQUARES; p3++)
                tab[p3 + NSQUARES * (p2 + NSQUARES * p1)] = -1;

    for (int p1 = 0; p1 < NSQUARES; p1++) {
        int col1 = Column(p1);
        int row1 = Row(p1);
        int parity1 = (row1 & 1) ^ (col1 & 1);
        for (int p2 = p1 + 1; p2 < NSQUARES; p2++) {
            int col2 = Column(p2);
            int row2 = Row(p2);
            int parity2 = (row2 & 1) ^ (col2 & 1);
            for (int p3 = p2 + 1; p3 < NSQUARES; p3++) {
                int col3 = Column(p3);
                int row3 = Row(p3);
                int parity3 = (row3 & 1) ^ (col3 & 1);
                if (parity1 == parity3 && parity1 == parity2)
                    continue;
                if (pos != NULL) {
                    pos[index] = p3 + NSQUARES * (p2 + NSQUARES * p1);
                    score = index++;
                }
                if (tab != NULL) {
                    tab[p1 + NSQUARES * (p2 + NSQUARES * p3)] = score;
                    tab[p1 + NSQUARES * (p3 + NSQUARES * p2)] = score;
                    tab[p2 + NSQUARES * (p1 + NSQUARES * p3)] = score;
                    tab[p2 + NSQUARES * (p3 + NSQUARES * p1)] = score;
                    tab[p3 + NSQUARES * (p1 + NSQUARES * p2)] = score;
                    tab[p3 + NSQUARES * (p2 + NSQUARES * p1)] = score;
                }
            }
        }
    }

    if (index != N3_ODD_PARITY) {
        fprintf(stderr, "Got %d odd triplets, expected %d\n", index,
                N3_ODD_PARITY);
        exit(1);
    }
}

static void InitN2_2Tables(int *tab, int *pos) {
    int index = 0, score;

    for (int p2 = 1; p2 < NUM_BLACK_SQUARES; p2++) {
        for (int p1 = 0; p1 <= p2; p1++) {
            if (p1 == p2)
                score = -1;
            else {
                if (pos != NULL)
                    pos[index] = p2 + NUM_BLACK_SQUARES * p1;
                int g_index = N2_2_Index_Function(p2, p1);
                if (index != g_index) {
                    fprintf(stderr,
                            "Bad same color pair index=%d computed index=%d "
                            "p1=%d p2=%d\n",
                            index, g_index, p1, p2);
                    exit(1);
                }
                score = index++;
            }
            if (tab != NULL) {
                tab[p1 + NUM_BLACK_SQUARES * p2] = score;
                tab[p2 + NUM_BLACK_SQUARES * p1] = score;
            }
        }
    }

    if (index != NUM_BLACK_PAIRS) {
        fprintf(stderr, "Got %d same color doublets, expected %d\n", index,
                NUM_BLACK_PAIRS);
        exit(1);
    }
}

static void InitN2OpposingTables(int *tab, int *pos) {
    int index = 0;

    for (int sq1 = 0; sq1 < NSQUARES; sq1++) {
        for (int sq2 = 0; sq2 < NSQUARES; sq2++) {
            tab[sq2 + NSQUARES * sq1] = -1;
        }
    }

    for (int sq1 = NCOLS; sq1 < (NSQUARES - 2 * NCOLS); sq1++) {
        int col = Column(sq1);
        int row1 = Row(sq1);
        for (int row2 = row1 + 1; row2 < (NROWS - 1); row2++) {
            int sq2 = SquareMake(row2, col);
            if (index >= NCOLS * (NROWS - 2) * (NROWS - 3) / 2) {
                fprintf(stderr, "Too many positions for opposing pawns\n");
                exit(1);
            }
            if (tab != NULL) {
                tab[sq2 + NSQUARES * sq1] = index;
            }
            if (pos != NULL) {
                pos[index] = sq2 + NSQUARES * sq1;
            }
            index++;
        }
    }

    if (index != NCOLS * (NROWS - 2) * (NROWS - 3) / 2) {
        fprintf(stderr, "Got %d opposing pawn positions, expected %d\n", index,
                NCOLS * (NROWS - 2) * (NROWS - 3) / 2);
        exit(1);
    }
}

static void InitN2_1_OpposingTables(int *tab, int *pos) {
    int index = 0, board[NSQUARES];

    for (int i = 0; i < NSQUARES * NSQUARES * NSQUARES; i++) {
        tab[i] = -1;
    }

    memset(board, 0, sizeof(board));

    for (int wp1 = 0; wp1 < NSQUARES - NCOLS; wp1++) {
        int wp1_physical = wp1;
        if (Row(wp1) == 0) {
            wp1_physical = wp1 + (3 * NCOLS);
        }
        board[wp1_physical] = PAWN;
        // the 2nd white pawn can be on last row for promotion
        for (int wp2 = wp1 + 1; wp2 < NSQUARES; wp2++) {
            // only one white pawn can be e.p. captured
            if (Row(wp2) == 0)
                continue;
            // if the pawn is promoted, no e.p. is possible
            if (Row(wp2) == (NROWS - 1) && Row(wp1) == 0)
                continue;
            if (board[wp2])
                continue;
            board[wp2] = PAWN;
            for (int bp1 = NCOLS; bp1 < NSQUARES; bp1++) {
                int bp1_physical = bp1;
                if (Row(bp1) == (NROWS - 1)) {
                    // can only have one e.p., and none if promoted
                    if (Row(wp1) == 0 || Row(wp2) == (NROWS - 1))
                        continue;
                    bp1_physical = bp1 - (3 * NCOLS);
                }
                if (board[bp1_physical])
                    continue;
                board[bp1_physical] = -PAWN;

                // now check whether there is a valid e.p.
                int ep_square = -1;
                if (Row(wp1) == 0) {
                    ep_square = wp1_physical - NCOLS;
                    if (board[ep_square] || board[ep_square - NCOLS]) {
                        board[bp1_physical] = 0;
                        continue;
                    }
                    bool ep_possible = false;
                    if ((Column(wp1_physical) > 0 &&
                         bp1_physical == wp1_physical - 1) ||
                        (Column(wp1_physical) < (NCOLS - 1) &&
                         bp1_physical == wp1_physical + 1)) {
                        ep_possible = true;
                    }
                    if (!ep_possible) {
                        board[bp1_physical] = 0;
                        continue;
                    }
                }
                if (Row(bp1) == (NROWS - 1)) {
                    ep_square = bp1_physical + NCOLS;
                    if (board[ep_square] || board[ep_square + NCOLS]) {
                        board[bp1_physical] = 0;
                        continue;
                    }
                    bool ep_possible = false;
                    if ((Column(bp1_physical) > 0 &&
                         (wp1_physical == bp1_physical - 1 ||
                          wp2 == bp1_physical - 1)) ||
                        (Column(bp1_physical) < (NCOLS - 1) &&
                         (wp1_physical == bp1_physical + 1 ||
                          wp2 == bp1_physical + 1))) {
                        ep_possible = true;
                    }
                    if (!ep_possible) {
                        board[bp1_physical] = 0;
                        continue;
                    }
                }
                // now check whether we have an opposing pair
                if ((Column(wp1_physical) == Column(bp1_physical) &&
                     wp1_physical < bp1_physical) ||
                    (Column(wp2) == Column(bp1_physical) &&
                     wp2 < bp1_physical)) {
                    if (index >= N2_1_OPPOSING) {
                        fprintf(stderr, "Two many 2 vs 1 pawn positions with "
                                        "an opposing pair\n");
                        exit(1);
                    }
                    pos[index] = bp1 + NSQUARES * (wp2 + NSQUARES * wp1);
                    tab[bp1 + NSQUARES * (wp2 + NSQUARES * wp1)] = index;
                    tab[bp1 + NSQUARES * (wp1 + NSQUARES * wp2)] = index;
                    index++;
                }
                board[bp1_physical] = 0;
            }
            board[wp2] = 0;
        }
        board[wp1_physical] = 0;
    }

    if (index != N2_1_OPPOSING) {
        fprintf(stderr,
                "Saw %d 2 vs 1 pawns with one opposing pairs, expected %d\n",
                index, N2_1_OPPOSING);
        exit(1);
    }
}

static void InitN1_2_OpposingTables(int *tab, int *pos) {
    int index = 0, board[NSQUARES];

    for (int i = 0; i < NSQUARES * NSQUARES * NSQUARES; i++) {
        tab[i] = -1;
    }

    memset(board, 0, sizeof(board));

    for (int wp1 = 0; wp1 < NSQUARES - NCOLS; wp1++) {
        int wp1_physical = wp1;
        if (Row(wp1) == 0) {
            wp1_physical = wp1 + (3 * NCOLS);
        }
        board[wp1_physical] = PAWN;
        // the first black pawn can be on first row for promotion
        for (int bp1 = 0; bp1 < (NSQUARES - NCOLS); bp1++) {
            // if the pawn is promoted, no e.p. is possible
            if (Row(bp1) == 0 && Row(wp1) == 0)
                continue;
            if (board[bp1])
                continue;
            board[bp1] = -PAWN;
            // the second black pawn can be on last row for e.p. capture
            for (int bp2 = bp1 + 1; bp2 < NSQUARES; bp2++) {
                // only one pawn can be promoted
                if (Row(bp2) == 0)
                    continue;
                int bp2_physical = bp2;
                if (Row(bp2) == (NROWS - 1)) {
                    // only one e.p. is possible
                    if (Row(wp1) == 0)
                        continue;
                    bp2_physical = bp2 - (3 * NCOLS);
                }
                if (board[bp2_physical])
                    continue;
                board[bp2_physical] = -PAWN;

                // now check whether there is a valid e.p.
                int ep_square = -1;
                if (Row(wp1) == 0) {
                    ep_square = wp1_physical - NCOLS;
                    if (board[ep_square] || board[ep_square - NCOLS]) {
                        board[bp2_physical] = 0;
                        continue;
                    }
                    bool ep_possible = false;
                    if ((Column(wp1_physical) > 0 &&
                         ((bp1 == wp1_physical - 1) ||
                          (bp2_physical == wp1_physical - 1))) ||
                        (Column(wp1_physical) < (NCOLS - 1) &&
                         ((bp1 == wp1_physical + 1) ||
                          (bp2_physical == wp1_physical + 1)))) {
                        ep_possible = true;
                    }
                    if (!ep_possible) {
                        board[bp2_physical] = 0;
                        continue;
                    }
                }
                if (Row(bp2) == (NROWS - 1)) {
                    ep_square = bp2_physical + NCOLS;
                    if (board[ep_square] || board[ep_square + NCOLS]) {
                        board[bp2_physical] = 0;
                        continue;
                    }
                    bool ep_possible = false;
                    if ((Column(bp2_physical) > 0 &&
                         (wp1_physical == bp2_physical - 1)) ||
                        (Column(bp2_physical) < (NCOLS - 1) &&
                         (wp1_physical == bp2_physical + 1))) {
                        ep_possible = true;
                    }
                    if (!ep_possible) {
                        board[bp2_physical] = 0;
                        continue;
                    }
                }
                // now check whether we have an opposing pair
                if ((Column(wp1_physical) == Column(bp1) &&
                     wp1_physical < bp1) ||
                    (Column(wp1_physical) == Column(bp2_physical) &&
                     wp1_physical < bp2_physical)) {
                    if (index >= N1_2_OPPOSING) {
                        fprintf(stderr, "Too many 1 vs 2 pawn positions with "
                                        "an opposing pair\n");
                        exit(1);
                    }
                    pos[index] = bp2 + NSQUARES * (bp1 + NSQUARES * wp1);
                    tab[bp2 + NSQUARES * (bp1 + NSQUARES * wp1)] = index;
                    tab[bp1 + NSQUARES * (bp2 + NSQUARES * wp1)] = index;
                    index++;
                }
                board[bp2_physical] = 0;
            }
            board[bp1] = 0;
        }
        board[wp1_physical] = 0;
    }

    if (index != N1_2_OPPOSING) {
        fprintf(stderr,
                "Saw %d 1 vs 2 pawns with one opposing pairs, expected %d\n",
                index, N1_2_OPPOSING);
        exit(1);
    }
}

static void InitN2_2_OpposingTables(int *tab, int *pos) {
    int index = 0, board[NSQUARES];

    for (int i = 0; i < NSQUARES * NSQUARES * NSQUARES * NSQUARES; i++) {
        tab[i] = -1;
    }

    memset(board, 0, sizeof(board));

    for (int wp1 = 0; wp1 < NSQUARES - NCOLS; wp1++) {
        int wp1_physical = wp1;
        if (Row(wp1) == 0) {
            wp1_physical = wp1 + (3 * NCOLS);
        }
        board[wp1_physical] = PAWN;
        for (int wp2 = wp1 + 1; wp2 < NSQUARES; wp2++) {
            // only one white pawn can be e.p. captured
            if (Row(wp2) == 0)
                continue;
            // if a pawn is promoted, there can be no e.p.
            if (Row(wp2) == (NROWS - 1) && Row(wp1) == 0)
                continue;
            if (board[wp2])
                continue;
            board[wp2] = PAWN;
            for (int bp1 = 0; bp1 < NSQUARES - NCOLS; bp1++) {
                if (Row(bp1) == 0 && (Row(wp2) == (NROWS - 1) || Row(wp1) == 0))
                    continue;
                if (board[bp1])
                    continue;
                board[bp1] = -PAWN;
                for (int bp2 = bp1 + 1; bp2 < NSQUARES; bp2++) {
                    if (Row(bp2) == 0)
                        continue;
                    int bp2_physical = bp2;
                    if (Row(bp2) == (NROWS - 1)) {
                        if (Row(bp1) == 0 || Row(wp1) == 0 ||
                            Row(wp2) == (NROWS - 1))
                            continue;
                        bp2_physical = bp2 - (3 * NCOLS);
                        if (board[bp2_physical]) {
                            continue;
                        }
                    } else {
                        if (board[bp2_physical])
                            continue;
                    }
                    board[bp2_physical] = -PAWN;

#if defined(OP_22_PROMOTION_CONSTRAINTS)
                    // if there is a promoted pawn, the square before the
                    // promoted pawn must be empty
                    if (Row(wp2) == (NROWS - 1)) {
                        if (board[wp2 - NCOLS]) {
                            board[bp2_physical] = 0;
                            continue;
                        }
                    }
                    if (Row(bp1) == 0) {
                        if (board[bp1 + NCOLS]) {
                            board[bp2_physical] = 0;
                            continue;
                        }
                    }
#endif

                    int ep_square = -1;
                    // if there is a ep_square, check whether it is indeed a
                    // e.p. position
                    if (wp1 != wp1_physical) {
                        ep_square = wp1_physical - NCOLS;
                        if (board[ep_square] || board[ep_square - NCOLS]) {
                            board[bp2_physical] = 0;
                            continue;
                        }
                        bool ep_possible = false;
                        if ((Column(wp1_physical) > 0 &&
                             board[wp1_physical - 1] == -PAWN) ||
                            (Column(wp1_physical) < (NCOLS - 1) &&
                             board[wp1_physical + 1] == -PAWN))
                            ep_possible = true;
                        if (!ep_possible) {
                            board[bp2_physical] = 0;
                            continue;
                        }
                    }
                    if (bp2 != bp2_physical) {
                        ep_square = bp2_physical + NCOLS;
                        if (board[ep_square] || board[ep_square + NCOLS]) {
                            board[bp2_physical] = 0;
                            continue;
                        }
                        bool ep_possible = false;
                        if ((Column(bp2_physical) > 0 &&
                             board[bp2_physical - 1] == PAWN) ||
                            (Column(bp2_physical) < (NCOLS - 1) &&
                             board[bp2_physical + 1] == PAWN))
                            ep_possible = true;
                        if (!ep_possible) {
                            board[bp2_physical] = 0;
                            continue;
                        }
                    }

                    // now check whether there are is at least one opposing pawn
                    // pair
                    int num_white_pawns = 2, num_black_pawns = 2;
                    int white_pawn_position[2] = {wp1_physical, wp2};
                    int black_pawn_position[2] = {bp1, bp2_physical};
                    bool black_pawn_paired[2] = {false, false};
                    int num_opposing = 0;
                    for (int i = 0; i < num_white_pawns; i++) {
                        int wpos = white_pawn_position[i];
                        int paired = -1;
                        // find the lowest available black pawn that can be
                        // paired
                        for (int j = 0; j < num_black_pawns; j++) {
                            if (black_pawn_paired[j])
                                continue;
                            int bpos = black_pawn_position[j];
                            if (Column(wpos) == Column(bpos) && bpos > wpos) {
                                if (paired == -1 || bpos < paired)
                                    paired = bpos;
                            }
                        }
                        if (paired != -1) {
                            num_opposing++;
                            // remove black pawns that have been paired
                            for (int j = 0; j < num_black_pawns; j++) {
                                if (black_pawn_paired[j])
                                    continue;
                                int bpos = black_pawn_position[j];
                                if (paired == bpos) {
                                    black_pawn_paired[j] = true;
                                    break;
                                }
                            }
                        }
                    }
                    if (num_opposing < 1) {
                        board[bp2_physical] = 0;
                        continue;
                    }
                    pos[index] =
                        bp2 +
                        NSQUARES * (bp1 + NSQUARES * (wp2 + NSQUARES * wp1));
                    tab[bp2 +
                        NSQUARES * (bp1 + NSQUARES * (wp2 + NSQUARES * wp1))] =
                        index;
                    tab[bp1 +
                        NSQUARES * (bp2 + NSQUARES * (wp2 + NSQUARES * wp1))] =
                        index;
                    tab[bp2 +
                        NSQUARES * (bp1 + NSQUARES * (wp1 + NSQUARES * wp2))] =
                        index;
                    tab[bp1 +
                        NSQUARES * (bp2 + NSQUARES * (wp1 + NSQUARES * wp2))] =
                        index;
                    index++;
                    board[bp2_physical] = 0;
                }
                board[bp1] = 0;
            }
            board[wp2] = 0;
        }
        board[wp1_physical] = 0;
    }

    if (index != N2_2_OPPOSING) {
        fprintf(stderr,
                "Saw %d 2 vs 2 pawns with at least one opposing pair, expected "
                "%d\n",
                index, N2_2_OPPOSING);
        exit(1);
    }
}

static void InitN3_1_OpposingTables(int *tab, int *pos) {
    int index = 0, board[NSQUARES];

    for (int i = 0; i < NSQUARES * NSQUARES * NSQUARES * NSQUARES; i++) {
        tab[i] = -1;
    }

    memset(board, 0, sizeof(board));

    for (int wp1 = 0; wp1 < NSQUARES - NCOLS; wp1++) {
        int wp1_physical = wp1;
        if (Row(wp1) == 0) {
            wp1_physical = wp1 + (3 * NCOLS);
        }
        board[wp1_physical] = PAWN;
        for (int wp2 = wp1 + 1; wp2 < NSQUARES - NCOLS; wp2++) {
            // only one white pawn can be e.p. captured
            if (Row(wp2) == 0)
                continue;
            if (board[wp2])
                continue;
            board[wp2] = PAWN;
            for (int wp3 = wp2 + 1; wp3 < NSQUARES; wp3++) {
                // only one white pawn can be e.p. captured
                if (Row(wp3) == 0)
                    continue;
                // if pawn is promoted, no e.p. possible
                if (Row(wp3) == (NROWS - 1) && Row(wp1) == 0)
                    continue;
                if (board[wp3])
                    continue;
                board[wp3] = PAWN;
                for (int bp1 = 2 * NCOLS; bp1 < NSQUARES; bp1++) {
                    int bp1_physical = bp1;
                    if (Row(bp1) == (NROWS - 1)) {
                        // can't have wp e.p. or promotion
                        if (Row(wp1) == 0 || Row(wp3) == (NROWS - 1))
                            continue;
                        bp1_physical = bp1 - (3 * NCOLS);
                    }
                    if (board[bp1_physical])
                        continue;
                    board[bp1_physical] = -PAWN;

                    int ep_square = -1;
                    // if there is a ep_square, check whether it is indeed a
                    // e.p. position
                    if (wp1 != wp1_physical) {
                        ep_square = wp1_physical - NCOLS;
                        if (board[ep_square] || board[ep_square - NCOLS]) {
                            board[bp1_physical] = 0;
                            continue;
                        }
                        bool ep_possible = false;
                        if ((Column(wp1_physical) > 0 &&
                             board[wp1_physical - 1] == -PAWN) ||
                            (Column(wp1_physical) < (NCOLS - 1) &&
                             board[wp1_physical + 1] == -PAWN))
                            ep_possible = true;
                        if (!ep_possible) {
                            board[bp1_physical] = 0;
                            continue;
                        }
                    }
                    if (bp1 != bp1_physical) {
                        ep_square = bp1_physical + NCOLS;
                        if (board[ep_square] || board[ep_square + NCOLS]) {
                            board[bp1_physical] = 0;
                            continue;
                        }
                        bool ep_possible = false;
                        if ((Column(bp1_physical) > 0 &&
                             board[bp1_physical - 1] == PAWN) ||
                            (Column(bp1_physical) < (NCOLS - 1) &&
                             board[bp1_physical + 1] == PAWN))
                            ep_possible = true;
                        if (!ep_possible) {
                            board[bp1_physical] = 0;
                            continue;
                        }
                    }

                    // now check whether there are is at least one opposing pawn
                    // pair
                    int num_white_pawns = 3, num_black_pawns = 1;
                    int white_pawn_position[3] = {wp1_physical, wp2, wp3};
                    int black_pawn_position[1] = {bp1_physical};
                    bool black_pawn_paired[1] = {false};
                    int num_opposing = 0;
                    for (int i = 0; i < num_white_pawns; i++) {
                        int wpos = white_pawn_position[i];
                        int paired = -1;
                        // find the lowest available black pawn that can be
                        // paired
                        for (int j = 0; j < num_black_pawns; j++) {
                            if (black_pawn_paired[j])
                                continue;
                            int bpos = black_pawn_position[j];
                            if (Column(wpos) == Column(bpos) && bpos > wpos) {
                                if (paired == -1 || bpos < paired)
                                    paired = bpos;
                            }
                        }
                        if (paired != -1) {
                            num_opposing++;
                            // remove black pawns that have been paired
                            for (int j = 0; j < num_black_pawns; j++) {
                                if (black_pawn_paired[j])
                                    continue;
                                int bpos = black_pawn_position[j];
                                if (paired == bpos) {
                                    black_pawn_paired[j] = true;
                                    break;
                                }
                            }
                        }
                    }
                    if (num_opposing < 1) {
                        board[bp1_physical] = 0;
                        continue;
                    }
                    pos[index] =
                        bp1 +
                        NSQUARES * (wp3 + NSQUARES * (wp2 + NSQUARES * wp1));
                    tab[bp1 +
                        NSQUARES * (wp3 + NSQUARES * (wp2 + NSQUARES * wp1))] =
                        index;
                    tab[bp1 +
                        NSQUARES * (wp2 + NSQUARES * (wp3 + NSQUARES * wp1))] =
                        index;
                    tab[bp1 +
                        NSQUARES * (wp3 + NSQUARES * (wp1 + NSQUARES * wp2))] =
                        index;
                    tab[bp1 +
                        NSQUARES * (wp1 + NSQUARES * (wp3 + NSQUARES * wp2))] =
                        index;
                    tab[bp1 +
                        NSQUARES * (wp2 + NSQUARES * (wp1 + NSQUARES * wp3))] =
                        index;
                    tab[bp1 +
                        NSQUARES * (wp1 + NSQUARES * (wp2 + NSQUARES * wp3))] =
                        index;
                    index++;
                    board[bp1_physical] = 0;
                }
                board[wp3] = 0;
            }
            board[wp2] = 0;
        }
        board[wp1_physical] = 0;
    }

    if (index != N3_1_OPPOSING) {
        fprintf(stderr,
                "Saw %d 3 vs 1 pawns with at least one opposing pair, expected "
                "%d\n",
                index, N3_1_OPPOSING);
        exit(1);
    }
}

static void InitN1_3_OpposingTables(int *tab, int *pos) {
    int index = 0, board[NSQUARES];

    for (int i = 0; i < NSQUARES * NSQUARES * NSQUARES * NSQUARES; i++) {
        tab[i] = -1;
    }

    memset(board, 0, sizeof(board));

    for (int bp1 = 0; bp1 < NSQUARES - NCOLS; bp1++) {
        board[bp1] = -PAWN;
        for (int bp2 = bp1 + 1; bp2 < NSQUARES - NCOLS; bp2++) {
            // only one black pawn can be promoted
            if (Row(bp2) == 0)
                continue;
            if (board[bp2])
                continue;
            board[bp2] = -PAWN;
            for (int bp3 = bp2 + 1; bp3 < NSQUARES; bp3++) {
                // only one black pawn can be promoted
                if (Row(bp3) == 0)
                    continue;
                int bp3_physical = bp3;
                if (Row(bp3) == (NROWS - 1)) {
                    // if pawn is promoted, no e.p. is possible
                    if (Row(bp1) == 0)
                        continue;
                    bp3_physical = bp3 - (3 * NCOLS);
                }
                if (board[bp3_physical])
                    continue;
                board[bp3_physical] = -PAWN;
                for (int wp1 = 0; wp1 < NSQUARES - NCOLS; wp1++) {
                    int wp1_physical = wp1;
                    if (Row(wp1) == 0) {
                        // can't have bp e.p. or promoted
                        if (Row(bp1) == 0 || Row(bp3) == (NROWS - 1))
                            continue;
                        wp1_physical = wp1 + (3 * NCOLS);
                    }
                    if (board[wp1_physical])
                        continue;
                    board[wp1_physical] = PAWN;

                    int ep_square = -1;
                    // if there is a ep_square, check whether it is indeed a
                    // e.p. position
                    if (wp1 != wp1_physical) {
                        ep_square = wp1_physical - NCOLS;
                        if (board[ep_square] || board[ep_square - NCOLS]) {
                            board[wp1_physical] = 0;
                            continue;
                        }
                        bool ep_possible = false;
                        if ((Column(wp1_physical) > 0 &&
                             board[wp1_physical - 1] == -PAWN) ||
                            (Column(wp1_physical) < (NCOLS - 1) &&
                             board[wp1_physical + 1] == -PAWN))
                            ep_possible = true;
                        if (!ep_possible) {
                            board[wp1_physical] = 0;
                            continue;
                        }
                    }
                    if (bp3 != bp3_physical) {
                        ep_square = bp3_physical + NCOLS;
                        if (board[ep_square] || board[ep_square + NCOLS]) {
                            board[wp1_physical] = 0;
                            continue;
                        }
                        bool ep_possible = false;
                        if ((Column(bp3_physical) > 0 &&
                             board[bp3_physical - 1] == PAWN) ||
                            (Column(bp3_physical) < (NCOLS - 1) &&
                             board[bp3_physical + 1] == PAWN))
                            ep_possible = true;
                        if (!ep_possible) {
                            board[wp1_physical] = 0;
                            continue;
                        }
                    }

                    // now check whether there are is at least one opposing pawn
                    // pair
                    int num_white_pawns = 1, num_black_pawns = 3;
                    int white_pawn_position[1] = {wp1_physical};
                    int black_pawn_position[3] = {bp1, bp2, bp3_physical};
                    bool black_pawn_paired[3] = {false, false, false};
                    int num_opposing = 0;
                    for (int i = 0; i < num_white_pawns; i++) {
                        int wpos = white_pawn_position[i];
                        int paired = -1;
                        // find the lowest available black pawn that can be
                        // paired
                        for (int j = 0; j < num_black_pawns; j++) {
                            if (black_pawn_paired[j])
                                continue;
                            int bpos = black_pawn_position[j];
                            if (Column(wpos) == Column(bpos) && bpos > wpos) {
                                if (paired == -1 || bpos < paired)
                                    paired = bpos;
                            }
                        }
                        if (paired != -1) {
                            num_opposing++;
                            // remove black pawns that have been paired
                            for (int j = 0; j < num_black_pawns; j++) {
                                if (black_pawn_paired[j])
                                    continue;
                                int bpos = black_pawn_position[j];
                                if (paired == bpos) {
                                    black_pawn_paired[j] = true;
                                    break;
                                }
                            }
                        }
                    }
                    if (num_opposing < 1) {
                        board[wp1_physical] = 0;
                        continue;
                    }
                    pos[index] =
                        bp3 +
                        NSQUARES * (bp2 + NSQUARES * (bp1 + NSQUARES * wp1));
                    tab[bp3 +
                        NSQUARES * (bp2 + NSQUARES * (bp1 + NSQUARES * wp1))] =
                        index;
                    tab[bp3 +
                        NSQUARES * (bp1 + NSQUARES * (bp2 + NSQUARES * wp1))] =
                        index;
                    tab[bp1 +
                        NSQUARES * (bp3 + NSQUARES * (bp2 + NSQUARES * wp1))] =
                        index;
                    tab[bp1 +
                        NSQUARES * (bp2 + NSQUARES * (bp3 + NSQUARES * wp1))] =
                        index;
                    tab[bp2 +
                        NSQUARES * (bp3 + NSQUARES * (bp1 + NSQUARES * wp1))] =
                        index;
                    tab[bp2 +
                        NSQUARES * (bp1 + NSQUARES * (bp3 + NSQUARES * wp1))] =
                        index;
                    index++;
                    board[wp1_physical] = 0;
                }
                board[bp3_physical] = 0;
            }
            board[bp2] = 0;
        }
        board[bp1] = 0;
    }

    if (index != N1_3_OPPOSING) {
        fprintf(stderr,
                "Saw %d 1 vs 3 pawns with at least one opposing pair, expected "
                "%d\n",
                index, N1_3_OPPOSING);
        exit(1);
    }
}

enum { ONE_COLUMN = 0, ADJACENT, NON_ADJACENT, NO_DP22 };

int IsValidDP22(int w1, int w2, int b1, int b2) {
    if (w1 == w2 || w1 == b1 || w1 == b2 || w2 == b1 || w2 == b2 || b1 == b2)
        return NO_DP22;
    int w1_row = Row(w1);
    if (w1_row < 1 || w1_row > (NROWS - 3))
        return NO_DP22;
    int w2_row = Row(w2);
    if (w2_row < 1 || w2_row > (NROWS - 3))
        return NO_DP22;
    int b1_row = Row(b1);
    if (b1_row < 2 || b1_row > (NROWS - 2))
        return NO_DP22;
    int b2_row = Row(b2);
    if (b2_row < 2 || b2_row > (NROWS - 2))
        return NO_DP22;

    int w1_col = Column(w1);
    int w2_col = Column(w2);
    int b1_col = Column(b1);
    int b2_col = Column(b2);

    // swap two white pawns so that the second one has the higher row
    if (w2_row < w1_row) {
        int tmp = w1_row;
        w1_row = w2_row;
        w2_row = tmp;
        tmp = w1_col;
        w1_col = w2_col;
        w2_col = tmp;
    }

    // swap black pawns so that b1 has same column as w1 and b2 the same column
    // as w2
    if (w1_col == b2_col && w2_col == b1_col) {
        int tmp = b1_col;
        b1_col = b2_col;
        b2_col = tmp;
        tmp = b1_row;
        b1_row = b2_row;
        b2_row = tmp;
    }

    if (w1_col == w2_col) {
        if (w1_col == b1_col && b1_col == b2_col &&
            w1_row < min(b1_row, b2_row) && w2_row < max(b1_row, b2_row))
            return ONE_COLUMN;
        return NO_DP22;
    }

    if (!(w1_col == b1_col && b1_row > w1_row && w2_col == b2_col &&
          b2_row > w2_row))
        return NO_DP22;

    if (w1_col == (w2_col + 1) || w1_col == (w2_col - 1)) {
        if (w2_row >= b1_row)
            return ADJACENT;
        return NO_DP22;
    }

    return NON_ADJACENT;
}

void DP22PosFromIndex(int index, int *w1, int *w2, int *b1, int *b2) {
    int pos_array = p4_opposing_tab[index];
    int b2_row = pos_array % NROWS;
    pos_array /= NROWS;
    int b1_row = pos_array % NROWS;
    pos_array /= NROWS;
    *w2 = pos_array % NSQUARES;
    pos_array /= NSQUARES;
    *w1 = pos_array % NSQUARES;
    int w1_col = Column(*w1);
    int w2_col = Column(*w2);
    *b1 = SquareMake(b1_row, w1_col);
    *b2 = SquareMake(b2_row, w2_col);
}

ZINDEX IndexDP22(int *);

static void InitN4OpposingTables(int *tab, int *pos) {
    for (int w1 = 0; w1 < NSQUARES; w1++) {
        for (int w2 = 0; w2 < NSQUARES; w2++) {
            for (int b1_r = 0; b1_r < NROWS; b1_r++) {
                for (int b2_r = 0; b2_r < NROWS; b2_r++) {
                    if (tab != NULL) {
                        tab[b2_r +
                            NROWS * (b1_r + NROWS * (w2 + NSQUARES * w1))] = -1;
                    }
                }
            }
        }
    }

    int index = 0, one_column = 0, adjacent = 0, non_adjacent = 0;
    for (int w1 = 0; w1 < NSQUARES; w1++) {
        for (int w2 = w1 + 1; w2 < NSQUARES; w2++) {
            for (int b1 = 0; b1 < NSQUARES; b1++) {
                for (int b2 = b1 + 1; b2 < NSQUARES; b2++) {
                    int dp_type = IsValidDP22(w1, w2, b1, b2);
                    if (dp_type == NO_DP22)
                        continue;
                    if (dp_type == ONE_COLUMN)
                        one_column++;
                    else if (dp_type == ADJACENT)
                        adjacent++;
                    else if (dp_type == NON_ADJACENT)
                        non_adjacent++;
                    if (index >= N4_OPPOSING) {
                        fprintf(stderr, "Too many pairs of opposing pawns\n");
                        exit(1);
                    }
                    int pos_array_00, pos_array_10, pos_array_01, pos_array_11;
                    int w1_col = Column(w1);
                    int b1_col = Column(b1);
                    int w2_col = Column(w2);
                    int b2_col = Column(b2);
                    if (w1_col == b1_col) {
                        pos_array_00 =
                            Row(b2) +
                            NROWS * (Row(b1) + NROWS * (w2 + NSQUARES * w1));
                        pos_array_10 =
                            Row(b2) +
                            NROWS * (Row(b1) + NROWS * (w1 + NSQUARES * w2));
                        pos_array_01 =
                            Row(b1) +
                            NROWS * (Row(b2) + NROWS * (w2 + NSQUARES * w1));
                        pos_array_11 =
                            Row(b1) +
                            NROWS * (Row(b2) + NROWS * (w1 + NSQUARES * w2));
                    } else {
                        pos_array_00 =
                            Row(b1) +
                            NROWS * (Row(b2) + NROWS * (w2 + NSQUARES * w1));
                        pos_array_10 =
                            Row(b1) +
                            NROWS * (Row(b2) + NROWS * (w1 + NSQUARES * w2));
                        pos_array_01 =
                            Row(b2) +
                            NROWS * (Row(b1) + NROWS * (w2 + NSQUARES * w1));
                        pos_array_11 =
                            Row(b2) +
                            NROWS * (Row(b1) + NROWS * (w1 + NSQUARES * w2));
                    }
                    if (tab != NULL) {
                        tab[pos_array_00] = index;
                        assert(tab[pos_array_11] == -1 ||
                               tab[pos_array_11] == index);
                        tab[pos_array_11] = index;
                        if (w1_col == w2_col) {
                            assert(tab[pos_array_10] == -1 ||
                                   tab[pos_array_10] == index);
                            tab[pos_array_10] = index;
                            assert(tab[pos_array_01] == -1 ||
                                   tab[pos_array_01] == index);
                            tab[pos_array_01] = index;
                        }
                    }
                    if (pos != NULL) {
                        pos[index] = pos_array_00;
                    }
                    index++;
                }
            }
        }
    }

    assert(one_column == N4_ONE_COLUMN);
    assert(adjacent == N4_ADJACENT);
    assert(non_adjacent == N4_NON_ADJACENT);
    if (index != N4_OPPOSING) {
        fprintf(stderr,
                "Got %d pairs of opposing pawn positions, expected %d\n", index,
                N4_OPPOSING);
        exit(1);
    }

    for (int w1 = 0; w1 < NSQUARES; w1++) {
        for (int w2 = 0; w2 < NSQUARES; w2++) {
            for (int b1 = 0; b1 < NSQUARES; b1++) {
                for (int b2 = 0; b2 < NSQUARES; b2++) {
                    int dp_type = IsValidDP22(w1, w2, b1, b2);
                    if (dp_type == NO_DP22)
                        continue;
                    int tpos[6];
                    tpos[2] = w1;
                    tpos[3] = w2;
                    tpos[4] = b1;
                    tpos[5] = b2;
                    ZINDEX index = IndexDP22(tpos);
                    if (index == ALL_ONES) {
                        fprintf(stderr, "Bad w1 %o w2 %o b1 %o b2 %o\n", w1, w2,
                                b1, b2);
                    }
                }
            }
        }
    }
}

static void InitN3Tables(int *tab, int *pos) {
    int index = 0, score;

#if defined(MB_INDEX)
    for (int p3 = 2; p3 < NSQUARES; p3++) {
        for (int p2 = 1; p2 <= p3; p2++) {
            for (int p1 = 0; p1 <= p2; p1++) {
#else // index used by Yakov Konoval
    for (int p1 = 0; p1 < NSQUARES; p1++) {
        for (int p2 = p1; p2 < NSQUARES; p2++) {
            for (int p3 = p2; p3 < NSQUARES; p3++) {
#endif
                if (p1 == p2 || p1 == p3 || p2 == p3) {
                    score = -1;
                } else {
                    if (pos != NULL)
                        pos[index] = p3 + NSQUARES * (p2 + NSQUARES * p1);
                    int g_index = N3_Index_Function(p3, p2, p1);
                    if (index != g_index) {
                        fprintf(stderr,
                                "Bad triplet index: index=%d computed index=%d "
                                "p1=%d p2=%d p3=%d\n",
                                index, g_index, p1, p2, p3);
                        exit(1);
                    }
                    score = index++;
                }
                if (tab != NULL) {
                    tab[p1 + NSQUARES * (p2 + NSQUARES * p3)] = score;
                    tab[p1 + NSQUARES * (p3 + NSQUARES * p2)] = score;

                    tab[p2 + NSQUARES * (p1 + NSQUARES * p3)] = score;
                    tab[p2 + NSQUARES * (p3 + NSQUARES * p1)] = score;

                    tab[p3 + NSQUARES * (p2 + NSQUARES * p1)] = score;
                    tab[p3 + NSQUARES * (p1 + NSQUARES * p2)] = score;
                }
            }
        }
    }

    if (index != N3) {
        fprintf(stderr, "Got %d triplets, expected %d\n", index, N3);
        exit(1);
    }
}

static void InitN4Tables(int *tab, int *pos) {
    int index = 0, score;

#if defined(MB_INDEX)
    for (int p4 = 3; p4 < NSQUARES; p4++) {
        for (int p3 = 2; p3 <= p4; p3++) {
            for (int p2 = 1; p2 <= p3; p2++) {
                for (int p1 = 0; p1 <= p2; p1++) {
#else // index used by Yakov Konoval
    for (int p1 = 0; p1 < NSQUARES; p1++) {
        for (int p2 = p1; p2 < NSQUARES; p2++) {
            for (int p3 = p2; p3 < NSQUARES; p3++) {
                for (int p4 = p3; p4 < NSQUARES; p4++) {
#endif
                    if (p1 == p2 || p1 == p3 || p1 == p4 || p2 == p3 ||
                        p2 == p4 || p3 == p4) {
                        score = -1;
                    } else {
                        if (pos != NULL)
                            pos[index] =
                                p4 + NSQUARES *
                                         (p3 + NSQUARES * (p2 + NSQUARES * p1));
                        int g_index = N4_Index_Function(p4, p3, p2, p1);
                        if (index != g_index) {
                            fprintf(stderr,
                                    "Bad quadruplet index: index=%d computed "
                                    "index=%d p1=%d p2=%d p3=%d p4=%d\n",
                                    index, g_index, p1, p2, p3, p4);
                            exit(1);
                        }
                        score = index++;
                    }
                    if (tab != NULL) {
                        tab[p1 +
                            NSQUARES * (p2 + NSQUARES * (p3 + p4 * NSQUARES))] =
                            score;
                        tab[p1 +
                            NSQUARES * (p2 + NSQUARES * (p4 + p3 * NSQUARES))] =
                            score;
                        tab[p1 +
                            NSQUARES * (p3 + NSQUARES * (p2 + p4 * NSQUARES))] =
                            score;
                        tab[p1 +
                            NSQUARES * (p3 + NSQUARES * (p4 + p2 * NSQUARES))] =
                            score;
                        tab[p1 +
                            NSQUARES * (p4 + NSQUARES * (p3 + p2 * NSQUARES))] =
                            score;
                        tab[p1 +
                            NSQUARES * (p4 + NSQUARES * (p2 + p3 * NSQUARES))] =
                            score;

                        tab[p2 +
                            NSQUARES * (p1 + NSQUARES * (p3 + p4 * NSQUARES))] =
                            score;
                        tab[p2 +
                            NSQUARES * (p1 + NSQUARES * (p4 + p3 * NSQUARES))] =
                            score;
                        tab[p2 +
                            NSQUARES * (p3 + NSQUARES * (p4 + p1 * NSQUARES))] =
                            score;
                        tab[p2 +
                            NSQUARES * (p3 + NSQUARES * (p1 + p4 * NSQUARES))] =
                            score;
                        tab[p2 +
                            NSQUARES * (p4 + NSQUARES * (p1 + p3 * NSQUARES))] =
                            score;
                        tab[p2 +
                            NSQUARES * (p4 + NSQUARES * (p3 + p1 * NSQUARES))] =
                            score;

                        tab[p3 +
                            NSQUARES * (p2 + NSQUARES * (p1 + p4 * NSQUARES))] =
                            score;
                        tab[p3 +
                            NSQUARES * (p2 + NSQUARES * (p4 + p1 * NSQUARES))] =
                            score;
                        tab[p3 +
                            NSQUARES * (p1 + NSQUARES * (p4 + p2 * NSQUARES))] =
                            score;
                        tab[p3 +
                            NSQUARES * (p1 + NSQUARES * (p2 + p4 * NSQUARES))] =
                            score;
                        tab[p3 +
                            NSQUARES * (p4 + NSQUARES * (p2 + p1 * NSQUARES))] =
                            score;
                        tab[p3 +
                            NSQUARES * (p4 + NSQUARES * (p1 + p2 * NSQUARES))] =
                            score;

                        tab[p4 +
                            NSQUARES * (p2 + NSQUARES * (p3 + p1 * NSQUARES))] =
                            score;
                        tab[p4 +
                            NSQUARES * (p2 + NSQUARES * (p1 + p3 * NSQUARES))] =
                            score;
                        tab[p4 +
                            NSQUARES * (p1 + NSQUARES * (p2 + p3 * NSQUARES))] =
                            score;
                        tab[p4 +
                            NSQUARES * (p1 + NSQUARES * (p3 + p2 * NSQUARES))] =
                            score;
                        tab[p4 +
                            NSQUARES * (p3 + NSQUARES * (p1 + p2 * NSQUARES))] =
                            score;
                        tab[p4 +
                            NSQUARES * (p3 + NSQUARES * (p2 + p1 * NSQUARES))] =
                            score;
                    }
                }
            }
        }
    }

    if (index != N4) {
        fprintf(stderr, "Got %d quadruplets, expected %d\n", index, N4);
        exit(1);
    }
}

static void InitN4TablesMB(int *pos) {
    int index = 0, score;

    for (int p4 = 3; p4 < NSQUARES; p4++) {
        for (int p3 = 2; p3 <= p4; p3++) {
            for (int p2 = 1; p2 <= p3; p2++) {
                for (int p1 = 0; p1 <= p2; p1++) {
                    if (p1 == p2 || p1 == p3 || p1 == p4 || p2 == p3 ||
                        p2 == p4 || p3 == p4) {
                        score = -1;
                    } else {
                        if (pos != NULL)
                            pos[index] =
                                p4 + NSQUARES *
                                         (p3 + NSQUARES * (p2 + NSQUARES * p1));
                        int g_index = p4 * (p4 - 1) * (p4 - 2) * (p4 - 3) / 24 +
                                      p3 * (p3 - 1) * (p3 - 2) / 6 +
                                      p2 * (p2 - 1) / 2 + p1;
                        if (index != g_index) {
                            fprintf(stderr,
                                    "Bad quadruplet index: index=%d computed "
                                    "index=%d p1=%d p2=%d p3=%d p4=%d\n",
                                    index, g_index, p1, p2, p3, p4);
                            exit(1);
                        }
                        score = index++;
                    }
                }
            }
        }
    }

    if (index != N4) {
        fprintf(stderr, "Got %d quadruplets, expected %d in InitN4TablesMB\n",
                index, N4);
        exit(1);
    }
}

static void InitN5Tables() {
    unsigned int index = 0;

    for (unsigned int i = 0; i <= NSQUARES; i++)
        k5_tab[i] = i * (i - 1) * (i - 2) * (i - 3) * (i - 4) / 120;

    for (int p5 = 4; p5 < NSQUARES; p5++) {
        for (int p4 = 3; p4 < p5; p4++) {
            for (int p3 = 2; p3 < p4; p3++) {
                for (int p2 = 1; p2 < p3; p2++) {
                    for (int p1 = 0; p1 < p2; p1++) {
                        int g_index = N5_Index_Function(p5, p4, p3, p2, p1);
                        if (index != g_index) {
                            fprintf(stderr,
                                    "Bad quintuplet index: index=%d computed "
                                    "index=%d p1=%d p2=%d p3=%d p4=%d p5=%d\n",
                                    index, g_index, p1, p2, p3, p4, p5);
                            exit(1);
                        }
                        index++;
                    }
                }
            }
        }
    }

    if (index != N5) {
        fprintf(stderr, "Got %lu quintuplets, expected %lu\n", index, N5);
        exit(1);
    }
}

static void InitN6Tables() {
    unsigned int index = 0;

    for (unsigned int i = 0; i <= NSQUARES; i++)
        k6_tab[i] =
            (i * (i - 1) * (i - 2) * (i - 3) * (i - 4) / 120) * (i - 5) / 6;

    for (int p6 = 5; p6 < NSQUARES; p6++) {
        for (int p5 = 4; p5 < p6; p5++) {
            for (int p4 = 3; p4 < p5; p4++) {
                for (int p3 = 2; p3 < p4; p3++) {
                    for (int p2 = 1; p2 < p3; p2++) {
                        for (int p1 = 0; p1 < p2; p1++) {
                            int g_index =
                                N6_Index_Function(p6, p5, p4, p3, p2, p1);
                            if (index != g_index) {
                                fprintf(stderr,
                                        "Bad sextuplet index: index=%d "
                                        "computed index=%d p1=%d p2=%d p3=%d "
                                        "p4=%d p5=%d p6=%d\n",
                                        index, g_index, p1, p2, p3, p4, p5, p6);
                                exit(1);
                            }
                            index++;
                        }
                    }
                }
            }
        }
    }

    if (index != N6) {
        fprintf(stderr, "Got %lu sextuplets, expected %lu\n", index, N6);
        exit(1);
    }
}

static void InitN7Tables() {
    ZINDEX index = 0;

    for (unsigned int i = 0; i <= NSQUARES; i++) {
        ZINDEX itmp =
            (i * (i - 1) * (i - 2) * (i - 3) * (i - 4) / 120) * (i - 5) / 6;
        if (itmp % 7)
            k7_tab[i] = itmp * ((i - 6) / 7);
        else
            k7_tab[i] = (itmp / 7) * (i - 6);
    }

    for (int p7 = 6; p7 < NSQUARES; p7++) {
        for (int p6 = 5; p6 < p7; p6++) {
            for (int p5 = 4; p5 < p6; p5++) {
                for (int p4 = 3; p4 < p5; p4++) {
                    for (int p3 = 2; p3 < p4; p3++) {
                        for (int p2 = 1; p2 < p3; p2++) {
                            for (int p1 = 0; p1 < p2; p1++) {
                                ZINDEX g_index = N7_Index_Function(
                                    p7, p6, p5, p4, p3, p2, p1);
                                if (index != g_index) {
                                    fprintf(stderr,
                                            "Bad septuplet index: "
                                            "index=" DEC_ZINDEX_FORMAT
                                            " computed index=" DEC_ZINDEX_FORMAT
                                            " p1=%d p2=%d p3=%d p4=%d p5=%d "
                                            "p6=%d p7=%d\n",
                                            index, g_index, p1, p2, p3, p4, p5,
                                            p6, p7);
                                    exit(1);
                                }
                                index++;
                            }
                        }
                    }
                }
            }
        }
    }

    if (index != N7) {
        fprintf(stderr, "Got " DEC_ZINDEX_FORMAT " septuplets, expected %d\n",
                index, N7);
        exit(1);
    }
}

static void InitPermutationTables(int count[2][KING], int bishop_parity[2]) {
    static bool m4_inited = false, m3_inited = false, m2_inited = false;
    static bool m5_inited = false, m6_inited = false, m7_inited = false;
    static bool m2_even_inited = false, m2_odd_inited = false;
    static bool m3_even_inited = false, m3_odd_inited = false;
    static bool m2_opposing_inited = false;
    static bool m2_1_opposing_inited = false;
    static bool m1_2_opposing_inited = false;
    static bool m2_2_opposing_inited = false;
    static bool m3_1_opposing_inited = false;
    static bool m1_3_opposing_inited = false;
    static bool m4_opposing_inited = false;

    if (!m2_opposing_inited) {
        if (Verbose > 1) {
            MyPrintf("Initializing and checking permutation table for 2 "
                     "opposing pawns\n");
        }
        if (k2_opposing_tab == NULL) {
            k2_opposing_tab =
                (int *)MyMalloc(NSQUARES * NSQUARES * sizeof(int));
        }
        if (p2_opposing_tab == NULL) {
            p2_opposing_tab = (int *)MyMalloc(NCOLS * (NROWS - 2) *
                                              (NROWS - 3) / 2 * sizeof(int));
        }
        InitN2OpposingTables(k2_opposing_tab, p2_opposing_tab);
        m2_opposing_inited = true;
    }

    if (!m2_1_opposing_inited) {
        if (Verbose > 1) {
            MyPrintf("Initializing permutation tables for two vs one pawn, "
                     "with one opposing pair\n");
        }
        if (k2_1_opposing_tab == NULL) {
            k2_1_opposing_tab =
                (int *)MyMalloc(NSQUARES * NSQUARES * NSQUARES * sizeof(int));
        }
        if (p2_1_opposing_tab == NULL) {
            p2_1_opposing_tab = (int *)MyMalloc(N2_1_OPPOSING * sizeof(int));
        }
        InitN2_1_OpposingTables(k2_1_opposing_tab, p2_1_opposing_tab);
        m2_1_opposing_inited = true;
    }

    if (!m1_2_opposing_inited) {
        if (Verbose > 1) {
            MyPrintf("Initializing permutation tables for one vs two pawns, "
                     "with one opposing pair\n");
        }
        if (k1_2_opposing_tab == NULL) {
            k1_2_opposing_tab =
                (int *)MyMalloc(NSQUARES * NSQUARES * NSQUARES * sizeof(int));
        }
        if (p1_2_opposing_tab == NULL) {
            p1_2_opposing_tab = (int *)MyMalloc(N1_2_OPPOSING * sizeof(int));
        }
        InitN1_2_OpposingTables(k1_2_opposing_tab, p1_2_opposing_tab);
        m1_2_opposing_inited = true;
    }

    if (!m2_2_opposing_inited) {
        if (Verbose > 1) {
            MyPrintf("Initializing permutation tables for two vs two pawns, "
                     "with one opposing pair\n");
        }
        if (k2_2_opposing_tab == NULL) {
            k2_2_opposing_tab = (int *)MyMalloc(NSQUARES * NSQUARES * NSQUARES *
                                                NSQUARES * sizeof(int));
        }
        if (p2_2_opposing_tab == NULL) {
            p2_2_opposing_tab = (int *)MyMalloc(N2_2_OPPOSING * sizeof(int));
        }
        InitN2_2_OpposingTables(k2_2_opposing_tab, p2_2_opposing_tab);
        m2_2_opposing_inited = true;
    }

    if (!m3_1_opposing_inited) {
        if (Verbose > 1) {
            MyPrintf("Initializing permutation tables for three vs one pawn, "
                     "with one opposing pair\n");
        }
        if (k3_1_opposing_tab == NULL) {
            k3_1_opposing_tab = (int *)MyMalloc(NSQUARES * NSQUARES * NSQUARES *
                                                NSQUARES * sizeof(int));
        }
        if (p3_1_opposing_tab == NULL) {
            p3_1_opposing_tab = (int *)MyMalloc(N3_1_OPPOSING * sizeof(int));
        }
        InitN3_1_OpposingTables(k3_1_opposing_tab, p3_1_opposing_tab);
        m3_1_opposing_inited = true;
    }

    if (!m1_3_opposing_inited) {
        if (Verbose > 1) {
            MyPrintf("Initializing permutation tables for one vs three pawns, "
                     "with one opposing pair\n");
        }
        if (k1_3_opposing_tab == NULL) {
            k1_3_opposing_tab = (int *)MyMalloc(NSQUARES * NSQUARES * NSQUARES *
                                                NSQUARES * sizeof(int));
        }
        if (p1_3_opposing_tab == NULL) {
            p1_3_opposing_tab = (int *)MyMalloc(N1_3_OPPOSING * sizeof(int));
        }
        InitN1_3_OpposingTables(k1_3_opposing_tab, p1_3_opposing_tab);
        m1_3_opposing_inited = true;
    }

    if (!m4_opposing_inited) {
        if (Verbose > 1) {
            MyPrintf("Initializing and checking permutation table for two "
                     "pairs of opposing pawns\n");
        }

        if (k4_opposing_tab == NULL) {
            k4_opposing_tab = (int *)MyMalloc(NSQUARES * NSQUARES * NSQUARES *
                                              NSQUARES * sizeof(int));
        }
        if (p4_opposing_tab == NULL) {
            p4_opposing_tab = (int *)MyMalloc(N4_OPPOSING * sizeof(int));
        }
        InitN4OpposingTables(k4_opposing_tab, p4_opposing_tab);
        m4_opposing_inited = true;
    }

    for (int c = WHITE; c <= BLACK; c++) {
        for (int p = PAWN; p < KING; p++) {
            if (count[c][p] == 7 && !m7_inited) {
                if (Verbose > 1) {
                    MyPrintf("Initializing and checking permutation table for "
                             "7 pieces\n");
                }
                if (p4_tab_mb == NULL) {
                    p4_tab_mb = (int *)MyMalloc(N4 * sizeof(int));
                    InitN4TablesMB(p4_tab_mb);
                }
                if (!m5_inited) {
                    InitN5Tables();
                    m5_inited = true;
                }
                if (!m6_inited) {
                    InitN6Tables();
                    m6_inited = true;
                }
                InitN7Tables();
                m7_inited = true;
            }
            if (count[c][p] == 6 && !m6_inited) {
                if (Verbose > 1) {
                    MyPrintf("Initializing and checking permutation table for "
                             "6 pieces\n");
                }
                if (p4_tab_mb == NULL) {
                    p4_tab_mb = (int *)MyMalloc(N4 * sizeof(int));
                    InitN4TablesMB(p4_tab_mb);
                }
                if (!m5_inited) {
                    InitN5Tables();
                    m5_inited = true;
                }
                InitN6Tables();
                m6_inited = true;
            }
            if (count[c][p] == 5 && !m5_inited) {
                if (Verbose > 1) {
                    MyPrintf("Initializing and checking permutation table for "
                             "5 pieces\n");
                }
                if (p4_tab_mb == NULL) {
                    p4_tab_mb = (int *)MyMalloc(N4 * sizeof(int));
                    InitN4TablesMB(p4_tab_mb);
                }
                InitN5Tables();
                m5_inited = true;
            }
            if (count[c][p] == 4 && !m4_inited) {
                if (Verbose > 1)
                    MyPrintf("Initializing and checking permutation tables for "
                             "4 piece\n");
#if defined(USE_PERMUTATION_FUNCTIONS)
                InitN4Tables(NULL, NULL);
#else
                if (k4_tab == NULL)
                    k4_tab = (int *)MyMalloc(NSQUARES * NSQUARES * NSQUARES *
                                             NSQUARES * sizeof(int));
                if (p4_tab == NULL)
                    p4_tab = (int *)MyMalloc(N4 * sizeof(int));
                InitN4Tables(k4_tab, p4_tab);
#endif
                m4_inited = true;
            }
            if (count[c][p] == 3) {
                if ((p != BISHOP || bishop_parity[c] == NONE) && !m3_inited) {
                    if (Verbose > 1)
                        MyPrintf("Initializing and checking permutation tables "
                                 "for 3 piece\n");
#if defined(USE_PERMUTATION_FUNCTIONS)
                    InitN3Tables(NULL, NULL);
#else
                    if (k3_tab == NULL)
                        k3_tab = (int *)MyMalloc(NSQUARES * NSQUARES *
                                                 NSQUARES * sizeof(int));
                    if (p3_tab == NULL)
                        p3_tab = (int *)MyMalloc(N3 * sizeof(int));
                    InitN3Tables(k3_tab, p3_tab);
#endif
                    m3_inited = true;
                }
#if (NUM_WHITE_SQUARES) == (NUM_BLACK_SQUARES)
                if (p == BISHOP && bishop_parity[c] == EVEN &&
                    !m3_even_inited) {
                    if (Verbose > 1)
                        MyPrintf("Initializing tables for even triplets\n");
                    if (k3_even_tab == NULL)
                        k3_even_tab = (int *)MyMalloc(NSQUARES * NSQUARES *
                                                      NSQUARES * sizeof(int));
                    if (p3_even_tab == NULL)
                        p3_even_tab =
                            (int *)MyMalloc(N3_EVEN_PARITY * sizeof(int));
                    InitN3EvenTables(k3_even_tab, p3_even_tab);
                    m3_even_inited = true;
                }
                if (p == BISHOP && bishop_parity[c] == ODD && !m3_odd_inited) {
                    if (Verbose > 1)
                        MyPrintf("Initializing tables for odd triplets\n");
                    if (k3_odd_tab == NULL)
                        k3_odd_tab = (int *)MyMalloc(NSQUARES * NSQUARES *
                                                     NSQUARES * sizeof(int));
                    if (p3_odd_tab == NULL)
                        p3_odd_tab =
                            (int *)MyMalloc(N3_ODD_PARITY * sizeof(int));
                    InitN3OddTables(k3_odd_tab, p3_odd_tab);
                    m3_odd_inited = true;
                }
#endif
            }
            if (count[c][p] == 2) {
                if ((p != BISHOP || bishop_parity[c] == NONE) && !m2_inited) {
                    if (Verbose > 1)
                        MyPrintf("Initializing and checking permutation tables "
                                 "for 2 piece\n");
#if defined(USE_PERMUTATION_FUNCTIONS)
                    InitN2Tables(NULL, NULL);
#else
                    if (k2_tab == NULL)
                        k2_tab =
                            (int *)MyMalloc(NSQUARES * NSQUARES * sizeof(int));
                    if (p2_tab == NULL)
                        p2_tab = (int *)MyMalloc(N2 * sizeof(int));
                    InitN2Tables(k2_tab, p2_tab);
#endif
                    m2_inited = true;
                }
#if (NUM_WHITE_SQUARES) == (NUM_BLACK_SQUARES)
                if (p == BISHOP && bishop_parity[c] == EVEN &&
                    !m2_even_inited) {
                    if (Verbose > 1)
                        MyPrintf("Initializing tables for even doublets\n");
                    if (k2_even_tab == NULL)
                        k2_even_tab =
                            (int *)MyMalloc(NSQUARES * NSQUARES * sizeof(int));
                    if (p2_even_tab == NULL)
                        p2_even_tab =
                            (int *)MyMalloc(N2_EVEN_PARITY * sizeof(int));
                    InitN2EvenTables(k2_even_tab, p2_even_tab);
                    m2_even_inited = true;
                }
#endif
                if (p == BISHOP && bishop_parity[c] == ODD && !m2_odd_inited) {
                    if (Verbose > 1)
                        MyPrintf("Initializing tables for odd doublets\n");
                    if (k2_odd_tab == NULL)
                        k2_odd_tab =
                            (int *)MyMalloc(NSQUARES * NSQUARES * sizeof(int));
                    if (p2_odd_tab == NULL)
                        p2_odd_tab =
                            (int *)MyMalloc(N2_ODD_PARITY * sizeof(int));
                    InitN2OddTables(k2_odd_tab, p2_odd_tab);
                    m2_odd_inited = true;
                }
            }
        }
    }
}

static ZINDEX GetZoneSize(int count[2][KING]) {
    ZINDEX size = 1;
    int pi, side;

    for (pi = KNIGHT; pi < KING; pi++) {
        for (side = WHITE; side <= BLACK; side++) {
            if (count[side][pi] == 1)
                size *= NSQUARES;
            else if (count[side][pi] == 2)
                size *= N2_Offset;
            else if (count[side][pi] == 3)
                size *= N3_Offset;
            else if (count[side][pi] == 4) {
                size *= N4_Offset;
            } else if (count[side][pi] == 5) {
                size *= N5_Offset;
            } else if (count[side][pi] == 6) {
                size *= N6_Offset;
            } else if (count[side][pi] == 7) {
                size *= N7_Offset;
            }
        }
    }

    return size;
}

int BinarySearchLeftmost(ZINDEX *arr, int n, ZINDEX x) {
    int l = 0, r = n;
    while (l < r) {
        int m = (l + r) / 2;
        if (arr[m] < x)
            l = m + 1;
        else
            r = m;
    }
    return l;
}

int LargestSquareInSeptuplet(ZINDEX *index) {
    if (*index == 0)
        return 6;

    int m = BinarySearchLeftmost(k7_tab, NSQUARES, *index);
    if (k7_tab[m] > *index)
        m--;

    *index -= k7_tab[m];
    return m;
}

int LargestSquareInSextuplet(ZINDEX *index) {
    if (*index == 0)
        return 5;

    int m = BinarySearchLeftmost(k6_tab, NSQUARES, *index);
    if (k6_tab[m] > *index)
        m--;

    *index -= k6_tab[m];
    return m;
}

int LargestSquareInQuintuplet(ZINDEX *index) {
    if (*index == 0)
        return 4;

    int m = BinarySearchLeftmost(k5_tab, NSQUARES, *index);
    if (k5_tab[m] > *index)
        m--;

    *index -= k5_tab[m];
    return m;
}

static ZINDEX Index1(int *pos) { return pos[2]; }

static bool Pos1(ZINDEX index, int *pos) {
    pos[2] = index;
    return true;
}

static ZINDEX Index11(int *pos) { return pos[3] + NSQUARES * pos[2]; }

static ZINDEX IndexBP11(int *pos) { return pos[2]; }

static ZINDEX IndexOP11(int *pos) {
    int index = N2_Opposing_Index(pos[3], pos[2]);
    assert(index != -1);
    return index;
}

static bool Pos11(ZINDEX index, int *pos) {
    pos[3] = index % NSQUARES;
    index /= NSQUARES;
    pos[2] = index;
    return true;
}

static bool PosBP11(ZINDEX index, int *pos) {
    pos[2] = index;
    pos[3] = pos[2] + NCOLS;
    return true;
}

static bool PosOP11(ZINDEX index, int *pos) {
    assert(index < NCOLS * (NROWS - 2) * (NROWS - 3) / 2);
    int p2 = p2_opposing_tab[index];
    pos[3] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[2] = p2;
    return true;
}

static ZINDEX Index111(int *pos) {
    return pos[4] + NSQUARES * (pos[3] + NSQUARES * pos[2]);
}

static ZINDEX IndexBP111(int *pos) { return pos[4] + NSQUARES * pos[2]; }

static ZINDEX IndexOP111(int *pos) {
    int id2 = N2_Opposing_Index(pos[3], pos[2]);
    assert(id2 != -1);
    return pos[4] + NSQUARES * id2;
}

static bool Pos111(ZINDEX index, int *pos) {
    pos[4] = index % NSQUARES;
    index /= NSQUARES;
    pos[3] = index % NSQUARES;
    index /= NSQUARES;
    pos[2] = index;
    return true;
}

static bool PosBP111(ZINDEX index, int *pos) {
    pos[4] = index % NSQUARES;
    index /= NSQUARES;
    pos[2] = index;
    pos[3] = pos[2] + NCOLS;
    return true;
}

static bool PosOP111(ZINDEX index, int *pos) {
    pos[4] = index % NSQUARES;
    index /= NSQUARES;
    assert(index < NCOLS * (NROWS - 2) * (NROWS - 3) / 2);
    int p2 = p2_opposing_tab[index];
    pos[3] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[2] = p2;
    return true;
}

static ZINDEX Index1111(int *pos) {
    return pos[5] +
           NSQUARES * (pos[4] + NSQUARES * (pos[3] + NSQUARES * pos[2]));
}

static ZINDEX IndexBP1111(int *pos) {
    return pos[5] + NSQUARES * (pos[4] + NSQUARES * pos[2]);
}

static ZINDEX IndexOP1111(int *pos) {
    int id2 = N2_Opposing_Index(pos[3], pos[2]);
    assert(id2 != -1);
    return pos[5] + NSQUARES * (pos[4] + NSQUARES * id2);
}

static bool Pos1111(ZINDEX index, int *pos) {
    pos[5] = index % NSQUARES;
    index /= NSQUARES;
    pos[4] = index % NSQUARES;
    index /= NSQUARES;
    pos[3] = index % NSQUARES;
    index /= NSQUARES;
    pos[2] = index;
    return true;
}

static bool PosBP1111(ZINDEX index, int *pos) {
    pos[5] = index % NSQUARES;
    index /= NSQUARES;
    pos[4] = index % NSQUARES;
    index /= NSQUARES;
    assert(index < NSQUARES);
    pos[2] = index;
    pos[3] = pos[2] + NCOLS;

    return true;
}

static bool PosOP1111(ZINDEX index, int *pos) {
    pos[5] = index % NSQUARES;
    index /= NSQUARES;
    pos[4] = index % NSQUARES;
    index /= NSQUARES;
    assert(index < NCOLS * (NROWS - 2) * (NROWS - 3) / 2);
    int p2 = p2_opposing_tab[index];
    pos[3] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[2] = p2;

    return true;
}

static ZINDEX Index11111(int *pos) {
    return pos[6] +
           NSQUARES *
               (pos[5] +
                NSQUARES * (pos[4] + NSQUARES * (pos[3] + NSQUARES * pos[2])));
}

static ZINDEX IndexBP11111(int *pos) {
    return pos[6] +
           NSQUARES * (pos[5] + NSQUARES * (pos[4] + NSQUARES * (pos[2])));
}

static ZINDEX IndexOP11111(int *pos) {
    int id2 = N2_Opposing_Index(pos[3], pos[2]);
    assert(id2 != -1);
    return pos[6] +
           NSQUARES * (pos[5] + NSQUARES * (pos[4] + NSQUARES * (id2)));
}

static bool Pos11111(ZINDEX index, int *pos) {
    pos[6] = index % NSQUARES;
    index /= NSQUARES;
    pos[5] = index % NSQUARES;
    index /= NSQUARES;
    pos[4] = index % NSQUARES;
    index /= NSQUARES;
    pos[3] = index % NSQUARES;
    index /= NSQUARES;
    pos[2] = index;
    return true;
}

static bool PosBP11111(ZINDEX index, int *pos) {
    pos[6] = index % NSQUARES;
    index /= NSQUARES;
    pos[5] = index % NSQUARES;
    index /= NSQUARES;
    pos[4] = index % NSQUARES;
    index /= NSQUARES;
    pos[2] = index;
    pos[3] = pos[2] + NCOLS;

    return true;
}

static bool PosOP11111(ZINDEX index, int *pos) {
    pos[6] = index % NSQUARES;
    index /= NSQUARES;
    pos[5] = index % NSQUARES;
    index /= NSQUARES;
    pos[4] = index % NSQUARES;
    index /= NSQUARES;
    assert(index < NCOLS * (NROWS - 2) * (NROWS - 3) / 2);
    int p2 = p2_opposing_tab[index];
    pos[3] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[2] = p2;

    return true;
}

static ZINDEX Index2(int *pos) { return N2_Index(pos[3], pos[2]); }

static bool Pos2(ZINDEX index, int *pos) {
    int p2;
    assert(index < NSQUARES * (NSQUARES - 1) / 2);
    p2 = p2_tab[index];
    pos[3] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[2] = p2;
    return true;
}

static ZINDEX Index2_1100(int *pos) { return N2_Odd_Index(pos[3], pos[2]); }

static bool Pos2_1100(ZINDEX index, int *pos) {
    int p2;
    assert(index < N2_ODD_PARITY);
    p2 = p2_odd_tab[index];
    pos[3] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[2] = p2;
    return true;
}

static ZINDEX Index21(int *pos) {
    return pos[4] + NSQUARES * N2_Index(pos[3], pos[2]);
}

static ZINDEX IndexOP21(int *pos) {
    int index = N2_1_Opposing_Index(pos[4], pos[3], pos[2]);

    if (index == -1)
        return ALL_ONES;

    return (ZINDEX)index;
}

static bool Pos21(ZINDEX index, int *pos) {
    int p2;

    pos[4] = index % NSQUARES;
    index /= NSQUARES;
    assert(index < NSQUARES * (NSQUARES - 1) / 2);
    p2 = p2_tab[index];
    pos[3] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[2] = p2;
    return true;
}

static bool PosOP21(ZINDEX index, int *pos) {
    assert(index < N2_1_OPPOSING);
    int p3 = p2_1_opposing_tab[index];
    assert(p3 != -1);
    pos[4] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[3] = p3 % NSQUARES;
    p3 /= NSQUARES;
    assert(p3 < NSQUARES);
    pos[2] = p3;
    return true;
}

static ZINDEX Index12(int *pos) {
    return pos[2] + NSQUARES * N2_Index(pos[4], pos[3]);
}

static ZINDEX IndexOP12(int *pos) {
    int index = N1_2_Opposing_Index(pos[4], pos[3], pos[2]);

    if (index == -1)
        return ALL_ONES;

    return (ZINDEX)index;
}

static bool Pos12(ZINDEX index, int *pos) {
    int p2;

    pos[2] = index % NSQUARES;
    index /= NSQUARES;
    assert(index < NSQUARES * (NSQUARES - 1) / 2);
    p2 = p2_tab[index];
    pos[4] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[3] = p2;
    return true;
}

static bool PosOP12(ZINDEX index, int *pos) {
    assert(index < N1_2_OPPOSING);
    int p3 = p1_2_opposing_tab[index];
    assert(p3 != -1);
    pos[4] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[3] = p3 % NSQUARES;
    p3 /= NSQUARES;
    assert(p3 < NSQUARES);
    pos[2] = p3;
    return true;
}

static ZINDEX Index211(int *pos) {
    return pos[5] + NSQUARES * (pos[4] + NSQUARES * N2_Index(pos[3], pos[2]));
}

static ZINDEX IndexOP211(int *pos) {
    ZINDEX op21 = IndexOP21(pos);
    if (op21 == ALL_ONES)
        return ALL_ONES;
    return pos[5] + NSQUARES * op21;
}

static bool Pos211(ZINDEX index, int *pos) {
    int p2;

    pos[5] = index % NSQUARES;
    index /= NSQUARES;
    pos[4] = index % NSQUARES;
    index /= NSQUARES;
    assert(index < NSQUARES * (NSQUARES - 1) / 2);
    p2 = p2_tab[index];
    pos[3] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[2] = p2;
    return true;
}

static bool PosOP211(ZINDEX index, int *pos) {
    pos[5] = index % NSQUARES;
    index /= NSQUARES;

    assert(index < N2_1_OPPOSING);

    return PosOP21(index, pos);
}

static ZINDEX Index121(int *pos) {
    return pos[5] + NSQUARES * (pos[2] + NSQUARES * N2_Index(pos[4], pos[3]));
}

static ZINDEX IndexOP121(int *pos) {
    ZINDEX op12 = IndexOP12(pos);
    if (op12 == ALL_ONES)
        return ALL_ONES;
    return pos[5] + NSQUARES * op12;
}

static bool Pos121(ZINDEX index, int *pos) {
    int p2;

    pos[5] = index % NSQUARES;
    index /= NSQUARES;
    pos[2] = index % NSQUARES;
    index /= NSQUARES;
    assert(index < NSQUARES * (NSQUARES - 1) / 2);
    p2 = p2_tab[index];
    pos[4] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[3] = p2;
    return true;
}

static bool PosOP121(ZINDEX index, int *pos) {
    pos[5] = index % NSQUARES;
    index /= NSQUARES;

    assert(index < N1_2_OPPOSING);

    return PosOP12(index, pos);
}

static ZINDEX Index112(int *pos) {
    return pos[3] + NSQUARES * (pos[2] + NSQUARES * N2_Index(pos[5], pos[4]));
}

static ZINDEX IndexBP112(int *pos) {
    return N2_Offset * pos[2] + N2_Index(pos[5], pos[4]);
}

static ZINDEX IndexOP112(int *pos) {
    int id2 = N2_Opposing_Index(pos[3], pos[2]);
    assert(id2 != -1);
    return N2_Offset * id2 + N2_Index(pos[5], pos[4]);
}

static bool Pos112(ZINDEX index, int *pos) {
    int p2;

    pos[3] = index % NSQUARES;
    index /= NSQUARES;
    pos[2] = index % NSQUARES;
    index /= NSQUARES;
    assert(index < NSQUARES * (NSQUARES - 1) / 2);
    p2 = p2_tab[index];
    pos[5] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[4] = p2;
    return true;
}

static bool PosBP112(ZINDEX index, int *pos) {
    int p2 = index % N2_Offset;
    assert(p2 < N2);
    p2 = p2_tab[p2];

    pos[5] = p2 % NSQUARES;
    pos[4] = p2 / NSQUARES;

    index /= N2_Offset;

    assert(index < NSQUARES);

    pos[2] = index;
    pos[3] = pos[2] + NCOLS;

    return true;
}

static bool PosOP112(ZINDEX index, int *pos) {
    int p2 = index % N2_Offset;
    assert(p2 < N2);
    p2 = p2_tab[p2];

    pos[5] = p2 % NSQUARES;
    pos[4] = p2 / NSQUARES;

    index /= N2_Offset;

    assert(index < NCOLS * (NROWS - 2) * (NROWS - 3) / 2);

    p2 = p2_opposing_tab[index];
    pos[3] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[2] = p2;

    return true;
}

static ZINDEX Index2111(int *pos) {
    return pos[6] +
           NSQUARES *
               (pos[5] +
                NSQUARES * (pos[4] + NSQUARES * N2_Index(pos[3], pos[2])));
}

static ZINDEX IndexOP2111(int *pos) {
    ZINDEX op21 = IndexOP21(pos);
    if (op21 == ALL_ONES)
        return ALL_ONES;
    return pos[6] + NSQUARES * (ZINDEX)(pos[5] + NSQUARES * op21);
}

static bool Pos2111(ZINDEX index, int *pos) {
    int p2;

    pos[6] = index % NSQUARES;
    index /= NSQUARES;
    pos[5] = index % NSQUARES;
    index /= NSQUARES;
    pos[4] = index % NSQUARES;
    index /= NSQUARES;
    assert(index < NSQUARES * (NSQUARES - 1) / 2);
    p2 = p2_tab[index];
    pos[3] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[2] = p2;
    return true;
}

static bool PosOP2111(ZINDEX index, int *pos) {
    pos[6] = index % NSQUARES;
    index /= NSQUARES;
    pos[5] = index % NSQUARES;
    index /= NSQUARES;

    assert(index < N2_1_OPPOSING);

    return PosOP21(index, pos);
}

static ZINDEX Index1211(int *pos) {
    return pos[6] +
           NSQUARES *
               (pos[5] +
                NSQUARES * (pos[2] + NSQUARES * N2_Index(pos[4], pos[3])));
}

static ZINDEX IndexOP1211(int *pos) {
    ZINDEX op12 = IndexOP12(pos);
    if (op12 == ALL_ONES)
        return ALL_ONES;
    return pos[6] + NSQUARES * (ZINDEX)(pos[5] + NSQUARES * op12);
}

static bool Pos1211(ZINDEX index, int *pos) {
    int p2;

    pos[6] = index % NSQUARES;
    index /= NSQUARES;
    pos[5] = index % NSQUARES;
    index /= NSQUARES;
    pos[2] = index % NSQUARES;
    index /= NSQUARES;
    assert(index < NSQUARES * (NSQUARES - 1) / 2);
    p2 = p2_tab[index];
    pos[4] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[3] = p2;
    return true;
}

static bool PosOP1211(ZINDEX index, int *pos) {
    pos[6] = index % NSQUARES;
    index /= NSQUARES;
    pos[5] = index % NSQUARES;
    index /= NSQUARES;

    assert(index < N1_2_OPPOSING);

    return PosOP12(index, pos);
}

static ZINDEX Index1121(int *pos) {
    return pos[6] +
           NSQUARES *
               (pos[3] +
                NSQUARES * (pos[2] + NSQUARES * N2_Index(pos[5], pos[4])));
}

static ZINDEX IndexBP1121(int *pos) {
    return pos[6] + NSQUARES * (N2_Index(pos[5], pos[4]) + N2_Offset * pos[2]);
}

static ZINDEX IndexOP1121(int *pos) {
    int id2 = N2_Opposing_Index(pos[3], pos[2]);
    assert(id2 != -1);
    return pos[6] + NSQUARES * (N2_Index(pos[5], pos[4]) + N2_Offset * id2);
}

static bool Pos1121(ZINDEX index, int *pos) {
    int p2;

    pos[6] = index % NSQUARES;
    index /= NSQUARES;
    pos[3] = index % NSQUARES;
    index /= NSQUARES;
    pos[2] = index % NSQUARES;
    index /= NSQUARES;
    assert(index < NSQUARES * (NSQUARES - 1) / 2);
    p2 = p2_tab[index];
    pos[5] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[4] = p2;
    return true;
}

static bool PosBP1121(ZINDEX index, int *pos) {
    int p2;

    pos[6] = index % NSQUARES;
    index /= NSQUARES;
    p2 = index % N2_Offset;
    assert(p2 < N2);
    p2 = p2_tab[p2];
    pos[5] = p2 % NSQUARES;
    pos[4] = p2 / NSQUARES;

    index /= N2_Offset;

    assert(index < NSQUARES);

    pos[2] = index;
    pos[3] = pos[2] + NCOLS;

    return true;
}

static bool PosOP1121(ZINDEX index, int *pos) {
    int p2;

    pos[6] = index % NSQUARES;
    index /= NSQUARES;
    p2 = index % N2_Offset;
    assert(p2 < N2);
    p2 = p2_tab[p2];
    pos[5] = p2 % NSQUARES;
    pos[4] = p2 / NSQUARES;

    index /= N2_Offset;

    assert(index < NCOLS * (NROWS - 2) * (NROWS - 3) / 2);

    p2 = p2_opposing_tab[index];
    pos[3] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[2] = p2;

    return true;
}

static ZINDEX Index1112(int *pos) {
    return pos[4] +
           NSQUARES *
               (pos[3] +
                NSQUARES * (pos[2] + NSQUARES * N2_Index(pos[6], pos[5])));
}

static ZINDEX IndexBP1112(int *pos) {
    return pos[4] + NSQUARES * (N2_Index(pos[6], pos[5]) + N2_Offset * pos[2]);
}

static ZINDEX IndexOP1112(int *pos) {
    int id2 = N2_Opposing_Index(pos[3], pos[2]);
    assert(id2 != -1);
    return pos[4] + NSQUARES * (N2_Index(pos[6], pos[5]) + N2_Offset * id2);
}

static bool Pos1112(ZINDEX index, int *pos) {
    int p2;

    pos[4] = index % NSQUARES;
    index /= NSQUARES;
    pos[3] = index % NSQUARES;
    index /= NSQUARES;
    pos[2] = index % NSQUARES;
    index /= NSQUARES;
    assert(index < NSQUARES * (NSQUARES - 1) / 2);
    p2 = p2_tab[index];
    pos[6] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[5] = p2;
    return true;
}

static bool PosBP1112(ZINDEX index, int *pos) {
    int p2;

    pos[4] = index % NSQUARES;
    index /= NSQUARES;

    p2 = index % N2_Offset;
    assert(p2 < N2);

    p2 = p2_tab[p2];

    pos[6] = p2 % NSQUARES;
    pos[5] = p2 / NSQUARES;

    index /= N2_Offset;

    assert(index < NSQUARES);

    pos[2] = index;
    pos[3] = pos[2] + NCOLS;

    return true;
}

static bool PosOP1112(ZINDEX index, int *pos) {
    int p2;

    pos[4] = index % NSQUARES;
    index /= NSQUARES;

    p2 = index % N2_Offset;
    assert(p2 < N2);

    p2 = p2_tab[p2];

    pos[6] = p2 % NSQUARES;
    pos[5] = p2 / NSQUARES;

    index /= N2_Offset;

    assert(index < NCOLS * (NROWS - 2) * (NROWS - 3) / 2);

    p2 = p2_opposing_tab[index];
    pos[3] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[2] = p2;

    return true;
}

static ZINDEX Index22(int *pos) {
    return N2_Index(pos[5], pos[4]) + N2_Offset * N2_Index(pos[3], pos[2]);
}

static ZINDEX IndexOP22(int *pos) {
    int index = N2_2_Opposing_Index(pos[5], pos[4], pos[3], pos[2]);

    if (index == -1)
        return ALL_ONES;

    return (ZINDEX)index;
}

static ZINDEX IndexDP22(int *pos) {
    int index = -1;
    int w1_col = Column(pos[2]);
    int w2_col = Column(pos[3]);
    int b1_col = Column(pos[4]);
    int b2_col = Column(pos[5]);
    if ((w1_col == b1_col) && (w2_col == b2_col)) {
        index = N4_Opposing_Index(pos[5], pos[4], pos[3], pos[2]);
    } else if ((w1_col == b2_col) && (w2_col == b1_col)) {
        index = N4_Opposing_Index(pos[4], pos[5], pos[3], pos[2]);
    }

    if (index != -1)
        return (ZINDEX)index;
    return ALL_ONES;
}

static bool Pos22(ZINDEX index, int *pos) {
    int p2, id2;

    id2 = index % N2_Offset;
    assert(id2 < NSQUARES * (NSQUARES - 1) / 2);
    p2 = p2_tab[id2];
    index /= N2_Offset;
    assert(index < NSQUARES * (NSQUARES - 1) / 2);
    pos[5] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[4] = p2;
    p2 = p2_tab[index];
    pos[3] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[2] = p2;
    return true;
}

static bool PosDP22(ZINDEX index, int *pos) {
    assert(index < N4_OPPOSING);
    int p4 = p4_opposing_tab[index];
    int b2_row = p4 % NROWS;
    p4 /= NROWS;
    int b1_row = p4 % NROWS;
    p4 /= NROWS;
    pos[3] = p4 % NSQUARES;
    p4 /= NSQUARES;
    pos[2] = p4 % NSQUARES;
    pos[5] = SquareMake(b2_row, Column(pos[3]));
    assert(b2_row > Row(pos[3]));
    pos[4] = SquareMake(b1_row, Column(pos[2]));
    assert(b1_row > Row(pos[2]));
    return true;
}

static bool PosOP22(ZINDEX index, int *pos) {
    assert(index < N2_2_OPPOSING);
    int p4 = p2_2_opposing_tab[index];
    assert(p4 != -1);
    pos[5] = p4 % NSQUARES;
    p4 /= NSQUARES;
    pos[4] = p4 % NSQUARES;
    p4 /= NSQUARES;
    pos[3] = p4 % NSQUARES;
    p4 /= NSQUARES;
    assert(p4 < NSQUARES);
    pos[2] = p4;
    return true;
}

static ZINDEX Index221(int *pos) {
    return pos[6] + NSQUARES * (N2_Index(pos[5], pos[4]) +
                                N2_Offset * N2_Index(pos[3], pos[2]));
}

static ZINDEX IndexOP221(int *pos) {
    ZINDEX op22 = IndexOP22(pos);
    if (op22 == ALL_ONES)
        return ALL_ONES;
    return pos[6] + NSQUARES * op22;
}

static ZINDEX IndexDP221(int *pos) {
    ZINDEX op22 = IndexDP22(pos);
    if (op22 == ALL_ONES)
        return ALL_ONES;
    return pos[6] + NSQUARES * op22;
}

static bool Pos221(ZINDEX index, int *pos) {
    int p2, id2;

    pos[6] = index % NSQUARES;
    index /= NSQUARES;
    id2 = index % N2_Offset;
    assert(id2 < NSQUARES * (NSQUARES - 1) / 2);
    p2 = p2_tab[id2];
    index /= N2_Offset;
    assert(index < NSQUARES * (NSQUARES - 1) / 2);
    pos[5] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[4] = p2;
    p2 = p2_tab[index];
    pos[3] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[2] = p2;
    return true;
}

static bool PosDP221(ZINDEX index, int *pos) {
    pos[6] = index % NSQUARES;
    index /= NSQUARES;

    assert(index < N4_OPPOSING);

    return PosDP22(index, pos);
}

static bool PosOP221(ZINDEX index, int *pos) {
    pos[6] = index % NSQUARES;
    index /= NSQUARES;

    assert(index < N2_2_OPPOSING);

    return PosOP22(index, pos);
}

static ZINDEX Index212(int *pos) {
    return pos[4] + NSQUARES * (N2_Index(pos[6], pos[5]) +
                                N2_Offset * N2_Index(pos[3], pos[2]));
}

static ZINDEX IndexOP212(int *pos) {
    ZINDEX op21 = IndexOP21(pos);
    if (op21 == ALL_ONES)
        return ALL_ONES;
    return N2_Index(pos[6], pos[5]) + N2_Offset * op21;
}

static bool Pos212(ZINDEX index, int *pos) {
    int p2, id2;

    pos[4] = index % NSQUARES;
    index /= NSQUARES;
    id2 = index % N2_Offset;
    assert(id2 < NSQUARES * (NSQUARES - 1) / 2);
    p2 = p2_tab[id2];
    index /= N2_Offset;
    assert(index < NSQUARES * (NSQUARES - 1) / 2);
    pos[6] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[5] = p2;
    p2 = p2_tab[index];
    pos[3] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[2] = p2;
    return true;
}

static bool PosOP212(ZINDEX index, int *pos) {
    int id2 = index % N2_Offset;
    assert(id2 < N2);
    int p2 = p2_tab[id2];
    pos[6] = p2 % NSQUARES;
    pos[5] = p2 / NSQUARES;

    index /= N2_Offset;
    assert(index < N2_1_OPPOSING);

    return PosOP21(index, pos);
}

static ZINDEX Index122(int *pos) {
    return pos[2] + NSQUARES * (N2_Index(pos[6], pos[5]) +
                                N2_Offset * N2_Index(pos[4], pos[3]));
}

static ZINDEX IndexOP122(int *pos) {
    ZINDEX op12 = IndexOP12(pos);
    if (op12 == ALL_ONES)
        return ALL_ONES;
    return N2_Index(pos[6], pos[5]) + N2_Offset * op12;
}

static bool Pos122(ZINDEX index, int *pos) {
    int p2, id2;

    pos[2] = index % NSQUARES;
    index /= NSQUARES;
    id2 = index % N2_Offset;
    assert(id2 < NSQUARES * (NSQUARES - 1) / 2);
    p2 = p2_tab[id2];
    index /= N2_Offset;
    assert(index < NSQUARES * (NSQUARES - 1) / 2);
    pos[6] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[5] = p2;
    p2 = p2_tab[index];
    pos[4] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[3] = p2;
    return true;
}

static bool PosOP122(ZINDEX index, int *pos) {
    int id2 = index % N2_Offset;
    assert(id2 < N2);
    int p2 = p2_tab[id2];
    pos[6] = p2 % NSQUARES;
    pos[5] = p2 / NSQUARES;

    index /= N2_Offset;
    assert(index < N1_2_OPPOSING);

    return PosOP12(index, pos);
}

static ZINDEX Index3(int *pos) { return N3_Index(pos[4], pos[3], pos[2]); }

static bool Pos3(ZINDEX index, int *pos) {
    int p3;

    assert(index < NSQUARES * (NSQUARES - 1) * (NSQUARES - 2) / 6);
    p3 = p3_tab[index];
    pos[4] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[3] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[2] = p3;
    return true;
}

static ZINDEX Index3_1100(int *pos) {
    return N3_Odd_Index(pos[4], pos[3], pos[2]);
}

static bool Pos3_1100(ZINDEX index, int *pos) {
    int p3;

    assert(index < N3_ODD_PARITY);
    p3 = p3_odd_tab[index];
    pos[4] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[3] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[2] = p3;
    return true;
}

static ZINDEX Index31(int *pos) {
    return pos[5] + NSQUARES * N3_Index(pos[4], pos[3], pos[2]);
}

static ZINDEX IndexOP31(int *pos) {
    int index = N3_1_Opposing_Index(pos[5], pos[4], pos[3], pos[2]);

    if (index == -1)
        return ALL_ONES;

    return (ZINDEX)index;
}

static bool Pos31(ZINDEX index, int *pos) {
    int p3;

    pos[5] = index % NSQUARES;
    index /= NSQUARES;
    assert(index < NSQUARES * (NSQUARES - 1) * (NSQUARES - 2) / 6);
    p3 = p3_tab[index];
    pos[4] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[3] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[2] = p3;
    return true;
}

static bool PosOP31(ZINDEX index, int *pos) {
    assert(index < N3_1_OPPOSING);
    int p4 = p3_1_opposing_tab[index];
    assert(p4 != -1);
    pos[5] = p4 % NSQUARES;
    p4 /= NSQUARES;
    pos[4] = p4 % NSQUARES;
    p4 /= NSQUARES;
    pos[3] = p4 % NSQUARES;
    p4 /= NSQUARES;
    assert(p4 < NSQUARES);
    pos[2] = p4;
    return true;
}

static ZINDEX Index13(int *pos) {
    return pos[2] + NSQUARES * N3_Index(pos[5], pos[4], pos[3]);
}

static ZINDEX IndexOP13(int *pos) {
    int index = N1_3_Opposing_Index(pos[5], pos[4], pos[3], pos[2]);

    if (index == -1)
        return ALL_ONES;

    return (ZINDEX)index;
}

static bool Pos13(ZINDEX index, int *pos) {
    int p3;

    pos[2] = index % NSQUARES;
    index /= NSQUARES;
    assert(index < NSQUARES * (NSQUARES - 1) * (NSQUARES - 2) / 6);
    p3 = p3_tab[index];
    pos[5] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[4] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[3] = p3;
    return true;
}

static bool PosOP13(ZINDEX index, int *pos) {
    assert(index < N1_3_OPPOSING);
    int p4 = p1_3_opposing_tab[index];
    assert(p4 != -1);
    pos[5] = p4 % NSQUARES;
    p4 /= NSQUARES;
    pos[4] = p4 % NSQUARES;
    p4 /= NSQUARES;
    pos[3] = p4 % NSQUARES;
    p4 /= NSQUARES;
    assert(p4 < NSQUARES);
    pos[2] = p4;
    return true;
}

static ZINDEX Index311(int *pos) {
    return pos[6] +
           NSQUARES * (pos[5] + NSQUARES * N3_Index(pos[4], pos[3], pos[2]));
}

static ZINDEX IndexOP311(int *pos) {
    ZINDEX op31 = IndexOP31(pos);
    if (op31 == ALL_ONES)
        return ALL_ONES;
    return pos[6] + NSQUARES * op31;
}

static bool Pos311(ZINDEX index, int *pos) {
    int p3;

    pos[6] = index % NSQUARES;
    index /= NSQUARES;
    pos[5] = index % NSQUARES;
    index /= NSQUARES;
    assert(index < NSQUARES * (NSQUARES - 1) * (NSQUARES - 2) / 6);
    p3 = p3_tab[index];
    pos[4] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[3] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[2] = p3;
    return true;
}

static bool PosOP311(ZINDEX index, int *pos) {
    pos[6] = index % NSQUARES;
    index /= NSQUARES;

    assert(index < N3_1_OPPOSING);

    return PosOP31(index, pos);
}

static ZINDEX Index131(int *pos) {
    return pos[6] +
           NSQUARES * (pos[2] + NSQUARES * N3_Index(pos[5], pos[4], pos[3]));
}

static ZINDEX IndexOP131(int *pos) {
    ZINDEX op13 = IndexOP13(pos);
    if (op13 == ALL_ONES)
        return ALL_ONES;
    return pos[6] + NSQUARES * op13;
}

static bool Pos131(ZINDEX index, int *pos) {
    int p3;

    pos[6] = index % NSQUARES;
    index /= NSQUARES;
    pos[2] = index % NSQUARES;
    index /= NSQUARES;
    assert(index < NSQUARES * (NSQUARES - 1) * (NSQUARES - 2) / 6);
    p3 = p3_tab[index];
    pos[5] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[4] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[3] = p3;
    return true;
}

static bool PosOP131(ZINDEX index, int *pos) {
    pos[6] = index % NSQUARES;
    index /= NSQUARES;

    assert(index < N1_3_OPPOSING);

    return PosOP13(index, pos);
}

static ZINDEX Index113(int *pos) {
    return pos[3] +
           NSQUARES * (pos[2] + NSQUARES * N3_Index(pos[6], pos[5], pos[4]));
}

static ZINDEX IndexBP113(int *pos) {
    return N3_Index(pos[6], pos[5], pos[4]) + N3_Offset * pos[2];
}

static ZINDEX IndexOP113(int *pos) {
    int id2 = N2_Opposing_Index(pos[3], pos[2]);
    assert(id2 != -1);
    return N3_Index(pos[6], pos[5], pos[4]) + N3_Offset * id2;
}

static bool Pos113(ZINDEX index, int *pos) {
    int p3;

    pos[3] = index % NSQUARES;
    index /= NSQUARES;
    pos[2] = index % NSQUARES;
    index /= NSQUARES;
    assert(index < NSQUARES * (NSQUARES - 1) * (NSQUARES - 2) / 6);
    p3 = p3_tab[index];
    pos[6] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[5] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[4] = p3;
    return true;
}

static bool PosBP113(ZINDEX index, int *pos) {
    int p3;

    p3 = index % N3_Offset;
    assert(p3 < N3);

    p3 = p3_tab[p3];
    pos[6] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[5] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[4] = p3;
    index /= N3_Offset;
    assert(index < NSQUARES);
    pos[2] = index;
    pos[3] = pos[2] + NCOLS;

    return true;
}

static bool PosOP113(ZINDEX index, int *pos) {
    int p3;

    p3 = index % N3_Offset;
    assert(p3 < N3);

    p3 = p3_tab[p3];
    pos[6] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[5] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[4] = p3;
    index /= N3_Offset;
    assert(index < NCOLS * (NROWS - 2) * (NROWS - 3) / 2);
    int p2 = p2_opposing_tab[index];
    pos[3] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[2] = p2;

    return true;
}

static ZINDEX Index32(int *pos) {
    return N2_Index(pos[6], pos[5]) +
           N2_Offset * N3_Index(pos[4], pos[3], pos[2]);
}

static bool Pos32(ZINDEX index, int *pos) {
    int p3, p2, id2;

    id2 = index % N2_Offset;
    assert(id2 < NSQUARES * (NSQUARES - 1) / 2);
    p2 = p2_tab[id2];
    index /= N2_Offset;
    pos[6] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[5] = p2;
    assert(index < NSQUARES * (NSQUARES - 1) * (NSQUARES - 2) / 6);
    p3 = p3_tab[index];
    pos[4] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[3] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[2] = p3;
    return true;
}

static ZINDEX Index23(int *pos) {
    return N2_Index(pos[3], pos[2]) +
           N2_Offset * N3_Index(pos[6], pos[5], pos[4]);
}

static bool Pos23(ZINDEX index, int *pos) {
    int p3, p2, id2;

    id2 = index % N2_Offset;
    assert(id2 < NSQUARES * (NSQUARES - 1) / 2);
    p2 = p2_tab[id2];
    index /= N2_Offset;
    pos[3] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[2] = p2;
    assert(index < NSQUARES * (NSQUARES - 1) * (NSQUARES - 2) / 6);
    p3 = p3_tab[index];
    pos[6] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[5] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[4] = p3;
    return true;
}

static ZINDEX Index4(int *pos) {
    return N4_Index(pos[5], pos[4], pos[3], pos[2]);
}

static bool Pos4(ZINDEX index, int *pos) {
    int p4;

    assert(index < N4);
    p4 = p4_tab[index];
    pos[5] = p4 % NSQUARES;
    p4 /= NSQUARES;
    pos[4] = p4 % NSQUARES;
    p4 /= NSQUARES;
    pos[3] = p4 % NSQUARES;
    p4 /= NSQUARES;
    pos[2] = p4;
    return true;
}

static ZINDEX Index41(int *pos) {
    return pos[6] + NSQUARES * N4_Index(pos[5], pos[4], pos[3], pos[2]);
}

static bool Pos41(ZINDEX index, int *pos) {
    int p4;

    pos[6] = index % NSQUARES;
    index /= NSQUARES;
    assert(index < N4);
    p4 = p4_tab[index];
    pos[5] = p4 % NSQUARES;
    p4 /= NSQUARES;
    pos[4] = p4 % NSQUARES;
    p4 /= NSQUARES;
    pos[3] = p4 % NSQUARES;
    p4 /= NSQUARES;
    pos[2] = p4;
    return true;
}

static ZINDEX Index14(int *pos) {
    return pos[2] + NSQUARES * N4_Index(pos[6], pos[5], pos[4], pos[3]);
}

static bool Pos14(ZINDEX index, int *pos) {
    int p4;

    pos[2] = index % NSQUARES;
    index /= NSQUARES;
    assert(index < N4);
    p4 = p4_tab[index];
    pos[6] = p4 % NSQUARES;
    p4 /= NSQUARES;
    pos[5] = p4 % NSQUARES;
    p4 /= NSQUARES;
    pos[4] = p4 % NSQUARES;
    p4 /= NSQUARES;
    pos[3] = p4;
    return true;
}

static ZINDEX Index5(int *pos) {
    return (N5 - 1) - N5_Index((NSQUARES - 1) - pos[2], (NSQUARES - 1) - pos[3],
                               (NSQUARES - 1) - pos[4], (NSQUARES - 1) - pos[5],
                               (NSQUARES - 1) - pos[6]);
}

static bool Pos5(ZINDEX index, int *pos) {
    int p4;

    assert(index < N5);
    index = (N5 - 1) - index;
    pos[2] = (NSQUARES - 1) - LargestSquareInQuintuplet(&index);
    p4 = p4_tab_mb[index];
    pos[3] = (NSQUARES - 1) - (p4 % NSQUARES);
    p4 /= NSQUARES;
    pos[4] = (NSQUARES - 1) - (p4 % NSQUARES);
    p4 /= NSQUARES;
    pos[5] = (NSQUARES - 1) - (p4 % NSQUARES);
    p4 /= NSQUARES;
    pos[6] = (NSQUARES - 1) - p4;
    return true;
}

static ZINDEX Index51(int *pos) { return pos[7] + NSQUARES * Index5(pos); }

static bool Pos51(ZINDEX index, int *pos) {
    pos[7] = index % NSQUARES;
    index /= NSQUARES;
    return Pos5(index, pos);
}

static ZINDEX Index15(int *pos) { return pos[2] + NSQUARES * Index5(pos + 1); }

static bool Pos15(ZINDEX index, int *pos) {
    pos[2] = index % NSQUARES;
    index /= NSQUARES;
    return Pos5(index, pos + 1);
}

static ZINDEX Index6(int *pos) {
    return (N6 - 1) - N6_Index((NSQUARES - 1) - pos[2], (NSQUARES - 1) - pos[3],
                               (NSQUARES - 1) - pos[4], (NSQUARES - 1) - pos[5],
                               (NSQUARES - 1) - pos[6],
                               (NSQUARES - 1) - pos[7]);
}

static bool Pos6(ZINDEX index, int *pos) {
    assert(index < N6);
    index = (N6 - 1) - index;
    pos[2] = (NSQUARES - 1) - LargestSquareInSextuplet(&index);
    return Pos5((N5 - 1) - index, pos + 1);
}

static ZINDEX Index7(int *pos) {
    return (N7 - 1) - N7_Index((NSQUARES - 1) - pos[2], (NSQUARES - 1) - pos[3],
                               (NSQUARES - 1) - pos[4], (NSQUARES - 1) - pos[5],
                               (NSQUARES - 1) - pos[6], (NSQUARES - 1) - pos[7],
                               (NSQUARES - 1) - pos[8]);
}

static bool Pos7(ZINDEX index, int *pos) {
    assert(index < N7);
    index = (N7 - 1) - index;
    pos[2] = (NSQUARES - 1) - LargestSquareInSeptuplet(&index);
    return Pos6((N6 - 1) - index, pos + 1);
}

#if defined(USE_64_BIT)

/* index functions for 8-man endings require 64 bit zone sizes */

static ZINDEX Index111111(int *pos) {
    return pos[7] +
           NSQUARES *
               (ZINDEX)(pos[6] +
                        NSQUARES *
                            (pos[5] +
                             NSQUARES *
                                 (pos[4] +
                                  NSQUARES * (pos[3] + NSQUARES * pos[2]))));
}

static ZINDEX IndexBP111111(int *pos) {
    return pos[7] +
           NSQUARES *
               (ZINDEX)(pos[6] +
                        NSQUARES * (pos[5] +
                                    NSQUARES * (pos[4] + NSQUARES * (pos[2]))));
}

static ZINDEX IndexOP111111(int *pos) {
    int id2 = N2_Opposing_Index(pos[3], pos[2]);
    assert(id2 != -1);
    return pos[7] +
           NSQUARES *
               (ZINDEX)(pos[6] +
                        NSQUARES *
                            (pos[5] + NSQUARES * (pos[4] + NSQUARES * (id2))));
}

static bool Pos111111(ZINDEX index, int *pos) {
    pos[7] = index % NSQUARES;
    index /= NSQUARES;
    pos[6] = index % NSQUARES;
    index /= NSQUARES;
    pos[5] = index % NSQUARES;
    index /= NSQUARES;
    pos[4] = index % NSQUARES;
    index /= NSQUARES;
    pos[3] = index % NSQUARES;
    index /= NSQUARES;
    pos[2] = index;
    return true;
}

static bool PosBP111111(ZINDEX index, int *pos) {
    pos[7] = index % NSQUARES;
    index /= NSQUARES;
    pos[6] = index % NSQUARES;
    index /= NSQUARES;
    pos[5] = index % NSQUARES;
    index /= NSQUARES;
    pos[4] = index % NSQUARES;
    index /= NSQUARES;
    assert(index < NSQUARES);
    pos[2] = index;
    pos[3] = pos[2] + NCOLS;
    return true;
}

static bool PosOP111111(ZINDEX index, int *pos) {
    pos[7] = index % NSQUARES;
    index /= NSQUARES;
    pos[6] = index % NSQUARES;
    index /= NSQUARES;
    pos[5] = index % NSQUARES;
    index /= NSQUARES;
    pos[4] = index % NSQUARES;
    index /= NSQUARES;
    assert(index < NCOLS * (NROWS - 2) * (NROWS - 3) / 2);
    int p2 = p2_opposing_tab[index];
    pos[3] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[2] = p2;
    return true;
}

static ZINDEX Index11112(int *pos) {
    return pos[5] +
           NSQUARES *
               (ZINDEX)(pos[4] +
                        NSQUARES *
                            (pos[3] +
                             NSQUARES * (pos[2] +
                                         NSQUARES * N2_Index(pos[7], pos[6]))));
}

static ZINDEX IndexBP11112(int *pos) {
    return pos[5] +
           NSQUARES * (ZINDEX)(pos[4] + NSQUARES * (N2_Index(pos[7], pos[6]) +
                                                    N2_Offset * pos[2]));
}

static ZINDEX IndexOP11112(int *pos) {
    int id2 = N2_Opposing_Index(pos[3], pos[2]);
    assert(id2 != -1);
    return pos[5] +
           NSQUARES * (ZINDEX)(pos[4] + NSQUARES * (N2_Index(pos[7], pos[6]) +
                                                    N2_Offset * id2));
}

static bool Pos11112(ZINDEX index, int *pos) {
    int p2;

    pos[5] = index % NSQUARES;
    index /= NSQUARES;
    pos[4] = index % NSQUARES;
    index /= NSQUARES;
    pos[3] = index % NSQUARES;
    index /= NSQUARES;
    pos[2] = index % NSQUARES;
    index /= NSQUARES;
    assert(index < N2);
    p2 = p2_tab[index];
    pos[7] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[6] = p2;
    return true;
}

static bool PosBP11112(ZINDEX index, int *pos) {
    int p2;

    pos[5] = index % NSQUARES;
    index /= NSQUARES;
    pos[4] = index % NSQUARES;
    index /= NSQUARES;
    p2 = index % N2_Offset;
    assert(p2 < N2);
    p2 = p2_tab[p2];
    pos[7] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[6] = p2;
    index /= N2_Offset;
    assert(index < NSQUARES);
    pos[2] = index;
    pos[3] = pos[2] + NCOLS;
    return true;
}

static bool PosOP11112(ZINDEX index, int *pos) {
    int p2;

    pos[5] = index % NSQUARES;
    index /= NSQUARES;
    pos[4] = index % NSQUARES;
    index /= NSQUARES;
    p2 = index % N2_Offset;
    assert(p2 < N2);
    p2 = p2_tab[p2];
    pos[7] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[6] = p2;
    index /= N2_Offset;
    assert(index < NCOLS * (NROWS - 2) * (NROWS - 3) / 2);
    p2 = p2_opposing_tab[index];
    pos[3] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[2] = p2;
    return true;
}

static ZINDEX Index11121(int *pos) {
    return pos[7] +
           NSQUARES *
               (ZINDEX)(pos[4] +
                        NSQUARES *
                            (pos[3] +
                             NSQUARES * (pos[2] +
                                         NSQUARES * N2_Index(pos[6], pos[5]))));
}

static ZINDEX IndexBP11121(int *pos) {
    return pos[7] +
           NSQUARES * (ZINDEX)(pos[4] + NSQUARES * (N2_Index(pos[6], pos[5]) +
                                                    N2_Offset * pos[2]));
}

static ZINDEX IndexOP11121(int *pos) {
    int id2 = N2_Opposing_Index(pos[3], pos[2]);
    assert(id2 != -1);
    return pos[7] +
           NSQUARES * (ZINDEX)(pos[4] + NSQUARES * (N2_Index(pos[6], pos[5]) +
                                                    N2_Offset * id2));
}

static bool Pos11121(ZINDEX index, int *pos) {
    int p2;

    pos[7] = index % NSQUARES;
    index /= NSQUARES;
    pos[4] = index % NSQUARES;
    index /= NSQUARES;
    pos[3] = index % NSQUARES;
    index /= NSQUARES;
    pos[2] = index % NSQUARES;
    index /= NSQUARES;
    assert(index < N2);
    p2 = p2_tab[index];
    pos[6] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[5] = p2;
    return true;
}

static bool PosBP11121(ZINDEX index, int *pos) {
    int p2;

    pos[7] = index % NSQUARES;
    index /= NSQUARES;
    pos[4] = index % NSQUARES;
    index /= NSQUARES;
    p2 = index % N2_Offset;
    assert(p2 < N2);
    p2 = p2_tab[p2];
    pos[6] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[5] = p2 % NSQUARES;
    index /= N2_Offset;
    assert(index < NSQUARES);
    pos[2] = index;
    pos[3] = pos[2] + NCOLS;
    return true;
}

static bool PosOP11121(ZINDEX index, int *pos) {
    int p2;

    pos[7] = index % NSQUARES;
    index /= NSQUARES;
    pos[4] = index % NSQUARES;
    index /= NSQUARES;
    p2 = index % N2_Offset;
    assert(p2 < N2);
    p2 = p2_tab[p2];
    pos[6] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[5] = p2 % NSQUARES;
    index /= N2_Offset;
    assert(index < NCOLS * (NROWS - 2) * (NROWS - 3) / 2);
    p2 = p2_opposing_tab[index];
    pos[3] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[2] = p2;
    return true;
}

static ZINDEX Index11211(int *pos) {
    return pos[7] +
           NSQUARES *
               (ZINDEX)(pos[6] +
                        NSQUARES *
                            (pos[3] +
                             NSQUARES * (pos[2] +
                                         NSQUARES * N2_Index(pos[5], pos[4]))));
}

static ZINDEX IndexBP11211(int *pos) {
    return pos[7] +
           NSQUARES * (ZINDEX)(pos[6] + NSQUARES * (N2_Index(pos[5], pos[4]) +
                                                    N2_Offset * pos[2]));
}

static ZINDEX IndexOP11211(int *pos) {
    int id2 = N2_Opposing_Index(pos[3], pos[2]);
    assert(id2 != -1);
    return pos[7] +
           NSQUARES * (ZINDEX)(pos[6] + NSQUARES * (N2_Index(pos[5], pos[4]) +
                                                    N2_Offset * id2));
}

static bool Pos11211(ZINDEX index, int *pos) {
    int p2;

    pos[7] = index % NSQUARES;
    index /= NSQUARES;
    pos[6] = index % NSQUARES;
    index /= NSQUARES;
    pos[3] = index % NSQUARES;
    index /= NSQUARES;
    pos[2] = index % NSQUARES;
    index /= NSQUARES;
    assert(index < N2);
    p2 = p2_tab[index];
    pos[5] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[4] = p2;
    return true;
}

static bool PosBP11211(ZINDEX index, int *pos) {
    int p2;

    pos[7] = index % NSQUARES;
    index /= NSQUARES;
    pos[6] = index % NSQUARES;
    index /= NSQUARES;
    p2 = index % N2_Offset;
    assert(p2 < N2);
    p2 = p2_tab[p2];
    pos[5] = p2 % NSQUARES;
    pos[4] = p2 / NSQUARES;
    index /= N2_Offset;
    assert(index < NSQUARES);
    pos[2] = index;
    pos[3] = pos[2] + NCOLS;
    return true;
}

static bool PosOP11211(ZINDEX index, int *pos) {
    int p2;

    pos[7] = index % NSQUARES;
    index /= NSQUARES;
    pos[6] = index % NSQUARES;
    index /= NSQUARES;
    p2 = index % N2_Offset;
    assert(p2 < N2);
    p2 = p2_tab[p2];
    pos[5] = p2 % NSQUARES;
    pos[4] = p2 / NSQUARES;
    index /= N2_Offset;
    assert(index < NCOLS * (NROWS - 2) * (NROWS - 3) / 2);
    p2 = p2_opposing_tab[index];
    pos[3] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[2] = p2;
    return true;
}

static ZINDEX Index12111(int *pos) {
    return pos[7] +
           NSQUARES *
               (ZINDEX)(pos[6] +
                        NSQUARES *
                            (pos[5] +
                             NSQUARES * (pos[2] +
                                         NSQUARES * N2_Index(pos[4], pos[3]))));
}

static ZINDEX IndexOP12111(int *pos) {
    ZINDEX op12 = IndexOP12(pos);
    if (op12 == ALL_ONES)
        return ALL_ONES;
    return pos[7] +
           NSQUARES * (ZINDEX)(pos[6] + NSQUARES * (pos[5] + NSQUARES * op12));
}

static bool Pos12111(ZINDEX index, int *pos) {
    int p2;

    pos[7] = index % NSQUARES;
    index /= NSQUARES;
    pos[6] = index % NSQUARES;
    index /= NSQUARES;
    pos[5] = index % NSQUARES;
    index /= NSQUARES;
    pos[2] = index % NSQUARES;
    index /= NSQUARES;
    assert(index < N2);
    p2 = p2_tab[index];
    pos[4] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[3] = p2;
    return true;
}

static bool PosOP12111(ZINDEX index, int *pos) {
    pos[7] = index % NSQUARES;
    index /= NSQUARES;
    pos[6] = index % NSQUARES;
    index /= NSQUARES;
    pos[5] = index % NSQUARES;
    index /= NSQUARES;

    assert(index < N1_2_OPPOSING);

    return PosOP12(index, pos);
}

static ZINDEX Index21111(int *pos) {
    return pos[7] +
           NSQUARES *
               (ZINDEX)(pos[6] +
                        NSQUARES *
                            (pos[5] +
                             NSQUARES * (pos[4] +
                                         NSQUARES * N2_Index(pos[3], pos[2]))));
}

static ZINDEX IndexOP21111(int *pos) {
    ZINDEX op21 = IndexOP21(pos);
    if (op21 == ALL_ONES)
        return ALL_ONES;
    return pos[7] +
           NSQUARES * (ZINDEX)(pos[6] + NSQUARES * (pos[5] + NSQUARES * op21));
}

static bool Pos21111(ZINDEX index, int *pos) {
    int p2;

    pos[7] = index % NSQUARES;
    index /= NSQUARES;
    pos[6] = index % NSQUARES;
    index /= NSQUARES;
    pos[5] = index % NSQUARES;
    index /= NSQUARES;
    pos[4] = index % NSQUARES;
    index /= NSQUARES;
    assert(index < N2);
    p2 = p2_tab[index];
    pos[3] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[2] = p2;
    return true;
}

static bool PosOP21111(ZINDEX index, int *pos) {
    pos[7] = index % NSQUARES;
    index /= NSQUARES;
    pos[6] = index % NSQUARES;
    index /= NSQUARES;
    pos[5] = index % NSQUARES;
    index /= NSQUARES;

    assert(index < N2_1_OPPOSING);

    return PosOP21(index, pos);
}

static ZINDEX Index2211(int *pos) {
    return pos[7] +
           NSQUARES *
               (ZINDEX)(pos[6] +
                        NSQUARES * (N2_Index(pos[5], pos[4]) +
                                    N2_Offset * N2_Index(pos[3], pos[2])));
}

static ZINDEX IndexDP2211(int *pos) {
    ZINDEX dp22 = IndexDP22(pos);
    if (dp22 == ALL_ONES)
        return ALL_ONES;
    return pos[7] + NSQUARES * (ZINDEX)(pos[6] + NSQUARES * dp22);
}

static ZINDEX IndexOP2211(int *pos) {
    ZINDEX op22 = IndexOP22(pos);
    if (op22 == ALL_ONES)
        return ALL_ONES;
    return pos[7] + NSQUARES * (ZINDEX)(pos[6] + NSQUARES * op22);
}

static bool Pos2211(ZINDEX index, int *pos) {
    int p2, id2;

    pos[7] = index % NSQUARES;
    index /= NSQUARES;
    pos[6] = index % NSQUARES;
    index /= NSQUARES;
    id2 = index % N2_Offset;
    assert(id2 < N2);
    p2 = p2_tab[id2];
    index /= N2_Offset;
    assert(index < N2);
    pos[5] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[4] = p2;
    p2 = p2_tab[index];
    pos[3] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[2] = p2;
    return true;
}

static bool PosDP2211(ZINDEX index, int *pos) {
    pos[7] = index % NSQUARES;
    index /= NSQUARES;
    pos[6] = index % NSQUARES;
    index /= NSQUARES;

    assert(index < N4_OPPOSING);

    return PosDP22(index, pos);
}

static bool PosOP2211(ZINDEX index, int *pos) {
    pos[7] = index % NSQUARES;
    index /= NSQUARES;
    pos[6] = index % NSQUARES;
    index /= NSQUARES;

    assert(index < N2_2_OPPOSING);

    return PosOP22(index, pos);
}

static ZINDEX Index2211_1100(int *pos) {
    return pos[7] +
           NSQUARES *
               (ZINDEX)(pos[6] + NSQUARES * (N2_Odd_Index(pos[3], pos[2]) +
                                             N2_ODD_PARITY_Offset *
                                                 N2_Index(pos[5], pos[4])));
}

static bool Pos2211_1100(ZINDEX index, int *pos) {
    int p2, id2;

    pos[7] = index % NSQUARES;
    index /= NSQUARES;
    pos[6] = index % NSQUARES;
    index /= NSQUARES;
    id2 = index % N2_ODD_PARITY_Offset;
    assert(id2 < N2_ODD_PARITY);
    p2 = p2_odd_tab[id2];
    index /= N2_ODD_PARITY_Offset;
    assert(index < N2);
    pos[3] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[2] = p2;
    p2 = p2_tab[index];
    pos[5] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[4] = p2;
    return true;
}

static ZINDEX Index2211_1000(int *pos) {
    return pos[7] +
           NSQUARES *
               (ZINDEX)(pos[6] + NSQUARES * (N2_Even_Index(pos[3], pos[2]) +
                                             N2_EVEN_PARITY_Offset *
                                                 N2_Index(pos[5], pos[4])));
}

static bool Pos2211_1000(ZINDEX index, int *pos) {
    int p2, id2;

    pos[7] = index % NSQUARES;
    index /= NSQUARES;
    pos[6] = index % NSQUARES;
    index /= NSQUARES;
    id2 = index % N2_EVEN_PARITY_Offset;
    assert(id2 < N2_EVEN_PARITY);
    p2 = p2_even_tab[id2];
    index /= N2_EVEN_PARITY_Offset;
    assert(index < N2);
    pos[3] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[2] = p2;
    p2 = p2_tab[index];
    pos[5] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[4] = p2;
    return true;
}

static ZINDEX Index2121(int *pos) {
    return pos[7] +
           NSQUARES *
               (ZINDEX)(pos[4] +
                        NSQUARES * (N2_Index(pos[6], pos[5]) +
                                    N2_Offset * N2_Index(pos[3], pos[2])));
}

static ZINDEX IndexOP2121(int *pos) {
    ZINDEX op21 = IndexOP21(pos);
    if (op21 == ALL_ONES)
        return ALL_ONES;
    return pos[7] +
           NSQUARES * (ZINDEX)(N2_Index(pos[6], pos[5]) + N2_Offset * op21);
}

static bool Pos2121(ZINDEX index, int *pos) {
    int p2, id2;

    pos[7] = index % NSQUARES;
    index /= NSQUARES;
    pos[4] = index % NSQUARES;
    index /= NSQUARES;
    id2 = index % N2_Offset;
    assert(id2 < N2);
    p2 = p2_tab[id2];
    index /= N2_Offset;
    assert(index < N2);
    pos[6] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[5] = p2;
    p2 = p2_tab[index];
    pos[3] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[2] = p2;
    return true;
}

static bool PosOP2121(ZINDEX index, int *pos) {
    pos[7] = index % NSQUARES;
    index /= NSQUARES;
    int id2 = index % N2_Offset;
    assert(id2 < N2);
    int p2 = p2_tab[id2];
    pos[6] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[5] = p2;
    index /= N2_Offset;
    return PosOP21(index, pos);
}

static ZINDEX Index2112(int *pos) {
    return pos[5] +
           NSQUARES *
               (ZINDEX)(pos[4] +
                        NSQUARES * (N2_Index(pos[7], pos[6]) +
                                    N2_Offset * N2_Index(pos[3], pos[2])));
}

static ZINDEX IndexOP2112(int *pos) {
    ZINDEX op21 = IndexOP21(pos);
    if (op21 == ALL_ONES)
        return ALL_ONES;
    return pos[5] +
           NSQUARES * (ZINDEX)(N2_Index(pos[7], pos[6]) + N2_Offset * op21);
}

static bool Pos2112(ZINDEX index, int *pos) {
    int p2, id2;

    pos[5] = index % NSQUARES;
    index /= NSQUARES;
    pos[4] = index % NSQUARES;
    index /= NSQUARES;
    id2 = index % N2_Offset;
    assert(id2 < N2);
    p2 = p2_tab[id2];
    index /= N2_Offset;
    assert(index < N2);
    pos[7] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[6] = p2;
    p2 = p2_tab[index];
    pos[3] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[2] = p2;
    return true;
}

static bool PosOP2112(ZINDEX index, int *pos) {
    pos[5] = index % NSQUARES;
    index /= NSQUARES;
    int id2 = index % N2_Offset;
    assert(id2 < N2);
    int p2 = p2_tab[id2];
    index /= N2_Offset;
    pos[7] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[6] = p2;
    assert(index < N2_1_OPPOSING);
    return PosOP21(index, pos);
}

static ZINDEX Index1221(int *pos) {
    return pos[7] +
           NSQUARES *
               (ZINDEX)(pos[2] +
                        NSQUARES * (N2_Index(pos[6], pos[5]) +
                                    N2_Offset * N2_Index(pos[4], pos[3])));
}

static ZINDEX IndexOP1221(int *pos) {
    ZINDEX op12 = IndexOP12(pos);
    if (op12 == ALL_ONES)
        return ALL_ONES;
    return pos[7] +
           NSQUARES * (ZINDEX)(N2_Index(pos[6], pos[5]) + N2_Offset * op12);
}

static bool Pos1221(ZINDEX index, int *pos) {
    int p2, id2;

    pos[7] = index % NSQUARES;
    index /= NSQUARES;
    pos[2] = index % NSQUARES;
    index /= NSQUARES;
    id2 = index % N2_Offset;
    assert(id2 < N2);
    p2 = p2_tab[id2];
    index /= N2_Offset;
    assert(index < N2);
    pos[6] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[5] = p2;
    p2 = p2_tab[index];
    pos[4] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[3] = p2;
    return true;
}

static bool PosOP1221(ZINDEX index, int *pos) {
    pos[7] = index % NSQUARES;
    index /= NSQUARES;
    int id2 = index % N2_Offset;
    assert(id2 < N2);
    int p2 = p2_tab[id2];
    pos[6] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[5] = p2;
    index /= N2_Offset;
    assert(index < N1_2_OPPOSING);
    return PosOP12(index, pos);
}

static ZINDEX Index1212(int *pos) {
    return pos[5] +
           NSQUARES *
               (ZINDEX)(pos[2] +
                        NSQUARES * (N2_Index(pos[7], pos[6]) +
                                    N2_Offset * N2_Index(pos[4], pos[3])));
}

static ZINDEX IndexOP1212(int *pos) {
    ZINDEX op12 = IndexOP12(pos);
    if (op12 == ALL_ONES)
        return ALL_ONES;
    return pos[5] +
           NSQUARES * (ZINDEX)(N2_Index(pos[7], pos[6]) + N2_Offset * op12);
}

static bool Pos1212(ZINDEX index, int *pos) {
    int p2, id2;

    pos[5] = index % NSQUARES;
    index /= NSQUARES;
    pos[2] = index % NSQUARES;
    index /= NSQUARES;
    id2 = index % N2_Offset;
    assert(id2 < N2);
    p2 = p2_tab[id2];
    index /= N2_Offset;
    assert(index < N2);
    pos[7] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[6] = p2;
    p2 = p2_tab[index];
    pos[4] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[3] = p2;
    return true;
}

static bool PosOP1212(ZINDEX index, int *pos) {
    pos[5] = index % NSQUARES;
    index /= NSQUARES;
    int id2 = index % N2_Offset;
    assert(id2 < N2);
    int p2 = p2_tab[id2];
    index /= N2_Offset;
    pos[7] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[6] = p2;
    assert(index < N1_2_OPPOSING);
    return PosOP12(index, pos);
}

static ZINDEX Index1122(int *pos) {
    return pos[3] +
           NSQUARES *
               (ZINDEX)(pos[2] +
                        NSQUARES * (N2_Index(pos[7], pos[6]) +
                                    N2_Offset * N2_Index(pos[5], pos[4])));
}

static ZINDEX IndexBP1122(int *pos) {
    return N2_Index(pos[7], pos[6]) +
           N2_Offset * (ZINDEX)(N2_Index(pos[5], pos[4]) + N2_Offset * pos[2]);
}

static ZINDEX IndexOP1122(int *pos) {
    int id2 = N2_Opposing_Index(pos[3], pos[2]);
    assert(id2 != -1);
    return N2_Index(pos[7], pos[6]) +
           N2_Offset * (ZINDEX)(N2_Index(pos[5], pos[4]) + N2_Offset * id2);
}

static bool Pos1122(ZINDEX index, int *pos) {
    int p2, id2;

    pos[3] = index % NSQUARES;
    index /= NSQUARES;
    pos[2] = index % NSQUARES;
    index /= NSQUARES;
    id2 = index % N2_Offset;
    assert(id2 < N2);
    p2 = p2_tab[id2];
    index /= N2_Offset;
    assert(index < N2);
    pos[7] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[6] = p2;
    p2 = p2_tab[index];
    pos[5] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[4] = p2;
    return true;
}

static bool PosBP1122(ZINDEX index, int *pos) {
    int p2, id2;

    id2 = index % N2_Offset;
    assert(id2 < N2);
    p2 = p2_tab[id2];
    index /= N2_Offset;
    pos[7] = p2 % NSQUARES;
    pos[6] = p2 / NSQUARES;
    id2 = index % N2_Offset;
    assert(id2 < N2);
    p2 = p2_tab[id2];
    index /= N2_Offset;
    pos[5] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[4] = p2;
    assert(index < NSQUARES);
    pos[2] = index;
    pos[3] = pos[2] + NCOLS;
    return true;
}

static bool PosOP1122(ZINDEX index, int *pos) {
    int p2, id2;

    id2 = index % N2_Offset;
    assert(id2 < N2);
    p2 = p2_tab[id2];
    index /= N2_Offset;
    pos[7] = p2 % NSQUARES;
    pos[6] = p2 / NSQUARES;
    id2 = index % N2_Offset;
    assert(id2 < N2);
    p2 = p2_tab[id2];
    index /= N2_Offset;
    pos[5] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[4] = p2;
    assert(index < NCOLS * (NROWS - 2) * (NROWS - 3) / 2);
    p2 = p2_opposing_tab[index];
    pos[3] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[2] = p2;
    return true;
}

static ZINDEX Index222(int *pos) {
    return N2_Index(pos[7], pos[6]) +
           N2_Offset * (ZINDEX)(N2_Index(pos[5], pos[4]) +
                                N2_Offset * N2_Index(pos[3], pos[2]));
}

static ZINDEX IndexOP222(int *pos) {
    ZINDEX op22 = IndexOP22(pos);
    if (op22 == ALL_ONES)
        return ALL_ONES;
    return N2_Index(pos[7], pos[6]) + N2_Offset * op22;
}
static ZINDEX IndexDP222(int *pos) {
    ZINDEX dp22 = IndexDP22(pos);
    if (dp22 == ALL_ONES)
        return ALL_ONES;
    return N2_Index(pos[7], pos[6]) + N2_Offset * dp22;
}

static bool Pos222(ZINDEX index, int *pos) {
    int p2, id2, id3;

    id2 = index % N2_Offset;
    assert(id2 < N2);
    index /= N2_Offset;
    id3 = index % N2_Offset;
    assert(id3 < N2);
    index /= N2_Offset;
    assert(index < N2);
    p2 = p2_tab[id2];
    pos[7] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[6] = p2;
    p2 = p2_tab[id3];
    pos[5] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[4] = p2;
    p2 = p2_tab[index];
    pos[3] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[2] = p2;
    return true;
}

static bool PosDP222(ZINDEX index, int *pos) {
    int id2 = index % N2_Offset;
    assert(id2 < N2);
    int p2 = p2_tab[id2];
    pos[7] = p2 % NSQUARES;
    pos[6] = p2 / NSQUARES;

    index /= N2_Offset;
    assert(index < N4_OPPOSING);

    return PosDP22(index, pos);
}

static bool PosOP222(ZINDEX index, int *pos) {
    int id2 = index % N2_Offset;
    assert(id2 < N2);
    int p2 = p2_tab[id2];
    pos[7] = p2 % NSQUARES;
    pos[6] = p2 / NSQUARES;

    index /= N2_Offset;
    assert(index < N2_2_OPPOSING);

    return PosOP22(index, pos);
}

static ZINDEX Index3111(int *pos) {
    return pos[7] +
           NSQUARES *
               (ZINDEX)(pos[6] +
                        NSQUARES * (pos[5] + NSQUARES * N3_Index(pos[4], pos[3],
                                                                 pos[2])));
}

static ZINDEX IndexOP3111(int *pos) {
    ZINDEX op31 = IndexOP31(pos);
    if (op31 == ALL_ONES)
        return ALL_ONES;
    return pos[7] + NSQUARES * (ZINDEX)(pos[6] + NSQUARES * op31);
}

static bool Pos3111(ZINDEX index, int *pos) {
    int p3;

    pos[7] = index % NSQUARES;
    index /= NSQUARES;
    pos[6] = index % NSQUARES;
    index /= NSQUARES;
    pos[5] = index % NSQUARES;
    index /= NSQUARES;
    assert(index < NSQUARES * (NSQUARES - 1) * (NSQUARES - 2) / 6);
    p3 = p3_tab[index];
    pos[4] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[3] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[2] = p3;
    return true;
}

static bool PosOP3111(ZINDEX index, int *pos) {
    pos[7] = index % NSQUARES;
    index /= NSQUARES;
    pos[6] = index % NSQUARES;
    index /= NSQUARES;

    assert(index < N3_1_OPPOSING);

    return PosOP31(index, pos);
}

static ZINDEX Index1311(int *pos) {
    return pos[7] +
           NSQUARES *
               (ZINDEX)(pos[6] +
                        NSQUARES * (pos[2] + NSQUARES * N3_Index(pos[5], pos[4],
                                                                 pos[3])));
}

static ZINDEX IndexOP1311(int *pos) {
    ZINDEX op13 = IndexOP13(pos);
    if (op13 == ALL_ONES)
        return ALL_ONES;
    return pos[7] + NSQUARES * (ZINDEX)(pos[6] + NSQUARES * op13);
}

static bool Pos1311(ZINDEX index, int *pos) {
    int p3;

    pos[7] = index % NSQUARES;
    index /= NSQUARES;
    pos[6] = index % NSQUARES;
    index /= NSQUARES;
    pos[2] = index % NSQUARES;
    index /= NSQUARES;
    assert(index < NSQUARES * (NSQUARES - 1) * (NSQUARES - 2) / 6);
    p3 = p3_tab[index];
    pos[5] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[4] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[3] = p3;
    return true;
}

static bool PosOP1311(ZINDEX index, int *pos) {
    pos[7] = index % NSQUARES;
    index /= NSQUARES;
    pos[6] = index % NSQUARES;
    index /= NSQUARES;

    assert(index < N1_3_OPPOSING);

    return PosOP13(index, pos);
}

static ZINDEX Index1131(int *pos) {
    return pos[7] +
           NSQUARES *
               (ZINDEX)(pos[3] +
                        NSQUARES * (pos[2] + NSQUARES * N3_Index(pos[6], pos[5],
                                                                 pos[4])));
}

static ZINDEX IndexBP1131(int *pos) {
    return pos[7] + NSQUARES * (ZINDEX)(N3_Index(pos[6], pos[5], pos[4]) +
                                        N3_Offset * pos[2]);
}

static ZINDEX IndexOP1131(int *pos) {
    int id2 = N2_Opposing_Index(pos[3], pos[2]);
    assert(id2 != -1);
    return pos[7] + NSQUARES * (ZINDEX)(N3_Index(pos[6], pos[5], pos[4]) +
                                        N3_Offset * id2);
}

static bool Pos1131(ZINDEX index, int *pos) {
    int p3;

    pos[7] = index % NSQUARES;
    index /= NSQUARES;
    pos[3] = index % NSQUARES;
    index /= NSQUARES;
    pos[2] = index % NSQUARES;
    index /= NSQUARES;
    assert(index < NSQUARES * (NSQUARES - 1) * (NSQUARES - 2) / 6);
    p3 = p3_tab[index];
    pos[6] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[5] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[4] = p3;
    return true;
}

static bool PosBP1131(ZINDEX index, int *pos) {
    int p3;

    pos[7] = index % NSQUARES;
    index /= NSQUARES;
    p3 = index % N3_Offset;
    assert(p3 < N3);
    p3 = p3_tab[p3];
    pos[6] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[5] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[4] = p3;
    index /= N3_Offset;
    assert(index < NSQUARES);
    pos[2] = index;
    pos[3] = pos[2] + NCOLS;
    return true;
}

static bool PosOP1131(ZINDEX index, int *pos) {
    int p3;

    pos[7] = index % NSQUARES;
    index /= NSQUARES;
    p3 = index % N3_Offset;
    assert(p3 < N3);
    p3 = p3_tab[p3];
    pos[6] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[5] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[4] = p3;
    index /= N3_Offset;
    assert(index < NCOLS * (NROWS - 2) * (NROWS - 3) / 2);
    int p2 = p2_opposing_tab[index];
    pos[3] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[2] = p2;
    return true;
}

static ZINDEX Index1113(int *pos) {
    return pos[4] +
           NSQUARES *
               (ZINDEX)(pos[3] +
                        NSQUARES * (pos[2] + NSQUARES * N3_Index(pos[7], pos[6],
                                                                 pos[5])));
}

static ZINDEX IndexBP1113(int *pos) {
    return pos[4] + NSQUARES * (ZINDEX)(N3_Index(pos[7], pos[6], pos[5]) +
                                        N3_Offset * pos[2]);
}

static ZINDEX IndexOP1113(int *pos) {
    int id2 = N2_Opposing_Index(pos[3], pos[2]);
    assert(id2 != -1);
    return pos[4] + NSQUARES * (ZINDEX)(N3_Index(pos[7], pos[6], pos[5]) +
                                        N3_Offset * id2);
}

static bool Pos1113(ZINDEX index, int *pos) {
    int p3;

    pos[4] = index % NSQUARES;
    index /= NSQUARES;
    pos[3] = index % NSQUARES;
    index /= NSQUARES;
    pos[2] = index % NSQUARES;
    index /= NSQUARES;
    assert(index < NSQUARES * (NSQUARES - 1) * (NSQUARES - 2) / 6);
    p3 = p3_tab[index];
    pos[7] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[6] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[5] = p3;
    return true;
}

static bool PosBP1113(ZINDEX index, int *pos) {
    int p3;

    pos[4] = index % NSQUARES;
    index /= NSQUARES;
    p3 = index % N3_Offset;
    assert(p3 < N3);
    p3 = p3_tab[p3];
    pos[7] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[6] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[5] = p3 % NSQUARES;
    index /= N3_Offset;
    assert(index < NSQUARES);
    pos[2] = index;
    pos[3] = pos[2] + NCOLS;
    return true;
}

static bool PosOP1113(ZINDEX index, int *pos) {
    int p3;

    pos[4] = index % NSQUARES;
    index /= NSQUARES;
    p3 = index % N3_Offset;
    assert(p3 < N3);
    p3 = p3_tab[p3];
    pos[7] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[6] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[5] = p3 % NSQUARES;
    index /= N3_Offset;
    assert(index < NCOLS * (NROWS - 2) * (NROWS - 3) / 2);
    int p2 = p2_opposing_tab[index];
    pos[3] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[2] = p2;
    return true;
}

static ZINDEX Index123(int *pos) {
    return pos[2] +
           NSQUARES * (ZINDEX)(N2_Index(pos[4], pos[3]) +
                               N2_Offset * N3_Index(pos[7], pos[6], pos[5]));
}

static ZINDEX IndexOP123(int *pos) {
    ZINDEX op12 = IndexOP12(pos);
    if (op12 == ALL_ONES)
        return ALL_ONES;
    return N3_Index(pos[7], pos[6], pos[5]) + N3_Offset * op12;
}

static bool Pos123(ZINDEX index, int *pos) {
    int p2, p3, id2;

    pos[2] = index % NSQUARES;
    index /= NSQUARES;
    id2 = index % N2_Offset;
    assert(id2 < N2);
    p2 = p2_tab[id2];
    pos[4] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[3] = p2;
    index /= N2_Offset;
    assert(index < NSQUARES * (NSQUARES - 1) * (NSQUARES - 2) / 6);
    p3 = p3_tab[index];
    pos[7] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[6] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[5] = p3;
    return true;
}

static bool PosOP123(ZINDEX index, int *pos) {
    int id3 = index % N3_Offset;
    assert(id3 < N3);
    index /= N3_Offset;
    assert(index < N1_2_OPPOSING);
    int p3 = p3_tab[id3];
    pos[7] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[6] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[5] = p3;
    return PosOP12(index, pos);
}

static ZINDEX Index132(int *pos) {
    return pos[2] +
           NSQUARES * (ZINDEX)(N2_Index(pos[7], pos[6]) +
                               N2_Offset * N3_Index(pos[5], pos[4], pos[3]));
}

static ZINDEX IndexOP132(int *pos) {
    ZINDEX op13 = IndexOP13(pos);
    if (op13 == ALL_ONES)
        return ALL_ONES;
    return N2_Index(pos[7], pos[6]) + N2_Offset * op13;
}

static bool Pos132(ZINDEX index, int *pos) {
    int p2, p3, id2;

    pos[2] = index % NSQUARES;
    index /= NSQUARES;
    id2 = index % N2_Offset;
    assert(id2 < N2);
    p2 = p2_tab[id2];
    pos[7] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[6] = p2;
    index /= N2_Offset;
    assert(index < NSQUARES * (NSQUARES - 1) * (NSQUARES - 2) / 6);
    p3 = p3_tab[index];
    pos[5] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[4] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[3] = p3;
    return true;
}

static bool PosOP132(ZINDEX index, int *pos) {
    int id2 = index % N2_Offset;
    assert(id2 < N2);
    int p2 = p2_tab[id2];
    pos[7] = p2 % NSQUARES;
    pos[6] = p2 / NSQUARES;

    index /= N2_Offset;
    assert(index < N1_3_OPPOSING);

    return PosOP13(index, pos);
}

static ZINDEX Index213(int *pos) {
    return pos[4] +
           NSQUARES * (ZINDEX)(N2_Index(pos[3], pos[2]) +
                               N2_Offset * N3_Index(pos[7], pos[6], pos[5]));
}

static ZINDEX IndexOP213(int *pos) {
    ZINDEX op21 = IndexOP21(pos);
    if (op21 == ALL_ONES)
        return ALL_ONES;
    return N3_Index(pos[7], pos[6], pos[5]) + N3_Offset * op21;
}

static bool Pos213(ZINDEX index, int *pos) {
    int p2, p3, id2;

    pos[4] = index % NSQUARES;
    index /= NSQUARES;
    id2 = index % N2_Offset;
    assert(id2 < N2);
    p2 = p2_tab[id2];
    pos[3] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[2] = p2;
    index /= N2_Offset;
    assert(index < NSQUARES * (NSQUARES - 1) * (NSQUARES - 2) / 6);
    p3 = p3_tab[index];
    pos[7] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[6] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[5] = p3;
    return true;
}

static bool PosOP213(ZINDEX index, int *pos) {
    int id3 = index % N3_Offset;
    assert(id3 < N3);
    index /= N3_Offset;
    assert(index < N2_1_OPPOSING);
    int p3 = p3_tab[id3];
    pos[7] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[6] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[5] = p3;
    return PosOP21(index, pos);
}

static ZINDEX Index231(int *pos) {
    return pos[7] +
           NSQUARES * (ZINDEX)(N2_Index(pos[3], pos[2]) +
                               N2_Offset * N3_Index(pos[6], pos[5], pos[4]));
}

static bool Pos231(ZINDEX index, int *pos) {
    int p2, p3, id2;

    pos[7] = index % NSQUARES;
    index /= NSQUARES;
    id2 = index % N2_Offset;
    assert(id2 < N2);
    p2 = p2_tab[id2];
    pos[3] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[2] = p2;
    index /= N2_Offset;
    assert(index < NSQUARES * (NSQUARES - 1) * (NSQUARES - 2) / 6);
    p3 = p3_tab[index];
    pos[6] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[5] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[4] = p3;
    return true;
}

static ZINDEX Index312(int *pos) {
    return pos[5] +
           NSQUARES * (ZINDEX)(N2_Index(pos[7], pos[6]) +
                               N2_Offset * N3_Index(pos[4], pos[3], pos[2]));
}

static ZINDEX IndexOP312(int *pos) {
    ZINDEX op31 = IndexOP31(pos);
    if (op31 == ALL_ONES)
        return ALL_ONES;
    return N2_Index(pos[7], pos[6]) + N2_Offset * op31;
}

static bool Pos312(ZINDEX index, int *pos) {
    int p2, p3, id2;

    pos[5] = index % NSQUARES;
    index /= NSQUARES;
    id2 = index % N2_Offset;
    assert(id2 < N2);
    p2 = p2_tab[id2];
    pos[7] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[6] = p2;
    index /= N2_Offset;
    assert(index < NSQUARES * (NSQUARES - 1) * (NSQUARES - 2) / 6);
    p3 = p3_tab[index];
    pos[4] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[3] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[2] = p3;
    return true;
}

static bool PosOP312(ZINDEX index, int *pos) {
    int id2 = index % N2_Offset;
    assert(id2 < N2);
    int p2 = p2_tab[id2];
    pos[7] = p2 % NSQUARES;
    pos[6] = p2 / NSQUARES;

    index /= N2_Offset;
    assert(index < N3_1_OPPOSING);

    return PosOP31(index, pos);
}

static ZINDEX Index321(int *pos) {
    return pos[7] +
           NSQUARES * (ZINDEX)(N2_Index(pos[6], pos[5]) +
                               N2_Offset * N3_Index(pos[4], pos[3], pos[2]));
}

static bool Pos321(ZINDEX index, int *pos) {
    int p2, p3, id2;

    pos[7] = index % NSQUARES;
    index /= NSQUARES;
    id2 = index % N2_Offset;
    assert(id2 < N2);
    p2 = p2_tab[id2];
    pos[6] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[5] = p2;
    index /= N2_Offset;
    assert(index < NSQUARES * (NSQUARES - 1) * (NSQUARES - 2) / 6);
    p3 = p3_tab[index];
    pos[4] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[3] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[2] = p3;
    return true;
}

static ZINDEX Index33(int *pos) {
    return N3_Index(pos[7], pos[6], pos[5]) +
           N3_Offset * (ZINDEX)N3_Index(pos[4], pos[3], pos[2]);
}

static bool Pos33(ZINDEX index, int *pos) {
    int p3, id3;

    id3 = index % N3_Offset;
    assert(id3 < NSQUARES * (NSQUARES - 1) * (NSQUARES - 2) / 6);
    p3 = p3_tab[id3];
    pos[7] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[6] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[5] = p3;
    index /= N3_Offset;
    assert(index < NSQUARES * (NSQUARES - 1) * (NSQUARES - 2) / 6);
    p3 = p3_tab[index];
    pos[4] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[3] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[2] = p3;
    return true;
}

static ZINDEX Index411(int *pos) {
    return pos[7] +
           NSQUARES * (ZINDEX)(pos[6] + NSQUARES * N4_Index(pos[5], pos[4],
                                                            pos[3], pos[2]));
}

static bool Pos411(ZINDEX index, int *pos) {
    int p4;

    pos[7] = index % NSQUARES;
    index /= NSQUARES;
    pos[6] = index % NSQUARES;
    index /= NSQUARES;
    assert(index < N4);
    p4 = p4_tab[index];
    pos[5] = p4 % NSQUARES;
    p4 /= NSQUARES;
    pos[4] = p4 % NSQUARES;
    p4 /= NSQUARES;
    pos[3] = p4 % NSQUARES;
    p4 /= NSQUARES;
    pos[2] = p4;
    return true;
}

static ZINDEX Index141(int *pos) {
    return pos[7] +
           NSQUARES * (ZINDEX)(pos[2] + NSQUARES * N4_Index(pos[6], pos[5],
                                                            pos[4], pos[3]));
}

static bool Pos141(ZINDEX index, int *pos) {
    int p4;

    pos[7] = index % NSQUARES;
    index /= NSQUARES;
    pos[2] = index % NSQUARES;
    index /= NSQUARES;
    assert(index < N4);
    p4 = p4_tab[index];
    pos[6] = p4 % NSQUARES;
    p4 /= NSQUARES;
    pos[5] = p4 % NSQUARES;
    p4 /= NSQUARES;
    pos[4] = p4 % NSQUARES;
    p4 /= NSQUARES;
    pos[3] = p4;
    return true;
}

static ZINDEX Index114(int *pos) {
    return pos[3] +
           NSQUARES * (ZINDEX)(pos[2] + NSQUARES * N4_Index(pos[7], pos[6],
                                                            pos[5], pos[4]));
}

static ZINDEX IndexBP114(int *pos) {
    return N4_Index(pos[7], pos[6], pos[5], pos[4]) +
           N4_Offset * (ZINDEX)pos[2];
}

static ZINDEX IndexOP114(int *pos) {
    int id2 = N2_Opposing_Index(pos[3], pos[2]);
    assert(id2 != -1);
    return N4_Index(pos[7], pos[6], pos[5], pos[4]) + N4_Offset * (ZINDEX)id2;
}

static bool Pos114(ZINDEX index, int *pos) {
    int p4;

    pos[3] = index % NSQUARES;
    index /= NSQUARES;
    pos[2] = index % NSQUARES;
    index /= NSQUARES;
    assert(index < N4);
    p4 = p4_tab[index];
    pos[7] = p4 % NSQUARES;
    p4 /= NSQUARES;
    pos[6] = p4 % NSQUARES;
    p4 /= NSQUARES;
    pos[5] = p4 % NSQUARES;
    p4 /= NSQUARES;
    pos[4] = p4;
    return true;
}

static bool PosBP114(ZINDEX index, int *pos) {
    int p4;

    p4 = index % N4_Offset;
    assert(p4 < N4);
    p4 = p4_tab[p4];
    pos[7] = p4 % NSQUARES;
    p4 /= NSQUARES;
    pos[6] = p4 % NSQUARES;
    p4 /= NSQUARES;
    pos[5] = p4 % NSQUARES;
    p4 /= NSQUARES;
    pos[4] = p4;
    index /= N4_Offset;
    assert(index < NSQUARES);
    pos[2] = index;
    pos[3] = pos[2] + NCOLS;
    return true;
}

static bool PosOP114(ZINDEX index, int *pos) {
    int p4;

    p4 = index % N4_Offset;
    assert(p4 < N4);
    p4 = p4_tab[p4];
    pos[7] = p4 % NSQUARES;
    p4 /= NSQUARES;
    pos[6] = p4 % NSQUARES;
    p4 /= NSQUARES;
    pos[5] = p4 % NSQUARES;
    p4 /= NSQUARES;
    pos[4] = p4;
    index /= N4_Offset;
    assert(index < NCOLS * (NROWS - 2) * (NROWS - 3) / 2);
    int p2 = p2_opposing_tab[index];
    pos[3] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[2] = p2;
    return true;
}

static ZINDEX Index42(int *pos) {
    return N2_Index(pos[7], pos[6]) +
           N2_Offset * (ZINDEX)N4_Index(pos[5], pos[4], pos[3], pos[2]);
}

static bool Pos42(ZINDEX index, int *pos) {
    int p4, id2, p2;

    id2 = index % N2_Offset;
    assert(id2 < N2);
    index /= N2_Offset;
    assert(index < N4);
    p2 = p2_tab[id2];
    pos[7] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[6] = p2;
    p4 = p4_tab[index];
    pos[5] = p4 % NSQUARES;
    p4 /= NSQUARES;
    pos[4] = p4 % NSQUARES;
    p4 /= NSQUARES;
    pos[3] = p4 % NSQUARES;
    p4 /= NSQUARES;
    pos[2] = p4;
    return true;
}

static ZINDEX Index24(int *pos) {
    return N2_Index(pos[3], pos[2]) +
           N2_Offset * (ZINDEX)N4_Index(pos[7], pos[6], pos[5], pos[4]);
}

static bool Pos24(ZINDEX index, int *pos) {
    int p4, id2, p2;

    id2 = index % N2_Offset;
    assert(id2 < N2);
    index /= N2_Offset;
    assert(index < N4);
    p2 = p2_tab[id2];
    pos[3] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[2] = p2;
    p4 = p4_tab[index];
    pos[7] = p4 % NSQUARES;
    p4 /= NSQUARES;
    pos[6] = p4 % NSQUARES;
    p4 /= NSQUARES;
    pos[5] = p4 % NSQUARES;
    p4 /= NSQUARES;
    pos[4] = p4;
    return true;
}

// 9-pieces

static ZINDEX Index1111111(int *pos) {
    return pos[8] +
           NSQUARES *
               (pos[7] +
                NSQUARES *
                    (pos[6] +
                     NSQUARES *
                         (ZINDEX)(pos[5] +
                                  NSQUARES *
                                      (pos[4] +
                                       NSQUARES *
                                           (pos[3] + NSQUARES * pos[2])))));
}

static bool Pos1111111(ZINDEX index, int *pos) {
    int p2, id2;

    pos[8] = index % NSQUARES;
    index /= NSQUARES;
    pos[7] = index % NSQUARES;
    index /= NSQUARES;
    pos[6] = index % NSQUARES;
    index /= NSQUARES;
    pos[5] = index % NSQUARES;
    index /= NSQUARES;
    pos[4] = index % NSQUARES;
    index /= NSQUARES;
    pos[3] = index % NSQUARES;
    index /= NSQUARES;
    assert(index < NSQUARES);
    pos[2] = index;
    return true;
}

static ZINDEX Index211111(int *pos) {
    return pos[8] +
           NSQUARES *
               (pos[7] +
                NSQUARES *
                    (pos[6] +
                     NSQUARES *
                         (ZINDEX)(pos[5] +
                                  NSQUARES *
                                      (pos[4] +
                                       NSQUARES * N2_Index(pos[3], pos[2])))));
}

static bool Pos211111(ZINDEX index, int *pos) {
    int p2, id2;

    pos[8] = index % NSQUARES;
    index /= NSQUARES;
    pos[7] = index % NSQUARES;
    index /= NSQUARES;
    pos[6] = index % NSQUARES;
    index /= NSQUARES;
    pos[5] = index % NSQUARES;
    index /= NSQUARES;
    pos[4] = index % NSQUARES;
    index /= NSQUARES;
    assert(index < N2);
    p2 = p2_tab[index];
    pos[3] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[2] = p2;
    return true;
}

static ZINDEX Index121111(int *pos) {
    return pos[8] +
           NSQUARES *
               (pos[7] +
                NSQUARES *
                    (pos[6] +
                     NSQUARES *
                         (ZINDEX)(pos[5] +
                                  NSQUARES *
                                      (pos[2] +
                                       NSQUARES * N2_Index(pos[4], pos[3])))));
}

static bool Pos121111(ZINDEX index, int *pos) {
    int p2, id2;

    pos[8] = index % NSQUARES;
    index /= NSQUARES;
    pos[7] = index % NSQUARES;
    index /= NSQUARES;
    pos[6] = index % NSQUARES;
    index /= NSQUARES;
    pos[5] = index % NSQUARES;
    index /= NSQUARES;
    pos[2] = index % NSQUARES;
    index /= NSQUARES;
    assert(index < N2);
    p2 = p2_tab[index];
    pos[4] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[3] = p2;
    return true;
}

static ZINDEX Index112111(int *pos) {
    return pos[8] +
           NSQUARES *
               (pos[7] +
                NSQUARES *
                    (pos[6] +
                     NSQUARES *
                         (ZINDEX)(pos[3] +
                                  NSQUARES *
                                      (pos[2] +
                                       NSQUARES * N2_Index(pos[5], pos[4])))));
}

static bool Pos112111(ZINDEX index, int *pos) {
    int p2, id2;

    pos[8] = index % NSQUARES;
    index /= NSQUARES;
    pos[7] = index % NSQUARES;
    index /= NSQUARES;
    pos[6] = index % NSQUARES;
    index /= NSQUARES;
    pos[3] = index % NSQUARES;
    index /= NSQUARES;
    pos[2] = index % NSQUARES;
    index /= NSQUARES;
    assert(index < N2);
    p2 = p2_tab[index];
    pos[5] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[4] = p2;
    return true;
}

static ZINDEX Index111211(int *pos) {
    return pos[8] +
           NSQUARES *
               (pos[7] +
                NSQUARES *
                    (pos[4] +
                     NSQUARES *
                         (ZINDEX)(pos[3] +
                                  NSQUARES *
                                      (pos[2] +
                                       NSQUARES * N2_Index(pos[6], pos[5])))));
}

static bool Pos111211(ZINDEX index, int *pos) {
    int p2, id2;

    pos[8] = index % NSQUARES;
    index /= NSQUARES;
    pos[7] = index % NSQUARES;
    index /= NSQUARES;
    pos[4] = index % NSQUARES;
    index /= NSQUARES;
    pos[3] = index % NSQUARES;
    index /= NSQUARES;
    pos[2] = index % NSQUARES;
    index /= NSQUARES;
    assert(index < N2);
    p2 = p2_tab[index];
    pos[6] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[5] = p2;
    return true;
}

static ZINDEX Index111121(int *pos) {
    return pos[8] +
           NSQUARES *
               (pos[5] +
                NSQUARES *
                    (pos[4] +
                     NSQUARES *
                         (ZINDEX)(pos[3] +
                                  NSQUARES *
                                      (pos[2] +
                                       NSQUARES * N2_Index(pos[7], pos[6])))));
}

static bool Pos111121(ZINDEX index, int *pos) {
    int p2, id2;

    pos[8] = index % NSQUARES;
    index /= NSQUARES;
    pos[5] = index % NSQUARES;
    index /= NSQUARES;
    pos[4] = index % NSQUARES;
    index /= NSQUARES;
    pos[3] = index % NSQUARES;
    index /= NSQUARES;
    pos[2] = index % NSQUARES;
    index /= NSQUARES;
    assert(index < N2);
    p2 = p2_tab[index];
    pos[7] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[6] = p2;
    return true;
}

static ZINDEX Index111112(int *pos) {
    return pos[6] +
           NSQUARES *
               (pos[5] +
                NSQUARES *
                    (pos[4] +
                     NSQUARES *
                         (ZINDEX)(pos[3] +
                                  NSQUARES *
                                      (pos[2] +
                                       NSQUARES * N2_Index(pos[8], pos[7])))));
}

static bool Pos111112(ZINDEX index, int *pos) {
    int p2, id2;

    pos[6] = index % NSQUARES;
    index /= NSQUARES;
    pos[5] = index % NSQUARES;
    index /= NSQUARES;
    pos[4] = index % NSQUARES;
    index /= NSQUARES;
    pos[3] = index % NSQUARES;
    index /= NSQUARES;
    pos[2] = index % NSQUARES;
    index /= NSQUARES;
    assert(index < N2);
    p2 = p2_tab[index];
    pos[8] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[7] = p2;
    return true;
}

static ZINDEX Index22111(int *pos) {
    return pos[8] +
           NSQUARES *
               (pos[7] +
                NSQUARES * (pos[6] +
                            NSQUARES * (ZINDEX)(N2_Index(pos[5], pos[4]) +
                                                N2_Offset *
                                                    N2_Index(pos[3], pos[2]))));
}

static ZINDEX IndexDP22111(int *pos) {
    ZINDEX dp22 = IndexDP22(pos);
    if (dp22 == ALL_ONES)
        return ALL_ONES;
    return pos[8] + NSQUARES * (pos[7] + NSQUARES * (pos[6] + NSQUARES * dp22));
}

static bool Pos22111(ZINDEX index, int *pos) {
    int p2, id2;

    pos[8] = index % NSQUARES;
    index /= NSQUARES;
    pos[7] = index % NSQUARES;
    index /= NSQUARES;
    pos[6] = index % NSQUARES;
    index /= NSQUARES;
    id2 = index % N2_Offset;
    assert(id2 < N2);
    p2 = p2_tab[id2];
    pos[5] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[4] = p2;
    index /= N2_Offset;
    assert(index < N2);
    p2 = p2_tab[index];
    pos[3] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[2] = p2;
    return true;
}

static bool PosDP22111(ZINDEX index, int *pos) {
    pos[8] = index % NSQUARES;
    index /= NSQUARES;
    pos[7] = index % NSQUARES;
    index /= NSQUARES;
    pos[6] = index % NSQUARES;
    index /= NSQUARES;

    assert(index < N4_OPPOSING);

    return PosDP22(index, pos);
}

static ZINDEX Index21211(int *pos) {
    return pos[8] +
           NSQUARES *
               (pos[7] +
                NSQUARES * (pos[4] +
                            NSQUARES * (ZINDEX)(N2_Index(pos[6], pos[5]) +
                                                N2_Offset *
                                                    N2_Index(pos[3], pos[2]))));
}

static bool Pos21211(ZINDEX index, int *pos) {
    int p2, id2;

    pos[8] = index % NSQUARES;
    index /= NSQUARES;
    pos[7] = index % NSQUARES;
    index /= NSQUARES;
    pos[4] = index % NSQUARES;
    index /= NSQUARES;
    id2 = index % N2_Offset;
    assert(id2 < N2);
    p2 = p2_tab[id2];
    pos[6] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[5] = p2;
    index /= N2_Offset;
    assert(index < N2);
    p2 = p2_tab[index];
    pos[3] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[2] = p2;
    return true;
}

static ZINDEX Index21121(int *pos) {
    return pos[8] +
           NSQUARES *
               (pos[5] +
                NSQUARES * (pos[4] +
                            NSQUARES * (ZINDEX)(N2_Index(pos[7], pos[6]) +
                                                N2_Offset *
                                                    N2_Index(pos[3], pos[2]))));
}

static bool Pos21121(ZINDEX index, int *pos) {
    int p2, id2;

    pos[8] = index % NSQUARES;
    index /= NSQUARES;
    pos[5] = index % NSQUARES;
    index /= NSQUARES;
    pos[4] = index % NSQUARES;
    index /= NSQUARES;
    id2 = index % N2_Offset;
    assert(id2 < N2);
    p2 = p2_tab[id2];
    pos[7] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[6] = p2;
    index /= N2_Offset;
    assert(index < N2);
    p2 = p2_tab[index];
    pos[3] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[2] = p2;
    return true;
}

static ZINDEX Index21112(int *pos) {
    return pos[6] +
           NSQUARES *
               (pos[5] +
                NSQUARES * (pos[4] +
                            NSQUARES * (ZINDEX)(N2_Index(pos[8], pos[7]) +
                                                N2_Offset *
                                                    N2_Index(pos[3], pos[2]))));
}

static bool Pos21112(ZINDEX index, int *pos) {
    int p2, id2;

    pos[6] = index % NSQUARES;
    index /= NSQUARES;
    pos[5] = index % NSQUARES;
    index /= NSQUARES;
    pos[4] = index % NSQUARES;
    index /= NSQUARES;
    id2 = index % N2_Offset;
    assert(id2 < N2);
    p2 = p2_tab[id2];
    pos[8] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[7] = p2;
    index /= N2_Offset;
    assert(index < N2);
    p2 = p2_tab[index];
    pos[3] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[2] = p2;
    return true;
}

static ZINDEX Index12211(int *pos) {
    return pos[8] +
           NSQUARES *
               (pos[7] +
                NSQUARES * (pos[2] +
                            NSQUARES * (ZINDEX)(N2_Index(pos[6], pos[5]) +
                                                N2_Offset *
                                                    N2_Index(pos[4], pos[3]))));
}

static bool Pos12211(ZINDEX index, int *pos) {
    int p2, id2;

    pos[8] = index % NSQUARES;
    index /= NSQUARES;
    pos[7] = index % NSQUARES;
    index /= NSQUARES;
    pos[2] = index % NSQUARES;
    index /= NSQUARES;
    id2 = index % N2_Offset;
    assert(id2 < N2);
    p2 = p2_tab[id2];
    pos[6] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[5] = p2;
    index /= N2_Offset;
    assert(index < N2);
    p2 = p2_tab[index];
    pos[4] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[3] = p2;
    return true;
}

static ZINDEX Index12121(int *pos) {
    return pos[8] +
           NSQUARES *
               (pos[5] +
                NSQUARES * (pos[2] +
                            NSQUARES * (ZINDEX)(N2_Index(pos[7], pos[6]) +
                                                N2_Offset *
                                                    N2_Index(pos[4], pos[3]))));
}

static bool Pos12121(ZINDEX index, int *pos) {
    int p2, id2;

    pos[8] = index % NSQUARES;
    index /= NSQUARES;
    pos[5] = index % NSQUARES;
    index /= NSQUARES;
    pos[2] = index % NSQUARES;
    index /= NSQUARES;
    id2 = index % N2_Offset;
    assert(id2 < N2);
    p2 = p2_tab[id2];
    pos[7] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[6] = p2;
    index /= N2_Offset;
    assert(index < N2);
    p2 = p2_tab[index];
    pos[4] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[3] = p2;
    return true;
}

static ZINDEX Index12112(int *pos) {
    return pos[6] +
           NSQUARES *
               (pos[5] +
                NSQUARES * (pos[2] +
                            NSQUARES * (ZINDEX)(N2_Index(pos[8], pos[7]) +
                                                N2_Offset *
                                                    N2_Index(pos[4], pos[3]))));
}

static bool Pos12112(ZINDEX index, int *pos) {
    int p2, id2;

    pos[6] = index % NSQUARES;
    index /= NSQUARES;
    pos[5] = index % NSQUARES;
    index /= NSQUARES;
    pos[2] = index % NSQUARES;
    index /= NSQUARES;
    id2 = index % N2_Offset;
    assert(id2 < N2);
    p2 = p2_tab[id2];
    pos[8] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[7] = p2;
    index /= N2_Offset;
    assert(index < N2);
    p2 = p2_tab[index];
    pos[4] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[3] = p2;
    return true;
}

static ZINDEX Index11221(int *pos) {
    return pos[8] +
           NSQUARES *
               (pos[3] +
                NSQUARES * (pos[2] +
                            NSQUARES * (ZINDEX)(N2_Index(pos[7], pos[6]) +
                                                N2_Offset *
                                                    N2_Index(pos[5], pos[4]))));
}

static bool Pos11221(ZINDEX index, int *pos) {
    int p2, id2;

    pos[8] = index % NSQUARES;
    index /= NSQUARES;
    pos[3] = index % NSQUARES;
    index /= NSQUARES;
    pos[2] = index % NSQUARES;
    index /= NSQUARES;
    id2 = index % N2_Offset;
    assert(id2 < N2);
    p2 = p2_tab[id2];
    pos[7] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[6] = p2;
    index /= N2_Offset;
    assert(index < N2);
    p2 = p2_tab[index];
    pos[5] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[4] = p2;
    return true;
}

static ZINDEX Index11212(int *pos) {
    return pos[6] +
           NSQUARES *
               (pos[3] +
                NSQUARES * (pos[2] +
                            NSQUARES * (ZINDEX)(N2_Index(pos[8], pos[7]) +
                                                N2_Offset *
                                                    N2_Index(pos[5], pos[4]))));
}

static bool Pos11212(ZINDEX index, int *pos) {
    int p2, id2;

    pos[6] = index % NSQUARES;
    index /= NSQUARES;
    pos[3] = index % NSQUARES;
    index /= NSQUARES;
    pos[2] = index % NSQUARES;
    index /= NSQUARES;
    id2 = index % N2_Offset;
    assert(id2 < N2);
    p2 = p2_tab[id2];
    pos[8] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[7] = p2;
    index /= N2_Offset;
    assert(index < N2);
    p2 = p2_tab[index];
    pos[5] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[4] = p2;
    return true;
}

static ZINDEX Index11122(int *pos) {
    return pos[4] +
           NSQUARES *
               (pos[3] +
                NSQUARES * (pos[2] +
                            NSQUARES * (ZINDEX)(N2_Index(pos[8], pos[7]) +
                                                N2_Offset *
                                                    N2_Index(pos[6], pos[5]))));
}

static bool Pos11122(ZINDEX index, int *pos) {
    int p2, id2;

    pos[4] = index % NSQUARES;
    index /= NSQUARES;
    pos[3] = index % NSQUARES;
    index /= NSQUARES;
    pos[2] = index % NSQUARES;
    index /= NSQUARES;
    id2 = index % N2_Offset;
    assert(id2 < N2);
    p2 = p2_tab[id2];
    pos[8] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[7] = p2;
    index /= N2_Offset;
    assert(index < N2);
    p2 = p2_tab[index];
    pos[6] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[5] = p2;
    return true;
}

static ZINDEX Index2221(int *pos) {
    return pos[8] +
           NSQUARES *
               (N2_Index(pos[7], pos[6]) +
                N2_Offset * (ZINDEX)(N2_Index(pos[5], pos[4]) +
                                     N2_Offset * N2_Index(pos[3], pos[2])));
}

static ZINDEX IndexDP2221(int *pos) {
    ZINDEX dp22 = IndexDP22(pos);
    if (dp22 == ALL_ONES)
        return ALL_ONES;
    return pos[8] + NSQUARES * (N2_Index(pos[7], pos[6]) + N2_Offset * dp22);
}

static bool Pos2221(ZINDEX index, int *pos) {
    int p2, id2;

    pos[8] = index % NSQUARES;
    index /= NSQUARES;
    id2 = index % N2_Offset;
    assert(id2 < N2);
    p2 = p2_tab[id2];
    pos[7] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[6] = p2;
    index /= N2_Offset;
    id2 = index % N2_Offset;
    assert(id2 < N2);
    p2 = p2_tab[id2];
    pos[5] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[4] = p2;
    index /= N2_Offset;
    assert(index < N2);
    p2 = p2_tab[index];
    pos[3] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[2] = p2;
    return true;
}

static bool PosDP2221(ZINDEX index, int *pos) {
    int p2, id2;

    pos[8] = index % NSQUARES;
    index /= NSQUARES;
    id2 = index % N2_Offset;
    assert(id2 < N2);
    p2 = p2_tab[id2];
    pos[7] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[6] = p2;
    index /= N2_Offset;

    assert(index < N4_OPPOSING);

    return PosDP22(index, pos);
}

static ZINDEX Index2221_1131(int *pos) {
    return pos[8] + NSQUARES * (N2_Odd_Index(pos[7], pos[6]) +
                                N2_ODD_PARITY_Offset *
                                    (ZINDEX)(N2_Odd_Index(pos[3], pos[2]) +
                                             N2_ODD_PARITY_Offset *
                                                 N2_Index(pos[5], pos[4])));
}

static bool Pos2221_1131(ZINDEX index, int *pos) {
    int p2, id2;

    pos[8] = index % NSQUARES;
    index /= NSQUARES;
    id2 = index % N2_ODD_PARITY_Offset;
    assert(id2 < N2_ODD_PARITY);
    p2 = p2_odd_tab[id2];
    pos[7] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[6] = p2;
    index /= N2_ODD_PARITY_Offset;
    id2 = index % N2_ODD_PARITY_Offset;
    assert(id2 < N2_ODD_PARITY);
    p2 = p2_odd_tab[id2];
    pos[3] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[2] = p2;
    index /= N2_ODD_PARITY_Offset;
    assert(index < N2);
    p2 = p2_tab[index];
    pos[5] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[4] = p2;
    return true;
}

static ZINDEX Index2221_1130(int *pos) {
    return pos[8] + NSQUARES * (N2_Even_Index(pos[7], pos[6]) +
                                N2_EVEN_PARITY_Offset *
                                    (ZINDEX)(N2_Odd_Index(pos[3], pos[2]) +
                                             N2_ODD_PARITY_Offset *
                                                 N2_Index(pos[5], pos[4])));
}

static bool Pos2221_1130(ZINDEX index, int *pos) {
    int p2, id2;

    pos[8] = index % NSQUARES;
    index /= NSQUARES;
    id2 = index % N2_EVEN_PARITY_Offset;
    assert(id2 < N2_EVEN_PARITY);
    p2 = p2_even_tab[id2];
    pos[7] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[6] = p2;
    index /= N2_EVEN_PARITY_Offset;
    id2 = index % N2_ODD_PARITY_Offset;
    assert(id2 < N2_ODD_PARITY);
    p2 = p2_odd_tab[id2];
    pos[3] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[2] = p2;
    index /= N2_ODD_PARITY_Offset;
    assert(index < N2);
    p2 = p2_tab[index];
    pos[5] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[4] = p2;
    return true;
}

static ZINDEX Index2221_1030(int *pos) {
    return pos[8] + NSQUARES * (N2_Even_Index(pos[7], pos[6]) +
                                N2_EVEN_PARITY_Offset *
                                    (ZINDEX)(N2_Even_Index(pos[3], pos[2]) +
                                             N2_EVEN_PARITY_Offset *
                                                 N2_Index(pos[5], pos[4])));
}

static bool Pos2221_1030(ZINDEX index, int *pos) {
    int p2, id2;

    pos[8] = index % NSQUARES;
    index /= NSQUARES;
    id2 = index % N2_EVEN_PARITY_Offset;
    assert(id2 < N2_EVEN_PARITY);
    p2 = p2_even_tab[id2];
    pos[7] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[6] = p2;
    index /= N2_EVEN_PARITY_Offset;
    id2 = index % N2_EVEN_PARITY_Offset;
    assert(id2 < N2_EVEN_PARITY);
    p2 = p2_even_tab[id2];
    pos[3] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[2] = p2;
    index /= N2_EVEN_PARITY_Offset;
    assert(index < N2);
    p2 = p2_tab[index];
    pos[5] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[4] = p2;
    return true;
}

static ZINDEX Index2212(int *pos) {
    return pos[6] +
           NSQUARES *
               (N2_Index(pos[8], pos[7]) +
                N2_Offset * (ZINDEX)(N2_Index(pos[5], pos[4]) +
                                     N2_Offset * N2_Index(pos[3], pos[2])));
}

static ZINDEX IndexDP2212(int *pos) {
    ZINDEX dp22 = IndexDP22(pos);
    if (dp22 == ALL_ONES)
        return ALL_ONES;
    return pos[6] + NSQUARES * (N2_Index(pos[8], pos[7]) + N2_Offset * dp22);
}

static bool Pos2212(ZINDEX index, int *pos) {
    int p2, id2;

    pos[6] = index % NSQUARES;
    index /= NSQUARES;
    id2 = index % N2_Offset;
    assert(id2 < N2);
    p2 = p2_tab[id2];
    pos[8] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[7] = p2;
    index /= N2_Offset;
    id2 = index % N2_Offset;
    assert(id2 < N2);
    p2 = p2_tab[id2];
    pos[5] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[4] = p2;
    index /= N2_Offset;
    assert(index < N2);
    p2 = p2_tab[index];
    pos[3] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[2] = p2;
    return true;
}

static bool PosDP2212(ZINDEX index, int *pos) {
    int p2, id2;

    pos[6] = index % NSQUARES;
    index /= NSQUARES;
    id2 = index % N2_Offset;
    assert(id2 < N2);
    p2 = p2_tab[id2];
    pos[8] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[7] = p2;
    index /= N2_Offset;

    assert(index < N4_OPPOSING);

    return PosDP22(index, pos);
}

static ZINDEX Index2122(int *pos) {
    return pos[4] +
           NSQUARES *
               (N2_Index(pos[8], pos[7]) +
                N2_Offset * (ZINDEX)(N2_Index(pos[6], pos[5]) +
                                     N2_Offset * N2_Index(pos[3], pos[2])));
}

static bool Pos2122(ZINDEX index, int *pos) {
    int p2, id2;

    pos[4] = index % NSQUARES;
    index /= NSQUARES;
    id2 = index % N2_Offset;
    assert(id2 < N2);
    p2 = p2_tab[id2];
    pos[8] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[7] = p2;
    index /= N2_Offset;
    id2 = index % N2_Offset;
    assert(id2 < N2);
    p2 = p2_tab[id2];
    pos[6] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[5] = p2;
    index /= N2_Offset;
    assert(index < N2);
    p2 = p2_tab[index];
    pos[3] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[2] = p2;
    return true;
}

static ZINDEX Index1222(int *pos) {
    return pos[2] +
           NSQUARES *
               (N2_Index(pos[8], pos[7]) +
                N2_Offset * (ZINDEX)(N2_Index(pos[6], pos[5]) +
                                     N2_Offset * N2_Index(pos[4], pos[3])));
}

static bool Pos1222(ZINDEX index, int *pos) {
    int p2, id2;

    pos[2] = index % NSQUARES;
    index /= NSQUARES;
    id2 = index % N2_Offset;
    assert(id2 < N2);
    p2 = p2_tab[id2];
    pos[8] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[7] = p2;
    index /= N2_Offset;
    id2 = index % N2_Offset;
    assert(id2 < N2);
    p2 = p2_tab[id2];
    pos[6] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[5] = p2;
    index /= N2_Offset;
    assert(index < N2);
    p2 = p2_tab[index];
    pos[4] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[3] = p2;
    return true;
}

static ZINDEX Index31111(int *pos) {
    return pos[8] +
           NSQUARES *
               (pos[7] +
                NSQUARES *
                    (ZINDEX)(pos[6] +
                             NSQUARES *
                                 (pos[5] + NSQUARES * N3_Index(pos[4], pos[3],
                                                               pos[2]))));
}

static bool Pos31111(ZINDEX index, int *pos) {
    int p3;

    pos[8] = index % NSQUARES;
    index /= NSQUARES;
    pos[7] = index % NSQUARES;
    index /= NSQUARES;
    pos[6] = index % NSQUARES;
    index /= NSQUARES;
    pos[5] = index % NSQUARES;
    index /= NSQUARES;
    assert(index < N3);
    p3 = p3_tab[index];
    pos[4] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[3] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[2] = p3;
    return true;
}

static ZINDEX Index13111(int *pos) {
    return pos[8] +
           NSQUARES *
               (pos[7] +
                NSQUARES *
                    (ZINDEX)(pos[6] +
                             NSQUARES *
                                 (pos[2] + NSQUARES * N3_Index(pos[5], pos[4],
                                                               pos[3]))));
}

static bool Pos13111(ZINDEX index, int *pos) {
    int p3;

    pos[8] = index % NSQUARES;
    index /= NSQUARES;
    pos[7] = index % NSQUARES;
    index /= NSQUARES;
    pos[6] = index % NSQUARES;
    index /= NSQUARES;
    pos[2] = index % NSQUARES;
    index /= NSQUARES;
    assert(index < N3);
    p3 = p3_tab[index];
    pos[5] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[4] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[3] = p3;
    return true;
}

static ZINDEX Index11311(int *pos) {
    return pos[8] +
           NSQUARES *
               (pos[7] +
                NSQUARES *
                    (ZINDEX)(pos[3] +
                             NSQUARES *
                                 (pos[2] + NSQUARES * N3_Index(pos[6], pos[5],
                                                               pos[4]))));
}

static bool Pos11311(ZINDEX index, int *pos) {
    int p3;

    pos[8] = index % NSQUARES;
    index /= NSQUARES;
    pos[7] = index % NSQUARES;
    index /= NSQUARES;
    pos[3] = index % NSQUARES;
    index /= NSQUARES;
    pos[2] = index % NSQUARES;
    index /= NSQUARES;
    assert(index < N3);
    p3 = p3_tab[index];
    pos[6] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[5] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[4] = p3;
    return true;
}

static ZINDEX Index11131(int *pos) {
    return pos[8] +
           NSQUARES *
               (pos[4] +
                NSQUARES *
                    (ZINDEX)(pos[3] +
                             NSQUARES *
                                 (pos[2] + NSQUARES * N3_Index(pos[7], pos[6],
                                                               pos[5]))));
}

static bool Pos11131(ZINDEX index, int *pos) {
    int p3;

    pos[8] = index % NSQUARES;
    index /= NSQUARES;
    pos[4] = index % NSQUARES;
    index /= NSQUARES;
    pos[3] = index % NSQUARES;
    index /= NSQUARES;
    pos[2] = index % NSQUARES;
    index /= NSQUARES;
    assert(index < N3);
    p3 = p3_tab[index];
    pos[7] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[6] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[5] = p3;
    return true;
}

static ZINDEX Index11113(int *pos) {
    return pos[5] +
           NSQUARES *
               (pos[4] +
                NSQUARES *
                    (ZINDEX)(pos[3] +
                             NSQUARES *
                                 (pos[2] + NSQUARES * N3_Index(pos[8], pos[7],
                                                               pos[6]))));
}

static bool Pos11113(ZINDEX index, int *pos) {
    int p3;

    pos[5] = index % NSQUARES;
    index /= NSQUARES;
    pos[4] = index % NSQUARES;
    index /= NSQUARES;
    pos[3] = index % NSQUARES;
    index /= NSQUARES;
    pos[2] = index % NSQUARES;
    index /= NSQUARES;
    assert(index < N3);
    p3 = p3_tab[index];
    pos[8] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[7] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[6] = p3;
    return true;
}

static ZINDEX Index3211(int *pos) {
    return pos[8] +
           NSQUARES *
               (pos[7] +
                (NSQUARES) *
                    (ZINDEX)(N2_Index(pos[6], pos[5]) +
                             N2_Offset * N3_Index(pos[4], pos[3], pos[2])));
}

static bool Pos3211(ZINDEX index, int *pos) {
    int p2, p3, id2;

    pos[8] = index % NSQUARES;
    index /= NSQUARES;
    pos[7] = index % NSQUARES;
    index /= NSQUARES;
    id2 = index % N2_Offset;
    assert(id2 < N2);
    p2 = p2_tab[id2];
    pos[6] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[5] = p2;
    index /= N2_Offset;
    assert(index < N3);
    p3 = p3_tab[index];
    pos[4] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[3] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[2] = p3;
    return true;
}

static ZINDEX Index3121(int *pos) {
    return pos[8] +
           NSQUARES *
               (pos[5] +
                (NSQUARES) *
                    (ZINDEX)(N2_Index(pos[7], pos[6]) +
                             N2_Offset * N3_Index(pos[4], pos[3], pos[2])));
}

static bool Pos3121(ZINDEX index, int *pos) {
    int p2, p3, id2;

    pos[8] = index % NSQUARES;
    index /= NSQUARES;
    pos[5] = index % NSQUARES;
    index /= NSQUARES;
    id2 = index % N2_Offset;
    assert(id2 < N2);
    p2 = p2_tab[id2];
    pos[7] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[6] = p2;
    index /= N2_Offset;
    assert(index < N3);
    p3 = p3_tab[index];
    pos[4] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[3] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[2] = p3;
    return true;
}

static ZINDEX Index3121_1100(int *pos) {
    return pos[8] +
           NSQUARES *
               (pos[5] +
                (NSQUARES) *
                    (ZINDEX)(N2_Index(pos[7], pos[6]) +
                             N2_Offset * N3_Odd_Index(pos[4], pos[3], pos[2])));
}

static bool Pos3121_1100(ZINDEX index, int *pos) {
    int p2, p3, id2;

    pos[8] = index % NSQUARES;
    index /= NSQUARES;
    pos[5] = index % NSQUARES;
    index /= NSQUARES;
    id2 = index % N2_Offset;
    assert(id2 < N2);
    p2 = p2_tab[id2];
    pos[7] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[6] = p2;
    index /= N2_Offset;
    assert(index < N3_ODD_PARITY);
    p3 = p3_odd_tab[index];
    pos[4] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[3] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[2] = p3;
    return true;
}

static ZINDEX Index3121_1111(int *pos) {
    return pos[8] +
           NSQUARES * (pos[5] +
                       (NSQUARES) *
                           (ZINDEX)(N2_Odd_Index(pos[7], pos[6]) +
                                    N2_ODD_PARITY_Offset *
                                        N3_Odd_Index(pos[4], pos[3], pos[2])));
}

static bool Pos3121_1111(ZINDEX index, int *pos) {
    int p2, p3, id2;

    pos[8] = index % NSQUARES;
    index /= NSQUARES;
    pos[5] = index % NSQUARES;
    index /= NSQUARES;
    id2 = index % N2_ODD_PARITY_Offset;
    assert(id2 < N2_ODD_PARITY);
    p2 = p2_odd_tab[id2];
    pos[7] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[6] = p2;
    index /= N2_ODD_PARITY_Offset;
    assert(index < N3_ODD_PARITY);
    p3 = p3_odd_tab[index];
    pos[4] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[3] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[2] = p3;
    return true;
}

static ZINDEX Index3121_1110(int *pos) {
    return pos[8] +
           NSQUARES * (pos[5] +
                       (NSQUARES) *
                           (ZINDEX)(N2_Even_Index(pos[7], pos[6]) +
                                    N2_EVEN_PARITY_Offset *
                                        N3_Odd_Index(pos[4], pos[3], pos[2])));
}

static bool Pos3121_1110(ZINDEX index, int *pos) {
    int p2, p3, id2;

    pos[8] = index % NSQUARES;
    index /= NSQUARES;
    pos[5] = index % NSQUARES;
    index /= NSQUARES;
    id2 = index % N2_EVEN_PARITY_Offset;
    assert(id2 < N2_EVEN_PARITY);
    p2 = p2_even_tab[id2];
    pos[7] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[6] = p2;
    index /= N2_EVEN_PARITY_Offset;
    assert(index < N3_ODD_PARITY);
    p3 = p3_odd_tab[index];
    pos[4] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[3] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[2] = p3;
    return true;
}

static ZINDEX Index3112(int *pos) {
    return pos[6] +
           NSQUARES *
               (pos[5] +
                (NSQUARES) *
                    (ZINDEX)(N2_Index(pos[8], pos[7]) +
                             N2_Offset * N3_Index(pos[4], pos[3], pos[2])));
}

static bool Pos3112(ZINDEX index, int *pos) {
    int p2, p3, id2;

    pos[6] = index % NSQUARES;
    index /= NSQUARES;
    pos[5] = index % NSQUARES;
    index /= NSQUARES;
    id2 = index % N2_Offset;
    assert(id2 < N2);
    p2 = p2_tab[id2];
    pos[8] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[7] = p2;
    index /= N2_Offset;
    assert(index < N3);
    p3 = p3_tab[index];
    pos[4] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[3] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[2] = p3;
    return true;
}

static ZINDEX Index2311(int *pos) {
    return pos[8] +
           NSQUARES *
               (pos[7] +
                (NSQUARES) *
                    (ZINDEX)(N2_Index(pos[3], pos[2]) +
                             N2_Offset * N3_Index(pos[6], pos[5], pos[4])));
}

static bool Pos2311(ZINDEX index, int *pos) {
    int p2, p3, id2;

    pos[8] = index % NSQUARES;
    index /= NSQUARES;
    pos[7] = index % NSQUARES;
    index /= NSQUARES;
    id2 = index % N2_Offset;
    assert(id2 < N2);
    p2 = p2_tab[id2];
    pos[3] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[2] = p2;
    index /= N2_Offset;
    assert(index < N3);
    p3 = p3_tab[index];
    pos[6] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[5] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[4] = p3;
    return true;
}

static ZINDEX Index2131(int *pos) {
    return pos[8] +
           NSQUARES *
               (pos[4] +
                (NSQUARES) *
                    (ZINDEX)(N2_Index(pos[3], pos[2]) +
                             N2_Offset * N3_Index(pos[7], pos[6], pos[5])));
}

static bool Pos2131(ZINDEX index, int *pos) {
    int p2, p3, id2;

    pos[8] = index % NSQUARES;
    index /= NSQUARES;
    pos[4] = index % NSQUARES;
    index /= NSQUARES;
    id2 = index % N2_Offset;
    assert(id2 < N2);
    p2 = p2_tab[id2];
    pos[3] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[2] = p2;
    index /= N2_Offset;
    assert(index < N3);
    p3 = p3_tab[index];
    pos[7] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[6] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[5] = p3;
    return true;
}

static ZINDEX Index2113(int *pos) {
    return pos[5] +
           NSQUARES *
               (pos[4] +
                (NSQUARES) *
                    (ZINDEX)(N2_Index(pos[3], pos[2]) +
                             N2_Offset * N3_Index(pos[8], pos[7], pos[6])));
}

static bool Pos2113(ZINDEX index, int *pos) {
    int p2, p3, id2;

    pos[5] = index % NSQUARES;
    index /= NSQUARES;
    pos[4] = index % NSQUARES;
    index /= NSQUARES;
    id2 = index % N2_Offset;
    assert(id2 < N2);
    p2 = p2_tab[id2];
    pos[3] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[2] = p2;
    index /= N2_Offset;
    assert(index < N3);
    p3 = p3_tab[index];
    pos[8] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[7] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[6] = p3;
    return true;
}

static ZINDEX Index1321(int *pos) {
    return pos[8] +
           NSQUARES *
               (pos[2] +
                (NSQUARES) *
                    (ZINDEX)(N2_Index(pos[7], pos[6]) +
                             N2_Offset * N3_Index(pos[5], pos[4], pos[3])));
}

static bool Pos1321(ZINDEX index, int *pos) {
    int p2, p3, id2;

    pos[8] = index % NSQUARES;
    index /= NSQUARES;
    pos[2] = index % NSQUARES;
    index /= NSQUARES;
    id2 = index % N2_Offset;
    assert(id2 < N2);
    p2 = p2_tab[id2];
    pos[7] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[6] = p2;
    index /= N2_Offset;
    assert(index < N3);
    p3 = p3_tab[index];
    pos[5] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[4] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[3] = p3;
    return true;
}

static ZINDEX Index1312(int *pos) {
    return pos[6] +
           NSQUARES *
               (pos[2] +
                (NSQUARES) *
                    (ZINDEX)(N2_Index(pos[8], pos[7]) +
                             N2_Offset * N3_Index(pos[5], pos[4], pos[3])));
}

static bool Pos1312(ZINDEX index, int *pos) {
    int p2, p3, id2;

    pos[6] = index % NSQUARES;
    index /= NSQUARES;
    pos[2] = index % NSQUARES;
    index /= NSQUARES;
    id2 = index % N2_Offset;
    assert(id2 < N2);
    p2 = p2_tab[id2];
    pos[8] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[7] = p2;
    index /= N2_Offset;
    assert(index < N3);
    p3 = p3_tab[index];
    pos[5] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[4] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[3] = p3;
    return true;
}

static ZINDEX Index1312_0010(int *pos) {
    return pos[6] +
           NSQUARES *
               (pos[2] +
                (NSQUARES) * (ZINDEX)(N2_Even_Index(pos[8], pos[7]) +
                                      N2_EVEN_PARITY_Offset *
                                          N3_Index(pos[5], pos[4], pos[3])));
}

static bool Pos1312_0010(ZINDEX index, int *pos) {
    int p2, p3, id2;

    pos[6] = index % NSQUARES;
    index /= NSQUARES;
    pos[2] = index % NSQUARES;
    index /= NSQUARES;
    id2 = index % N2_EVEN_PARITY_Offset;
    assert(id2 < N2_EVEN_PARITY);
    p2 = p2_even_tab[id2];
    pos[8] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[7] = p2;
    index /= N2_EVEN_PARITY_Offset;
    assert(index < N3);
    p3 = p3_tab[index];
    pos[5] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[4] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[3] = p3;
    return true;
}

static ZINDEX Index1312_0011(int *pos) {
    return pos[6] +
           NSQUARES *
               (pos[2] +
                (NSQUARES) * (ZINDEX)(N2_Odd_Index(pos[8], pos[7]) +
                                      N2_ODD_PARITY_Offset *
                                          N3_Index(pos[5], pos[4], pos[3])));
}

static bool Pos1312_0011(ZINDEX index, int *pos) {
    int p2, p3, id2;

    pos[6] = index % NSQUARES;
    index /= NSQUARES;
    pos[2] = index % NSQUARES;
    index /= NSQUARES;
    id2 = index % N2_ODD_PARITY_Offset;
    assert(id2 < N2_ODD_PARITY);
    p2 = p2_odd_tab[id2];
    pos[8] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[7] = p2;
    index /= N2_ODD_PARITY_Offset;
    assert(index < N3);
    p3 = p3_tab[index];
    pos[5] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[4] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[3] = p3;
    return true;
}

static ZINDEX Index1231(int *pos) {
    return pos[8] +
           NSQUARES *
               (pos[2] +
                (NSQUARES) *
                    (ZINDEX)(N2_Index(pos[4], pos[3]) +
                             N2_Offset * N3_Index(pos[7], pos[6], pos[5])));
}

static bool Pos1231(ZINDEX index, int *pos) {
    int p2, p3, id2;

    pos[8] = index % NSQUARES;
    index /= NSQUARES;
    pos[2] = index % NSQUARES;
    index /= NSQUARES;
    id2 = index % N2_Offset;
    assert(id2 < N2);
    p2 = p2_tab[id2];
    pos[4] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[3] = p2;
    index /= N2_Offset;
    assert(index < N3);
    p3 = p3_tab[index];
    pos[7] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[6] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[5] = p3;
    return true;
}

static ZINDEX Index1213(int *pos) {
    return pos[5] +
           NSQUARES *
               (pos[2] +
                (NSQUARES) *
                    (ZINDEX)(N2_Index(pos[4], pos[3]) +
                             N2_Offset * N3_Index(pos[8], pos[7], pos[6])));
}

static bool Pos1213(ZINDEX index, int *pos) {
    int p2, p3, id2;

    pos[5] = index % NSQUARES;
    index /= NSQUARES;
    pos[2] = index % NSQUARES;
    index /= NSQUARES;
    id2 = index % N2_Offset;
    assert(id2 < N2);
    p2 = p2_tab[id2];
    pos[4] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[3] = p2;
    index /= N2_Offset;
    assert(index < N3);
    p3 = p3_tab[index];
    pos[8] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[7] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[6] = p3;
    return true;
}

static ZINDEX Index1132(int *pos) {
    return pos[3] +
           NSQUARES *
               (pos[2] +
                (NSQUARES) *
                    (ZINDEX)(N2_Index(pos[8], pos[7]) +
                             N2_Offset * N3_Index(pos[6], pos[5], pos[4])));
}

static bool Pos1132(ZINDEX index, int *pos) {
    int p2, p3, id2;

    pos[3] = index % NSQUARES;
    index /= NSQUARES;
    pos[2] = index % NSQUARES;
    index /= NSQUARES;
    id2 = index % N2_Offset;
    assert(id2 < N2);
    p2 = p2_tab[id2];
    pos[8] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[7] = p2;
    index /= N2_Offset;
    assert(index < N3);
    p3 = p3_tab[index];
    pos[6] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[5] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[4] = p3;
    return true;
}

static ZINDEX Index1123(int *pos) {
    return pos[3] +
           NSQUARES *
               (pos[2] +
                (NSQUARES) *
                    (ZINDEX)(N2_Index(pos[5], pos[4]) +
                             N2_Offset * N3_Index(pos[8], pos[7], pos[6])));
}

static bool Pos1123(ZINDEX index, int *pos) {
    int p2, p3, id2;

    pos[3] = index % NSQUARES;
    index /= NSQUARES;
    pos[2] = index % NSQUARES;
    index /= NSQUARES;
    id2 = index % N2_Offset;
    assert(id2 < N2);
    p2 = p2_tab[id2];
    pos[5] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[4] = p2;
    index /= N2_Offset;
    assert(index < N3);
    p3 = p3_tab[index];
    pos[8] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[7] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[6] = p3;
    return true;
}

static ZINDEX Index331(int *pos) {
    return pos[8] +
           NSQUARES * (N3_Index(pos[7], pos[6], pos[5]) +
                       N3_Offset * (ZINDEX)N3_Index(pos[4], pos[3], pos[2]));
}

static bool Pos331(ZINDEX index, int *pos) {
    int id3, p3;

    pos[8] = index % NSQUARES;
    index /= NSQUARES;
    id3 = index % N3_Offset;
    assert(id3 < N3);
    index /= N3_Offset;
    assert(index < N3);
    p3 = p3_tab[id3];
    pos[7] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[6] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[5] = p3;
    p3 = p3_tab[index];
    pos[4] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[3] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[2] = p3;
    return true;
}

static ZINDEX Index331_0020(int *pos) {
    return pos[8] + NSQUARES * (N3_Even_Index(pos[7], pos[6], pos[5]) +
                                N3_EVEN_PARITY_Offset *
                                    (ZINDEX)N3_Index(pos[4], pos[3], pos[2]));
}

static bool Pos331_0020(ZINDEX index, int *pos) {
    int id3, p3;

    pos[8] = index % NSQUARES;
    index /= NSQUARES;
    id3 = index % N3_EVEN_PARITY_Offset;
    assert(id3 < N3_EVEN_PARITY);
    index /= N3_EVEN_PARITY_Offset;
    assert(index < N3);
    p3 = p3_even_tab[id3];
    pos[7] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[6] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[5] = p3;
    p3 = p3_tab[index];
    pos[4] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[3] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[2] = p3;
    return true;
}

static ZINDEX Index331_0021(int *pos) {
    return pos[8] + NSQUARES * (N3_Odd_Index(pos[7], pos[6], pos[5]) +
                                N3_ODD_PARITY_Offset *
                                    (ZINDEX)N3_Index(pos[4], pos[3], pos[2]));
}

static bool Pos331_0021(ZINDEX index, int *pos) {
    int id3, p3;

    pos[8] = index % NSQUARES;
    index /= NSQUARES;
    id3 = index % N3_ODD_PARITY_Offset;
    assert(id3 < N3_ODD_PARITY);
    index /= N3_ODD_PARITY_Offset;
    assert(index < N3);
    p3 = p3_odd_tab[id3];
    pos[7] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[6] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[5] = p3;
    p3 = p3_tab[index];
    pos[4] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[3] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[2] = p3;
    return true;
}

static ZINDEX Index313(int *pos) {
    return pos[5] +
           NSQUARES * (N3_Index(pos[8], pos[7], pos[6]) +
                       N3_Offset * (ZINDEX)N3_Index(pos[4], pos[3], pos[2]));
}

static bool Pos313(ZINDEX index, int *pos) {
    int id3, p3;

    pos[5] = index % NSQUARES;
    index /= NSQUARES;
    id3 = index % N3_Offset;
    assert(id3 < N3);
    index /= N3_Offset;
    assert(index < N3);
    p3 = p3_tab[id3];
    pos[8] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[7] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[6] = p3;
    p3 = p3_tab[index];
    pos[4] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[3] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[2] = p3;
    return true;
}

static ZINDEX Index133(int *pos) {
    return pos[2] +
           NSQUARES * (N3_Index(pos[8], pos[7], pos[6]) +
                       N3_Offset * (ZINDEX)N3_Index(pos[5], pos[4], pos[3]));
}

static bool Pos133(ZINDEX index, int *pos) {
    int id3, p3;

    pos[2] = index % NSQUARES;
    index /= NSQUARES;
    id3 = index % N3_Offset;
    assert(id3 < N3);
    index /= N3_Offset;
    assert(index < N3);
    p3 = p3_tab[id3];
    pos[8] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[7] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[6] = p3;
    p3 = p3_tab[index];
    pos[5] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[4] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[3] = p3;
    return true;
}

static ZINDEX Index322(int *pos) {
    return N2_Index(pos[8], pos[7]) +
           (N2_Offset) * (N2_Index(pos[6], pos[5]) +
                          N2_Offset * (ZINDEX)N3_Index(pos[4], pos[3], pos[2]));
}

static bool Pos322(ZINDEX index, int *pos) {
    unsigned int id2 = index % N2_Offset;
    assert(id2 < N2);
    unsigned int p2 = p2_tab[id2];
    pos[8] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[7] = p2;
    index /= N2_Offset;
    id2 = index % N2_Offset;
    assert(id2 < N2);
    p2 = p2_tab[id2];
    pos[6] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[5] = p2;
    index /= N2_Offset;
    assert(index < N3);
    unsigned int p3 = p3_tab[index];
    pos[4] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[3] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[2] = p3;
    return true;
}

static ZINDEX Index322_0010(int *pos) {
    return N2_Even_Index(pos[6], pos[5]) +
           (N2_EVEN_PARITY_Offset) *
               (N2_Index(pos[8], pos[7]) +
                N2_Offset * (ZINDEX)N3_Index(pos[4], pos[3], pos[2]));
}

static bool Pos322_0010(ZINDEX index, int *pos) {
    unsigned int id2 = index % N2_EVEN_PARITY_Offset;
    assert(id2 < N2_EVEN_PARITY);
    unsigned int p2 = p2_even_tab[id2];
    pos[6] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[5] = p2;
    index /= N2_EVEN_PARITY_Offset;
    id2 = index % N2_Offset;
    assert(id2 < N2);
    p2 = p2_tab[id2];
    pos[8] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[7] = p2;
    index /= N2_Offset;
    assert(index < N3);
    unsigned int p3 = p3_tab[index];
    pos[4] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[3] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[2] = p3;
    return true;
}

static ZINDEX Index322_0011(int *pos) {
    return N2_Odd_Index(pos[6], pos[5]) +
           (N2_ODD_PARITY_Offset) *
               (N2_Index(pos[8], pos[7]) +
                N2_Offset * (ZINDEX)N3_Index(pos[4], pos[3], pos[2]));
}

static bool Pos322_0011(ZINDEX index, int *pos) {
    unsigned int id2 = index % N2_ODD_PARITY_Offset;
    assert(id2 < N2_ODD_PARITY);
    unsigned int p2 = p2_odd_tab[id2];
    pos[6] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[5] = p2;
    index /= N2_ODD_PARITY_Offset;
    id2 = index % N2_Offset;
    assert(id2 < N2);
    p2 = p2_tab[id2];
    pos[8] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[7] = p2;
    index /= N2_Offset;
    assert(index < N3);
    unsigned int p3 = p3_tab[index];
    pos[4] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[3] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[2] = p3;
    return true;
}

static ZINDEX Index232(int *pos) {
    return N2_Index(pos[8], pos[7]) +
           (N2_Offset) * (N2_Index(pos[3], pos[2]) +
                          N2_Offset * (ZINDEX)N3_Index(pos[6], pos[5], pos[4]));
}

static bool Pos232(ZINDEX index, int *pos) {
    unsigned int id2 = index % N2_Offset;
    assert(id2 < N2);
    unsigned int p2 = p2_tab[id2];
    pos[8] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[7] = p2;
    index /= N2_Offset;
    id2 = index % N2_Offset;
    assert(id2 < N2);
    p2 = p2_tab[id2];
    pos[3] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[2] = p2;
    index /= N2_Offset;
    assert(index < N3);
    unsigned int p3 = p3_tab[index];
    pos[6] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[5] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[4] = p3;
    return true;
}

static ZINDEX Index223(int *pos) {
    return N2_Index(pos[5], pos[4]) +
           (N2_Offset) *
               (N2_Index(pos[3], pos[2]) +
                (N2_Offset) * (ZINDEX)N3_Index(pos[8], pos[7], pos[6]));
}

static ZINDEX IndexDP223(int *pos) {
    ZINDEX dp22 = IndexDP22(pos);
    if (dp22 == ALL_ONES)
        return ALL_ONES;
    return N3_Index(pos[8], pos[7], pos[6]) + (N3_Offset)*dp22;
}

static bool Pos223(ZINDEX index, int *pos) {
    unsigned int id2 = index % N2_Offset;
    assert(id2 < N2);
    unsigned int p2 = p2_tab[id2];
    pos[5] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[4] = p2;
    index /= N2_Offset;
    id2 = index % N2_Offset;
    assert(id2 < N2);
    p2 = p2_tab[id2];
    pos[3] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[2] = p2;
    index /= N2_Offset;
    assert(index < N3);
    unsigned int p3 = p3_tab[index];
    pos[8] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[7] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[6] = p3;
    return true;
}

static bool PosDP223(ZINDEX index, int *pos) {
    unsigned int id3 = index % N3_Offset;
    assert(id3 < N3);
    unsigned int p3 = p3_tab[id3];
    pos[8] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[7] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[6] = p3;
    index /= N3_Offset;

    assert(index < N4_OPPOSING);
    return PosDP22(index, pos);
}

static ZINDEX Index223_1100(int *pos) {
    return N2_Odd_Index(pos[3], pos[2]) +
           (N2_ODD_PARITY_Offset) *
               (N2_Index(pos[5], pos[4]) +
                (N2_Offset) * (ZINDEX)N3_Index(pos[8], pos[7], pos[6]));
}

static bool Pos223_1100(ZINDEX index, int *pos) {
    unsigned int id2 = index % N2_ODD_PARITY_Offset;
    assert(id2 < N2_ODD_PARITY);
    unsigned int p2 = p2_odd_tab[id2];
    pos[3] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[2] = p2;
    index /= N2_ODD_PARITY_Offset;
    id2 = index % N2_Offset;
    assert(id2 < N2);
    p2 = p2_tab[id2];
    pos[5] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[4] = p2;
    index /= N2_Offset;
    assert(index < N3);
    unsigned int p3 = p3_tab[index];
    pos[8] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[7] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[6] = p3;
    return true;
}

static ZINDEX Index223_1000(int *pos) {
    return N2_Even_Index(pos[3], pos[2]) +
           (N2_EVEN_PARITY_Offset) *
               (N2_Index(pos[5], pos[4]) +
                (N2_Offset) * (ZINDEX)N3_Index(pos[8], pos[7], pos[6]));
}

static bool Pos223_1000(ZINDEX index, int *pos) {
    unsigned int id2 = index % N2_EVEN_PARITY_Offset;
    assert(id2 < N2_EVEN_PARITY);
    unsigned int p2 = p2_even_tab[id2];
    pos[3] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[2] = p2;
    index /= N2_EVEN_PARITY_Offset;
    id2 = index % N2_Offset;
    assert(id2 < N2);
    p2 = p2_tab[id2];
    pos[5] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[4] = p2;
    index /= N2_Offset;
    assert(index < N3);
    unsigned int p3 = p3_tab[index];
    pos[8] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[7] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[6] = p3;
    return true;
}

static ZINDEX Index4111(int *pos) {
    return pos[8] +
           (NSQUARES) *
               (pos[7] +
                (NSQUARES) *
                    (pos[6] + (NSQUARES) * (ZINDEX)N4_Index(pos[5], pos[4],
                                                            pos[3], pos[2])));
}

static bool Pos4111(ZINDEX index, int *pos) {
    int p4;

    pos[8] = index % NSQUARES;
    index /= NSQUARES;
    pos[7] = index % NSQUARES;
    index /= NSQUARES;
    pos[6] = index % NSQUARES;
    index /= NSQUARES;
    assert(index < N4);
    p4 = p4_tab[index];
    pos[5] = p4 % NSQUARES;
    p4 /= NSQUARES;
    pos[4] = p4 % NSQUARES;
    p4 /= NSQUARES;
    pos[3] = p4 % NSQUARES;
    p4 /= NSQUARES;
    pos[2] = p4;
    return true;
}

static ZINDEX Index1411(int *pos) {
    return pos[8] +
           (NSQUARES) *
               (pos[7] +
                (NSQUARES) *
                    (pos[2] + (NSQUARES) * (ZINDEX)N4_Index(pos[6], pos[5],
                                                            pos[4], pos[3])));
}

static bool Pos1411(ZINDEX index, int *pos) {
    int p4;

    pos[8] = index % NSQUARES;
    index /= NSQUARES;
    pos[7] = index % NSQUARES;
    index /= NSQUARES;
    pos[2] = index % NSQUARES;
    index /= NSQUARES;
    assert(index < N4);
    p4 = p4_tab[index];
    pos[6] = p4 % NSQUARES;
    p4 /= NSQUARES;
    pos[5] = p4 % NSQUARES;
    p4 /= NSQUARES;
    pos[4] = p4 % NSQUARES;
    p4 /= NSQUARES;
    pos[3] = p4;
    return true;
}

static ZINDEX Index1141(int *pos) {
    return pos[8] +
           (NSQUARES) *
               (pos[3] +
                (NSQUARES) *
                    (pos[2] + (NSQUARES) * (ZINDEX)N4_Index(pos[7], pos[6],
                                                            pos[5], pos[4])));
}

static bool Pos1141(ZINDEX index, int *pos) {
    int p4;

    pos[8] = index % NSQUARES;
    index /= NSQUARES;
    pos[3] = index % NSQUARES;
    index /= NSQUARES;
    pos[2] = index % NSQUARES;
    index /= NSQUARES;
    assert(index < N4);
    p4 = p4_tab[index];
    pos[7] = p4 % NSQUARES;
    p4 /= NSQUARES;
    pos[6] = p4 % NSQUARES;
    p4 /= NSQUARES;
    pos[5] = p4 % NSQUARES;
    p4 /= NSQUARES;
    pos[4] = p4;
    return true;
}

static ZINDEX Index1114(int *pos) {
    return pos[4] +
           (NSQUARES) *
               (pos[3] +
                (NSQUARES) *
                    (pos[2] + (NSQUARES) * (ZINDEX)N4_Index(pos[8], pos[7],
                                                            pos[6], pos[5])));
}

static bool Pos1114(ZINDEX index, int *pos) {
    int p4;

    pos[4] = index % NSQUARES;
    index /= NSQUARES;
    pos[3] = index % NSQUARES;
    index /= NSQUARES;
    pos[2] = index % NSQUARES;
    index /= NSQUARES;
    assert(index < N4);
    p4 = p4_tab[index];
    pos[8] = p4 % NSQUARES;
    p4 /= NSQUARES;
    pos[7] = p4 % NSQUARES;
    p4 /= NSQUARES;
    pos[6] = p4 % NSQUARES;
    p4 /= NSQUARES;
    pos[5] = p4;
    return true;
}

static ZINDEX Index421(int *pos) {
    return pos[8] + NSQUARES * (ZINDEX)(N2_Index(pos[7], pos[6]) +
                                        N2_Offset * N4_Index(pos[5], pos[4],
                                                             pos[3], pos[2]));
}

static bool Pos421(ZINDEX index, int *pos) {
    int p2, p4, id2;

    pos[8] = index % NSQUARES;
    index /= NSQUARES;
    id2 = index % N2_Offset;
    assert(id2 < N2);
    p2 = p2_tab[id2];
    pos[7] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[6] = p2;
    index /= N2_Offset;
    assert(index < N4);
    p4 = p4_tab[index];
    pos[5] = p4 % NSQUARES;
    p4 /= NSQUARES;
    pos[4] = p4 % NSQUARES;
    p4 /= NSQUARES;
    pos[3] = p4 % NSQUARES;
    p4 /= NSQUARES;
    pos[2] = p4;
    return true;
}

static ZINDEX Index421_0010(int *pos) {
    return pos[8] +
           NSQUARES * (ZINDEX)(N2_Even_Index(pos[7], pos[6]) +
                               N2_EVEN_PARITY_Offset *
                                   N4_Index(pos[5], pos[4], pos[3], pos[2]));
}

static bool Pos421_0010(ZINDEX index, int *pos) {
    int p2, p4, id2;

    pos[8] = index % NSQUARES;
    index /= NSQUARES;
    id2 = index % N2_EVEN_PARITY_Offset;
    assert(id2 < N2_EVEN_PARITY);
    p2 = p2_even_tab[id2];
    pos[7] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[6] = p2;
    index /= N2_EVEN_PARITY_Offset;
    assert(index < N4);
    p4 = p4_tab[index];
    pos[5] = p4 % NSQUARES;
    p4 /= NSQUARES;
    pos[4] = p4 % NSQUARES;
    p4 /= NSQUARES;
    pos[3] = p4 % NSQUARES;
    p4 /= NSQUARES;
    pos[2] = p4;
    return true;
}

static ZINDEX Index421_0011(int *pos) {
    return pos[8] +
           NSQUARES * (ZINDEX)(N2_Odd_Index(pos[7], pos[6]) +
                               N2_ODD_PARITY_Offset *
                                   N4_Index(pos[5], pos[4], pos[3], pos[2]));
}

static bool Pos421_0011(ZINDEX index, int *pos) {
    int p2, p4, id2;

    pos[8] = index % NSQUARES;
    index /= NSQUARES;
    id2 = index % N2_ODD_PARITY_Offset;
    assert(id2 < N2_ODD_PARITY);
    p2 = p2_odd_tab[id2];
    pos[7] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[6] = p2;
    index /= N2_ODD_PARITY_Offset;
    assert(index < N4);
    p4 = p4_tab[index];
    pos[5] = p4 % NSQUARES;
    p4 /= NSQUARES;
    pos[4] = p4 % NSQUARES;
    p4 /= NSQUARES;
    pos[3] = p4 % NSQUARES;
    p4 /= NSQUARES;
    pos[2] = p4;
    return true;
}

static ZINDEX Index412(int *pos) {
    return pos[6] + NSQUARES * (ZINDEX)(N2_Index(pos[8], pos[7]) +
                                        N2_Offset * N4_Index(pos[5], pos[4],
                                                             pos[3], pos[2]));
}

static bool Pos412(ZINDEX index, int *pos) {
    int p2, p4, id2;

    pos[6] = index % NSQUARES;
    index /= NSQUARES;
    id2 = index % N2_Offset;
    assert(id2 < N2);
    p2 = p2_tab[id2];
    pos[8] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[7] = p2;
    index /= N2_Offset;
    assert(index < N4);
    p4 = p4_tab[index];
    pos[5] = p4 % NSQUARES;
    p4 /= NSQUARES;
    pos[4] = p4 % NSQUARES;
    p4 /= NSQUARES;
    pos[3] = p4 % NSQUARES;
    p4 /= NSQUARES;
    pos[2] = p4;
    return true;
}

static ZINDEX Index241(int *pos) {
    return pos[8] + NSQUARES * (ZINDEX)(N2_Index(pos[3], pos[2]) +
                                        N2_Offset * N4_Index(pos[7], pos[6],
                                                             pos[5], pos[4]));
}

static bool Pos241(ZINDEX index, int *pos) {
    int p2, p4, id2;

    pos[8] = index % NSQUARES;
    index /= NSQUARES;
    id2 = index % N2_Offset;
    assert(id2 < N2);
    p2 = p2_tab[id2];
    pos[3] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[2] = p2;
    index /= N2_Offset;
    assert(index < N4);
    p4 = p4_tab[index];
    pos[7] = p4 % NSQUARES;
    p4 /= NSQUARES;
    pos[6] = p4 % NSQUARES;
    p4 /= NSQUARES;
    pos[5] = p4 % NSQUARES;
    p4 /= NSQUARES;
    pos[4] = p4;
    return true;
}

static ZINDEX Index214(int *pos) {
    return pos[4] + NSQUARES * (ZINDEX)(N2_Index(pos[3], pos[2]) +
                                        N2_Offset * N4_Index(pos[8], pos[7],
                                                             pos[6], pos[5]));
}

static bool Pos214(ZINDEX index, int *pos) {
    int p2, p4, id2;

    pos[4] = index % NSQUARES;
    index /= NSQUARES;
    id2 = index % N2_Offset;
    assert(id2 < N2);
    p2 = p2_tab[id2];
    pos[3] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[2] = p2;
    index /= N2_Offset;
    assert(index < N4);
    p4 = p4_tab[index];
    pos[8] = p4 % NSQUARES;
    p4 /= NSQUARES;
    pos[7] = p4 % NSQUARES;
    p4 /= NSQUARES;
    pos[6] = p4 % NSQUARES;
    p4 /= NSQUARES;
    pos[5] = p4;
    return true;
}

static ZINDEX Index142(int *pos) {
    return pos[2] + NSQUARES * (ZINDEX)(N2_Index(pos[8], pos[7]) +
                                        N2_Offset * N4_Index(pos[6], pos[5],
                                                             pos[4], pos[3]));
}

static bool Pos142(ZINDEX index, int *pos) {
    int p2, p4, id2;

    pos[2] = index % NSQUARES;
    index /= NSQUARES;
    id2 = index % N2_Offset;
    assert(id2 < N2);
    p2 = p2_tab[id2];
    pos[8] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[7] = p2;
    index /= N2_Offset;
    assert(index < N4);
    p4 = p4_tab[index];
    pos[6] = p4 % NSQUARES;
    p4 /= NSQUARES;
    pos[5] = p4 % NSQUARES;
    p4 /= NSQUARES;
    pos[4] = p4 % NSQUARES;
    p4 /= NSQUARES;
    pos[3] = p4;
    return true;
}

static ZINDEX Index124(int *pos) {
    return pos[2] + NSQUARES * (ZINDEX)(N2_Index(pos[4], pos[3]) +
                                        N2_Offset * N4_Index(pos[8], pos[7],
                                                             pos[6], pos[5]));
}

static bool Pos124(ZINDEX index, int *pos) {
    int p2, p4, id2;

    pos[2] = index % NSQUARES;
    index /= NSQUARES;
    id2 = index % N2_Offset;
    assert(id2 < N2);
    p2 = p2_tab[id2];
    pos[4] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[3] = p2;
    index /= N2_Offset;
    assert(index < N4);
    p4 = p4_tab[index];
    pos[8] = p4 % NSQUARES;
    p4 /= NSQUARES;
    pos[7] = p4 % NSQUARES;
    p4 /= NSQUARES;
    pos[6] = p4 % NSQUARES;
    p4 /= NSQUARES;
    pos[5] = p4;
    return true;
}

static ZINDEX Index43(int *pos) {
    return N3_Index(pos[8], pos[7], pos[6]) +
           N3_Offset * (ZINDEX)N4_Index(pos[5], pos[4], pos[3], pos[2]);
}

static bool Pos43(ZINDEX index, int *pos) {
    int p4, id3, p3;

    id3 = index % N3_Offset;
    assert(id3 < N3);
    index /= N3_Offset;
    assert(index < N4);
    p3 = p3_tab[id3];
    pos[8] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[7] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[6] = p3;
    p4 = p4_tab[index];
    pos[5] = p4 % NSQUARES;
    p4 /= NSQUARES;
    pos[4] = p4 % NSQUARES;
    p4 /= NSQUARES;
    pos[3] = p4 % NSQUARES;
    p4 /= NSQUARES;
    pos[2] = p4;
    return true;
}

static ZINDEX Index34(int *pos) {
    return N3_Index(pos[4], pos[3], pos[2]) +
           N3_Offset * (ZINDEX)N4_Index(pos[8], pos[7], pos[6], pos[5]);
}

static bool Pos34(ZINDEX index, int *pos) {
    int p4, id3, p3;

    id3 = index % N3_Offset;
    assert(id3 < N3);
    index /= N3_Offset;
    assert(index < N4);
    p3 = p3_tab[id3];
    pos[4] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[3] = p3 % NSQUARES;
    p3 /= NSQUARES;
    pos[2] = p3;
    p4 = p4_tab[index];
    pos[8] = p4 % NSQUARES;
    p4 /= NSQUARES;
    pos[7] = p4 % NSQUARES;
    p4 /= NSQUARES;
    pos[6] = p4 % NSQUARES;
    p4 /= NSQUARES;
    pos[5] = p4;
    return true;
}

static ZINDEX Index511(int *pos) {
    return pos[8] + NSQUARES * (pos[7] + NSQUARES * (ZINDEX)Index5(pos));
}

static bool Pos511(ZINDEX index, int *pos) {
    pos[8] = index % NSQUARES;
    index /= NSQUARES;
    pos[7] = index % NSQUARES;
    index /= NSQUARES;
    return Pos5(index, pos);
}

static ZINDEX Index151(int *pos) {
    return pos[8] + NSQUARES * (pos[2] + NSQUARES * (ZINDEX)Index5(pos + 1));
}

static bool Pos151(ZINDEX index, int *pos) {
    pos[8] = index % NSQUARES;
    index /= NSQUARES;
    pos[2] = index % NSQUARES;
    index /= NSQUARES;
    return Pos5(index, pos + 1);
}

static ZINDEX Index115(int *pos) {
    return pos[3] + NSQUARES * (pos[2] + NSQUARES * (ZINDEX)Index5(pos + 2));
}

static bool Pos115(ZINDEX index, int *pos) {
    pos[3] = index % NSQUARES;
    index /= NSQUARES;
    pos[2] = index % NSQUARES;
    index /= NSQUARES;
    return Pos5(index, pos + 2);
}

static ZINDEX Index52(int *pos) {
    return N2_Index(pos[8], pos[7]) + N2_Offset * ((ZINDEX)Index5(pos));
}

static bool Pos52(ZINDEX index, int *pos) {
    unsigned int p2, id2;

    id2 = index % N2_Offset;
    assert(id2 < N2);
    p2 = p2_tab[id2];
    pos[8] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[7] = p2;
    index /= N2_Offset;
    return Pos5(index, pos);
}

static ZINDEX Index25(int *pos) {
    return N2_Index(pos[3], pos[2]) + N2_Offset * ((ZINDEX)Index5(pos + 2));
}

static bool Pos25(ZINDEX index, int *pos) {
    unsigned int p2, id2;

    id2 = index % N2_Offset;
    assert(id2 < N2);
    p2 = p2_tab[id2];
    pos[3] = p2 % NSQUARES;
    p2 /= NSQUARES;
    pos[2] = p2;
    index /= N2_Offset;
    return Pos5(index, pos + 2);
}

static ZINDEX Index61(int *pos) {
    return pos[8] + NSQUARES * ((ZINDEX)Index6(pos));
}

static bool Pos61(ZINDEX index, int *pos) {
    pos[8] = index % NSQUARES;
    index /= NSQUARES;
    return Pos6(index, pos);
}

static ZINDEX Index16(int *pos) {
    return pos[2] + NSQUARES * ((ZINDEX)Index6(pos + 1));
}

static bool Pos16(ZINDEX index, int *pos) {
    pos[2] = index % NSQUARES;
    index /= NSQUARES;
    return Pos6(index, pos + 1);
}

#endif /* 8-man indices, using 64 bit zones */

static IndexType IndexTable[] = {
#if defined(USE_64_BIT)
    {111111, FREE_PAWNS, 0, Pos111111, Index111111},
    {111111, BP_11_PAWNS, 0, PosBP111111, IndexBP111111},
    {111111, OP_11_PAWNS, 0, PosOP111111, IndexOP111111},
    {21111, FREE_PAWNS, 0, Pos21111, Index21111},
    {21111, OP_21_PAWNS, 0, PosOP21111, IndexOP21111},
    {12111, FREE_PAWNS, 0, Pos12111, Index12111},
    {12111, OP_12_PAWNS, 0, PosOP12111, IndexOP12111},
    {11211, FREE_PAWNS, 0, Pos11211, Index11211},
    {11211, BP_11_PAWNS, 0, PosBP11211, IndexBP11211},
    {11211, OP_11_PAWNS, 0, PosOP11211, IndexOP11211},
    {11121, FREE_PAWNS, 0, Pos11121, Index11121},
    {11121, BP_11_PAWNS, 0, PosBP11121, IndexBP11121},
    {11121, OP_11_PAWNS, 0, PosOP11121, IndexOP11121},
    {11112, FREE_PAWNS, 0, Pos11112, Index11112},
    {11112, BP_11_PAWNS, 0, PosBP11112, IndexBP11112},
    {11112, OP_11_PAWNS, 0, PosOP11112, IndexOP11112},
    {2211, FREE_PAWNS, 0, Pos2211, Index2211},
    {2211, DP_22_PAWNS, 0, PosDP2211, IndexDP2211},
    {2211, OP_22_PAWNS, 0, PosOP2211, IndexOP2211},
    {2211, FREE_PAWNS, 1100, Pos2211_1100, Index2211_1100},
    {2211, FREE_PAWNS, 1000, Pos2211_1000, Index2211_1000},
    {2121, FREE_PAWNS, 0, Pos2121, Index2121},
    {2121, OP_21_PAWNS, 0, PosOP2121, IndexOP2121},
    {1221, FREE_PAWNS, 0, Pos1221, Index1221},
    {1221, OP_12_PAWNS, 0, PosOP1221, IndexOP1221},
    {2112, FREE_PAWNS, 0, Pos2112, Index2112},
    {2112, OP_21_PAWNS, 0, PosOP2112, IndexOP2112},
    {1212, FREE_PAWNS, 0, Pos1212, Index1212},
    {1212, OP_12_PAWNS, 0, PosOP1212, IndexOP1212},
    {1122, FREE_PAWNS, 0, Pos1122, Index1122},
    {1122, BP_11_PAWNS, 0, PosBP1122, IndexBP1122},
    {1122, OP_11_PAWNS, 0, PosOP1122, IndexOP1122},
    {222, FREE_PAWNS, 0, Pos222, Index222},
    {222, DP_22_PAWNS, 0, PosDP222, IndexDP222},
    {222, OP_22_PAWNS, 0, PosOP222, IndexOP222},
    {3111, FREE_PAWNS, 0, Pos3111, Index3111},
    {3111, OP_31_PAWNS, 0, PosOP3111, IndexOP3111},
    {1311, FREE_PAWNS, 0, Pos1311, Index1311},
    {1311, OP_13_PAWNS, 0, PosOP1311, IndexOP1311},
    {1131, FREE_PAWNS, 0, Pos1131, Index1131},
    {1131, BP_11_PAWNS, 0, PosBP1131, IndexBP1131},
    {1131, OP_11_PAWNS, 0, PosOP1131, IndexOP1131},
    {1113, FREE_PAWNS, 0, Pos1113, Index1113},
    {1113, BP_11_PAWNS, 0, PosBP1113, IndexBP1113},
    {1113, OP_11_PAWNS, 0, PosOP1113, IndexOP1113},
    {123, FREE_PAWNS, 0, Pos123, Index123},
    {123, OP_12_PAWNS, 0, PosOP123, IndexOP123},
    {213, FREE_PAWNS, 0, Pos213, Index213},
    {213, OP_21_PAWNS, 0, PosOP213, IndexOP213},
    {132, FREE_PAWNS, 0, Pos132, Index132},
    {132, OP_13_PAWNS, 0, PosOP132, IndexOP132},
    {231, FREE_PAWNS, 0, Pos231, Index231},
    {312, FREE_PAWNS, 0, Pos312, Index312},
    {312, OP_31_PAWNS, 0, PosOP312, IndexOP312},
    {321, FREE_PAWNS, 0, Pos321, Index321},
    {33, FREE_PAWNS, 0, Pos33, Index33},
    {411, FREE_PAWNS, 0, Pos411, Index411},
    {141, FREE_PAWNS, 0, Pos141, Index141},
    {114, FREE_PAWNS, 0, Pos114, Index114},
    {114, BP_11_PAWNS, 0, PosBP114, IndexBP114},
    {114, OP_11_PAWNS, 0, PosOP114, IndexOP114},
    {42, FREE_PAWNS, 0, Pos42, Index42},
    {24, FREE_PAWNS, 0, Pos24, Index24},
    {1111111, FREE_PAWNS, 0, Pos1111111, Index1111111},
    {211111, FREE_PAWNS, 0, Pos211111, Index211111},
    {121111, FREE_PAWNS, 0, Pos121111, Index121111},
    {112111, FREE_PAWNS, 0, Pos112111, Index112111},
    {111211, FREE_PAWNS, 0, Pos111211, Index111211},
    {111121, FREE_PAWNS, 0, Pos111121, Index111121},
    {111112, FREE_PAWNS, 0, Pos111112, Index111112},
    {22111, FREE_PAWNS, 0, Pos22111, Index22111},
    {22111, DP_22_PAWNS, 0, PosDP22111, IndexDP22111},
    {21211, FREE_PAWNS, 0, Pos21211, Index21211},
    {21121, FREE_PAWNS, 0, Pos21121, Index21121},
    {21112, FREE_PAWNS, 0, Pos21112, Index21112},
    {12211, FREE_PAWNS, 0, Pos12211, Index12211},
    {12121, FREE_PAWNS, 0, Pos12121, Index12121},
    {12112, FREE_PAWNS, 0, Pos12112, Index12112},
    {11221, FREE_PAWNS, 0, Pos11221, Index11221},
    {11212, FREE_PAWNS, 0, Pos11212, Index11212},
    {11122, FREE_PAWNS, 0, Pos11122, Index11122},
    {2221, FREE_PAWNS, 0, Pos2221, Index2221},
    {2221, DP_22_PAWNS, 0, PosDP2221, IndexDP2221},
    {2221, FREE_PAWNS, 1131, Pos2221_1131, Index2221_1131},
    {2221, FREE_PAWNS, 1130, Pos2221_1130, Index2221_1130},
    {2221, FREE_PAWNS, 1030, Pos2221_1030, Index2221_1030},
    {2212, FREE_PAWNS, 0, Pos2212, Index2212},
    {2212, DP_22_PAWNS, 0, PosDP2212, IndexDP2212},
    {2122, FREE_PAWNS, 0, Pos2122, Index2122},
    {1222, FREE_PAWNS, 0, Pos1222, Index1222},
    {31111, FREE_PAWNS, 0, Pos31111, Index31111},
    {13111, FREE_PAWNS, 0, Pos13111, Index13111},
    {11311, FREE_PAWNS, 0, Pos11311, Index11311},
    {11131, FREE_PAWNS, 0, Pos11131, Index11131},
    {11113, FREE_PAWNS, 0, Pos11113, Index11113},
    {3211, FREE_PAWNS, 0, Pos3211, Index3211},
    {3121, FREE_PAWNS, 0, Pos3121, Index3121},
    {3121, FREE_PAWNS, 1100, Pos3121_1100, Index3121_1100},
    {3121, FREE_PAWNS, 1111, Pos3121_1111, Index3121_1111},
    {3121, FREE_PAWNS, 1110, Pos3121_1110, Index3121_1110},
    {3112, FREE_PAWNS, 0, Pos3112, Index3112},
    {2311, FREE_PAWNS, 0, Pos2311, Index2311},
    {2131, FREE_PAWNS, 0, Pos2131, Index2131},
    {2113, FREE_PAWNS, 0, Pos2113, Index2113},
    {1321, FREE_PAWNS, 0, Pos1321, Index1321},
    {1312, FREE_PAWNS, 0, Pos1312, Index1312},
    {1312, FREE_PAWNS, 10, Pos1312_0010, Index1312_0010},
    {1312, FREE_PAWNS, 11, Pos1312_0011, Index1312_0011},
    {1231, FREE_PAWNS, 0, Pos1231, Index1231},
    {1213, FREE_PAWNS, 0, Pos1213, Index1213},
    {1132, FREE_PAWNS, 0, Pos1132, Index1132},
    {1123, FREE_PAWNS, 0, Pos1123, Index1123},
    {322, FREE_PAWNS, 0, Pos322, Index322},
    {322, FREE_PAWNS, 10, Pos322_0010, Index322_0010},
    {322, FREE_PAWNS, 11, Pos322_0011, Index322_0011},
    {232, FREE_PAWNS, 0, Pos232, Index232},
    {223, FREE_PAWNS, 0, Pos223, Index223},
    {223, DP_22_PAWNS, 0, PosDP223, IndexDP223},
    {223, FREE_PAWNS, 1100, Pos223_1100, Index223_1100},
    {223, FREE_PAWNS, 1000, Pos223_1000, Index223_1000},
    {331, FREE_PAWNS, 0, Pos331, Index331},
    {331, FREE_PAWNS, 20, Pos331_0020, Index331_0020},
    {331, FREE_PAWNS, 21, Pos331_0021, Index331_0021},
    {313, FREE_PAWNS, 0, Pos313, Index313},
    {133, FREE_PAWNS, 0, Pos133, Index133},
    {4111, FREE_PAWNS, 0, Pos4111, Index4111},
    {1411, FREE_PAWNS, 0, Pos1411, Index1411},
    {1141, FREE_PAWNS, 0, Pos1141, Index1141},
    {1114, FREE_PAWNS, 0, Pos1114, Index1114},
    {421, FREE_PAWNS, 0, Pos421, Index421},
    {421, FREE_PAWNS, 10, Pos421_0010, Index421_0010},
    {421, FREE_PAWNS, 11, Pos421_0010, Index421_0011},
    {412, FREE_PAWNS, 0, Pos412, Index412},
    {241, FREE_PAWNS, 0, Pos241, Index241},
    {214, FREE_PAWNS, 0, Pos214, Index214},
    {142, FREE_PAWNS, 0, Pos142, Index142},
    {124, FREE_PAWNS, 0, Pos124, Index124},
    {43, FREE_PAWNS, 0, Pos43, Index43},
    {34, FREE_PAWNS, 0, Pos34, Index34},
    {511, FREE_PAWNS, 0, Pos511, Index511},
    {151, FREE_PAWNS, 0, Pos151, Index151},
    {115, FREE_PAWNS, 0, Pos115, Index115},
    {52, FREE_PAWNS, 0, Pos52, Index52},
    {25, FREE_PAWNS, 0, Pos25, Index25},
    {61, FREE_PAWNS, 0, Pos61, Index61},
    {16, FREE_PAWNS, 0, Pos16, Index16},
#endif
    {1, FREE_PAWNS, 0, Pos1, Index1},
    {11, FREE_PAWNS, 0, Pos11, Index11},
    {11, BP_11_PAWNS, 0, PosBP11, IndexBP11},
    {11, OP_11_PAWNS, 0, PosOP11, IndexOP11},
    {111, FREE_PAWNS, 0, Pos111, Index111},
    {111, BP_11_PAWNS, 0, PosBP111, IndexBP111},
    {111, OP_11_PAWNS, 0, PosOP111, IndexOP111},
    {1111, FREE_PAWNS, 0, Pos1111, Index1111},
    {1111, BP_11_PAWNS, 0, PosBP1111, IndexBP1111},
    {1111, OP_11_PAWNS, 0, PosOP1111, IndexOP1111},
    {11111, FREE_PAWNS, 0, Pos11111, Index11111},
    {11111, BP_11_PAWNS, 0, PosBP11111, IndexBP11111},
    {11111, OP_11_PAWNS, 0, PosOP11111, IndexOP11111},
    {2, FREE_PAWNS, 0, Pos2, Index2},
    {2, FREE_PAWNS, 1100, Pos2_1100, Index2_1100},
    {21, FREE_PAWNS, 0, Pos21, Index21},
    {21, OP_21_PAWNS, 0, PosOP21, IndexOP21},
    {12, FREE_PAWNS, 0, Pos12, Index12},
    {12, OP_12_PAWNS, 0, PosOP12, IndexOP12},
    {211, FREE_PAWNS, 0, Pos211, Index211},
    {211, OP_21_PAWNS, 0, PosOP211, IndexOP211},
    {121, FREE_PAWNS, 0, Pos121, Index121},
    {121, OP_12_PAWNS, 0, PosOP121, IndexOP121},
    {112, FREE_PAWNS, 0, Pos112, Index112},
    {112, BP_11_PAWNS, 0, PosBP112, IndexBP112},
    {112, OP_11_PAWNS, 0, PosOP112, IndexOP112},
    {2111, FREE_PAWNS, 0, Pos2111, Index2111},
    {2111, OP_21_PAWNS, 0, PosOP2111, IndexOP2111},
    {1211, FREE_PAWNS, 0, Pos1211, Index1211},
    {1211, OP_12_PAWNS, 0, PosOP1211, IndexOP1211},
    {1121, FREE_PAWNS, 0, Pos1121, Index1121},
    {1121, BP_11_PAWNS, 0, PosBP1121, IndexBP1121},
    {1121, OP_11_PAWNS, 0, PosOP1121, IndexOP1121},
    {1112, FREE_PAWNS, 0, Pos1112, Index1112},
    {1112, BP_11_PAWNS, 0, PosBP1112, IndexBP1112},
    {1112, OP_11_PAWNS, 0, PosOP1112, IndexOP1112},
    {22, FREE_PAWNS, 0, Pos22, Index22},
    {22, DP_22_PAWNS, 0, PosDP22, IndexDP22},
    {22, OP_22_PAWNS, 0, PosOP22, IndexOP22},
    {221, FREE_PAWNS, 0, Pos221, Index221},
    {221, DP_22_PAWNS, 0, PosDP221, IndexDP221},
    {221, OP_22_PAWNS, 0, PosOP221, IndexOP221},
    {212, FREE_PAWNS, 0, Pos212, Index212},
    {212, OP_21_PAWNS, 0, PosOP212, IndexOP212},
    {122, FREE_PAWNS, 0, Pos122, Index122},
    {122, OP_12_PAWNS, 0, PosOP122, IndexOP122},
    {3, FREE_PAWNS, 0, Pos3, Index3},
    {3, FREE_PAWNS, 1100, Pos3_1100, Index3_1100},
    {31, FREE_PAWNS, 0, Pos31, Index31},
    {31, OP_31_PAWNS, 0, PosOP31, IndexOP31},
    {13, FREE_PAWNS, 0, Pos13, Index13},
    {13, OP_13_PAWNS, 0, PosOP13, IndexOP13},
    {311, FREE_PAWNS, 0, Pos311, Index311},
    {311, OP_31_PAWNS, 0, PosOP311, IndexOP311},
    {131, FREE_PAWNS, 0, Pos131, Index131},
    {131, OP_13_PAWNS, 0, PosOP131, IndexOP131},
    {113, FREE_PAWNS, 0, Pos113, Index113},
    {113, BP_11_PAWNS, 0, PosBP113, IndexBP113},
    {113, OP_11_PAWNS, 0, PosOP113, IndexOP113},
    {32, FREE_PAWNS, 0, Pos32, Index32},
    {23, FREE_PAWNS, 0, Pos23, Index23},
    {4, FREE_PAWNS, 0, Pos4, Index4},
    {41, FREE_PAWNS, 0, Pos41, Index41},
    {14, FREE_PAWNS, 0, Pos14, Index14},
    {5, FREE_PAWNS, 0, Pos5, Index5},
    {51, FREE_PAWNS, 0, Pos51, Index51},
    {15, FREE_PAWNS, 0, Pos15, Index15},
    {6, FREE_PAWNS, 0, Pos6, Index6},
    {7, FREE_PAWNS, 0, Pos7, Index7}
};

#define NumIndexTypes (sizeof(IndexTable) / sizeof(IndexTable[0]))

typedef struct {
    ZINDEX index;
    IndexType *eptr;
    int bishop_parity[2];
} PARITY_INDEX;

typedef struct {
    PARITY_INDEX parity_index[4];
    int num_parities;
    int mb_position[MAX_PIECES_MB], mb_piece_types[MAX_PIECES_MB];
    int piece_type_count[2][KING];
    int parity;
    int pawn_file_type;
    IndexType *eptr_bp_11, *eptr_op_11, *eptr_op_21, *eptr_op_12, *eptr_dp_22,
        *eptr_op_22, *eptr_op_31, *eptr_op_13, *eptr_op_41, *eptr_op_14,
        *eptr_op_32, *eptr_op_23, *eptr_op_33, *eptr_op_42, *eptr_op_24;
    ZINDEX index_bp_11, index_op_11, index_op_21, index_op_12, index_dp_22,
        index_op_22, index_op_31, index_op_13, index_op_41, index_op_14,
        index_op_32, index_op_23, index_op_33, index_op_42, index_op_24;
    int num_pieces;
    int kk_index;
} MB_INFO;

typedef struct {
    int yk_position[MAX_PIECES_YK], yk_piece_types[MAX_PIECES_YK];
    int piece_type_count[2][KING];
    IndexType *eptr;
    ZINDEX index;
    int num_pieces;
    int kk_index;
} YK_INFO;

typedef struct {
    int kk_index;
    ZINDEX index;
    BYTE metric;
} INDEX_DATA;

typedef struct {
    unsigned int dtc;
    unsigned int kindex;
    unsigned int offset;
} HDATA;

static int CompareHigh(const void *a, const void *b) {
    const HDATA *x = (const HDATA *)a;
    const HDATA *y = (const HDATA *)b;

    if (x->kindex < y->kindex)
        return -1;
    if (x->kindex > y->kindex)
        return 1;
    if (x->offset < y->offset)
        return -1;
    if (x->offset > y->offset)
        return 1;

    return 0;
}

static unsigned int Promotions =
    (1 << KNIGHT) | (1 << BISHOP) | (1 << ROOK) | (1 << QUEEN);
static bool SearchAllPromotions = true;
static bool SearchSubgamePromotions = true;
static int PromoteRow[] = {(NROWS)-1, 0};
static int StartRow[] = {1, (NROWS)-2};
static int PieceStrengths[KING];
static char *TbDirs = "";

static char piece_char(int type) {
    switch (type) {
    case PAWN:
        return 'p';
    case KNIGHT:
        return 'n';
    case BISHOP:
        return 'b';
    case ARCHBISHOP:
        return 'a';
    case ROOK:
        return 'r';
    case CARDINAL:
        return 'c';
    case QUEEN:
        return 'q';
    case MAHARAJA:
        return 'm';
    case KING:
        return 'k';
    default:
        return ' ';
    }
    return ' ';
}

static char PIECE_CHAR(int type) {
    switch (type) {
    case PAWN:
        return 'P';
    case KNIGHT:
        return 'N';
    case BISHOP:
        return 'B';
    case ARCHBISHOP:
        return 'A';
    case ROOK:
        return 'R';
    case CARDINAL:
        return 'C';
    case QUEEN:
        return 'Q';
    case MAHARAJA:
        return 'M';
    case KING:
        return 'K';
    default:
        return ' ';
    }
    return ' ';
}

static char eg_piece_char(int type) {
    switch (type) {
    case PAWN:
        return 'p';
    case KNIGHT:
        return 's';
    case BISHOP:
        return 'b';
    case ARCHBISHOP:
        return 'a';
    case ROOK:
        return 'r';
    case CARDINAL:
        return 'c';
    case QUEEN:
        return 'q';
    case MAHARAJA:
        return 'm';
    case KING:
        return 'k';
    default:
        return ' ';
    }
    return ' ';
}

static int GetEndingName(int type_count[2][KING], char *ending) {
    int len = 0, piece;

    ending[len++] = 'k';
    for (piece = KING - 1; piece >= PAWN; piece--) {
        for (int k = len; k < len + type_count[WHITE][piece]; k++)
            ending[k] = piece_char(piece);
        len += type_count[WHITE][piece];
    }
    ending[len++] = 'k';
    for (piece = KING - 1; piece >= PAWN; piece--) {
        for (int k = len; k < len + type_count[BLACK][piece]; k++)
            ending[k] = piece_char(piece);
        len += type_count[BLACK][piece];
    }
    ending[len] = '\0';
    return len;
}

static int GetEnPassant(int *pos, int *types, int npieces) {
    if (UseEnPassant) {
        int i;
        for (i = 2; i < npieces; i++) {
            if (types[i] == PAWN) {
                if (Row(pos[i]) == 0) {
                    pos[i] = SquareMake(3, Column(pos[i]));
                    return SquareMake(2, Column(pos[i]));
                }
            } else if (types[i] == -PAWN) {
                if (Row(pos[i]) == (NROWS - 1)) {
                    pos[i] = SquareMake(NROWS - 4, Column(pos[i]));
                    return SquareMake(NROWS - 3, Column(pos[i]));
                }
            }
        }
    }
    return 0;
}

static int ParseScore(char *score) {
    int value;

    if (strchr(score, '?'))
        return UNKNOWN;
    if (strchr(score, '=')) {
        if (strchr(score, '+'))
            return NOT_LOST;
        if (strchr(score, '-'))
            return NOT_WON;
        return DRAW;
    }
    if (sscanf(score, "%d", &value) == 1)
        return value;
    return UNKNOWN;
}

static int ParseZZType(char *score) {
    if (!strcmp(score, "-+"))
        return MINUS_PLUS;
    else if (!strcmp(score, "-="))
        return MINUS_EQUAL;
    else if (!strcmp(score, "=+"))
        return EQUAL_PLUS;
    else if (!strcmp(score, "--"))
        return NO_MZUG;

    return UNKNOWN;
}

static char ParseZZTypeChar(char *score) {
    if (!strcmp(score, "-+"))
        return 'f';
    else if (!strcmp(score, "-="))
        return 'm';
    else if (!strcmp(score, "=+"))
        return 'p';
    else if (!strcmp(score, "--"))
        return '-';

    return '?';
}

static void ScoreToString(int score, char *string) {
    if (score == UNKNOWN)
        strcpy(string, "?");
    else if (score == DRAW)
        strcpy(string, "=");
    else if (score == NOT_LOST)
        strcpy(string, "=+");
    else if (score == NOT_WON)
        strcpy(string, "=-");
    else
        sprintf(string, "%+d", score);
}

static void ZZTypeToString(int score, char *string) {
    if (score == UNKNOWN)
        strcpy(string, "?");
    else if (score == NO_MZUG)
        strcpy(string, "--");
    else if (score == MINUS_PLUS)
        strcpy(string, "-+");
    else if (score == MINUS_EQUAL)
        strcpy(string, "-=");
    else if (score == EQUAL_PLUS)
        strcpy(string, "=+");
}

void MoveScoreToString(int move_score, char *score_string) {
    if (move_score == PLUS_EQUAL) {
        strcpy(score_string, "+=");
    } else if (move_score == EQUAL_MINUS) {
        strcpy(score_string, "=-");
    } else if (move_score == PLUS_MINUS) {
        strcpy(score_string, "+-");
    } else if (move_score == PLUSEQUAL_MINUS) {
        strcpy(score_string, "+=/-");
    } else if (move_score == PLUS_EQUALMINUS) {
        strcpy(score_string, "+ /=-");
    } else if (move_score == MINUS_EQUAL) {
        strcpy(score_string, "-=");
    } else if (move_score == MINUS_PLUS) {
        strcpy(score_string, "-+");
    } else if (move_score == EQUAL_PLUS) {
        strcpy(score_string, "=+");
    } else if (move_score == EQUALMINUS_PLUS) {
        strcpy(score_string, "=- /+");
    } else {
        sprintf(score_string, "%+d", move_score);
    }
}

static unsigned int *EndingStats = NULL;
static int num_total_endings = 0;

typedef struct {
    int types[KING];
    int index;
} ENDING_INDEX;

static ENDING_INDEX *SingleSideCount = NULL;
static int num_side_endings = 0;

static int single_piece_side[KING];

static void fill_single_side(int loc, int nleft, bool count_only) {
    int i;

    if (loc == 4) {
        single_piece_side[QUEEN] = nleft;
        if (!count_only) {
            memcpy(SingleSideCount[num_side_endings].types, single_piece_side,
                   sizeof(single_piece_side));
            SingleSideCount[num_side_endings].index = num_side_endings;
        }
        num_side_endings++;
        return;
    }
    for (i = 0; i <= nleft; i++) {
        if (loc == 3)
            single_piece_side[ROOK] = i;
        else if (loc == 2)
            single_piece_side[BISHOP] = i;
        else if (loc == 1)
            single_piece_side[KNIGHT] = i;
        else
            single_piece_side[PAWN] = i;
        fill_single_side(loc + 1, nleft - i, count_only);
    }
}

static int side_compare(const void *a, const void *b) {
    const ENDING_INDEX *x = (ENDING_INDEX *)a;
    const ENDING_INDEX *y = (ENDING_INDEX *)b;

    return memcmp(x->types, y->types, sizeof(x->types));
}

static void InitEndingStats() {
    int np;

    if (num_side_endings != 0) {
        MyFree(SingleSideCount, num_side_endings * sizeof(ENDING_INDEX));
        num_side_endings = 0;
    }

    if (num_total_endings != 0) {
        MyFree(EndingStats, num_total_endings * sizeof(unsigned int));
        num_total_endings = 0;
    }

    for (np = 0; np <= MAX_PIECES_PER_SIDE; np++) {
        memset(single_piece_side, 0, sizeof(single_piece_side));
        fill_single_side(0, np, true);
    }

    if (Verbose > 1) {
        MyPrintf("Total number of configurations with up to %d per side: %d\n",
                 MAX_PIECES_PER_SIDE, num_side_endings);
        MyFlush();
    }

    SingleSideCount =
        (ENDING_INDEX *)MyMalloc(num_side_endings * sizeof(ENDING_INDEX));

    num_side_endings = 0;

    for (np = 0; np <= MAX_PIECES_PER_SIDE; np++) {
        memset(single_piece_side, 0, sizeof(single_piece_side));
        fill_single_side(0, np, false);
    }

    qsort(SingleSideCount, num_side_endings, sizeof(ENDING_INDEX),
          side_compare);

    num_total_endings = num_side_endings * num_side_endings;

    EndingStats =
        (unsigned int *)MyMalloc(num_total_endings * sizeof(unsigned int));

    memset(EndingStats, 0, num_total_endings * sizeof(unsigned int));
}

static int ending_stat_index(int piece_types[2][KING]) {
    ENDING_INDEX single_side, *eptr;
    int id_w, id_b;

    memcpy(single_side.types, piece_types[0], KING * sizeof(int));

    eptr =
        (ENDING_INDEX *)bsearch(&single_side, SingleSideCount, num_side_endings,
                                sizeof(ENDING_INDEX), side_compare);

    if (eptr == NULL) {
        fprintf(stderr, "Internal error for white in ending_stat_index\n");
        exit(1);
    }

    id_w = eptr->index;

    memcpy(single_side.types, piece_types[1], KING * sizeof(int));

    eptr =
        (ENDING_INDEX *)bsearch(&single_side, SingleSideCount, num_side_endings,
                                sizeof(ENDING_INDEX), side_compare);

    if (eptr == NULL) {
        fprintf(stderr, "Internal error for black in ending_stat_index\n");
        exit(1);
    }

    id_b = eptr->index;

    return num_side_endings * id_w + id_b;
}

static void GetEndingFromIndex(int piece_types[2][KING], int index) {
    int id_b, id_w, i;

    id_b = index % num_side_endings;
    id_w = index / num_side_endings;

    if (id_w >= num_side_endings) {
        fprintf(stderr, "Internal error in GetndingFromIndex\n");
        exit(1);
    }

    for (i = 0; i < num_side_endings; i++) {
        if (SingleSideCount[i].index == id_w) {
            memcpy(piece_types[0], SingleSideCount[i].types,
                   KING * sizeof(int));
            break;
        }
    }

    for (i = 0; i < num_side_endings; i++) {
        if (SingleSideCount[i].index == id_b) {
            memcpy(piece_types[1], SingleSideCount[i].types,
                   KING * sizeof(int));
            break;
        }
    }
}

#define MAX_LINES 10000
#define MAX_PGN_OUTPUT 126
#define MAX_GAME_MOVES 2048

static unsigned int num_games = 0;

static char OutputLine[MAX_PGN_OUTPUT + 2];
static int OutputColumn = 0;
static int NumOutputLines = 0;

static SCORE *ScoreListPGN = NULL;
static SCORE *ScoreListDeltaPGN = NULL;
static int NumBadMovesPGN = 0;
static int NumDeltaMovesPGN = 0;
static int MaxBadEnding = -1;
static int MaxBadVariation = -1;
static bool AnnotatorSeen = false;
static int AnnotatorLine = 0;
static int LastTagLine = 0;
static int AddEGTBCommentLine = 0;

typedef struct {
    char OutputLine[MAX_PGN_OUTPUT + 2];
} PGN_OUTPUT;

static PGN_OUTPUT *PGN_Game = NULL;

void ResetCashedPGN() {
    NumBadMovesPGN = 0;
    NumDeltaMovesPGN = 0;
    MaxBadEnding = -1;
    MaxBadVariation = -1;
    AnnotatorSeen = false;
    AnnotatorLine = 0;
    LastTagLine = 0;
    AddEGTBCommentLine = 0;
    OutputColumn = 0;
    NumOutputLines = 0;
    memset(OutputLine, 0, sizeof(OutputLine));
}

static void InitCashedPGNOutput() {
    if (PGN_Game == NULL) {
        if ((PGN_Game = (PGN_OUTPUT *)malloc(MAX_LINES * sizeof(PGN_OUTPUT))) ==
            NULL) {
            fprintf(stderr, "Could not allocate %d lines for PGN output\n",
                    MAX_LINES);
            exit(1);
        }
    }
    if (ScoreListPGN == NULL) {
        if ((ScoreListPGN = (SCORE *)malloc(MAX_GAME_MOVES * sizeof(SCORE))) ==
            NULL) {
            fprintf(stderr, "Could not allocate %d scores\n", MAX_GAME_MOVES);
            exit(1);
        }
    }
    if (ScoreListDeltaPGN == NULL) {
        if ((ScoreListDeltaPGN =
                 (SCORE *)malloc(MAX_GAME_MOVES * sizeof(SCORE))) == NULL) {
            fprintf(stderr, "Could not allocate %d scores\n", MAX_GAME_MOVES);
            exit(1);
        }
    }
    ResetCashedPGN();
}

static void InitPGNOutput() {
    OutputColumn = 0;
    memset(OutputLine, 0, sizeof(OutputLine));
}

static void FlushPGNOutput() {
    MyPrintf("%s\n", OutputLine);
    OutputColumn = 0;
    MyFlush();
}

static void FlushPGNOutputCashed() {
    if (NumOutputLines >= MAX_LINES) {
        fprintf(stderr, "Game length exceeds %d, increase MAX_LINES\n",
                MAX_LINES);
        exit(1);
    }

    if (OutputColumn > 0) {
        strcpy(PGN_Game[NumOutputLines].OutputLine, OutputLine);
    } else {
        PGN_Game[NumOutputLines].OutputLine[0] = '\0';
    }
    OutputColumn = 0;
    NumOutputLines++;
}

static void InsertBlankLine() {
    PGN_Game[NumOutputLines].OutputLine[0] = '\0';
    NumOutputLines++;
}

static int OutputPGNString(char *string) {
    int len = strlen(string);
    bool append_blank = false;

    if (len == 0)
        return 0;

    if (OutputColumn > 0) {
        char last_char = OutputLine[OutputColumn - 1];
        char next_char = string[0];

        if (!(next_char == ')' || next_char == '}' || last_char == ' ' ||
              next_char == ' ' || last_char == '(' || last_char == '{' ||
              (last_char == '.' && EGFormat)))
            append_blank = true;
    }

    if (OutputColumn + len + (append_blank ? 1 : 0) >= LineWidth) {
        FlushPGNOutput();
    }

    if (OutputColumn == 0) {
        int offset = 0;
        for (;;) {
            strncpy(OutputLine, string + offset, LineWidth);
            OutputLine[LineWidth] = '\0';
            OutputColumn = strlen(OutputLine);
            if (len < LineWidth)
                break;
            else {
                FlushPGNOutput();
                offset += LineWidth;
                len -= LineWidth;
            }
        }
    } else {
        if (append_blank)
            strcat(OutputLine, " ");

        OutputColumn = strlen(OutputLine);

        if (OutputColumn + len >= LineWidth) {
            FlushPGNOutput();
            int offset = 0;
            for (;;) {
                strncpy(OutputLine, string + offset, LineWidth);
                OutputLine[LineWidth] = '\0';
                OutputColumn = strlen(OutputLine);
                if (len < LineWidth)
                    break;
                else {
                    FlushPGNOutput();
                    offset += LineWidth;
                    len -= LineWidth;
                }
            }
        } else
            strcat(OutputLine, string);
    }

    OutputColumn = strlen(OutputLine);

    return OutputColumn;
}

static bool Is_Seven_Tag(char *tag) {
    if (!strcmp(tag, "Event"))
        return true;
    if (!strcmp(tag, "Site"))
        return true;
    if (!strcmp(tag, "Date"))
        return true;
    if (!strcmp(tag, "Round"))
        return true;
    if (!strcmp(tag, "White"))
        return true;
    if (!strcmp(tag, "Black"))
        return true;
    if (!strcmp(tag, "Result"))
        return true;

    return false;
}

static int OutputPGNCommentString(char *string) {
    int len = strlen(string);
    bool append_blank = false;

    if (len == 0)
        return 0;

    if (OutputColumn > 0) {
        char last_char = OutputLine[OutputColumn - 1];
        char next_char = string[0];

        if (!(next_char == ')' || next_char == '}' || last_char == ' ' ||
              next_char == ' ' || last_char == '(' || last_char == '{' ||
              (last_char == '.' && EGFormat)))
            append_blank = true;
    }

    if (OutputColumn + len + (append_blank ? 1 : 0) >= LineWidth) {
        FlushPGNOutput();
    }

    if (OutputColumn == 0) {
        int offset = 0;
        for (;;) {
            int len_to_write = LineWidth;
            int line_start = 0;
            // make sure new line does not start with %, and for chessbase also
            // not with [Tag where Tag is one of the seven standard tags
            if (string[offset] == '%' ||
                (string[offset] == '[' && Is_Seven_Tag(&string[offset + 1]))) {
                len_to_write = LineWidth - 1;
                line_start = 1;
                OutputLine[0] = ' ';
            }
            // the enhanced PGN standard uses [% inside a comment as a special
            // comment symbol. Make sure that symbol does not get split between
            // lines
            if (string[offset + LineWidth - 1 - line_start] == '[' &&
                string[offset + LineWidth - line_start] == '%') {
                len_to_write--;
            }
            strncpy(OutputLine + line_start, string + offset, len_to_write);
            OutputLine[line_start + len_to_write] = '\0';
            OutputColumn = strlen(OutputLine);
            if (len < len_to_write)
                break;
            else {
                FlushPGNOutput();
                offset += len_to_write;
                len -= len_to_write;
            }
        }
    } else {
        if (append_blank)
            strcat(OutputLine, " ");

        OutputColumn = strlen(OutputLine);

        if (OutputColumn + len >= LineWidth) {
            FlushPGNOutput();
            int offset = 0;
            for (;;) {
                int len_to_write = LineWidth;
                int line_start = 0;
                // make sure new line does not start with %, and for chessbase
                // also not with [Tag where Tag is one of the seven standard
                // tags
                if (string[offset] == '%' ||
                    (string[offset] == '[' &&
                     Is_Seven_Tag(&string[offset + 1]))) {
                    len_to_write = LineWidth - 1;
                    line_start = 1;
                    OutputLine[0] = ' ';
                }
                // the enhanced PGN standard uses [% inside a comment as a
                // special comment symbol. Make sure that symbol does not get
                // split between lines
                if (string[offset + LineWidth - 1 - line_start] == '[' &&
                    string[offset + LineWidth - line_start] == '%') {
                    len_to_write--;
                }
                strncpy(OutputLine + line_start, string + offset, len_to_write);
                OutputLine[line_start + len_to_write] = '\0';
                OutputColumn = strlen(OutputLine);
                if (len < len_to_write)
                    break;
                else {
                    FlushPGNOutput();
                    offset += len_to_write;
                    len -= len_to_write;
                }
            }
        } else
            strcat(OutputLine, string);
    }

    OutputColumn = strlen(OutputLine);

    return OutputColumn;
}

static int OutputPGNStringCashed(char *string) {
    int len = strlen(string);
    bool append_blank = false;

    if (len == 0)
        return 0;

    if (OutputColumn > 0) {
        char last_char = OutputLine[OutputColumn - 1];
        char next_char = string[0];

        if (!(next_char == ')' || next_char == '}' || last_char == ' ' ||
              next_char == ' ' || last_char == '(' || last_char == '{' ||
              (last_char == '.' && EGFormat)))
            append_blank = true;
    }

    if (OutputColumn + len + (append_blank ? 1 : 0) >= LineWidth) {
        FlushPGNOutputCashed();
    }

    if (OutputColumn == 0) {
        int offset = 0;
        for (;;) {
            strncpy(OutputLine, string + offset, LineWidth);
            OutputLine[LineWidth] = '\0';
            OutputColumn = strlen(OutputLine);
            if (len <= LineWidth)
                break;
            else {
                FlushPGNOutputCashed();
                offset += LineWidth;
                len -= LineWidth;
            }
        }
    } else {
        if (append_blank)
            strcat(OutputLine, " ");

        OutputColumn = strlen(OutputLine);

        if (OutputColumn + len >= LineWidth) {
            FlushPGNOutputCashed();
            int offset = 0;
            for (;;) {
                strncpy(OutputLine, string + offset, LineWidth);
                OutputLine[LineWidth] = '\0';
                OutputColumn = strlen(OutputLine);
                if (len <= LineWidth)
                    break;
                else {
                    FlushPGNOutputCashed();
                    offset += LineWidth;
                    len -= LineWidth;
                }
            }
        } else
            strcat(OutputLine, string);
    }

    OutputColumn = strlen(OutputLine);

    return OutputColumn;
}

static int OutputPGNCommentStringCashed(char *string) {
    int len = strlen(string);
    bool append_blank = false;

    if (len == 0)
        return 0;

    if (OutputColumn > 0) {
        char last_char = OutputLine[OutputColumn - 1];
        char next_char = string[0];

        if (!(next_char == ')' || next_char == '}' || last_char == ' ' ||
              next_char == ' ' || last_char == '(' || last_char == '{' ||
              (last_char == '.' && EGFormat)))
            append_blank = true;
    }

    if (OutputColumn + len + (append_blank ? 1 : 0) >= LineWidth) {
        FlushPGNOutputCashed();
    }

    if (OutputColumn == 0) {
        int offset = 0;
        for (;;) {
            int len_to_write = LineWidth;
            int line_start = 0;
            // make sure new line does not start with %, and for chessbase also
            // not with [Tag where Tag is one of the seven standard tags
            if (string[offset] == '%' ||
                (string[offset] == '[' && Is_Seven_Tag(&string[offset + 1]))) {
                len_to_write = LineWidth - 1;
                line_start = 1;
                OutputLine[0] = ' ';
            }
            // the enhanced PGN standard uses [% inside a comment as a special
            // comment symbol. Make sure that symbol does not get split between
            // lines
            if (string[offset + LineWidth - 1 - line_start] == '[' &&
                string[offset + LineWidth - line_start] == '%') {
                len_to_write--;
            }
            strncpy(OutputLine + line_start, string + offset, len_to_write);
            OutputLine[line_start + len_to_write] = '\0';
            OutputColumn = strlen(OutputLine);
            if (len <= len_to_write)
                break;
            else {
                FlushPGNOutputCashed();
                offset += len_to_write;
                len -= len_to_write;
            }
        }
    } else {
        if (append_blank)
            strcat(OutputLine, " ");

        OutputColumn = strlen(OutputLine);

        if (OutputColumn + len >= LineWidth) {
            FlushPGNOutputCashed();
            int offset = 0;
            for (;;) {
                int len_to_write = LineWidth;
                int line_start = 0;
                // make sure new line does not start with %, and for chessbase
                // also not with [Tag where Tag is one of the seven standard
                // tags
                if (string[offset] == '%' ||
                    (string[offset] == '[' &&
                     Is_Seven_Tag(&string[offset + 1]))) {
                    len_to_write = LineWidth - 1;
                    line_start = 1;
                    OutputLine[0] = ' ';
                }
                // the enhanced PGN standard uses [% inside a comment as a
                // special comment symbol. Make sure that symbol does not get
                // split between lines
                if (string[offset + LineWidth - 1 - line_start] == '[' &&
                    string[offset + LineWidth - line_start] == '%') {
                    len_to_write--;
                }
                strncpy(OutputLine + line_start, string + offset, len_to_write);
                OutputLine[line_start + len_to_write] = '\0';
                OutputColumn = strlen(OutputLine);
                if (len <= len_to_write)
                    break;
                else {
                    FlushPGNOutputCashed();
                    offset += len_to_write;
                    len -= len_to_write;
                }
            }
        } else
            strcat(OutputLine, string);
    }

    OutputColumn = strlen(OutputLine);

    return OutputColumn;
}

static int OutputPGNComment(char *comment) {
    static bool first_call = true;
    static char *string;

    if (first_call) {
        if ((string = (char *)malloc(MAX_PGN_LINE * sizeof(char))) == NULL) {
            fprintf(stderr, "OutputPGNComment: could not allocate string\n");
            exit(1);
        }
        first_call = false;
    }

    if (strlen(comment) == 0)
        return 0;

    OutputPGNString("{");

    strcpy(string, comment);
    char *sptr = strtok(string, " \t\f\r\n");

    while (sptr != NULL) {
        OutputPGNCommentString(sptr);
        sptr = strtok(NULL, " \t\f\r\n");
    }

    OutputPGNString("}");

    return OutputColumn;
}

static int OutputPGNCommentCashed(char *comment) {
    static bool first_call = true;
    static char *string;

    if (first_call) {
        if ((string = (char *)malloc(MAX_PGN_LINE * sizeof(char))) == NULL) {
            fprintf(stderr,
                    "OutputPGNCommentCashed: could not allocate string\n");
            exit(1);
        }
        first_call = false;
    }

    if (strlen(comment) == 0)
        return 0;

    OutputPGNStringCashed("{");

    strcpy(string, comment);
    char *sptr = strtok(string, " \t\f\r\n");

    while (sptr != NULL) {
        OutputPGNCommentStringCashed(sptr);
        sptr = strtok(NULL, " \t\f\r\n");
    }

    OutputPGNStringCashed("}");

    return OutputColumn;
}

static void OutputCashedPGN() {
    if (NumOutputLines == 0)
        return;

    char annotator[512];
    annotator[0] = '\0';
    if (AddAnnotator) {
        if (MaxBadEnding > 0) {
            sprintf(annotator, "%s_%d", Annotator, MaxBadEnding);
        }
        if (MaxBadVariation > 0) {
            if (strlen(annotator) > 0)
                strcat(annotator, "/");
            char var[256];
            sprintf(var, "Var%s_%d", Annotator, MaxBadVariation);
            strcat(annotator, var);
        }
    }

    if (AddAnnotator && strlen(annotator) > 0) {
        for (int i = 0; i < LastTagLine; i++) {
            if (AnnotatorSeen && i == AnnotatorLine) {
                char *name = strtok(PGN_Game[i].OutputLine, "\"");
                if (name != NULL)
                    name = strtok(NULL, "\"");
                char out[512];
                if (name != NULL)
                    sprintf(out, "[Annotator \"%s/%s\"]", name, annotator);
                else
                    sprintf(out, "[Annotator \"%s\"]", annotator);
                MyPrintf("%s\n", out);
            } else
                MyPrintf("%s\n", PGN_Game[i].OutputLine);
        }
        if (!AnnotatorSeen) {
            MyPrintf("[Annotator \"%s\"]\n", annotator);
        }
    } else {
        for (int i = 0; i < LastTagLine; i++) {
            MyPrintf("%s\n", PGN_Game[i].OutputLine);
        }
    }

    MyPrintf("\n");

    if (AddEGTBComments && (NumBadMovesPGN > 0)) {
        OutputColumn = 0;
        memset(OutputLine, 0, sizeof(OutputLine));
        char out_string[64];
        sprintf(out_string, "{%s_0 ", Annotator);
        OutputPGNString(out_string);
        for (int i = 0; i < NumBadMovesPGN; i++) {
            char move_string[16], comment[MAX_PGN_OUTPUT];
            SCORE *sptr = &ScoreListPGN[i];
            MoveScoreToString(sptr->score, move_string);
            if (sptr->move_no != 999) {
                sprintf(comment, "%d%c %s", sptr->move_no,
                        (sptr->side == WHITE ? 'w' : 'b'), move_string);
            } else {
                sprintf(comment, "999 %s", move_string);
            }
            if (i < (NumBadMovesPGN - 1))
                strcat(comment, " ");
            OutputPGNString(comment);
        }
        OutputPGNString("}");
        FlushPGNOutput();
    }

    if (AddEGTBDepthComments && (NumDeltaMovesPGN > 0)) {
        OutputColumn = 0;
        memset(OutputLine, 0, sizeof(OutputLine));
        char out_string[64];
        sprintf(out_string, "{%s_0 ", Annotator);
        OutputPGNString(out_string);
        for (int i = 0; i < NumDeltaMovesPGN; i++) {
            char comment[MAX_PGN_OUTPUT];
            SCORE *sptr = &ScoreListDeltaPGN[i];
            if (sptr->move_no != 999) {
                sprintf(comment, "%d%c D%d", sptr->move_no,
                        (sptr->side == WHITE ? 'w' : 'b'), sptr->score);
                if (i < (NumDeltaMovesPGN - 1))
                    strcat(comment, " ");
                OutputPGNString(comment);
            }
        }
        OutputPGNString("}");
        FlushPGNOutput();
    }

    for (int i = LastTagLine; i < NumOutputLines; i++) {
        MyPrintf("%s\n", PGN_Game[i].OutputLine);
        MyFlush();
    }

    ResetCashedPGN();
}

static bool PawnsPresent(BOARD *Board) {
    return (Board->piece_type_count[WHITE][PAWN] ||
            Board->piece_type_count[BLACK][PAWN]);
}

int SetBoard(BOARD *Board, int *board, int side, int ep_square, int castle,
             int half_move, int full_move) {
    int npieces = 0, nwhite = 0, nblack = 0, strength_w = 0, strength_b = 0, i;

    memcpy(Board->board, board, sizeof(Board->board));
    Board->side = side;
    Board->ep_square = ep_square;
    Board->castle = castle;
    Board->half_move = half_move;
    Board->full_move = full_move;
    memset(Board->piece_type_count, 0, sizeof(Board->piece_type_count));
    memset(Board->piece_locations, 0, sizeof(Board->piece_locations));

    for (i = 0; i < NSQUARES; i++) {
        if (board[i] > 0) {
            if (board[i] == KING)
                Board->wkpos = i;
            else {
                Board->piece_locations[WHITE][board[i]]
                                      [Board->piece_type_count[WHITE]
                                                              [board[i]]++] = i;
                nwhite++;
                strength_w += PieceStrengths[board[i]];
            }
            npieces++;
        } else if (board[i] < 0) {
            if (board[i] == -KING)
                Board->bkpos = i;
            else {
                Board->piece_locations[BLACK][-board[i]]
                                      [Board->piece_type_count[BLACK]
                                                              [-board[i]]++] =
                    i;
                nblack++;
                strength_b += PieceStrengths[-board[i]];
            }
            npieces++;
        }
    }

    Board->strength_w = strength_w;
    Board->strength_b = strength_b;
    Board->num_pieces = npieces;
    Board->nwhite = nwhite;
    Board->nblack = nblack;

    return npieces;
}

static void DisplayRawBoard(int *board, char *label) {
    int row, col, sq, pi;

    MyPrintf("\n");
    for (row = (NROWS - 1); row >= 0; row--) {
#if (NROWS >= 10)
        MyPrintf("%-2d", row + 1);
#else
        MyPrintf("%-1d", row + 1);
#endif
        for (col = 0; col < NCOLS; col++) {
            sq = SquareMake(row, col);
            pi = board[sq];
            MyPrintf(" ");
            if (pi) {
                MyPrintf("%c",
                         (pi > 0) ? toupper(piece_char(pi)) : piece_char(-pi));
            } else
                MyPrintf(".");
        }
        if (row == (NROWS - 1) && label != NULL)
            MyPrintf("  %s", label);
        MyPrintf("\n");
    }
#if (NROWS >= 10)
    MyPrintf("  ");
#else
    MyPrintf(" ");
#endif
    for (col = 0; col < NCOLS; col++)
        MyPrintf(" %c", 'a' + col);
    MyPrintf("\n\n");
    MyFlush();
}

static void DisplayBoard(BOARD *Board, char *label) {
    int row, col, sq, pi;

    MyPrintf("\n");
    for (row = (NROWS - 1); row >= 0; row--) {
#if (NROWS >= 10)
        MyPrintf("%-2d", row + 1);
#else
        MyPrintf("%-1d", row + 1);
#endif
        for (col = 0; col < NCOLS; col++) {
            sq = SquareMake(row, col);
            pi = Board->board[sq];
            MyPrintf(" ");
            if (pi) {
                MyPrintf("%c",
                         (pi > 0) ? toupper(piece_char(pi)) : piece_char(-pi));
            } else if (Board->ep_square > 0 && sq == Board->ep_square) {
                MyPrintf("e");
            } else
                MyPrintf(".");
        }
        if (row == (NROWS - 1) && label != NULL)
            MyPrintf("  %s", label);
        if (row == 1 && Board->castle) {
            char castle_str[6] = {0, 0, 0, 0, 0, 0};
            int nc = 0;
            if (Board->castle & WK_CASTLE)
                castle_str[nc++] = 'K';
            if (Board->castle & WQ_CASTLE)
                castle_str[nc++] = 'Q';
            if (Board->castle & BK_CASTLE)
                castle_str[nc++] = 'k';
            if (Board->castle & BQ_CASTLE)
                castle_str[nc++] = 'q';
            MyPrintf("  %s", castle_str);
        }
        if (row == 0) {
            MyPrintf("  %c...", (Board->side == WHITE) ? 'w' : 'b');
        }
        MyPrintf("\n");
    }
#if (NROWS >= 10)
    MyPrintf("  ");
#else
    MyPrintf(" ");
#endif
    for (col = 0; col < NCOLS; col++)
        MyPrintf(" %c", 'a' + col);
    MyPrintf("\n\n");
    MyFlush();
}

static bool skipping_game = false;
static bool output_pgn = true;
static unsigned int RAV_level = 0;

static unsigned int num_positions = 0;
static unsigned int max_rav_game = 0;
static unsigned int max_rav_level = 0;

static PGNToken ChTab[MAX_CHAR];
static short MoveChars[MAX_CHAR];

/* Return TRUE if line contains a non-space character, but
 * is not a comment line.
 */

bool NonBlankLine(const char *line) {
    bool blank = true;

    if (line != NULL) {
        if (line[0] != COMMENT_CHAR) {
            while (blank && (*line != '\0')) {
                if (!isspace((int)*line)) {
                    blank = false;
                } else {
                    line++;
                }
            }
        }
    }
    return !blank;
}

bool BlankLine(const char *line) { return !NonBlankLine(line); }

static char *NextInputLine(FILE *fp) {
    /* Retain each line in turn. */
    static bool first_call = true;
    static char *line, *linep;

    if (first_call) {
        if ((line = (char *)malloc(MAX_PGN_LINE * sizeof(char))) == NULL) {
            fprintf(stderr, "Could not allocate line of length %d\n",
                    MAX_PGN_LINE);
            exit(1);
        }
        first_call = false;
    }

    linep = fgets(line, MAX_PGN_LINE, fp);

    if (strlen(line) >= (MAX_PGN_LINE - 1)) {
        fprintf(stderr, "WARNING: input contains lines > %d\n", MAX_PGN_LINE);
        printf("%s\n", line);
        exit(1);
    }
    return linep;
}

/* Initialise ChTab[], the classification of the initial characters
 * of symbols.
 * Initialise MoveChars, the classification of secondary characters
 * of moves.
 */
void InitLexTables(void) {
    int i;

    /* Initialise ChTab[]. */
    for (i = 0; i < MAX_CHAR; i++) {
        ChTab[i] = ERROR_TOKEN;
    }
    ChTab[' '] = WHITESPACE;
    ChTab['\t'] = WHITESPACE;
    /* Take account of DOS line-ends. */
    ChTab['\r'] = WHITESPACE;
    ChTab['\n'] = WHITESPACE;
    ChTab['['] = TAG_START;
    ChTab[']'] = TAG_END;
    ChTab['"'] = DOUBLE_QUOTE;
    ChTab['{'] = COMMENT_START;
    ChTab['}'] = COMMENT_END;
    ChTab['$'] = NAG;
    ChTab['!'] = ANNOTATE;
    ChTab['?'] = ANNOTATE;
    ChTab['+'] = CHECK_SYMBOL;
    ChTab['#'] = CHECK_SYMBOL;
    ChTab['.'] = DOT;
    ChTab['('] = RAV_START;
    ChTab[')'] = RAV_END;
    ChTab['%'] = PERCENT;
    ChTab['\\'] = ESCAPE;
    ChTab['\0'] = EOS;
    ChTab['*'] = STAR;
    ChTab['-'] = ALPHA;

    /* Operators allowed only in the tag file. */
    ChTab['<'] = OPERATOR;
    ChTab['>'] = OPERATOR;
    ChTab['='] = OPERATOR; /* Overloaded in MoveChars. */

    for (i = '0'; i <= '9'; i++) {
        ChTab[i] = DIGIT;
    }
    for (i = 'A'; i <= 'Z'; i++) {
        ChTab[i] = ALPHA;
        ChTab[i + ALPHA_DIST] = ALPHA;
    }
    ChTab['_'] = ALPHA;

    /* Initialise MoveChars[]. */
    for (i = 0; i < MAX_CHAR; i++) {
        MoveChars[i] = 0;
    }
    /* Files. */
    for (i = 'a'; i <= 'a' + NCOLS - 1; i++) {
        MoveChars[i] = 1;
    }
    /* Ranks. */
    for (i = '1'; i <= '1' + NROWS - 1; i++) {
        MoveChars[i] = 1;
    }
    /* Upper-case pieces. */
    MoveChars['K'] = 1;
    MoveChars['Q'] = 1;
    MoveChars['R'] = 1;
    MoveChars['N'] = 1;
    MoveChars['B'] = 1;
    MoveChars['C'] = 1; /* chancellor or cardinal */
    MoveChars['A'] = 1; /* archbishop */
    MoveChars['S'] = 1; /* German knight */
    /* Lower-case pieces. */
    MoveChars['k'] = 1;
    MoveChars['q'] = 1;
    MoveChars['r'] = 1;
    MoveChars['n'] = 1;
    MoveChars['b'] = 1;

    /* Capture and square separators. */
    MoveChars['x'] = 1;
    MoveChars['X'] = 1;
    MoveChars[':'] = 1;
    MoveChars['-'] = 1;
    /* Promotion character. */
    MoveChars['='] = 1;
    /* Castling. */
    MoveChars['O'] = 1;
    MoveChars['o'] = 1;
    MoveChars['0'] = 1;
    /* Allow a trailing p for ep. */
    MoveChars['p'] = 1;
    /* Allow a null move to start with Z */
    MoveChars['Z'] = 1;
}

/* Starting from linep in line, gather up the string until
 * the closing quote.  Skip over the closing quote.
 */

static LinePair GatherString(char *line, unsigned char *linep, char *string) {
    LinePair resulting_line;
    char ch;
    unsigned len = 0;
    char *str;

    do {
        ch = *linep++;
        len++;
        if (ch == '\\') {
            /* Escape the next character. */
            len++;
            ch = *linep++;
            if (ch != '\0') {
                len++;
                ch = *linep++;
            }
        }
    } while ((ch != '"') && (ch != '\0'));
    /* The last one doesn't belong in the string. */
    len--;
    strncpy(string, (const char *)(linep - len - 1), len);
    string[len] = '\0';

    /* Make sure that the string was properly terminated, by
     * looking at the last character examined.
     */
    if (ch == '\0') {
        /* Too far. */
        if (!skipping_game) {
            if (Verbose > 1 || CheckSyntaxOnly)
                MyPrintf("Missing closing quote in %s\n", line);
        }
        if (len != 1) {
            /* Move back to the null. */
            linep--;
            string[len - 1] = '\0';
        }
    } else {
        /* We have already skipped over the closing quote. */
    }
    resulting_line.line = line;
    resulting_line.linep = linep;
    resulting_line.token = STRING;
    return resulting_line;
}

static LinePair GatherSimpleString(char *line, unsigned char *linep,
                                   char *string) {
    LinePair resulting_line;
    unsigned len = 0;
    char *str_end;

    str_end = strrchr((char *)linep, '"');

    if (str_end != NULL) {
        while ((char *)linep != str_end) {
            string[len++] = *linep++;
        }
        linep++;
        string[len] = '\0';
    } else {
        if (!skipping_game) {
            if (Verbose > 1 || CheckSyntaxOnly)
                MyPrintf("Missing closing quote in %s\n", line);
        }
        while (*linep != '\0') {
            string[len++] = *linep++;
        }
        string[len] = '\0';
    }

    resulting_line.line = line;
    resulting_line.linep = linep;
    resulting_line.token = STRING;
    return resulting_line;
}

/* Starting from linep in line, gather up a comment until
 * the END_COMMENT.  Skip over the END_COMMENT.
 */
static LinePair GatherComment(char *line, unsigned char *linep, FILE *fp,
                              char *comment) {
    LinePair resulting_line;
    char ch;
    unsigned len = 0, tot_len = 0;

    comment[0] = '\0';

    if (Verbose > 7) {
        MyPrintf("comment: %s\n", linep);
    }
    do {
        /* Restart a new segment. */
        len = 0;
        do {
            ch = *linep++;
            len++;
        } while ((ch != '}') && (ch != '\0'));
        /* The last one doesn't belong in the comment. */
        len--;
        tot_len += len;
        if (tot_len >= MAX_STRING) {
            fflush(stdout);
            fprintf(stderr, "Comment too long %s\n", line);
            char myout[2 * MAX_STRING];
            strcpy(myout, comment);
            strncat(myout, (const char *)(linep - len - 1), len);
            fprintf(stderr, "Comment seen so far:\n");
            fprintf(stderr, "%s\n", myout);
            exit(1);
        }
        strncat(comment, (const char *)(linep - len - 1), len);
        comment[tot_len] = '\0';
        if (ch == '\0') {
            line = NextInputLine(fp);
            linep = (unsigned char *)line;
            if (Verbose > 7) {
                MyPrintf("next comment line: %s\n", linep);
            }
        }
    } while ((ch != '}') && (line != NULL));

    resulting_line.line = line;
    resulting_line.linep = linep;
    resulting_line.token = COMMENT;
    return resulting_line;
}

/* Initialise the TagList. This should be stored in alphabetical order,
 * by virtue of the order in which the _TAG values are defined.
 */

static void InitTagList(void) {
    int i;
    tag_list_length = ORIGINAL_NUMBER_OF_TAGS;
    /* Be paranoid and put a string in every entry. */
    for (i = 0; i < MAX_TAGS; i++) {
        TagList[i] = NULL;
        TagValues[i] = NULL;
    }
    TagList[ANNOTATOR_TAG] = "Annotator";
    TagList[BLACK_TAG] = "Black";
    TagList[BLACK_ELO_TAG] = "BlackElo";
    TagList[BLACK_NA_TAG] = "BlackNA";
    TagList[BLACK_TITLE_TAG] = "BlackTitle";
    TagList[BLACK_TYPE_TAG] = "BlackType";
    TagList[BLACK_USCF_TAG] = "BlackUSCF";
    TagList[BOARD_TAG] = "Board";
    TagList[DATE_TAG] = "Date";
    TagList[ECO_TAG] = "ECO";
    TagList[PSEUDO_ELO_TAG] = "Elo";
    TagList[EVENT_TAG] = "Event";
    TagList[EVENT_DATE_TAG] = "EventDate";
    TagList[EVENT_SPONSOR_TAG] = "EventSponsor";
    TagList[FEN_TAG] = "FEN";
    TagList[LONG_ECO_TAG] = "LongECO";
    TagList[MODE_TAG] = "Mode";
    TagList[NIC_TAG] = "NIC";
    TagList[OPENING_TAG] = "Opening";
    TagList[PSEUDO_PLAYER_TAG] = "Player";
    TagList[PLY_COUNT_TAG] = "PlyCount";
    TagList[RESULT_TAG] = "Result";
    TagList[ROUND_TAG] = "Round";
    TagList[SECTION_TAG] = "Section";
    TagList[SETUP_TAG] = "SetUp";
    TagList[SITE_TAG] = "Site";
    TagList[STAGE_TAG] = "Stage";
    TagList[SUB_VARIATION_TAG] = "SubVariation";
    TagList[TERMINATION_TAG] = "Termination";
    TagList[TIME_TAG] = "Time";
    TagList[TIME_CONTROL_TAG] = "TimeControl";
    TagList[UTC_DATE_TAG] = "UTCDate";
    TagList[UTC_TIME_TAG] = "UTCTime";
    TagList[VARIANT_TAG] = "Variant";
    TagList[VARIATION_TAG] = "Variation";
    TagList[WHITE_TAG] = "White";
    TagList[WHITE_ELO_TAG] = "WhiteElo";
    TagList[WHITE_NA_TAG] = "WhiteNA";
    TagList[WHITE_TITLE_TAG] = "WhiteTitle";
    TagList[WHITE_TYPE_TAG] = "WhiteType";
    TagList[WHITE_USCF_TAG] = "WhiteUSCF";
}

/* Extend TagList to accomodate a new tag string.
 * Return the current value of tag_list_length as its
 * index, having incremented its value.
 */

static int MakeNewTag(const char *tag) {
    int tag_index = tag_list_length;

    tag_list_length++;
    if (tag_list_length >= MAX_TAGS) {
        fprintf(stderr, "Maximum number of tags %d exceeded\n", MAX_TAGS);
        exit(1);
    }
    if (TagList[tag_index] != NULL)
        free(TagList[tag_index]);
    /*
    char *d = (char *)malloc(strlen(tag)+1);
    if(d != NULL) {
       strcpy(d, tag);
    }
    TagList[tag_index] = d;
    */
    TagList[tag_index] = strdup(tag);
    return tag_index;
}

static int IdentifyTag(const char *tag_string) {
    int tag_index;

    for (tag_index = 0; tag_index < tag_list_length; tag_index++) {
        if (strcmp(tag_string, TagList[tag_index]) == 0) {
            return tag_index;
        }
    }
    return -1;
}

/* Starting from linep in line, gather up the tag name.
 * Skip over any preceding white space.
 */
static LinePair GatherTag(char *line, unsigned char *linep, FILE *fp,
                          int *tag_index) {
    LinePair resulting_line;
    char ch;
    unsigned len = 0;
    char tag_string[256];

    do {
        /* Check for end of line whilst skipping white space. */
        if (*linep == '\0') {
            line = NextInputLine(fp);
            linep = (unsigned char *)line;
        }
        if (line != NULL) {
            while (ChTab[(unsigned)*linep] == WHITESPACE) {
                linep++;
            }
        }
    } while ((line != NULL) && (ChTab[(unsigned)*linep] == '\0'));

    if (line != NULL) {
        ch = *linep++;
        while (isalpha((unsigned)ch) || isdigit((unsigned)ch) || (ch == '_')) {
            len++;
            ch = *linep++;
        }
        /* The last one wasn't part of the tag. */
        linep--;
        if (len > 0) {
            int tag_item;

            if (len > sizeof(tag_string) / sizeof(char)) {
                fprintf(stderr, "Tag too long %s\n", linep);
                exit(1);
            }
            strncpy(tag_string, (const char *)(linep - len), len);
            tag_string[len] = '\0';
            tag_item = IdentifyTag(tag_string);
            if (tag_item < 0) {
                tag_item = MakeNewTag(tag_string);
            }
            if (tag_item >= 0 && tag_item < tag_list_length) {
                *tag_index = tag_item;
                resulting_line.token = TAG;
            } else {
                fprintf(stderr,
                        "Internal error: invalid tag index %d in GatherTag.\n",
                        tag_item);
                exit(1);
            }
        } else {
            resulting_line.token = NO_TOKEN;
        }
    } else {
        resulting_line.token = NO_TOKEN;
    }
    resulting_line.line = line;
    resulting_line.linep = linep;
    return resulting_line;
}

/* Does the character represent a column of the board? */
bool IsColumn(char c) { return (c >= 'a') && (c <= 'a' + NCOLS - 1); }

/* Does the character represent a rank of the board? */
bool IsRank(char c) { return (c >= '1') && (c <= '1' + NROWS - 1); }

static int GetPiece(char p) {
    switch (p) {
    case 'k':
    case 'K':
        return KING;
        break;
    case 'm':
    case 'M':
        return MAHARAJA;
        break;
    case 'q':
    case 'Q':
        return QUEEN;
        break;
    case 'c':
    case 'C':
        return CARDINAL;
        break;
    case 'r':
    case 'R':
        return ROOK;
        break;
    case 'a':
    case 'A':
        return ARCHBISHOP;
        break;
    case 'b':
    case 'B':
        return BISHOP;
        break;
    case 'n':
    case 'N':
        return KNIGHT;
        break;
    case 'p':
    case 'P':
        return PAWN;
        break;
    default:
        return -1;
    }
}

/* What kind of piece is *move likely to represent? */
int GetPiecePGN(char *move) {
    int piece = NO_PIECE;

    switch (*move) {
    case 'K':
    case 'k':
        piece = KING;
        break;
    case 'Q':
    case 'q':
        piece = QUEEN;
        break;
    case 'R':
    case 'r':
        piece = ROOK;
        break;
    case 'N':
    case 'n':
    case 'S':
        piece = KNIGHT;
        break;
    case 'B':
        piece = BISHOP;
        break;
    case 'A':
        piece = ARCHBISHOP;
        break;
    case 'C':
        piece = CARDINAL;
        break;
    case 'M':
        piece = MAHARAJA;
        break;
    }
    return piece;
}

static int ParseEndingName(char *ending, int count[2][KING]) {
    int i, j, n_kings = 0, n_pieces = 0, color = BLACK;

    for (i = 0; i < 2; i++)
        for (j = 0; j < KING; j++)
            count[i][j] = 0;

    for (i = 0; i < strlen(ending); i++) {
        int piece = GetPiece(ending[i]);

        if (piece < 0) {
            if (Verbose > 1) {
                MyPrintf("Invalid piece %c in ParseEndingName\n", ending[i]);
                MyFlush();
            }
            continue;
        }

        if (piece == KING) {
            color = OtherSide(color);
            n_kings++;
            continue;
        }

        count[color][piece]++;
        n_pieces++;
    }
    n_pieces += 2;

    if (n_kings != 2) {
        if (Verbose > 1) {
            MyPrintf("Strange number of kings %d in %s\n", n_kings, ending);
            MyFlush();
        }
    }

    return n_pieces;
}

void NormalizeEnding(char *ending) {
    char lending[16];
    int count[2][KING];

    int npieces = ParseEndingName(ending, count);

    int strength_w = 0, strength_b = 0, n_w = 0, n_b = 0;
    for (int i = 0; i < KING; i++) {
        strength_w += count[0][i] * PieceStrengths[i];
        strength_b += count[1][i] * PieceStrengths[i];
        n_w += count[0][i];
        n_b += count[1][i];
    }

    bool reversed = false;
    for (int i = (KING - 1); i >= 0; i--) {
        if (count[0][i] != count[1][i]) {
            if (count[1][i] > count[0][i])
                reversed = true;
            break;
        }
    }

    if (strength_w < strength_b || (strength_w == strength_b && n_w < n_b) ||
        (strength_w == strength_b && n_w == n_b && reversed)) {
        for (int i = 0; i < KING; i++) {
            int tmp = count[0][i];
            count[0][i] = count[1][i];
            count[1][i] = tmp;
        }
        GetEndingName(count, lending);
        strcpy(ending, lending);
    }
}

/* Is the symbol a capturing one?
 * In fact, this is used to recognise any general separator
 * between two parts of a move, e.g.:
 *        Nxc3, e2-e4, etc.
 */

static bool IsCapture(char c) {
    return (c == 'x') || (c == 'X') || (c == ':') || (c == '-');
}

static bool IsCastlingCharacter(char c) {
    return (c == 'O') || (c == '0') || (c == 'o');
}

static bool IsCheck(char c) { return (c == '+') || (c == '#'); }

/* Work out whatever can be gleaned from move_string of
 * the starting and ending points of the given move.
 * The move may be any legal string.
 * The scanning here is libertarian, so it relies heavily on
 * illegal moves having already been filtered out by the process
 * of lexical analysis.
 */

bool DecodeMove(char *move_string, Move *move_details, Move *move_list,
                int nmoves) {
    /* The four components of the co-ordinates when known. */
    int from_row = -1, to_row = -1;
    int from_col = -1, to_col = -1;
    int piece_promoted = NO_PIECE;
    MoveClass mclass;
    bool Ok = true;

    if (nmoves == 0)
        return false;

    /* Temporary locations until known whether they are from_ or to_. */
    int col = -1;
    int row = -1;
    /* A pointer to move along the move string. */
    char *move = move_string;
    int piece_to_move = NO_PIECE;

    /* Make an initial distinction between pawn moves and piece moves. */
    if (IsColumn(*move)) {
        /* Pawn move. */
        mclass = PAWN_MOVE;
        piece_to_move = PAWN;
        col = (*move) - 'a';
        move++;
        if (IsRank(*move)) {
            /* e4, e2e4 */
            row = (*move) - '1';
            move++;
            if (IsCapture(*move)) {
                move++;
            }
            if (IsColumn(*move)) {
                from_col = col;
                from_row = row;
                to_col = (*move) - 'a';
                move++;
                if (IsRank(*move)) {
                    to_row = (*move) - '1';
                    move++;
                }
            } else {
                to_col = col;
                to_row = row;
            }
        } else {
            if (IsCapture(*move)) {
                /* axb */
                move++;
            }
            if (IsColumn(*move)) {
                /* ab, or bg8 for liberal bishop moves. */
                from_col = col;
                to_col = (*move) - 'a';
                move++;
                if (IsRank(*move)) {
                    to_row = (*move) - '1';
                    move++;
                    /* Check the sanity of this. */
                    if ((from_col != 'b' - 'a') && (from_col != (to_col + 1)) &&
                        (from_col != (to_col - 1))) {
                        Ok = false;
                    }
                } else {
                    /* Check the sanity of this. */
                    if ((from_col != (to_col + 1)) &&
                        (from_col != (to_col - 1))) {
                        Ok = false;
                    }
                }
            } else {
                if (Verbose > 1 || CheckSyntaxOnly)
                    MyPrintf("Unknown pawn move %s.\n", move_string);
                Ok = false;
            }
        }
        if (Ok) {
            /* Look for promotions. */
            if (*move == '=') {
                move++;
            }
            piece_promoted = GetPiecePGN(move);
            if (piece_promoted != NO_PIECE) {
                mclass = PAWN_MOVE_WITH_PROMOTION;
                /* @@@ Strictly speaking, if the piece is a RUSSIAN_KING
                 * then we should skip two chars.
                 */
                move++;
            }
        }
    } else if ((piece_to_move = GetPiecePGN(move)) != NO_PIECE) {
        mclass = PIECE_MOVE;
        move++;
        if (IsRank(*move)) {
            /* A disambiguating rank.
             * R1e1, R1xe3.
             */
            from_row = (*move) - '1';
            move++;
            if (IsCapture(*move)) {
                move++;
            }
            if (IsColumn(*move)) {
                to_col = (*move) - 'a';
                move++;
                if (IsRank(*move)) {
                    to_row = (*move) - '1';
                    move++;
                }
            } else {
                Ok = false;
                if (Verbose > 1 || CheckSyntaxOnly)
                    MyPrintf("Unknown piece move %s.\n", move_string);
            }
        } else {
            if (IsCapture(*move)) {
                /* Rxe1 */
                move++;
                if (IsColumn(*move)) {
                    to_col = (*move) - 'a';
                    move++;
                    if (IsRank(*move)) {
                        to_row = (*move) - '1';
                        move++;
                    } else {
                        Ok = false;
                        if (Verbose > 1 || CheckSyntaxOnly)
                            MyPrintf("Unknown piece move %s.\n", move_string);
                    }
                } else {
                    Ok = false;
                    if (Verbose > 1 || CheckSyntaxOnly)
                        MyPrintf("Unknown piece move %s.\n", move_string);
                }
            } else if (IsColumn(*move)) {
                col = (*move) - 'a';
                move++;
                if (IsCapture(*move)) {
                    move++;
                }
                if (IsRank(*move)) {
                    /* Re1, Re1d1, Re1xd1 */
                    row = (*move) - '1';
                    move++;
                    if (IsCapture(*move)) {
                        move++;
                    }
                    if (IsColumn(*move)) {
                        /* Re1d1 */
                        from_col = col;
                        from_row = row;
                        to_col = (*move) - 'a';
                        move++;
                        if (IsRank(*move)) {
                            to_row = (*move) - '1';
                            move++;
                        } else {
                            Ok = false;
                            if (Verbose > 1 || CheckSyntaxOnly)
                                MyPrintf("Unknown piece move %s.\n",
                                         move_string);
                        }
                    } else {
                        to_col = col;
                        to_row = row;
                    }
                } else if (IsColumn(*move)) {
                    /* Rae1 */
                    from_col = col;
                    to_col = (*move) - 'a';
                    move++;
                    if (IsRank(*move)) {
                        to_row = (*move) - '1';
                        move++;
                    }
                } else {
                    Ok = false;
                    if (Verbose > 1 || CheckSyntaxOnly)
                        MyPrintf("Unknown piece move %s.\n", move_string);
                }
            } else {
                Ok = false;
                if (Verbose > 1 || CheckSyntaxOnly)
                    MyPrintf("Unknown piece move %s.\n", move_string);
            }
        }
    } else if (IsCastlingCharacter(*move)) {
        /* Some form of castling. */
        move++;
        /* Allow separators to be optional. */
        if (*move == '-') {
            move++;
        }
        if (IsCastlingCharacter(*move)) {
            move++;
            if (*move == '-') {
                move++;
            }
            if (IsCastlingCharacter(*move)) {
                mclass = QUEENSIDE_CASTLE;
                move++;
            } else {
                mclass = KINGSIDE_CASTLE;
            }
        } else {
            if (Verbose > 1 || CheckSyntaxOnly)
                MyPrintf("Unknown castling move %s.\n", move_string);
            Ok = false;
        }
    } else if (*move == '-' ||
               *move == 'Z') { /* allow possibility of null move -- */
        char prior_move = *move;
        move++;
        if ((prior_move == '-' && *move == '-') ||
            (prior_move == 'Z' && *move == '0')) {
            mclass = NULL_MOVE;
            move++;
        } else {
            if (Verbose > 1 || CheckSyntaxOnly)
                MyPrintf("Unknown null move %s\n", move_string);
            Ok = false;
        }
    } else {
        if (Verbose > 1 || CheckSyntaxOnly)
            MyPrintf("Unknown move %s.\n", move_string);
        Ok = false;
    }
    if (Ok) {
        /* Allow trailing checks. */
        while (IsCheck(*move)) {
            move++;
        }
        if (*move == '\0') {
            /* Nothing more to check. */
        } else if (((strcmp((const char *)move, "ep") == 0) ||
                    (strcmp((const char *)move, "e.p.") == 0)) &&
                   (mclass == PAWN_MOVE)) {
            /* These are ok. */
            mclass = ENPASSANT_PAWN_MOVE;
        } else {
            Ok = false;
            if (Verbose > 1 || CheckSyntaxOnly)
                MyPrintf("Unknown text trailing move %s <%s>.\n", move_string,
                         move);
        }
    }
    /* Store all of the details gathered, even if the move is illegal. */
    if (!Ok) {
        mclass = UNKNOWN_MOVE;
        return false;
    }

    if (mclass == NULL_MOVE) {
        move_details->from = move_details->to = 0;
        return true;
    }

    int nmatch = 0;
    int move_index = 0;
    for (int i = 0; i < nmoves; i++) {
        if (mclass == KINGSIDE_CASTLE) {
            if (move_list[i].flag & (WK_CASTLE | BK_CASTLE)) {
                memcpy(move_details, &move_list[i], sizeof(Move));
                return true;
            }
            continue;
        }

        if (mclass == QUEENSIDE_CASTLE) {
            if (move_list[i].flag & (WQ_CASTLE | BQ_CASTLE)) {
                memcpy(move_details, &move_list[i], sizeof(Move));
                return true;
            }
            continue;
        }

        if (mclass == PAWN_MOVE_WITH_PROMOTION) {
            if (!(move_list[i].flag & PROMOTION))
                continue;
            if (piece_promoted != abs(move_list[i].piece_promoted))
                continue;
        }

        if (piece_to_move != abs(move_list[i].piece_moved))
            continue;

        // Because of chess 960, don't want to match castling because a king
        // move may match it unless the "mclass" is a castling move

        if (move_list[i].flag & (WK_CASTLE | BK_CASTLE)) {
            if (mclass != KINGSIDE_CASTLE) {
                continue;
            }
        }

        if (move_list[i].flag & (WQ_CASTLE | BQ_CASTLE)) {
            if (mclass != QUEENSIDE_CASTLE) {
                continue;
            }
        }

        if (from_col != -1 && from_col != Column(move_list[i].from))
            continue;

        if (from_row != -1 && from_row != Row(move_list[i].from))
            continue;

        if (to_col != -1 && to_col != Column(move_list[i].to))
            continue;

        if (to_row != -1 && to_row != Row(move_list[i].to))
            continue;

        if (mclass == ENPASSANT_PAWN_MOVE && !(move_list[i].flag & EN_PASSANT))
            continue;

        nmatch++;
        move_index = i;
    }

    if (nmatch == 0) {
        if (Verbose > 1 || CheckSyntaxOnly) {
            MyPrintf("Could not find legal move for %s\n", move_string);
        }
        return false;
    }

    if (nmatch > 1) {
        if (Verbose > 1 || CheckSyntaxOnly) {
            MyPrintf("Non-unique move, found %d matches for %s\n", nmatch,
                     move_string);
        }
    }

    memcpy(move_details, &move_list[move_index], sizeof(Move));

    return true;
}

/* See if move_string seems to represent the text of a valid move.
 * Don't print any error messages, just return TRUE or FALSE.
 */
bool MoveSeemsValid(char *move_string) {
    MoveClass mclass;
    bool Ok = true;
    /* A pointer to move along the move string. */
    char *move = move_string;

    /* Make an initial distinction between pawn moves and piece moves. */
    if (IsColumn(*move)) {
        /* Pawn move. */
        mclass = PAWN_MOVE;
        move++;
        if (IsRank(*move)) {
            /* e4, e2e4 */
            move++;
            if (IsCapture(*move)) {
                move++;
            }
            if (IsColumn(*move)) {
                move++;
                if (IsRank(*move)) {
                    move++;
                }
            } else {
            }
        } else {
            if (IsCapture(*move)) {
                /* axb */
                move++;
            }
            if (IsColumn(*move)) {
                /* ab */
                move++;
                if (IsRank(*move)) {
                    move++;
                }
            } else {
                Ok = false;
            }
        }
        if (Ok) {
            /* Look for promotions. */
            if (*move == '=') {
                move++;
            }
            if (GetPiecePGN(move) != NO_PIECE) {
                mclass = PAWN_MOVE_WITH_PROMOTION;
                move++;
            }
        }
    } else if (GetPiecePGN(move) != NO_PIECE) {
        mclass = PIECE_MOVE;
        move++;
        if (IsRank(*move)) {
            /* A disambiguating rank.
             * R1e1, R1xe3.
             */
            move++;
            if (IsCapture(*move)) {
                move++;
            }
            if (IsColumn(*move)) {
                move++;
                if (IsRank(*move)) {
                    move++;
                }
            } else {
                Ok = false;
            }
        } else {
            if (IsCapture(*move)) {
                /* Rxe1 */
                move++;
                if (IsColumn(*move)) {
                    move++;
                    if (IsRank(*move)) {
                        move++;
                    } else {
                        Ok = false;
                    }
                } else {
                    Ok = false;
                }
            } else if (IsColumn(*move)) {
                move++;
                if (IsCapture(*move)) {
                    move++;
                }
                if (IsRank(*move)) {
                    /* Re1, Re1d1, Re1xd1 */
                    move++;
                    if (IsCapture(*move)) {
                        move++;
                    }
                    if (IsColumn(*move)) {
                        /* Re1d1 */
                        move++;
                        if (IsRank(*move)) {
                            move++;
                        } else {
                            Ok = false;
                        }
                    }
                } else if (IsColumn(*move)) {
                    /* Rae1 */
                    move++;
                    if (IsRank(*move)) {
                        move++;
                    } else {
                        Ok = false;
                    }
                } else {
                    Ok = false;
                }
            } else {
                Ok = false;
            }
        }
    } else if (IsCastlingCharacter(*move)) {
        /* Some form of castling. */
        move++;
        /* Allow separators to be optional. */
        if (*move == '-') {
            move++;
        }
        if (IsCastlingCharacter(*move)) {
            move++;
            if (*move == '-') {
                move++;
            }
            if (IsCastlingCharacter(*move)) {
                mclass = QUEENSIDE_CASTLE;
                move++;
            } else {
                mclass = KINGSIDE_CASTLE;
            }
        } else {
            Ok = false;
        }
    } else if (*move == '-' ||
               *move == 'Z') { /* allow possibility of null move -- */
        char prior_move = *move;
        move++;
        if (!((prior_move == '-' && *move == '-') ||
              (prior_move == 'Z' && *move == '0'))) {
            Ok = false;
        }
    } else {
        Ok = false;
    }
    if (Ok) {
        /* Allow trailing checks. */
        while ((*move)) {
            move++;
        }
        if (*move == '\0') {
            /* Nothing more to check. */
        } else if (((strcmp((const char *)move, "ep") == 0) ||
                    (strcmp((const char *)move, "e.p.") == 0)) &&
                   (mclass == PAWN_MOVE)) {
            /* These are ok. */
            mclass = ENPASSANT_PAWN_MOVE;
        } else {
            Ok = false;
        }
    }
    return Ok;
}

/* Remember that 0 can start 0-1 and 0-0.
 * Remember that 1 can start 1-0 and 1/2.
 */
static LinePair GatherPossibleNumeric(char *line, unsigned char *linep,
                                      char initial_digit, ParseType *yylval) {
    LinePair resulting_line;
    PGNToken token = MOVE_NUMBER;
    /* Keep a record of where this token started. */
    const unsigned char *symbol_start = linep - 1;

    if (initial_digit == '0') {
        /* Could be castling or a result. */
        if (strncmp((const char *)linep, "-1", 2) == 0) {
            token = TERMINATING_RESULT;
            strcpy(yylval->string_info, "0-1");
            linep += 2;
        } else if (strncmp((const char *)linep, "-0-0", 4) == 0) {
            token = MOVE;
            strcpy(yylval->string_info, "0-0-0");
            linep += 4;
        } else if (strncmp((const char *)linep, "-0", 2) == 0) {
            token = MOVE;
            strcpy(yylval->string_info, "0-0");
            linep += 2;
        } else {
            /* MOVE_NUMBER */
        }
    } else if (initial_digit == '1') {
        if (strncmp((const char *)linep, "-0", 2) == 0) {
            token = TERMINATING_RESULT;
            strcpy(yylval->string_info, "1-0");
            linep += 2;
        } else if (strncmp((const char *)linep, "/2", 2) == 0) {
            token = TERMINATING_RESULT;
            linep += 2;
            /* Check for the full form. */
            if (strncmp((const char *)linep, "-1/2", 4) == 0) {
                token = TERMINATING_RESULT;
                linep += 4;
            }
            /* Make sure that the full form of the draw result
             * is saved.
             */
            strcpy(yylval->string_info, "1/2-1/2");
        } else {
            /* MOVE_NUMBER */
        }
    } else {
        /* MOVE_NUMBER */
    }
    if (token == MOVE_NUMBER) {
        /* Gather the remaining digits. */
        while (isdigit((unsigned)*linep)) {
            linep++;
        }
    }
    if (token == MOVE_NUMBER) {
        int move_len = linep - symbol_start;
        if (move_len > MAX_PGN_LINE)
            move_len = MAX_PGN_LINE;
        strncpy(yylval->string_info, (const char *)symbol_start, move_len);
        yylval->string_info[move_len] = '\0';
        yylval->move_number = 0;
        (void)sscanf((const char *)yylval->string_info, "%d",
                     &yylval->move_number);
        /* Skip any trailing dots. */
        while (*linep == '.') {
            linep++;
        }
    } else {
        /* TERMINATING_RESULT and MOVE have already been dealt with. */
    }
    resulting_line.line = line;
    resulting_line.linep = linep;
    resulting_line.token = token;
    return resulting_line;
}

/* Identify the next symbol.
 * Don't take any action on EOF -- leave that to next_token.
 */
static PGNToken GetNextToken(FILE *fp, ParseType *yylval) {
    static char *line = NULL;
    static unsigned char *linep = NULL;
    /* The token to be returned. */
    PGNToken token;
    LinePair resulting_line;

    do {
        /* Remember where in line the current symbol starts. */
        const unsigned char *symbol_start;

        /* Clear any remaining symbol. */
        yylval->string_info[0] = '\0';
        if (line == NULL) {
            line = NextInputLine(fp);
            linep = (unsigned char *)line;
            if (line != NULL) {
                token = NO_TOKEN;
            } else {
                token = EOF_TOKEN;
            }
        } else {
            int next_char = *linep & 0x0ff;

            /* Remember where we start. */
            symbol_start = linep;
            linep++;
            token = ChTab[next_char];

            if (token == WHITESPACE) {
                while (ChTab[(unsigned)*linep] == WHITESPACE)
                    linep++;
                token = NO_TOKEN;
            } else if (token == TAG_START) {
                resulting_line = GatherTag(line, linep, fp, &yylval->tag_index);
                /* Pick up where we are now. */
                line = resulting_line.line;
                linep = resulting_line.linep;
                token = resulting_line.token;
            } else if (token == TAG_END) {
                token = NO_TOKEN;
            } else if (token == DOUBLE_QUOTE) {
                resulting_line =
                    GatherSimpleString(line, linep, yylval->string_info);
                /* Pick up where we are now. */
                line = resulting_line.line;
                linep = resulting_line.linep;
                token = resulting_line.token;
            } else if (token == COMMENT_START) {
                resulting_line =
                    GatherComment(line, linep, fp, yylval->string_info);
                /* Pick up where we are now. */
                line = resulting_line.line;
                linep = resulting_line.linep;
                token = resulting_line.token;
            } else if (token == COMMENT_END) {
                /* look for unmatched braces ??? */
                token = NO_TOKEN;
            } else if (token == NAG) {
                int nag_size = 0;
                yylval->string_info[nag_size++] = *symbol_start;
                while (isdigit((unsigned)*linep)) {
                    if (nag_size >= MAX_STRING) {
                        fprintf(stderr, "NAG value too big %s\n", line);
                        exit(1);
                    }
                    yylval->string_info[nag_size++] = *linep;
                    linep++;
                }
                yylval->string_info[nag_size] = '\0';
                if (nag_size == 1) {
                    token = NO_TOKEN;
                }
            } else if (token == ANNOTATE) {
                int annot_size = 0;
                /* Don't return anything in case of error. */
                token = NO_TOKEN;
                yylval->string_info[annot_size++] = *symbol_start;
                while (ChTab[(unsigned)*linep] == ANNOTATE) {
                    if (annot_size > MAX_STRING) {
                        fprintf(stderr, "Annotation too big %s\n", line);
                        exit(1);
                    }
                    yylval->string_info[annot_size++] = *linep;
                    linep++;
                }
                if (yylval->string_info[0] == '!') {
                    if (yylval->string_info[1] == '!')
                        strcpy(yylval->string_info, "$3");
                    else if (yylval->string_info[1] == '?')
                        strcpy(yylval->string_info, "$5");
                    else
                        strcpy(yylval->string_info, "$1");
                    token = NAG;
                } else if (yylval->string_info[0] == '?') {
                    if (yylval->string_info[1] == '!')
                        strcpy(yylval->string_info, "$6");
                    else if (yylval->string_info[1] == '?')
                        strcpy(yylval->string_info, "$4");
                    else
                        strcpy(yylval->string_info, "$2");
                    token = NAG;
                }
            } else if (token == CHECK_SYMBOL) {
                /* Allow ++ */
                int check_size = 0;
                yylval->string_info[check_size++] = *symbol_start;
                while (ChTab[(unsigned)*linep] == CHECK_SYMBOL) {
                    yylval->string_info[check_size++] = *linep;
                    linep++;
                }
                yylval->string_info[check_size] = '\0';
            } else if (token == DOT) {
                while (ChTab[(unsigned)*linep] == DOT) {
                    linep++;
                }
                token = NO_TOKEN;
            } else if (token == PERCENT) {
                /* Trash the rest of the line. */
                line = NextInputLine(fp);
                linep = (unsigned char *)line;
                token = NO_TOKEN;
            } else if (token == ESCAPE) {
                /* @@@ What to do about this? */
                if (*linep != '\0') {
                    linep++;
                }
                token = NO_TOKEN;
            } else if (token == ALPHA) {
                /* Not all ALPHAs are move characters. */
                if (MoveChars[next_char]) {
                    int move_len = 0;
                    /* Scan through the possible move characters. */
                    yylval->string_info[move_len++] = *symbol_start;
                    while (MoveChars[*linep & 0x0ff]) {
                        if (move_len >= MAX_STRING) {
                            fprintf(stderr, "Move length too large %s\n", line);
                            exit(1);
                        }
                        yylval->string_info[move_len++] = *linep;
                        linep++;
                    }
                    yylval->string_info[move_len] = '\0';
                    /* Only classify it as a move if it
                     * seems to be a complete move.
                     */
                    if (MoveSeemsValid(yylval->string_info)) {
                        token = MOVE;
                    } else {
                        if (Verbose > 1 || CheckSyntaxOnly) {
                            MyPrintf("Unknown move text %s.\n",
                                     yylval->string_info);
                        }
                        token = NO_TOKEN;
                    }
                } else {
                    if (!skipping_game) {
                        if (Verbose > 1 || CheckSyntaxOnly)
                            MyPrintf("Unknown character %c (Hex: %x).\n",
                                     next_char, next_char);
                    }
                    /* Skip any sequence of them. */
                    while (ChTab[(unsigned)*linep] == ERROR_TOKEN)
                        linep++;
                }
            } else if (token == DIGIT) {
                /* Remember that 0 can start 0-1 and 0-0.
                 * Remember that 1 can start 1-0 and 1/2.
                 */
                resulting_line =
                    GatherPossibleNumeric(line, linep, next_char, yylval);
                /* Pick up where we are now. */
                line = resulting_line.line;
                linep = resulting_line.linep;
                token = resulting_line.token;
            } else if (token == EOF_TOKEN) {
                /* do nothing */
            } else if (token == RAV_START) {
                /* leave handling of RAV_level to processing */
            } else if (token == RAV_END) {
                if (RAV_level > 0) {
                    /* leave handling of RAV_level to processing */
                } else {
                    if (!skipping_game) {
                        if (Verbose > 1 || CheckSyntaxOnly)
                            MyPrintf("Too many ')' found in game %u.\n",
                                     num_games);
                    }
                    token = NO_TOKEN;
                }
            } else if (token == STAR) {
                strcpy(yylval->string_info, "*");
                token = TERMINATING_RESULT;
            } else if (token == EOS) {
                /* End of the string. */
                line = NextInputLine(fp);
                linep = (unsigned char *)line;
                token = NO_TOKEN;
            } else if (token == ERROR_TOKEN) {
                if (!skipping_game) {
                    if (Verbose > 1 || CheckSyntaxOnly)
                        MyPrintf("Unknown character %c (Hex: %x).\n", next_char,
                                 next_char);
                }
                /* Skip any sequence of them. */
                while (ChTab[(unsigned)*linep] == ERROR_TOKEN)
                    linep++;
            } else if (token == OPERATOR) {
                if (Verbose > 1 || CheckSyntaxOnly)
                    MyPrintf("Operator in illegal context: %c.\n",
                             *symbol_start);
                /* Skip any sequence of them. */
                while (ChTab[(unsigned)*linep] == OPERATOR) {
                    linep++;
                }
                token = NO_TOKEN;
            } else {
                if (!skipping_game) {
                    if (Verbose > 1 || CheckSyntaxOnly)
                        MyPrintf(
                            "Internal error: Missing case for %d on char %x.\n",
                            token, next_char);
                }
                token = NO_TOKEN;
            }
        }
    } while (token == NO_TOKEN);
    return token;
}

static bool SkipToken(PGNToken token) {
    switch (token) {
    case TERMINATING_RESULT:
    case TAG:
    case MOVE:
    case EOF_TOKEN:
        return false;
    default:
        return true;
    }
    return true;
}

PGNToken SkipToNextGame(PGNToken token, FILE *fp, ParseType *yylval) {
    if (SkipToken(token)) {
        skipping_game = true;
        do {
            token = GetNextToken(fp, yylval);
        } while (SkipToken(token));
        skipping_game = false;
    }
    return token;
}

PGNToken ParseGamePrefix(PGNToken symbol, FILE *fp, ParseType *yylval) {
    while (symbol != TAG && symbol != EOF_TOKEN) {
        symbol = GetNextToken(fp, yylval);
        if (symbol == COMMENT && !CheckSyntaxOnly && !PositionDatabase &&
            !PrintBadlyPlayedPositions) {
            if (Verbose > 3)
                MyPrintf("Writing pre-game comment %s\n", yylval->string_info);
            OutputPGNCommentCashed(yylval->string_info);
            FlushPGNOutputCashed();
        }
    }

    if (Verbose > 3)
        MyPrintf("Line number after parsing game prefix: %d\n", NumOutputLines);
    return symbol;
}

typedef struct {
    int piece_type_count[2][KING];
    int kk_index;
    int pawn_file_type;
    int bishop_parity[2];
    ULONG max_num_blocks;
    file fp;
    HEADER header;
    INDEX *offsets;
    BYTE *block;
    ULONG max_block_size;
    int block_index;
} FILE_CACHE;

typedef struct {
    int piece_type_count[2][KING];
    ULONG max_num_blocks, num_blocks;
    int compression_method;
    int max_depth;
    INDEX num_high_dtc;
    file fp, fp_high;
    HEADER header;
    INDEX *offsets;
    BYTE *block;
    HDATA *block_high;
    ULONG max_block_size, block_size;
    int block_index;
} FILE_CACHE_YK;

static FILE_CACHE FileCache[MAX_FILES][2];
static FILE_CACHE_YK FileCacheYK[MAX_FILES_YK][2];

static int num_cached_files[2] = {0, 0};
static int cached_file_lru[MAX_FILES][2];
static int num_cached_files_yk[2] = {0, 0};
static int cached_file_lru_yk[MAX_FILES_YK][2];

typedef struct {
    int piece_type_count[2][KING];
    int kk_index;
    int pawn_file_type;
    int bishop_parity[2];
    ULONG max_num_blocks;
    file fp;
    HEADER header;
    INDEX *offsets;
    ZINDEX *starting_index;
    BYTE *block;
    ULONG max_block_size;
    int block_index;
} FILE_CACHE_HIGH_DTZ;

static FILE_CACHE_HIGH_DTZ FileCacheHighDTZ[MAX_FILES_HIGH_DTZ][2];

static int num_cached_files_high_dtz[2] = {0, 0};
static int cached_file_high_dtz_lru[MAX_FILES_HIGH_DTZ][2];

static BYTE *CompressionBuffer = NULL;
static ULONG CompressionBufferSize = 0;

static void InitCaches() {
    memset(FileCache, 0, sizeof(FileCache));
    memset(FileCacheYK, 0, sizeof(FileCacheYK));
    memset(FileCacheHighDTZ, 0, sizeof(FileCacheHighDTZ));

    for (int i = 0; i < MAX_FILES; i++) {
        FileCache[i][0].fp = FileCache[i][1].fp = NULL;
        FileCache[i][0].offsets = FileCache[i][1].offsets = NULL;
        FileCache[i][0].block = FileCache[i][1].block = NULL;
    }

    for (int i = 0; i < MAX_FILES_YK; i++) {
        FileCacheYK[i][0].fp = FileCacheYK[i][1].fp = NULL;
        FileCacheYK[i][0].fp_high = FileCacheYK[i][1].fp_high = NULL;
        FileCacheYK[i][0].offsets = FileCacheYK[i][1].offsets = NULL;
        FileCacheYK[i][0].block = FileCacheYK[i][1].block = NULL;
        FileCacheYK[i][0].block_high = FileCacheYK[i][1].block_high = NULL;
    }

    for (int i = 0; i < MAX_FILES_HIGH_DTZ; i++) {
        FileCacheHighDTZ[i][0].fp = FileCacheHighDTZ[i][1].fp = NULL;
        FileCacheHighDTZ[i][0].offsets = FileCacheHighDTZ[i][1].offsets = NULL;
        FileCacheHighDTZ[i][0].starting_index =
            FileCacheHighDTZ[i][1].starting_index = NULL;
        FileCacheHighDTZ[i][0].block = FileCacheHighDTZ[i][1].block = NULL;
    }
}

static INDEX MaxCount = (INDEX)(-1);

static char *SymmetryNames[] = {"Identity",
                                "Reflect columns",
                                "Reflect rows",
                                "Rotate 180",
                                "Reflect diag (a1-h8)",
                                "Rotate 90 clockwise",
                                "Rotate 90 anti-clockwise",
                                "Reflect diag (h1-a8)"};

#if (NROWS > NCOLS)
static int RookMoves[NSQUARES][4][NROWS];
#else
static int RookMoves[NSQUARES][4][NCOLS];
#endif

static int BishopMoves[NSQUARES][4][NCOLS];
static int KingMoves[NSQUARES][9];
static int KnightMoves[NSQUARES][9];

static int KnightAttack[NSQUARES * NSQUARES];
static int KingAttack[NSQUARES * NSQUARES];
static int BishopDirection[NSQUARES * NSQUARES];
static int RookDirection[NSQUARES * NSQUARES];

static size_t MyCompressBound(size_t src_size) {
    size_t max_bound = src_size;
#if !defined(NO_ZLIB)
    size_t zlib_bound = compressBound(src_size);
    if (zlib_bound > max_bound)
        max_bound = zlib_bound;
#endif
#if !defined(NO_ZSTD)
    size_t zstd_bound = ZSTD_compressBound(src_size);
    if (zstd_bound > max_bound)
        max_bound = zstd_bound;
#endif
    return max_bound;
}

int MyUncompress(BYTE *dest, ULONG *dest_size, const BYTE *source,
                 ULONG source_size, int method) {
    if (method == NO_COMPRESSION) {
        *dest_size = source_size;
        memcpy(dest, source, source_size);
    }
#if !defined(NO_ZSTD)
    else if (method == ZSTD) {
        size_t destCapacity = *dest_size;
        if (ZSTD_DecompressionContext == NULL)
            destCapacity =
                ZSTD_decompress(dest, destCapacity, source, source_size);
        else
            destCapacity =
                ZSTD_decompressDCtx(ZSTD_DecompressionContext, dest,
                                    destCapacity, source, source_size);
        if (ZSTD_isError(destCapacity)) {
            if (Verbose > 0) {
                MyPrintf("ZSTD decompress error %s\n",
                         ZSTD_getErrorName(destCapacity));
            }
            return COMPRESS_NOT_OK;
        }
        *dest_size = destCapacity;
    }
#endif
#if !defined(NO_ZLIB)
    else if (method == ZLIB) {
        int err_code = uncompress(dest, dest_size, source, source_size);

        if (err_code != Z_OK) {
            if (Verbose > 0) {
                if (err_code == Z_MEM_ERROR)
                    MyPrintf("ZLIB uncompress: not enough memory\n");
                else if (err_code == Z_BUF_ERROR)
                    MyPrintf(
                        "ZLIB uncompress: not enough room in output buffer\n");
                else if (err_code == Z_DATA_ERROR)
                    MyPrintf("ZLIB uncompress: data corrupted or incomplete\n");
                else if (err_code == Z_VERSION_ERROR)
                    MyPrintf("ZLIB uncompress: version error\n");
                else
                    MyPrintf("ZLIB uncompress: error code %d\n", err_code);
                MyFlush();
            }
            return COMPRESS_NOT_OK;
        }
    }
#endif
    else {
        fprintf(stderr, "MyUnompress: unknown de-compression method\n");
        exit(1);
    }

    return COMPRESS_OK;
}

static void FlipBoard(BOARD *Board) {
    int board[NSQUARES];
    int row, col, ep_square = 0, castle = 0;

    memset(board, 0, sizeof(board));

    for (row = 0; row < NROWS; row++) {
        for (col = 0; col < NCOLS; col++) {
            int sq = SquareMake(row, col);
            int sq_y = ReflectH[sq];
            board[sq] = -Board->board[sq_y];
        }
    }

    if (Board->ep_square)
        ep_square = ReflectH[Board->ep_square];

    if (Board->castle & WK_CASTLE)
        castle |= BK_CASTLE;
    if (Board->castle & BK_CASTLE)
        castle |= WK_CASTLE;
    if (Board->castle & WQ_CASTLE)
        castle |= BQ_CASTLE;
    if (Board->castle & BQ_CASTLE)
        castle |= WQ_CASTLE;

    SetBoard(Board, board, OtherSide(Board->side), ep_square, castle,
             Board->half_move, Board->full_move);
}

static void FlipMove(Move *mv) {
    mv->from = ReflectH[mv->from];
    mv->to = ReflectH[mv->to];
    mv->piece_moved = -mv->piece_moved;
    mv->piece_captured = -mv->piece_captured;
    mv->piece_promoted = -mv->piece_promoted;
    if (mv->save_ep_square > 0) {
        mv->save_ep_square = ReflectH[mv->save_ep_square];
    }
    if (mv->flag & WK_CASTLE) {
        mv->flag &= ~(WK_CASTLE);
        mv->flag |= BK_CASTLE;
    } else if (mv->flag & WQ_CASTLE) {
        mv->flag &= ~(WQ_CASTLE);
        mv->flag |= BQ_CASTLE;
    } else if (mv->flag & BK_CASTLE) {
        mv->flag &= ~(BK_CASTLE);
        mv->flag |= WK_CASTLE;
    } else if (mv->flag & BQ_CASTLE) {
        mv->flag &= ~(BQ_CASTLE);
        mv->flag |= WQ_CASTLE;
    }
    if (mv->save_castle) {
        int new_castle = 0;
        if ((mv->save_castle) & WK_CASTLE)
            new_castle |= BK_CASTLE;
        if ((mv->save_castle) & WQ_CASTLE)
            new_castle |= BQ_CASTLE;
        if ((mv->save_castle) & BK_CASTLE)
            new_castle |= WK_CASTLE;
        if ((mv->save_castle) & BQ_CASTLE)
            new_castle |= WQ_CASTLE;
        mv->save_castle = new_castle;
    }
}

static int GetEndingType(int count[2][KING], int *piece_types,
                         int bishop_parity[2], int pawn_file_type) {
    int etype = 0, sub_type = 0;
    int ptypes[MAX_PIECES], npieces = 2, eindex = -1;

    ptypes[0] = KING;
    ptypes[1] = -KING;

    if (pawn_file_type == BP_11_PAWNS || pawn_file_type == OP_11_PAWNS) {
        if (count[WHITE][PAWN] != 1 || count[BLACK][PAWN] != 1)
            return -1;
        npieces = 4;
        ptypes[2] = PAWN;
        ptypes[3] = -PAWN;
        etype = 11;
    } else if (pawn_file_type == OP_21_PAWNS) {
        if (count[WHITE][PAWN] != 2 || count[BLACK][PAWN] != 1)
            return -1;
        npieces = 5;
        ptypes[2] = PAWN;
        ptypes[3] = PAWN;
        ptypes[4] = -PAWN;
        etype = 21;
    } else if (pawn_file_type == OP_12_PAWNS) {
        if (count[WHITE][PAWN] != 1 || count[BLACK][PAWN] != 2)
            return -1;
        npieces = 5;
        ptypes[2] = PAWN;
        ptypes[3] = -PAWN;
        ptypes[4] = -PAWN;
        etype = 12;
    } else if (pawn_file_type == OP_22_PAWNS || pawn_file_type == DP_22_PAWNS) {
        if (count[WHITE][PAWN] != 2 || count[BLACK][PAWN] != 2)
            return -1;
        npieces = 6;
        ptypes[2] = PAWN;
        ptypes[3] = PAWN;
        ptypes[4] = -PAWN;
        ptypes[5] = -PAWN;
        etype = 22;
    } else if (pawn_file_type == OP_31_PAWNS) {
        if (count[WHITE][PAWN] != 3 || count[BLACK][PAWN] != 1)
            return -1;
        npieces = 6;
        ptypes[2] = PAWN;
        ptypes[3] = PAWN;
        ptypes[4] = PAWN;
        ptypes[5] = -PAWN;
        etype = 31;
    } else if (pawn_file_type == OP_13_PAWNS) {
        if (count[WHITE][PAWN] != 1 || count[BLACK][PAWN] != 3)
            return -1;
        npieces = 6;
        ptypes[2] = PAWN;
        ptypes[3] = -PAWN;
        ptypes[4] = -PAWN;
        ptypes[5] = -PAWN;
        etype = 13;
    } else if (pawn_file_type == OP_41_PAWNS) {
        if (count[WHITE][PAWN] != 4 || count[BLACK][PAWN] != 1)
            return -1;
        npieces = 7;
        ptypes[2] = PAWN;
        ptypes[3] = PAWN;
        ptypes[4] = PAWN;
        ptypes[5] = PAWN;
        ptypes[6] = -PAWN;
        etype = 41;
    } else if (pawn_file_type == OP_14_PAWNS) {
        if (count[WHITE][PAWN] != 1 || count[BLACK][PAWN] != 4)
            return -1;
        npieces = 7;
        ptypes[2] = PAWN;
        ptypes[3] = -PAWN;
        ptypes[4] = -PAWN;
        ptypes[5] = -PAWN;
        ptypes[6] = -PAWN;
        etype = 14;
    } else if (pawn_file_type == OP_32_PAWNS) {
        if (count[WHITE][PAWN] != 3 || count[BLACK][PAWN] != 2)
            return -1;
        npieces = 7;
        ptypes[2] = PAWN;
        ptypes[3] = PAWN;
        ptypes[4] = PAWN;
        ptypes[5] = -PAWN;
        ptypes[6] = -PAWN;
        etype = 32;
    } else if (pawn_file_type == OP_23_PAWNS) {
        if (count[WHITE][PAWN] != 2 || count[BLACK][PAWN] != 3)
            return -1;
        npieces = 7;
        ptypes[2] = PAWN;
        ptypes[3] = PAWN;
        ptypes[4] = -PAWN;
        ptypes[5] = -PAWN;
        ptypes[6] = -PAWN;
        etype = 23;
    } else if (pawn_file_type == OP_33_PAWNS) {
        if (count[WHITE][PAWN] != 3 || count[BLACK][PAWN] != 3)
            return -1;
        npieces = 8;
        ptypes[2] = PAWN;
        ptypes[3] = PAWN;
        ptypes[4] = PAWN;
        ptypes[5] = -PAWN;
        ptypes[6] = -PAWN;
        ptypes[7] = -PAWN;
        etype = 33;
    } else if (pawn_file_type == OP_42_PAWNS) {
        if (count[WHITE][PAWN] != 4 || count[BLACK][PAWN] != 2)
            return -1;
        npieces = 8;
        ptypes[2] = PAWN;
        ptypes[3] = PAWN;
        ptypes[4] = PAWN;
        ptypes[5] = PAWN;
        ptypes[6] = -PAWN;
        ptypes[7] = -PAWN;
        etype = 42;
    } else if (pawn_file_type == OP_24_PAWNS) {
        if (count[WHITE][PAWN] != 2 || count[BLACK][PAWN] != 4)
            return -1;
        npieces = 8;
        ptypes[2] = PAWN;
        ptypes[3] = PAWN;
        ptypes[4] = -PAWN;
        ptypes[5] = -PAWN;
        ptypes[6] = -PAWN;
        ptypes[7] = -PAWN;
        etype = 24;
    } else if (pawn_file_type != FREE_PAWNS)
        return -1;

    if (pawn_file_type != FREE_PAWNS) {
        for (int color = WHITE; color <= BLACK; color++) {
            for (int piece = KING - 1; piece >= KNIGHT; piece--) {
                if (count[color][piece] > 0)
                    etype = 10 * etype + count[color][piece];
            }
        }

        for (int color = WHITE; color <= BLACK; color++) {
            for (int piece = KING - 1; piece >= KNIGHT; piece--) {
                for (int i = npieces; i < npieces + count[color][piece]; i++) {
                    ptypes[i] = (color == WHITE) ? piece : -piece;
                }
                npieces += count[color][piece];
            }
        }

        eindex = -1;

        int pawn_file_type_effective = pawn_file_type;
        if (pawn_file_type == OP_41_PAWNS || pawn_file_type == OP_14_PAWNS ||
            pawn_file_type == OP_32_PAWNS || pawn_file_type == OP_23_PAWNS ||
            pawn_file_type == OP_33_PAWNS || pawn_file_type == OP_42_PAWNS ||
            pawn_file_type == OP_24_PAWNS)
            pawn_file_type_effective = FREE_PAWNS;

        for (int i = 0; i < NumIndexTypes; i++) {
            if (IndexTable[i].etype == etype &&
                IndexTable[i].op_type == pawn_file_type_effective) {
                eindex = i;
                break;
            }
        }
    } else {
        npieces = 2;

        for (int color = WHITE; color <= BLACK; color++) {
            if (count[color][PAWN] > 0)
                etype = 10 * etype + count[color][PAWN];
        }

        for (int color = WHITE; color <= BLACK; color++) {
            for (int piece = KING - 1; piece >= KNIGHT; piece--) {
                if (count[color][piece] > 0)
                    etype = 10 * etype + count[color][piece];
            }
        }

        for (int color = WHITE; color <= BLACK; color++) {
            for (int i = npieces; i < npieces + count[color][PAWN]; i++) {
                ptypes[i] = (color == WHITE) ? PAWN : -PAWN;
            }
            npieces += count[color][PAWN];
        }

        for (int color = WHITE; color <= BLACK; color++) {
            for (int piece = KING - 1; piece >= KNIGHT; piece--) {
                for (int i = npieces; i < npieces + count[color][piece]; i++) {
                    ptypes[i] = (color == WHITE) ? piece : -piece;
                }
                npieces += count[color][piece];
            }
        }

        if (bishop_parity[WHITE] != NONE) {
            if (count[WHITE][BISHOP] == 2) {
                int pair_index = 1;
                if (count[WHITE][PAWN] == 2)
                    pair_index++;
                if (count[BLACK][PAWN] == 2)
                    pair_index++;
                for (int piece = KING - 1; piece > BISHOP; piece--) {
                    if (count[WHITE][piece] == 2)
                        pair_index++;
                }
                if (Verbose > 1) {
                    MyPrintf("White bishop pair is doublet number %d\n",
                             pair_index);
                }
                if (bishop_parity[WHITE] == EVEN) {
                    sub_type = 10 * pair_index + 0;
                } else if (bishop_parity[WHITE] == ODD) {
                    sub_type = 10 * pair_index + 1;
                }
            } else if (count[WHITE][BISHOP] == 3) {
                int triplet_index = 1;
                if (count[WHITE][PAWN] == 3)
                    triplet_index++;
                if (count[BLACK][PAWN] == 3)
                    triplet_index++;
                for (int piece = KING - 1; piece > BISHOP; piece--) {
                    if (count[WHITE][piece] == 3)
                        triplet_index++;
                }
                if (Verbose > 1) {
                    MyPrintf("White bishop triplet is triplet number %d\n",
                             triplet_index);
                }
                if (bishop_parity[WHITE] == EVEN) {
                    sub_type = 10 * triplet_index + 0;
                } else if (bishop_parity[WHITE] == ODD) {
                    sub_type = 10 * triplet_index + 1;
                }
            } else {
                fprintf(stderr, "Can only fix white bishop parity if white has "
                                "two or three bishops\n");
                exit(1);
            }
        }

        int sub_type_black = 0;

        if (bishop_parity[BLACK] != NONE) {
            if (count[BLACK][BISHOP] == 2) {
                int pair_index = 1;
                for (int piece = KING - 1; piece >= PAWN; piece--) {
                    if (count[WHITE][piece] == 2)
                        pair_index++;
                }
                if (count[BLACK][PAWN] == 2)
                    pair_index++;

                for (int piece = KING - 1; piece > BISHOP; piece--) {
                    if (count[BLACK][piece] == 2)
                        pair_index++;
                }
                if (Verbose > 1) {
                    MyPrintf("Black bishop pair is doublet number %d\n",
                             pair_index);
                }
                if (bishop_parity[BLACK] == EVEN) {
                    sub_type_black = 10 * pair_index + 0;
                } else if (bishop_parity[BLACK] == ODD) {
                    sub_type_black = 10 * pair_index + 1;
                }
            } else if (count[BLACK][BISHOP] == 3) {
                int triplet_index = 1;
                for (int piece = KING - 1; piece >= PAWN; piece--) {
                    if (count[WHITE][piece] == 3)
                        triplet_index++;
                }
                if (count[BLACK][PAWN] == 3)
                    triplet_index++;

                for (int piece = KING - 1; piece > BISHOP; piece--) {
                    if (count[BLACK][piece] == 3)
                        triplet_index++;
                }
                if (Verbose > 1) {
                    MyPrintf("Black bishop triplet is triplet number %d\n",
                             triplet_index);
                }
                if (bishop_parity[BLACK] == EVEN) {
                    sub_type_black = 10 * triplet_index + 0;
                } else if (bishop_parity[BLACK] == ODD) {
                    sub_type_black = 10 * triplet_index + 1;
                }
            } else {
                fprintf(stderr, "Can only fix black bishop parity if black has "
                                "two or three bishops\n");
                exit(1);
            }
        }

        sub_type = 100 * sub_type + sub_type_black;

        eindex = -1;

        for (int i = 0; i < NumIndexTypes; i++) {
            if (IndexTable[i].etype == etype &&
                IndexTable[i].sub_type == sub_type &&
                IndexTable[i].op_type == FREE_PAWNS) {
                eindex = i;
                break;
            }
        }
    }

    if (piece_types != NULL) {
        memcpy(piece_types, ptypes, npieces * sizeof(piece_types[0]));
    }

    return eindex;
}

static int GetEndingTypeYK(int count[2][KING], int *piece_types) {
    int etype = 0, ptypes[MAX_PIECES], npieces = 2, eindex = -1;

    ptypes[0] = KING;
    ptypes[1] = -KING;

    for (int color = WHITE; color <= BLACK; color++) {
        for (int piece = KING - 1; piece >= PAWN; piece--) {
            if (count[color][piece] > 0)
                etype = 10 * etype + count[color][piece];
        }
    }

    for (int color = WHITE; color <= BLACK; color++) {
        for (int piece = KING - 1; piece >= PAWN; piece--) {
            for (int i = npieces; i < npieces + count[color][piece]; i++) {
                ptypes[i] = (color == WHITE) ? piece : -piece;
            }
            npieces += count[color][piece];
        }
    }

    eindex = -1;

    for (int i = 0; i < NumIndexTypes; i++) {
        if (IndexTable[i].etype == etype && IndexTable[i].sub_type == 0 &&
            IndexTable[i].op_type == FREE_PAWNS) {
            eindex = i;
            break;
        }
    }

    if (piece_types != NULL) {
        memcpy(piece_types, ptypes, npieces * sizeof(piece_types[0]));
    }

    return eindex;
}

bool IsResultChangingMove(int score) {
    if (score == PLUS_EQUAL || score == EQUAL_MINUS || score == PLUS_MINUS ||
        score == PLUS_EQUALMINUS || score == EQUALMINUS_PLUS ||
        score == PLUSEQUAL_MINUS || score == EQUAL_PLUS ||
        score == MINUS_PLUS || score == MINUS_EQUAL ||
        score == MINUS_PLUSEQUAL) {
        return true;
    }
    return false;
}

/*
 * RetrogradeResult for a given score is the score of the best move that could
 * have led to that score. For example, for a score of -n (lost in n),
 * RetrogradeResult is (n + 1), while for a score of n it is -n.
 * RetrogradeResult may be symbolic, for example if only partial information
 * is available such as from a bitbase.
 * Symbolic information WON, LOST, DRAW are also useful when treating a game
 * result as a move as well. For example, if Black resigns after a white move,
 * this can be considered a move by black that leads to a WON position by White.
 * The RetrogradeResult for WON is LOST, implying that the position before Black
 * resigned should have been lost for Black. If the actual score before Black
 * resigned was not lost, for example DRAW, then Black's resignation was an
 * error which dropped half a point.
 */

int RetrogradeResult(int score) {
    if (score == UNKNOWN || score == UNRESOLVED || score == DRAW ||
        score == ILLEGAL)
        return score;
    else if (score == NOT_WON)
        return NOT_LOST;
    else if (score == NOT_LOST)
        return NOT_WON;
    else if (score == LOST)
        return WON;
    else if (score == WON)
        return LOST;
    else if (score == STALE_MATE)
        return DRAW;
    else if (score == HIGH_DTZ_MISSING)
        return HIGH_DTZ_MISSING;

    return (score <= 0) - score;
}

int EvaluateMove(int prev_score, int curr_score, bool phase_change) {
    if (prev_score == UNKNOWN || prev_score == UNRESOLVED ||
        curr_score == UNKNOWN || curr_score == UNRESOLVED)
        return 0;

    int implied_score = RetrogradeResult(curr_score);

    if (prev_score == WON) {
        if (implied_score == DRAW)
            return PLUS_EQUAL;
        if (implied_score == NOT_WON)
            return PLUS_EQUALMINUS;
        if (implied_score == LOST || implied_score <= 0)
            return PLUS_MINUS;
        return 0;
    } else if (prev_score == DRAW) {
        if (implied_score == DRAW || implied_score == NOT_LOST ||
            implied_score == NOT_WON)
            return 0;
        if (implied_score == LOST || implied_score <= 0)
            return EQUAL_MINUS;
        return EQUAL_PLUS;
    } else if (prev_score == NOT_WON) {
        if (implied_score == DRAW || implied_score <= 0 ||
            implied_score == NOT_LOST || implied_score == LOST ||
            implied_score == NOT_WON)
            return 0;
        return EQUALMINUS_PLUS;
    } else if (prev_score == NOT_LOST) {
        if (implied_score == LOST || implied_score <= 0)
            return PLUSEQUAL_MINUS;
        return 0;
    } else if (prev_score == LOST) {
        if (implied_score == LOST || implied_score == NOT_WON ||
            implied_score <= 0)
            return 0;
        if (implied_score == DRAW)
            return MINUS_EQUAL;
        if (implied_score == NOT_LOST)
            return MINUS_PLUSEQUAL;
        return MINUS_PLUS;
    } else if (prev_score > 0) {
        if (implied_score == NOT_LOST || implied_score == WON)
            return 0;
        if (implied_score == DRAW)
            return PLUS_EQUAL;
        if (implied_score == NOT_WON)
            return PLUS_EQUALMINUS;
        if (implied_score == LOST || implied_score <= 0)
            return PLUS_MINUS;
        if (phase_change)
            return 0;
        return implied_score - prev_score;
    } else if (prev_score <= 0) {
        if (implied_score == LOST || implied_score == NOT_WON)
            return 0;
        if (implied_score == DRAW)
            return MINUS_EQUAL;
        if (implied_score == NOT_LOST)
            return MINUS_PLUSEQUAL;
        if (implied_score == WON || implied_score > 0)
            return MINUS_PLUS;
        if (phase_change)
            return 0;
        return prev_score - implied_score;
    }
    return 0;
}

static bool IsWinningScore(int score) {
    if (score == WON)
        return true;

    if (!(score == UNKNOWN || score == DRAW || score == LOST ||
          score == NOT_WON || score == NOT_LOST || score == UNRESOLVED) &&
        score > 0)
        return true;

    return false;
}

static bool IsLosingScore(int score) {
    if (score == LOST)
        return true;

    if (!(score == UNKNOWN || score == DRAW || score == WON ||
          score == NOT_WON || score == NOT_LOST || score == UNRESOLVED) &&
        score <= 0)
        return true;

    return false;
}

static bool IsDrawnScore(int score) {
    if (score == DRAW)
        return true;

    return false;
}

static int ScoreCompare(int score1, int score2) {
    if (score1 == score2)
        return 0;

    if (score1 == UNKNOWN)
        return 1;
    else if (score2 == UNKNOWN)
        return -1;

    if (score1 == WON) {
        if (score2 < 0 || score2 == DRAW || score2 == LOST ||
            score2 == NOT_WON || score2 == STALE_MATE || score2 == CHECK_MATE ||
            score2 == UNRESOLVED)
            return -1;
        return 1;
    }

    if (score1 == NOT_WON) {
        if (score2 < 0 || score2 == LOST)
            return -1;
        return 1;
    }

    if (score1 == DRAW) {
        if (score2 < 0 || score2 == NOT_WON || score2 == LOST)
            return -1;
        return 1;
    }

    if (score1 == NOT_LOST) {
        if (score2 < 0 || score2 == DRAW || score2 == NOT_WON || score2 == LOST)
            return -1;
        return 1;
    }

    if (score1 == LOST) {
        if (score2 < 0)
            return -1;
        return 1;
    }

    if (score2 == NOT_WON || score2 == DRAW || score2 == NOT_LOST ||
        score2 == LOST) {
        if (score1 >= 0)
            return -1;
        return 1;
    }

    if (score2 == WON) {
        if (score1 > 0)
            return -1;
        return 1;
    }

    if (score1 >= 0) {
        if (score2 < 0)
            return -1;
        return (score1 - score2);
    }

    if (score2 >= 0)
        return 1;

    return (score1 - score2);
}

static int MoveCompare(const void *v1, const void *v2) {
    Move *m1 = (Move *)v1;
    Move *m2 = (Move *)v2;
    int score_diff, material1 = 0, material2 = 0;

    /* mate trumps everything */
    if ((m1->flag & MATE) && !(m2->flag & MATE))
        return -1;
    if ((m2->flag & MATE) && !(m1->flag & MATE))
        return 1;

    /* for winning moves, prefer promotions or captures */
    if ((m1->flag & (CAPTURE | PROMOTION)) &&
        !(m2->flag & (CAPTURE | PROMOTION))) {
        if (IsWinningScore(m1->score))
            return -1;
    }
    if ((m2->flag & (CAPTURE | PROMOTION)) &&
        !(m1->flag & (CAPTURE | PROMOTION))) {
        if (IsWinningScore(m2->score))
            return 1;
    }

    score_diff = ScoreCompare(m1->score, m2->score);

    if (score_diff != 0)
        return score_diff;

    if (m1->flag & CAPTURE)
        material1 += abs(m1->piece_captured);

    if (m2->flag & CAPTURE)
        material2 += abs(m2->piece_captured);

    if (m1->flag & PROMOTION)
        material1 += abs(m1->piece_promoted);

    if (m2->flag & PROMOTION)
        material2 += abs(m2->piece_promoted);

    if (material1 > 0 || material2 > 0) {
        if (material1 > material2)
            return -1;
        if (material1 < material2)
            return 1;
    }

    if (abs(m1->piece_moved) < abs(m2->piece_moved))
        return -1;
    if (abs(m1->piece_moved) > abs(m2->piece_moved))
        return 1;

    if (m1->from < m2->from)
        return -1;
    if (m1->from > m2->from)
        return 1;

    if (m1->to < m2->to)
        return -1;
    if (m1->to > m2->to)
        return 1;

    return 0;
}

static void MoveToUCI(Move *mv, char *move) {
    if (mv->flag & WK_CASTLE)
        sprintf(move, "e1g1");
    else if (mv->flag & WQ_CASTLE)
        sprintf(move, "e1c1");
    if (mv->flag & BK_CASTLE)
        sprintf(move, "e8g8");
    else if (mv->flag & BQ_CASTLE)
        sprintf(move, "e8c8");
    else {
        move[0] = 'a' + Column(mv->from);
        move[1] = '1' + Row(mv->from);
        move[2] = 'a' + Column(mv->to);
        move[3] = '1' + Row(mv->to);
        move[4] = '\0';
        if (mv->flag & PROMOTION) {
            move[4] = piece_char(abs(mv->piece_promoted));
            move[5] = '\0';
        }
    }
}

static void MakeMove(BOARD *Board, Move *mv) {
    int i;
    int *board = Board->board;

    mv->save_ep_square = Board->ep_square;
    mv->save_castle = Board->castle;
    mv->save_half_move = Board->half_move;

    if (mv->from == 0 && mv->to == 0) {
        if (Board->side == BLACK)
            Board->full_move++;
        Board->side = OtherSide(Board->side);
        return;
    }

    if (!(mv->flag & (WK_CASTLE | WQ_CASTLE | BK_CASTLE | BQ_CASTLE))) {
        board[mv->from] = 0;
        board[mv->to] = mv->piece_moved;
        if (Verbose > 6) {
            MyPrintf("Setting square %d to 0, %d to %d\n", mv->from, mv->to,
                     mv->piece_moved);
        }
    }

    if (Board->side == WHITE) {
        int *pos = Board->piece_locations[WHITE][mv->piece_moved];
        int *type_count = Board->piece_type_count[WHITE];

        if ((mv->flag & PROMOTION) && mv->piece_promoted != PAWN) {
            board[mv->to] = mv->piece_promoted;
            if (Verbose > 6) {
                MyPrintf("In promotion, setting square %d to %d\n", mv->to,
                         mv->piece_promoted);
            }
            for (i = 0; i < type_count[PAWN]; i++) {
                if (mv->from == pos[i])
                    break;
            }
            type_count[PAWN]--;
            for (; i < type_count[PAWN]; i++) {
                pos[i] = pos[i + 1];
            }
            int *promo_pos = Board->piece_locations[WHITE][mv->piece_promoted];
            promo_pos[type_count[mv->piece_promoted]++] = mv->to;
            Board->strength_w +=
                (PieceStrengths[mv->piece_promoted] - PieceStrengths[PAWN]);
        } else {
            for (i = 0; i < type_count[mv->piece_moved]; i++) {
                if (pos[i] == mv->from) {
                    pos[i] = mv->to;
                    break;
                }
            }
        }

        if (mv->flag & CAPTURE) {
            int cap_sq = mv->to;
            int *cap_pos = Board->piece_locations[BLACK][-mv->piece_captured];
            int *cap_type_count = Board->piece_type_count[BLACK];
            if (mv->flag & EN_PASSANT) {
                if (Board->ep_square != mv->to) {
                    DisplayBoard(Board, "bad e.p. square make");
                } else {
                    cap_sq = SquareMake(Row(cap_sq) - 1, Column(cap_sq));
                    board[cap_sq] = 0;
                    if (Verbose > 6) {
                        MyPrintf("Setting e.p. square %d to 0\n", cap_sq);
                    }
                }
            }
            for (i = 0; i < cap_type_count[-mv->piece_captured]; i++) {
                if (cap_pos[i] == cap_sq)
                    break;
            }
            cap_type_count[-mv->piece_captured]--;
            for (; i < cap_type_count[-mv->piece_captured]; i++) {
                cap_pos[i] = cap_pos[i + 1];
            }
            Board->strength_b -= PieceStrengths[-mv->piece_captured];
            Board->num_pieces--;
            Board->nblack--;
        }

        Board->ep_square = 0;

        if (mv->piece_moved == KING) {
            Board->castle &= ~(WK_CASTLE | WQ_CASTLE);
            Board->wkpos = mv->to;
            if (mv->flag & WK_CASTLE) {
                int *wr_pos = Board->piece_locations[WHITE][ROOK];
                board[mv->from] = 0;
                board[SquareMake(0, grook_orig_col)] = 0;
                board[SquareMake(0, ROOK_GCASTLE_DEST_COL)] = ROOK;
                board[mv->to] = KING;
                if (Verbose > 6) {
                    MyPrintf("WKing side case, square %d = 0, %d = %d,\n",
                             mv->from, mv->to, KING);
                    MyPrintf("  %d = 0, %d = %d\n",
                             SquareMake(0, grook_orig_col),
                             SquareMake(0, ROOK_GCASTLE_DEST_COL), ROOK);
                }
                for (i = 0; i < Board->piece_type_count[WHITE][ROOK]; i++) {
                    if (wr_pos[i] == SquareMake(0, grook_orig_col)) {
                        wr_pos[i] = SquareMake(0, ROOK_GCASTLE_DEST_COL);
                        break;
                    }
                }
            } else if (mv->flag & WQ_CASTLE) {
                int *wr_pos = Board->piece_locations[WHITE][ROOK];
                board[mv->from] = 0;
                board[SquareMake(0, crook_orig_col)] = 0;
                board[SquareMake(0, ROOK_CCASTLE_DEST_COL)] = ROOK;
                board[mv->to] = KING;
                if (Verbose > 6) {
                    MyPrintf("WQueen side case, square %d = 0, %d = %d,\n",
                             mv->from, mv->to, KING);
                    MyPrintf("  %d = 0, %d = %d\n",
                             SquareMake(0, crook_orig_col),
                             SquareMake(0, ROOK_CCASTLE_DEST_COL), ROOK);
                }

                for (i = 0; i < Board->piece_type_count[WHITE][ROOK]; i++) {
                    if (wr_pos[i] == SquareMake(0, crook_orig_col)) {
                        wr_pos[i] = SquareMake(0, ROOK_CCASTLE_DEST_COL);
                        break;
                    }
                }
            }
        } else if (mv->piece_moved == ROOK) {
            if (mv->from == SquareMake(0, grook_orig_col))
                Board->castle &= ~(WK_CASTLE);
            else if (mv->from == SquareMake(0, crook_orig_col))
                Board->castle &= ~(WQ_CASTLE);
        } else if (mv->piece_moved == PAWN) {
            int row_from = Row(mv->from);
            int row_to = Row(mv->to);
            int col_to = Column(mv->to);

            if (row_from == 1 && row_to == 3) {
                if ((col_to >= 1 &&
                     board[SquareMake(3, col_to - 1)] == -PAWN) ||
                    (col_to <= NCOLS - 2 &&
                     board[SquareMake(3, col_to + 1)] == -PAWN))
                    Board->ep_square = SquareMake(2, col_to);
            }
        }

        if (mv->to == SquareMake(NROWS - 1, crook_orig_col))
            Board->castle &= ~(BQ_CASTLE);
        if (mv->to == SquareMake(NROWS - 1, grook_orig_col))
            Board->castle &= ~(BK_CASTLE);
    } else { // black moves
        int *pos = Board->piece_locations[BLACK][-mv->piece_moved];
        int *type_count = Board->piece_type_count[BLACK];

        if ((mv->flag & PROMOTION) && mv->piece_promoted != -PAWN) {
            board[mv->to] = mv->piece_promoted;
            if (Verbose > 6) {
                MyPrintf("In promotion, setting square %d to %d\n", mv->to,
                         mv->piece_promoted);
            }
            for (i = 0; i < type_count[PAWN]; i++) {
                if (mv->from == pos[i])
                    break;
            }
            type_count[PAWN]--;
            for (; i < type_count[PAWN]; i++) {
                pos[i] = pos[i + 1];
            }
            int *promo_pos = Board->piece_locations[BLACK][-mv->piece_promoted];
            promo_pos[type_count[-mv->piece_promoted]++] = mv->to;
            Board->strength_b +=
                (PieceStrengths[-mv->piece_promoted] - PieceStrengths[PAWN]);
        } else {
            for (i = 0; i < type_count[-mv->piece_moved]; i++) {
                if (pos[i] == mv->from) {
                    pos[i] = mv->to;
                    break;
                }
            }
        }

        if (mv->flag & CAPTURE) {
            int cap_sq = mv->to;
            int *cap_pos = Board->piece_locations[WHITE][mv->piece_captured];
            int *cap_type_count = Board->piece_type_count[WHITE];
            if (mv->flag & EN_PASSANT) {
                if (Board->ep_square != mv->to) {
                    DisplayBoard(Board, "bad e.p. capture made");
                } else {
                    cap_sq = SquareMake(Row(cap_sq) + 1, Column(cap_sq));
                    board[cap_sq] = 0;
                    if (Verbose > 6) {
                        MyPrintf("Setting e.p. square %d to 0\n", cap_sq);
                    }
                }
            }
            for (i = 0; i < cap_type_count[mv->piece_captured]; i++) {
                if (cap_pos[i] == cap_sq)
                    break;
            }
            cap_type_count[mv->piece_captured]--;
            for (; i < cap_type_count[mv->piece_captured]; i++) {
                cap_pos[i] = cap_pos[i + 1];
            }
            Board->strength_w -= PieceStrengths[mv->piece_captured];
            Board->num_pieces--;
            Board->nwhite--;
        }

        Board->ep_square = 0;

        if (mv->piece_moved == -KING) {
            Board->castle &= ~(BK_CASTLE | BQ_CASTLE);
            Board->bkpos = mv->to;
            if (mv->flag & BK_CASTLE) {
                int *br_pos = Board->piece_locations[BLACK][ROOK];
                board[mv->from] = 0;
                board[SquareMake(NROWS - 1, grook_orig_col)] = 0;
                board[SquareMake(NROWS - 1, ROOK_GCASTLE_DEST_COL)] = -ROOK;
                board[mv->to] = -KING;
                if (Verbose > 6) {
                    MyPrintf("BKing side castle, square %d = 0, %d = %d,\n",
                             mv->from, mv->to, -KING);
                    MyPrintf("  %d = 0, %d = %d\n",
                             SquareMake(NROWS - 1, grook_orig_col),
                             SquareMake(NROWS - 1, ROOK_GCASTLE_DEST_COL),
                             -ROOK);
                }
                for (i = 0; i < Board->piece_type_count[BLACK][ROOK]; i++) {
                    if (br_pos[i] == SquareMake(NROWS - 1, grook_orig_col)) {
                        br_pos[i] =
                            SquareMake(NROWS - 1, ROOK_GCASTLE_DEST_COL);
                        break;
                    }
                }
            } else if (mv->flag & BQ_CASTLE) {
                int *br_pos = Board->piece_locations[BLACK][ROOK];
                board[mv->from] = 0;
                board[SquareMake(NROWS - 1, crook_orig_col)] = 0;
                board[SquareMake(NROWS - 1, ROOK_CCASTLE_DEST_COL)] = -ROOK;
                board[mv->to] = -KING;
                if (Verbose > 6) {
                    MyPrintf("BQueen side castle, square %d = 0, %d = %d,\n",
                             mv->from, mv->to, -KING);
                    MyPrintf("  %d = 0, %d = %d\n",
                             SquareMake(NROWS - 1, crook_orig_col),
                             SquareMake(NROWS - 1, ROOK_CCASTLE_DEST_COL),
                             -ROOK);
                }
                for (i = 0; i < Board->piece_type_count[BLACK][ROOK]; i++) {
                    if (br_pos[i] == SquareMake(NROWS - 1, crook_orig_col)) {
                        br_pos[i] =
                            SquareMake(NROWS - 1, ROOK_CCASTLE_DEST_COL);
                        break;
                    }
                }
            }
        } else if (mv->piece_moved == -ROOK) {
            if (mv->from == SquareMake(NROWS - 1, grook_orig_col))
                Board->castle &= ~(BK_CASTLE);
            else if (mv->from == SquareMake(NROWS - 1, crook_orig_col))
                Board->castle &= ~(BQ_CASTLE);
        } else if (mv->piece_moved == -PAWN) {
            int row_from = Row(mv->from);
            int row_to = Row(mv->to);
            int col_to = Column(mv->to);

            if (row_from == NROWS - 2 && row_to == NROWS - 4) {
                if ((col_to >= 1 &&
                     board[SquareMake(NROWS - 4, col_to - 1)] == PAWN) ||
                    (col_to <= NCOLS - 2 &&
                     board[SquareMake(NROWS - 4, col_to + 1)] == PAWN))
                    Board->ep_square = SquareMake(NROWS - 3, col_to);
            }
        }

        if (mv->to == SquareMake(0, crook_orig_col))
            Board->castle &= ~(WQ_CASTLE);
        if (mv->to == SquareMake(0, grook_orig_col))
            Board->castle &= ~(WK_CASTLE);

        Board->full_move++;
    }

    Board->side = OtherSide(Board->side);

    if (mv->piece_moved == PAWN || mv->piece_moved == -PAWN ||
        (mv->flag & CAPTURE))
        Board->half_move = 0;
    else
        Board->half_move++;
}

static void MakeMoveUCI(BOARD *Board, char *mv) {
    int *board = Board->board;
    int flag = 0, piece_promoted = -1, piece_captured = -1;

    int from = SquareMake((mv[1] - '1'), (mv[0] - 'a'));
    int to = SquareMake((mv[3] - '1'), (mv[2] - 'a'));

    // null move
    if (from == 0 && to == 0) {
        if (Board->side == BLACK)
            Board->full_move++;
        Board->side = OtherSide(Board->side);
        return;
    }

    int piece_moved = board[from];

    if (strlen(mv) > 4) {
        piece_promoted = GetPiece(mv[4]);
        flag |= PROMOTION;
    }

    if (board[to] != 0) {
        piece_captured = board[to];
        flag |= CAPTURE;
    } else {
        if (abs(piece_moved) == PAWN && mv[0] != mv[2]) {
            piece_captured = -piece_moved;
            flag |= (CAPTURE | EN_PASSANT);
        }
    }

    if (mv[0] == 'e' && mv[1] == '1') {
        if (piece_moved == KING) {
            if (mv[2] == 'g' && mv[3] == '1')
                flag |= WK_CASTLE;
            if (mv[2] == 'c' && mv[3] == '1')
                flag |= WQ_CASTLE;
        }
    }

    if (mv[0] == 'e' && mv[1] == '8') {
        if (piece_moved == -KING) {
            if (mv[2] == 'g' && mv[3] == '8')
                flag |= BK_CASTLE;
            if (mv[2] == 'c' && mv[3] == '8')
                flag |= BQ_CASTLE;
        }
    }

    if (!(flag & (WK_CASTLE | WQ_CASTLE | BK_CASTLE | BQ_CASTLE))) {
        board[from] = 0;
        board[to] = piece_moved;
        if (Verbose > 6) {
            MyPrintf("Setting square %d to 0, %d to %d\n", from, to,
                     piece_moved);
        }
    }

    if (Board->side == WHITE) {
        int *pos = Board->piece_locations[WHITE][piece_moved];
        int *type_count = Board->piece_type_count[WHITE];

        if ((flag & PROMOTION) && piece_promoted != PAWN) {
            board[to] = piece_promoted;
            if (Verbose > 6) {
                MyPrintf("In promotion, setting square %d to %d\n", to,
                         piece_promoted);
            }
            int i;
            for (i = 0; i < type_count[PAWN]; i++) {
                if (from == pos[i])
                    break;
            }
            type_count[PAWN]--;
            for (; i < type_count[PAWN]; i++) {
                pos[i] = pos[i + 1];
            }
            int *promo_pos = Board->piece_locations[WHITE][piece_promoted];
            promo_pos[type_count[piece_promoted]++] = to;
            Board->strength_w +=
                (PieceStrengths[piece_promoted] - PieceStrengths[PAWN]);
        } else {
            for (int i = 0; i < type_count[piece_moved]; i++) {
                if (pos[i] == from) {
                    pos[i] = to;
                    break;
                }
            }
        }

        if (flag & CAPTURE) {
            int cap_sq = to;
            int *cap_pos = Board->piece_locations[BLACK][-piece_captured];
            int *cap_type_count = Board->piece_type_count[BLACK];
            if (flag & EN_PASSANT) {
                if (Board->ep_square != to) {
                    DisplayBoard(Board, "bad e.p. square make");
                } else {
                    cap_sq = SquareMake(Row(cap_sq) - 1, Column(cap_sq));
                    board[cap_sq] = 0;
                    if (Verbose > 6) {
                        MyPrintf("Setting e.p. square %d to 0\n", cap_sq);
                    }
                }
            }
            int i;
            for (i = 0; i < cap_type_count[-piece_captured]; i++) {
                if (cap_pos[i] == cap_sq)
                    break;
            }
            cap_type_count[-piece_captured]--;
            for (; i < cap_type_count[-piece_captured]; i++) {
                cap_pos[i] = cap_pos[i + 1];
            }
            Board->strength_b -= PieceStrengths[-piece_captured];
            Board->num_pieces--;
            Board->nblack--;
        }

        Board->ep_square = 0;

        if (piece_moved == KING) {
            Board->castle &= ~(WK_CASTLE | WQ_CASTLE);
            Board->wkpos = to;
            if (flag & WK_CASTLE) {
                int *wr_pos = Board->piece_locations[WHITE][ROOK];
                board[from] = 0;
                board[SquareMake(0, grook_orig_col)] = 0;
                board[SquareMake(0, ROOK_GCASTLE_DEST_COL)] = ROOK;
                board[to] = KING;
                if (Verbose > 6) {
                    MyPrintf("WKing side case, square %d = 0, %d = %d,\n", from,
                             to, KING);
                    MyPrintf("  %d = 0, %d = %d\n",
                             SquareMake(0, grook_orig_col),
                             SquareMake(0, ROOK_GCASTLE_DEST_COL), ROOK);
                }
                for (int i = 0; i < Board->piece_type_count[WHITE][ROOK]; i++) {
                    if (wr_pos[i] == SquareMake(0, grook_orig_col)) {
                        wr_pos[i] = SquareMake(0, ROOK_GCASTLE_DEST_COL);
                        break;
                    }
                }
            } else if (flag & WQ_CASTLE) {
                int *wr_pos = Board->piece_locations[WHITE][ROOK];
                board[from] = 0;
                board[SquareMake(0, crook_orig_col)] = 0;
                board[SquareMake(0, ROOK_CCASTLE_DEST_COL)] = ROOK;
                board[to] = KING;
                if (Verbose > 6) {
                    MyPrintf("WQueen side case, square %d = 0, %d = %d,\n",
                             from, to, KING);
                    MyPrintf("  %d = 0, %d = %d\n",
                             SquareMake(0, crook_orig_col),
                             SquareMake(0, ROOK_CCASTLE_DEST_COL), ROOK);
                }

                for (int i = 0; i < Board->piece_type_count[WHITE][ROOK]; i++) {
                    if (wr_pos[i] == SquareMake(0, crook_orig_col)) {
                        wr_pos[i] = SquareMake(0, ROOK_CCASTLE_DEST_COL);
                        break;
                    }
                }
            }
        } else if (piece_moved == ROOK) {
            if (from == SquareMake(0, grook_orig_col))
                Board->castle &= ~(WK_CASTLE);
            else if (from == SquareMake(0, crook_orig_col))
                Board->castle &= ~(WQ_CASTLE);
        } else if (piece_moved == PAWN) {
            int row_from = Row(from);
            int row_to = Row(to);
            int col_to = Column(to);

            if (row_from == 1 && row_to == 3) {
                if ((col_to >= 1 &&
                     board[SquareMake(3, col_to - 1)] == -PAWN) ||
                    (col_to <= NCOLS - 2 &&
                     board[SquareMake(3, col_to + 1)] == -PAWN))
                    Board->ep_square = SquareMake(2, col_to);
            }
        }

        if (to == SquareMake(NROWS - 1, crook_orig_col))
            Board->castle &= ~(BQ_CASTLE);
        if (to == SquareMake(NROWS - 1, grook_orig_col))
            Board->castle &= ~(BK_CASTLE);
    } else { // black moves
        int *pos = Board->piece_locations[BLACK][-piece_moved];
        int *type_count = Board->piece_type_count[BLACK];

        if ((flag & PROMOTION) && piece_promoted != -PAWN) {
            board[to] = piece_promoted;
            if (Verbose > 6) {
                MyPrintf("In promotion, setting square %d to %d\n", to,
                         piece_promoted);
            }
            int i;
            for (i = 0; i < type_count[PAWN]; i++) {
                if (from == pos[i])
                    break;
            }
            type_count[PAWN]--;
            for (; i < type_count[PAWN]; i++) {
                pos[i] = pos[i + 1];
            }
            int *promo_pos = Board->piece_locations[BLACK][-piece_promoted];
            promo_pos[type_count[-piece_promoted]++] = to;
            Board->strength_b +=
                (PieceStrengths[-piece_promoted] - PieceStrengths[PAWN]);
        } else {
            for (int i = 0; i < type_count[-piece_moved]; i++) {
                if (pos[i] == from) {
                    pos[i] = to;
                    break;
                }
            }
        }

        if (flag & CAPTURE) {
            int cap_sq = to;
            int *cap_pos = Board->piece_locations[WHITE][piece_captured];
            int *cap_type_count = Board->piece_type_count[WHITE];
            if (flag & EN_PASSANT) {
                if (Board->ep_square != to) {
                    DisplayBoard(Board, "bad e.p. capture made");
                } else {
                    cap_sq = SquareMake(Row(cap_sq) + 1, Column(cap_sq));
                    board[cap_sq] = 0;
                    if (Verbose > 6) {
                        MyPrintf("Setting e.p. square %d to 0\n", cap_sq);
                    }
                }
            }
            int i;
            for (i = 0; i < cap_type_count[piece_captured]; i++) {
                if (cap_pos[i] == cap_sq)
                    break;
            }
            cap_type_count[piece_captured]--;
            for (; i < cap_type_count[piece_captured]; i++) {
                cap_pos[i] = cap_pos[i + 1];
            }
            Board->strength_w -= PieceStrengths[piece_captured];
            Board->num_pieces--;
            Board->nwhite--;
        }

        Board->ep_square = 0;

        if (piece_moved == -KING) {
            Board->castle &= ~(BK_CASTLE | BQ_CASTLE);
            Board->bkpos = to;
            if (flag & BK_CASTLE) {
                int *br_pos = Board->piece_locations[BLACK][ROOK];
                board[from] = 0;
                board[SquareMake(NROWS - 1, grook_orig_col)] = 0;
                board[SquareMake(NROWS - 1, ROOK_GCASTLE_DEST_COL)] = -ROOK;
                board[to] = -KING;
                if (Verbose > 6) {
                    MyPrintf("BKing side castle, square %d = 0, %d = %d,\n",
                             from, to, -KING);
                    MyPrintf("  %d = 0, %d = %d\n",
                             SquareMake(NROWS - 1, grook_orig_col),
                             SquareMake(NROWS - 1, ROOK_GCASTLE_DEST_COL),
                             -ROOK);
                }
                for (int i = 0; i < Board->piece_type_count[BLACK][ROOK]; i++) {
                    if (br_pos[i] == SquareMake(NROWS - 1, grook_orig_col)) {
                        br_pos[i] =
                            SquareMake(NROWS - 1, ROOK_GCASTLE_DEST_COL);
                        break;
                    }
                }
            } else if (flag & BQ_CASTLE) {
                int *br_pos = Board->piece_locations[BLACK][ROOK];
                board[from] = 0;
                board[SquareMake(NROWS - 1, crook_orig_col)] = 0;
                board[SquareMake(NROWS - 1, ROOK_CCASTLE_DEST_COL)] = -ROOK;
                board[to] = -KING;
                if (Verbose > 6) {
                    MyPrintf("BQueen side castle, square %d = 0, %d = %d,\n",
                             from, to, -KING);
                    MyPrintf("  %d = 0, %d = %d\n",
                             SquareMake(NROWS - 1, crook_orig_col),
                             SquareMake(NROWS - 1, ROOK_CCASTLE_DEST_COL),
                             -ROOK);
                }
                for (int i = 0; i < Board->piece_type_count[BLACK][ROOK]; i++) {
                    if (br_pos[i] == SquareMake(NROWS - 1, crook_orig_col)) {
                        br_pos[i] =
                            SquareMake(NROWS - 1, ROOK_CCASTLE_DEST_COL);
                        break;
                    }
                }
            }
        } else if (piece_moved == -ROOK) {
            if (from == SquareMake(NROWS - 1, grook_orig_col))
                Board->castle &= ~(BK_CASTLE);
            else if (from == SquareMake(NROWS - 1, crook_orig_col))
                Board->castle &= ~(BQ_CASTLE);
        } else if (piece_moved == -PAWN) {
            int row_from = Row(from);
            int row_to = Row(to);
            int col_to = Column(to);

            if (row_from == NROWS - 2 && row_to == NROWS - 4) {
                if ((col_to >= 1 &&
                     board[SquareMake(NROWS - 4, col_to - 1)] == PAWN) ||
                    (col_to <= NCOLS - 2 &&
                     board[SquareMake(NROWS - 4, col_to + 1)] == PAWN))
                    Board->ep_square = SquareMake(NROWS - 3, col_to);
            }
        }

        if (to == SquareMake(0, crook_orig_col))
            Board->castle &= ~(WQ_CASTLE);
        if (to == SquareMake(0, grook_orig_col))
            Board->castle &= ~(WK_CASTLE);

        Board->full_move++;
    }

    Board->side = OtherSide(Board->side);

    if (piece_moved == PAWN || piece_moved == -PAWN || (flag & CAPTURE))
        Board->half_move = 0;
    else
        Board->half_move++;
}

static void UnMakeMove(BOARD *Board, Move *mv) {
    int i;
    int *board = Board->board;

    Board->side = OtherSide(Board->side);
    Board->ep_square = mv->save_ep_square;

    if (mv->from == 0 && mv->to == 0) {
        if (Board->side == BLACK)
            Board->full_move--;
        Board->castle = mv->save_castle;
        Board->half_move = mv->save_half_move;
        return;
    }

    if (!(mv->flag & (WK_CASTLE | WQ_CASTLE | BK_CASTLE | BQ_CASTLE))) {
        board[mv->to] = 0;
        board[mv->from] = mv->piece_moved;
    }

    if (Board->side == WHITE) {
        int *pos = Board->piece_locations[WHITE][mv->piece_moved];
        int *type_count = Board->piece_type_count[WHITE];
        if ((mv->flag & PROMOTION) && mv->piece_promoted != PAWN) {
            int *promo_pos = Board->piece_locations[WHITE][mv->piece_promoted];
            board[mv->from] = PAWN;
            pos[type_count[PAWN]++] = mv->from;
            for (i = 0; i < type_count[mv->piece_promoted]; i++) {
                if (mv->to == promo_pos[i])
                    break;
            }
            type_count[mv->piece_promoted]--;
            for (; i < type_count[mv->piece_promoted]; i++) {
                promo_pos[i] = promo_pos[i + 1];
            }
            Board->strength_w -=
                (PieceStrengths[mv->piece_promoted] - PieceStrengths[PAWN]);
        } else {
            for (i = 0; i < type_count[mv->piece_moved]; i++) {
                if (pos[i] == mv->to) {
                    pos[i] = mv->from;
                    break;
                }
            }
        }

        if (mv->flag & CAPTURE) {
            int cap_sq = mv->to;
            int *cap_pos = Board->piece_locations[BLACK][-mv->piece_captured];
            int *cap_type_count = Board->piece_type_count[BLACK];
            if (mv->flag & EN_PASSANT) {
                if (Board->ep_square != mv->to) {
                    DisplayBoard(Board, "bad e.p. capture in unmake");
                } else {
                    cap_sq = SquareMake(Row(cap_sq) - 1, Column(cap_sq));
                    board[cap_sq] = -PAWN;
                }
            } else {
                board[mv->to] = mv->piece_captured;
            }
            cap_pos[cap_type_count[-mv->piece_captured]++] = cap_sq;
            Board->strength_b += PieceStrengths[-mv->piece_captured];
            Board->nblack++;
            Board->num_pieces++;
        }

        if (mv->piece_moved == KING) {
            Board->wkpos = mv->from;
            if (mv->flag & WK_CASTLE) {
                int *wr_pos = Board->piece_locations[WHITE][ROOK];
                board[mv->to] = 0;
                board[SquareMake(0, ROOK_GCASTLE_DEST_COL)] = 0;
                board[SquareMake(0, grook_orig_col)] = ROOK;
                board[mv->from] = KING;
                for (i = 0; i < Board->piece_type_count[WHITE][ROOK]; i++) {
                    if (wr_pos[i] == SquareMake(0, ROOK_GCASTLE_DEST_COL)) {
                        wr_pos[i] = SquareMake(0, grook_orig_col);
                        break;
                    }
                }
            } else if (mv->flag & WQ_CASTLE) {
                int *wr_pos = Board->piece_locations[WHITE][ROOK];
                board[mv->to] = 0;
                board[SquareMake(0, ROOK_CCASTLE_DEST_COL)] = 0;
                board[SquareMake(0, crook_orig_col)] = ROOK;
                board[mv->from] = KING;
                for (i = 0; i < Board->piece_type_count[WHITE][ROOK]; i++) {
                    if (wr_pos[i] == SquareMake(0, ROOK_CCASTLE_DEST_COL)) {
                        wr_pos[i] = SquareMake(0, crook_orig_col);
                        break;
                    }
                }
            }
        }
    } else { // black moves
        int *pos = Board->piece_locations[BLACK][-mv->piece_moved];
        int *type_count = Board->piece_type_count[BLACK];
        if ((mv->flag & PROMOTION) && mv->piece_promoted != -PAWN) {
            int *promo_pos = Board->piece_locations[BLACK][-mv->piece_promoted];
            board[mv->from] = -PAWN;
            pos[type_count[PAWN]++] = mv->from;
            for (i = 0; i < type_count[-mv->piece_promoted]; i++) {
                if (mv->to == promo_pos[i])
                    break;
            }
            type_count[-mv->piece_promoted]--;
            for (; i < type_count[-mv->piece_promoted]; i++) {
                promo_pos[i] = promo_pos[i + 1];
            }
            Board->strength_b -=
                (PieceStrengths[-mv->piece_promoted] - PieceStrengths[PAWN]);
        } else {
            for (i = 0; i < type_count[-mv->piece_moved]; i++) {
                if (pos[i] == mv->to) {
                    pos[i] = mv->from;
                    break;
                }
            }
        }

        if (mv->flag & CAPTURE) {
            int cap_sq = mv->to;
            int *cap_pos = Board->piece_locations[WHITE][mv->piece_captured];
            int *cap_type_count = Board->piece_type_count[WHITE];
            if (mv->flag & EN_PASSANT) {
                if (Board->ep_square != mv->to) {
                    DisplayBoard(Board, "bad e.p. capture unmake");
                } else {
                    cap_sq = SquareMake(Row(cap_sq) + 1, Column(cap_sq));
                    board[cap_sq] = PAWN;
                }
            } else {
                board[mv->to] = mv->piece_captured;
            }
            cap_pos[cap_type_count[mv->piece_captured]++] = cap_sq;
            Board->strength_w += PieceStrengths[mv->piece_captured];
            Board->nwhite++;
            Board->num_pieces++;
        }

        if (mv->piece_moved == -KING) {
            Board->bkpos = mv->from;
            if (mv->flag & BK_CASTLE) {
                int *br_pos = Board->piece_locations[BLACK][ROOK];
                board[mv->to] = 0;
                board[SquareMake(NROWS - 1, ROOK_GCASTLE_DEST_COL)] = 0;
                board[SquareMake(NROWS - 1, grook_orig_col)] = -ROOK;
                board[mv->from] = -KING;
                for (i = 0; i < Board->piece_type_count[BLACK][ROOK]; i++) {
                    if (br_pos[i] ==
                        SquareMake(NROWS - 1, ROOK_GCASTLE_DEST_COL)) {
                        br_pos[i] = SquareMake(NROWS - 1, grook_orig_col);
                        break;
                    }
                }
            } else if (mv->flag & BQ_CASTLE) {
                int *br_pos = Board->piece_locations[BLACK][ROOK];
                board[mv->to] = 0;
                board[SquareMake(NROWS - 1, ROOK_CCASTLE_DEST_COL)] = 0;
                board[SquareMake(NROWS - 1, crook_orig_col)] = -ROOK;
                board[mv->from] = -KING;
                for (i = 0; i < Board->piece_type_count[BLACK][ROOK]; i++) {
                    if (br_pos[i] ==
                        SquareMake(NROWS - 1, ROOK_CCASTLE_DEST_COL)) {
                        br_pos[i] = SquareMake(NROWS - 1, crook_orig_col);
                        break;
                    }
                }
            }
        }
        Board->full_move--;
    }

    Board->castle = mv->save_castle;
    Board->half_move = mv->save_half_move;
}

static int GetMoveStringBleicher(Move *mv, char *str) {
    int from = mv->from;
    int to = mv->to;

    if (mv->flag & PROMOTION) {
        if (mv->piece_promoted == KNIGHT)
            to += 512;
        if (mv->piece_promoted == BISHOP)
            to += 256;
        if (mv->piece_promoted == ROOK)
            to += 128;
        if (mv->piece_promoted == QUEEN)
            to += 64;
    }

    sprintf(str, "%dx%d", from, to);
    return 0;
}

static int GetMoveString(Move *mv, char *str, bool simple) {
    char p = GermanKnight ? eg_piece_char(abs(mv->piece_moved))
                          : piece_char(abs(mv->piece_moved));
    p = islower(p) ? toupper(p) : p;
    if (p == 'P')
        p = ' ';

    if ((mv->flag & WK_CASTLE) || (mv->flag & BK_CASTLE)) {
        sprintf(str, "O-O");
    } else if ((mv->flag & WQ_CASTLE) || (mv->flag & BQ_CASTLE)) {
        sprintf(str, "O-O-O");
    } else {
        sprintf(str, "%c%c%d%c%c%d", p, 'a' + Column(mv->from),
                1 + Row(mv->from), (mv->flag & CAPTURE) ? 'x' : '-',
                'a' + Column(mv->to), 1 + Row(mv->to));
        if (mv->flag & PROMOTION) {
            char prom[2];
            if (abs(mv->piece_promoted) == PAWN)
                p = '?';
            else
                p = piece_char(abs(mv->piece_promoted));
            sprintf(prom, "%1c", islower(p) ? toupper(p) : p);
            strcat(str, prom);
        }
    }

    if ((mv->flag & CAPTURE) && (mv->flag & EN_PASSANT))
        strcat(str, "ep");
    if (!simple) {
        if (mv->flag & MATE)
            strcat(str, "#");
        else if (mv->flag & CHECK)
            strcat(str, "+");
        if (mv->flag & BEST)
            strcat(str, "!");
        if (mv->flag & UNIQUE)
            strcat(str, "!");
    }
    return strlen(str);
}

static int GetShortMoveString(Move *mv, Move *mv_list, int nmoves, char *str) {
    int i, len = 0;
    char p;
    bool ambig = false, col_ok = true, row_ok = true;

    if (mv->from == 0 && mv->to == 0) {
        str[len++] = '-';
        str[len++] = '-';
        str[len++] = '\0';
        return len;
    }

    for (i = 0; i < nmoves; i++) {
        if ((mv->flag & PROMOTION) && (abs(mv->piece_promoted) == PAWN))
            continue;
        if (mv->from == mv_list[i].from && mv->to == mv_list[i].to)
            continue;
        if (mv->piece_moved == mv_list[i].piece_moved &&
            mv->to == mv_list[i].to) {
            ambig = true;
            if (Column(mv->from) == Column(mv_list[i].from))
                col_ok = false;
            if (Row(mv->from) == Row(mv_list[i].from))
                row_ok = false;
        }
    }

    if ((mv->flag & WK_CASTLE) || (mv->flag & BK_CASTLE)) {
        str[len++] = 'O';
        str[len++] = '-';
        str[len++] = 'O';
    } else if ((mv->flag & WQ_CASTLE) || (mv->flag & BQ_CASTLE)) {
        str[len++] = 'O';
        str[len++] = '-';
        str[len++] = 'O';
        str[len++] = '-';
        str[len++] = 'O';
    } else if (abs(mv->piece_moved) != PAWN) {

        p = GermanKnight ? eg_piece_char(abs(mv->piece_moved))
                         : piece_char(abs(mv->piece_moved));

        p = islower(p) ? toupper(p) : p;

        str[len++] = p;

        if (ambig) {
            if (col_ok || !row_ok)
                str[len++] = 'a' + Column(mv->from);
            if (!col_ok) {
                sprintf(&str[len], "%d", 1 + Row(mv->from));
                len = strlen(str);
            }
        }
    } else if (mv->flag & CAPTURE)
        str[len++] = 'a' + Column(mv->from);

    if (mv->flag & CAPTURE)
        str[len++] = 'x';

    if ((mv->flag & (WK_CASTLE | BK_CASTLE | WQ_CASTLE | BQ_CASTLE)) == 0) {
        str[len++] = 'a' + Column(mv->to);

        sprintf(&str[len], "%d", 1 + Row(mv->to));
        len = strlen(str);
    }

    if (mv->flag & PROMOTION) {
        str[len++] = '=';
        str[len++] = PIECE_CHAR(abs(mv->piece_promoted));
    }

    if (mv->flag & MATE)
        str[len++] = '#';
    else if (mv->flag & CHECK)
        str[len++] = '+';

    if ((mv->flag & BEST) || (mv->flag & UNIQUE)) {
        if (StrictPGN) {
            str[len++] = ' ';
            str[len++] = '$';
            if ((mv->flag & UNIQUE) && (mv->flag & BEST))
                str[len++] = '3';
            else
                str[len++] = '1';
        } else {
            if (mv->flag & BEST)
                str[len++] = '!';
            if (mv->flag & UNIQUE)
                str[len++] = '!';
        }
    }

    str[len] = '\0';

    return len;
}

static int CastleRights(int *board, int proposed_castle) {
    int castle = 0;

    if ((proposed_castle & WK_CASTLE) || (proposed_castle & WQ_CASTLE)) {
        bool wk_castle = false;
        int wkcol = -1, i;
        for (i = 1; i < (NCOLS - 1); i++) {
            if (board[SquareMake(0, i)] == KING) {
                wkcol = i;
                break;
            }
        }
        if (wkcol != -1) {
            if (Chess960) {
                king_orig_col = wkcol;
                wk_castle = true;
            } else {
                wk_castle = (wkcol == KING_ORIG_COL_TRADITIONAL);
                if (wk_castle)
                    king_orig_col = wkcol;
            }
        }

        if (wk_castle) {
            if (proposed_castle & WK_CASTLE) {
                int wrcol = -1;
                int nrooks = 0;
                for (i = (NCOLS - 1); i > wkcol; i--) {
                    if (board[SquareMake(0, i)] == ROOK) {
                        if (wrcol == -1)
                            wrcol = i;
                        nrooks++;
                    }
                }
                if (wrcol != -1) {
                    if (Chess960) {
                        grook_orig_col = wrcol;
                        if (nrooks > 1) {
                            if (Verbose > 1) {
                                printf("Warning: more than one rook availble "
                                       "for white k-side castling\n");
                            }
                        }
                        castle |= WK_CASTLE;
                    } else {
                        if (wrcol == GROOK_ORIG_COL_TRADITIONAL) {
                            castle |= WK_CASTLE;
                            grook_orig_col = GROOK_ORIG_COL_TRADITIONAL;
                        }
                    }
                }
            }

            if (proposed_castle & WQ_CASTLE) {
                int wrcol = -1;
                int nrooks = 0;
                for (i = 0; i < wkcol; i++) {
                    if (board[SquareMake(0, i)] == ROOK) {
                        if (wrcol == -1)
                            wrcol = i;
                        nrooks++;
                    }
                }
                if (wrcol != -1) {
                    if (Chess960) {
                        crook_orig_col = wrcol;
                        if (nrooks > 1) {
                            if (Verbose > 1) {
                                printf("Warning: more than one rook availble "
                                       "for white q-side castling\n");
                            }
                        }
                        castle |= WQ_CASTLE;
                    } else {
                        if (wrcol == CROOK_ORIG_COL_TRADITIONAL) {
                            castle |= WQ_CASTLE;
                            crook_orig_col = CROOK_ORIG_COL_TRADITIONAL;
                        }
                    }
                }
            }
        }
    }

    if ((proposed_castle & BK_CASTLE) || (proposed_castle & BQ_CASTLE)) {
        bool bk_castle = false;
        int bkcol = -1, i;
        if ((castle & WK_CASTLE) || (castle & WQ_CASTLE)) {
            if (board[SquareMake(NROWS - 1, king_orig_col)] == -KING) {
                bkcol = king_orig_col;
            }
        } else {
            for (i = 1; i < (NCOLS - 1); i++) {
                if (board[SquareMake(NROWS - 1, i)] == -KING) {
                    bkcol = i;
                    break;
                }
            }
        }
        if (bkcol != -1) {
            if (Chess960) {
                king_orig_col = bkcol;
                bk_castle = true;
            } else {
                bk_castle = (bkcol == KING_ORIG_COL_TRADITIONAL);
                if (bk_castle)
                    king_orig_col = KING_ORIG_COL_TRADITIONAL;
            }
        }

        if (bk_castle) {
            if (proposed_castle & BK_CASTLE) {
                int brcol = -1;
                int nrooks = 0;
                if (castle & WK_CASTLE) {
                    if (board[SquareMake(NROWS - 1, grook_orig_col)] == -ROOK)
                        brcol = grook_orig_col;
                } else {
                    for (i = (NCOLS - 1); i > bkcol; i--) {
                        if (board[SquareMake(NROWS - 1, i)] == -ROOK) {
                            if (brcol == -1)
                                brcol = i;
                            nrooks++;
                        }
                    }
                }
                if (brcol != -1) {
                    if (Chess960) {
                        grook_orig_col = brcol;
                        if (nrooks > 1) {
                            if (Verbose > 1) {
                                printf("Warning: more than one rook availble "
                                       "for black k-side castling\n");
                            }
                        }
                        castle |= BK_CASTLE;
                    } else {
                        if (brcol == GROOK_ORIG_COL_TRADITIONAL) {
                            castle |= BK_CASTLE;
                            grook_orig_col = GROOK_ORIG_COL_TRADITIONAL;
                        }
                    }
                }
            }

            if (proposed_castle & BQ_CASTLE) {
                int brcol = -1;
                int nrooks = 0;
                if (castle & WQ_CASTLE) {
                    if (board[SquareMake(NROWS - 1, crook_orig_col)] == -ROOK)
                        brcol = crook_orig_col;
                } else {
                    for (i = 0; i < bkcol; i++) {
                        if (board[SquareMake(NROWS - 1, i)] == -ROOK) {
                            if (brcol == -1)
                                brcol = i;
                            nrooks++;
                        }
                    }
                }
                if (brcol != -1) {
                    if (Chess960) {
                        crook_orig_col = brcol;
                        if (nrooks > 1) {
                            if (Verbose > 1) {
                                printf("Warning: more than one rook availble "
                                       "for black q-side castling\n");
                            }
                        }
                        castle |= BQ_CASTLE;
                    } else {
                        if (brcol == CROOK_ORIG_COL_TRADITIONAL) {
                            castle |= BQ_CASTLE;
                            crook_orig_col = CROOK_ORIG_COL_TRADITIONAL;
                        }
                    }
                }
            }
        }
    }

    return castle;
}

static int ReadPositionData(char *line, POSITION_DATA *pos_data) {
    char ending[32];
    int kk_index;
    INDEX offset;
    char side, result;
    int game_num, move_no;
    int score, zz_type = UNKNOWN;
    char cz_type = '0';
    char score_string[16], zz_string[16], cz_string[16];
    int nwords = sscanf(line, "%s %d " DEC_INDEX_FORMAT " %c %d %d %c %s %s %s",
                        ending, &kk_index, &offset, &side, &game_num, &move_no,
                        &result, score_string, zz_string, cz_string);
    if (line[0] == '#' || nwords == 0)
        return 0;

    if (nwords >= 1) {
        char *upromo = NULL;
        if ((upromo = strchr(ending, '_')) != NULL) {
            int promos = 0;
            for (int i = 0; i < strlen(upromo); i++) {
                if (upromo[i] == 'q' || upromo[i] == 'Q')
                    promos |= (1 << (QUEEN));
                else if (upromo[i] == 'r' || upromo[i] == 'R')
                    promos |= (1 << (ROOK));
                else if (upromo[i] == 'b' || upromo[i] == 'B')
                    promos |= (1 << (BISHOP));
                else if (upromo[i] == 'n' || upromo[i] == 'N')
                    promos |= (1 << (KNIGHT));
            }
            pos_data->promos = promos;
            *upromo = '\0';
        } else
            pos_data->promos = QRBN_PROMOTIONS;
        strncpy(pos_data->ending, ending, sizeof(pos_data->ending));
    }
    if (nwords >= 2)
        pos_data->kk_index = kk_index;
    if (nwords >= 3)
        pos_data->offset = offset;
    if (nwords >= 4) {
        pos_data->side = NEUTRAL;
        if (side == 'w' || side == 'W') {
            pos_data->side = WHITE;
        } else if (side == 'b' || side == 'B') {
            pos_data->side = BLACK;
        }
    }
    if (nwords >= 5)
        pos_data->game_num = game_num;
    if (nwords >= 6)
        pos_data->move_no = move_no;
    if (nwords >= 7) {
        pos_data->result = UNKNOWN;
        if (result == 'w' || result == 'W') {
            pos_data->result = WON;
        } else if (result == 'd' || result == 'D') {
            pos_data->result = DRAW;
        } else if (result == 'l' || result == 'L') {
            pos_data->result = LOST;
        }
    }
    if (nwords >= 8) {
        pos_data->score = ParseScore(score_string);
        pos_data->zz_type = UNKNOWN;
        pos_data->cz_type = '0';
    }
    if (nwords >= 9)
        pos_data->zz_type = ParseZZType(zz_string);
    if (nwords >= 10) {
        pos_data->cz_type = cz_string[0];
    }

    return nwords;
}

static void WritePositionData(POSITION_DATA *pos_data, int nwords) {
    if (nwords < 1)
        return;

    if (nwords >= 1) {
        printf("%s", pos_data->ending);
        if (pos_data->promos != QRBN_PROMOTIONS && pos_data->promos != 0) {
            printf("_");
            if (pos_data->promos & (1 << QUEEN))
                printf("q");
            if (pos_data->promos & (1 << ROOK))
                printf("r");
            if (pos_data->promos & (1 << BISHOP))
                printf("b");
            if (pos_data->promos & (1 << KNIGHT))
                printf("n");
        }
    }
    if (nwords >= 2)
        printf(" %d", pos_data->kk_index);
    if (nwords >= 3)
        printf(" " DEC_INDEX_FORMAT, pos_data->offset);
    if (nwords >= 4) {
        if (pos_data->side == WHITE)
            printf(" w");
        else if (pos_data->side == BLACK)
            printf(" b");
        else
            printf(" u");
    }
    if (nwords >= 5) {
        printf(" %d", pos_data->game_num);
    }
    if (nwords >= 6) {
        printf(" %d", pos_data->move_no);
    }
    if (nwords >= 7) {
        if (pos_data->result == WON)
            printf(" w");
        else if (pos_data->result == LOST)
            printf(" l");
        else if (pos_data->result == DRAW)
            printf(" d");
        else
            printf(" u");
    }
    if (nwords >= 8) {
        char score_string[16];
        ScoreToString(pos_data->score, score_string);
        printf(" %s", score_string);
    }
    if (nwords >= 9) {
        char zz_string[16];
        ZZTypeToString(pos_data->zz_type, zz_string);
        printf(" %s", zz_string);
    }
    if (nwords >= 10) {
        printf(" %c", pos_data->cz_type);
    }

    printf("\n");
}

static int ScanPosition(char *pos_string, int *board, int *ep_square,
                        int *castle, char *title) {
    int i = 0, side, row, col, pi, sq, n, wk, bk;
    char ccol, ptype, *pptr;

    memset(board, 0, NSQUARES * sizeof(board[0]));
    *ep_square = 0;
    *castle = 0;

    while (pos_string[i] != '\0') {
        switch (pos_string[i]) {
        case ' ':
        case '\t':
        case '\n':
        case '\r':
            i++;
            break;
        case '"':
            pptr = &pos_string[i];
            i++;
            if (title != NULL) {
                pptr++;
                while (*pptr != '"' && *pptr != '\0') {
                    *title++ = *pptr++;
                    i++;
                }
                *title = '\0';
                i++;
            }
            break;
        case 'b':
            side = BLACK;
            goto scan_piece;
        case 'w':
            side = WHITE;
        scan_piece:
            n = sscanf(&pos_string[i + 1], "%c%c%d", &ptype, &ccol, &row);
            if (n != 3) {
                i++;
                break;
            }
            col = (int)(ccol - 'a');
            row--;
            if (col < 0 || col >= NCOLS || row < 0 || row >= NROWS) {
                if (Verbose > 1) {
                    MyPrintf("Invalid coordinate in %s\n", &pos_string[i]);
                }
                return NEUTRAL;
            }
            sq = SquareMake(row, col);
            pi = GetPiece(ptype);
            if (pi == -1) {
                if (Verbose > 1) {
                    MyPrintf("Invalid piece %c\n", ptype);
                }
                return NEUTRAL;
            }
            i += (4 + (row >= 9));
            if (side == WHITE)
                board[sq] = pi;
            else
                board[sq] = -pi;
            break;
        case 'e':
            n = sscanf(&pos_string[i + 1], "%c%d", &ccol, &row);
            if (n != 2) {
                if (Verbose > 1) {
                    MyPrintf("Could not read e.p. from %s\n", &pos_string[i]);
                }
                return NEUTRAL;
            }
            col = (int)(ccol - 'a');
            row--;
            if (col < 0 || col >= NCOLS || row < 0 || row >= NROWS) {
                if (Verbose > 1) {
                    MyPrintf("e.p. square %c%d out of range\n", ccol, 1 + row);
                }
                return NEUTRAL;
            }
            *ep_square = SquareMake(row, col);
            i += (3 + (row >= 9));
            break;
        case 'K':
        case 'Q':
        case 'k':
        case 'q':
            char castle_str[8];
            sscanf(&pos_string[i], "%s", castle_str);
            if (strchr(castle_str, 'K'))
                *castle |= WK_CASTLE;
            if (strchr(castle_str, 'Q'))
                *castle |= WQ_CASTLE;
            if (strchr(castle_str, 'k'))
                *castle |= BK_CASTLE;
            if (strchr(castle_str, 'q'))
                *castle |= BQ_CASTLE;
            i += strlen(castle_str);
            break;
        default:
            if (Verbose > 1) {
                MyPrintf("Invalid character %c in position string\n",
                         pos_string[i]);
            }
            return NEUTRAL;
        }
    }

    wk = -1;
    bk = -1;

    for (i = 0; i < NSQUARES; i++) {
        if (board[i] == KING) {
            if (wk != -1) {
                if (Verbose > 1) {
                    MyPrintf("Too many white kings\n");
                }
                return NEUTRAL;
            }
            wk = i;
        } else if (board[i] == -KING) {
            if (bk != -1) {
                if (Verbose > 1) {
                    MyPrintf("Too many black kings\n");
                }
                return NEUTRAL;
            }
            bk = i;
        }
    }

    int row_wk = Row(wk);
    int row_bk = Row(bk);
    int col_wk = Column(wk);
    int col_bk = Column(bk);

    if (wk == bk || (abs(row_wk - row_bk) <= 1 && abs(col_wk - col_bk) <= 1)) {
        if (Verbose > 1) {
            MyPrintf("Invalid king positions\n");
        }
        return NEUTRAL;
    }

    if (*ep_square != 0) {
        row = Row(*ep_square);
        col = Column(*ep_square);
        for (;;) {
            if (side == WHITE) {
                if (row != NROWS - 3) {
                    if (Verbose > 1) {
                        MyPrintf("e.p. square %c%d on wrong row for wtm\n",
                                 'a' + col, 1 + row);
                    }
                    *ep_square = 0;
                    break;
                }
                if (board[SquareMake(row, col)] != 0) {
                    if (Verbose > 1) {
                        MyPrintf("e.p. %c%d square occupied\n", 'a' + col,
                                 1 + row);
                    }
                    *ep_square = 0;
                    break;
                }
                if (board[SquareMake(row + 1, col)] != 0) {
                    if (Verbose > 1) {
                        MyPrintf("e.p. starting square %c%d occupied\n",
                                 'a' + col, 1 + row + 1);
                    }
                    *ep_square = 0;
                    break;
                }
                if (board[SquareMake(row - 1, col)] != -PAWN) {
                    if (Verbose > 1) {
                        MyPrintf("No black pawn available to be e.p. captured "
                                 "on %c%d\n",
                                 'a' + col, 1 + row);
                    }
                    *ep_square = 0;
                    break;
                }
                bool ok = false;
                if (col > 0 && board[SquareMake(row - 1, col - 1)] == PAWN ||
                    col < (NCOLS - 1) &&
                        board[SquareMake(row - 1, col + 1)] == PAWN)
                    ok = true;
                if (!ok) {
                    if (Verbose > 1) {
                        MyPrintf(
                            "No white pawn available to e.p. capture on %c%d\n",
                            'a' + col, 1 + row);
                    }
                    *ep_square = 0;
                    break;
                }
                break;
            } else if (side == BLACK) {
                if (row != 2) {
                    if (Verbose > 1) {
                        MyPrintf("e.p. square %c%d on wrong row for btm\n",
                                 'a' + col, 1 + row);
                    }
                    break;
                }
                if (board[SquareMake(row, col)] != 0) {
                    if (Verbose > 1) {
                        MyPrintf("e.p. square %c%d occupied\n", 'a' + col,
                                 1 + row);
                    }
                    break;
                }
                if (board[SquareMake(row - 1, col)] != 0) {
                    if (Verbose > 1) {
                        MyPrintf("e.p. starting square %c%d occupied\n",
                                 'a' + col, 1 + row - 1);
                    }
                    break;
                }
                if (board[SquareMake(row + 1, col)] != PAWN) {
                    if (Verbose > 1) {
                        MyPrintf("No white pawn available to be e.p. captured "
                                 "on %c%d\n",
                                 'a' + col, 1 + row);
                    }
                    break;
                }
                bool ok = false;
                if (col > 0 && board[SquareMake(row + 1, col - 1)] == -PAWN ||
                    col < (NCOLS - 1) &&
                        board[SquareMake(row + 1, col + 1)] == -PAWN)
                    ok = true;
                if (!ok) {
                    if (Verbose > 1) {
                        MyPrintf(
                            "No black pawn available to e.p. capture on %c%d\n",
                            'a' + col, 1 + row);
                    }
                    break;
                }
                break;
            }
        }
    }

    if (*castle != 0) {
        int avail_castle = CastleRights(board, *castle);
        if ((*castle) != avail_castle) {
            if (Verbose > 1) {
                char invalid_castle[5] = {0, 0, 0, 0, 0};
                int nc = 0;
                if (((*castle) & WK_CASTLE) && !(avail_castle & WK_CASTLE))
                    invalid_castle[nc++] = 'K';
                if (((*castle) & WQ_CASTLE) && !(avail_castle & WQ_CASTLE))
                    invalid_castle[nc++] = 'Q';
                if (((*castle) & BK_CASTLE) && !(avail_castle & BK_CASTLE))
                    invalid_castle[nc++] = 'k';
                if (((*castle) & BQ_CASTLE) && !(avail_castle & BQ_CASTLE))
                    invalid_castle[nc++] = 'q';
                MyPrintf("Bad castle status %s\n", invalid_castle);
            }
            *castle = avail_castle;
        }
    }

    return side;
}

static bool IsAttacked(int *board, int attacker, int ptype, int victim) {
    int atype;

    if (ptype == PAWN) {
        int col = Column(attacker);
        if (col > 0 && (victim == (attacker + NCOLS - 1)))
            return true;
        if (col < (NCOLS - 1) && (victim == (attacker + NCOLS + 1)))
            return true;
        return false;
    }

    if (ptype == -PAWN) {
        int col = Column(attacker);
        if (col > 0 && (victim == (attacker - NCOLS - 1)))
            return true;
        if (col < (NCOLS - 1) && (victim == (attacker - NCOLS + 1)))
            return true;
        return false;
    }

    atype = abs(ptype);

    if (atype & KNIGHT)
        if (KnightAttack[NSQUARES * attacker + victim])
            return true;

    if (atype & BISHOP) {
        int direc = BishopDirection[NSQUARES * attacker + victim];
        if (direc >= 0) {
            int *sqp = BishopMoves[attacker][direc];
            int sq;
            while ((sq = *sqp++) != -1) {
                if (sq == victim)
                    return true;
                if (board[sq])
                    break;
            }
        }
    }

    if (atype & ROOK) {
        int direc = RookDirection[NSQUARES * attacker + victim];
        if (direc >= 0) {
            int *sqp = RookMoves[attacker][direc];
            int sq;
            while ((sq = *sqp++) != -1) {
                if (sq == victim)
                    return true;
                if (board[sq])
                    break;
            }
        }
    }

    return false;
}

static bool IsInCheck(BOARD *Board, int side) {
    if (side == WHITE) {
        for (int ptype = KING - 1; ptype >= PAWN; ptype--) {
            int *pos = Board->piece_locations[BLACK][ptype];
            for (int i = 0; i < Board->piece_type_count[BLACK][ptype]; i++) {
                if (IsAttacked(Board->board, pos[i], -ptype, Board->wkpos))
                    return true;
            }
        }
    } else {
        for (int ptype = KING - 1; ptype >= PAWN; ptype--) {
            int *pos = Board->piece_locations[WHITE][ptype];
            for (int i = 0; i < Board->piece_type_count[WHITE][ptype]; i++) {
                if (IsAttacked(Board->board, pos[i], ptype, Board->bkpos))
                    return true;
            }
        }
    }

    return false;
}

static int ScanFEN(char *fen_string, int *board, int *ep_square, int *castle,
                   int *half_move, int *full_move, char *title) {
    char pos_str[128], side_str[128], castle_str[128], ep_str[128];
    char half_move_str[128], full_move_str[128];
    char *pptr, save_char;
    int kings[2], n, row, col, side, wk, bk;

    if (fen_string[0] == '#' || strlen(fen_string) < 6)
        return NEUTRAL;

    pptr = strstr(fen_string, "c0");

    if (pptr != NULL) {
        save_char = *pptr;
        *pptr = '\0';
    }

    n = sscanf(fen_string, "%s %s %s %s %s %s", pos_str, side_str, castle_str,
               ep_str, half_move_str, full_move_str);

    if (pptr != NULL)
        *pptr = save_char;

    if (n < 1) {
        if (Verbose > 1)
            MyPrintf("ScanFEN: Too few elements in FEN string %s\n",
                     fen_string);
        goto error;
    }

    row = NROWS - 1;
    col = 0;

    kings[0] = kings[1] = 0;
    memset(board, 0, NSQUARES * sizeof(board[0]));

    for (pptr = pos_str; *pptr; pptr++) {
        char c = *pptr;
        if (c == '/') {
            row--;
            col = 0;
        } else if (isdigit(c)) {
            if (isdigit(*(pptr + 1))) {
                int ncols;
                sscanf(pptr, "%d", &ncols);
                col += ncols;
                pptr++;
            } else
                col += (int)(c - '0');
        } else {
            int piece = GetPiece(c);
            int color = WHITE, sq;
            if (piece < 0) {
                if (Verbose > 1)
                    MyPrintf("ScanFEN: Invalid piece %c in %s\n", c, pos_str);
                goto error;
            }
            if (islower(c))
                color = BLACK;
            if (piece == KING)
                (color == WHITE) ? kings[0]++ : kings[1]++;

            if (row < 0 || col > (NCOLS - 1)) {
                if (Verbose > 1)
                    MyPrintf("ScanFEN: Square %c%d outside board\n", '1' + col,
                             1 + row);
                goto error;
            }

            sq = SquareMake(row, col);

            board[sq] = piece;
            if (color == BLACK)
                board[sq] = -board[sq];

            if (board[sq] == KING)
                wk = sq;
            else if (board[sq] == -KING)
                bk = sq;
            col++;
        }
    }

    if (kings[0] != 1 || kings[1] != 1) {
        if (Verbose > 1)
            MyPrintf("ScanFEN: Wrong number of kings in %s\n", pos_str);
        goto error;
    }

    int row_wk = Row(wk);
    int row_bk = Row(bk);
    int col_wk = Column(wk);
    int col_bk = Column(bk);

    if (abs(row_wk - row_bk) <= 1 && abs(col_wk - col_bk) <= 1) {
        if (Verbose > 1)
            MyPrintf("ScanFEN: Kings are adjacent in %s\n", pos_str);
        goto error;
    }

    side = WHITE;
    if (n > 1) {
        if (side_str[0] == 'w' || side_str[0] == 'W') {
            side = WHITE;
        } else if (side_str[0] == 'b' || side_str[0] == 'B') {
            side = BLACK;
        } else {
            if (Verbose > 1)
                MyPrintf("ScanFEN: Unrecognized side to move %s\n", side_str);
            goto error;
        }
    }

    if (n >= 3) {
        int my_castle = 0, avail_castle = 0;
        if (Chess960Game) {
            avail_castle = (WK_CASTLE | WQ_CASTLE | BK_CASTLE | BQ_CASTLE);
            avail_castle = CastleRights(board, avail_castle);
            goto castle_done;
        }
        if (strchr(castle_str, 'K'))
            my_castle |= WK_CASTLE;
        if (strchr(castle_str, 'Q'))
            my_castle |= WQ_CASTLE;
        if (strchr(castle_str, 'k'))
            my_castle |= BK_CASTLE;
        if (strchr(castle_str, 'q'))
            my_castle |= BQ_CASTLE;
        avail_castle = CastleRights(board, my_castle);
        if (my_castle != avail_castle) {
            if (Verbose > 1) {
                char invalid_castle[5] = {0, 0, 0, 0, 0};
                int nc = 0;
                if ((my_castle & WK_CASTLE) & !(avail_castle & WK_CASTLE))
                    invalid_castle[nc++] = 'K';
                if ((my_castle & WQ_CASTLE) & !(avail_castle & WQ_CASTLE))
                    invalid_castle[nc++] = 'Q';
                if ((my_castle & BK_CASTLE) & !(avail_castle & BK_CASTLE))
                    invalid_castle[nc++] = 'k';
                if ((my_castle & BQ_CASTLE) & !(avail_castle & BQ_CASTLE))
                    invalid_castle[nc++] = 'q';
                MyPrintf("ScanFEN: bad castle status %s\n", invalid_castle);
                MyPrintf("For %s\n", fen_string);
            }
        }
    castle_done:
        *castle = avail_castle;
    }

    *ep_square = 0;

    if (n >= 4 && ep_str[0] != '-') {
        for (;;) {
            if (strlen(ep_str) < 2)
                break;
            col = (int)(ep_str[0] - (isupper(ep_str[0]) ? 'A' : 'a'));
            sscanf(&ep_str[1], "%d", &row);
            row--;
            if (col < 0 || col >= NCOLS || row < 0 | row >= NROWS) {
                if (Verbose > 1) {
                    MyPrintf(
                        "ScanFEN: Warning: e.p. square %c%d out of range\n",
                        'a' + col, 1 + row);
                }
                break;
            }

            if (side == WHITE) {
                if (row != NROWS - 3) {
                    if (Verbose > 1) {
                        MyPrintf("ScanFEN: Warning: e.p. square %c%d on wrong "
                                 "row for wtm\n",
                                 'a' + col, 1 + row);
                    }
                    break;
                }
                if (board[SquareMake(row, col)] != 0) {
                    if (Verbose > 1) {
                        MyPrintf(
                            "ScanFEN: Warning: e.p. square %c%d occupied\n",
                            'a' + col, 1 + row);
                    }
                    break;
                }
                if (board[SquareMake(row + 1, col)] != 0) {
                    if (Verbose > 1) {
                        MyPrintf("ScanFEN: Warning: e.p. starting square %c%d "
                                 "occupied\n",
                                 'a' + col, 1 + row + 1);
                    }
                    break;
                }
                if (board[SquareMake(row - 1, col)] != -PAWN) {
                    if (Verbose > 1) {
                        MyPrintf("ScanFEN: Warning: no black pawn available to "
                                 "be e.p. captured on %c%d\n",
                                 'a' + col, 1 + row);
                    }
                    break;
                }
                bool ok = false;
                if (col > 0 && board[SquareMake(row - 1, col - 1)] == PAWN ||
                    col < (NCOLS - 1) &&
                        board[SquareMake(row - 1, col + 1)] == PAWN)
                    ok = true;
                if (!ok) {
                    if (Verbose > 1) {
                        MyPrintf("ScanFEN: Warning: no white pawn available to "
                                 "e.p. capture on %c%d\n",
                                 'a' + col, 1 + row);
                    }
                    break;
                }
                *ep_square = SquareMake(row, col);
                break;
            } else if (side == BLACK) {
                if (row != 2) {
                    if (Verbose > 1) {
                        MyPrintf("ScanFEN: Warning: e.p. square %c%d on wrong "
                                 "row for btm\n",
                                 'a' + col, 1 + row);
                    }
                    break;
                }
                if (board[SquareMake(row, col)] != 0) {
                    if (Verbose > 1) {
                        MyPrintf(
                            "ScanFEN: Warning: e.p. square %c%d occupied\n",
                            'a' + col, 1 + row);
                    }
                    break;
                }
                if (board[SquareMake(row - 1, col)] != 0) {
                    if (Verbose > 1) {
                        MyPrintf("ScanFEN: Warning: e.p. starting square %c%d "
                                 "occupied\n",
                                 'a' + col, 1 + row - 1);
                    }
                    break;
                }
                if (board[SquareMake(row + 1, col)] != PAWN) {
                    if (Verbose > 1) {
                        MyPrintf("ScanFEN: Warning: no white pawn available to "
                                 "be e.p. captured on %c%d\n",
                                 'a' + col, 1 + row);
                    }
                    break;
                }
                bool ok = false;
                if (col > 0 && board[SquareMake(row + 1, col - 1)] == -PAWN ||
                    col < (NCOLS - 1) &&
                        board[SquareMake(row + 1, col + 1)] == -PAWN)
                    ok = true;
                if (!ok) {
                    if (Verbose > 1) {
                        MyPrintf("ScanFEN: Warning: no black pawn available to "
                                 "e.p. capture on %c%d\n",
                                 'a' + col, 1 + row);
                    }
                    break;
                }
                *ep_square = SquareMake(row, col);
                break;
            }
        }
        if (*ep_square == 0) {
            if (Verbose > 1) {
                MyPrintf("ScanFEN: Warning: Could not assign e.p. from %s\n",
                         ep_str);
            }
        }
    }

    *half_move = 0;

    if (n >= 5) {
        int half;
        sscanf(half_move_str, "%d", &half);
        if (half >= 0 && half < 16384)
            *half_move = half;
    }

    *full_move = 1;

    if (n >= 6) {
        int full;
        sscanf(full_move_str, "%d", &full);
        if (full >= 1 && full < 16384)
            *full_move = full;
    }

    pptr = strstr(fen_string, "c0");

    if (title != NULL && pptr != NULL) {
        pptr = strchr(pptr, '"');
        if (pptr != NULL) {
            pptr++;
            while (*pptr != '\0' && *pptr != '"') {
                *title++ = *pptr++;
            }
            *title = '\0';
        }
    }

    return side;
error:
    return NEUTRAL;
}

static int GetBoardFromMB(BOARD *Board, char *ending, int side, int kk_index,
                          ZINDEX offset) {
    int piece_type_count[2][KING], piece_types[MAX_PIECES];
    int board[NSQUARES], pos[MAX_PIECES], n_pieces, eindex, kpos;
    int castle = 0, full_move = 1, half_move = 0, ep_square = 0;
    int bishop_parity[2] = {NONE, NONE};
    int pawn_file_type = FREE_PAWNS;

    n_pieces = ParseEndingName(ending, piece_type_count);

    eindex = GetEndingType(piece_type_count, piece_types, bishop_parity,
                           pawn_file_type);

    if (eindex == -1)
        return 0;

    bool pawns_present =
        piece_type_count[WHITE][PAWN] || piece_type_count[BLACK][PAWN];

    if (pawns_present) {
        pos[0] = KK_List[kk_index].wk;
        pos[1] = KK_List[kk_index].bk;
    } else {
        pos[0] = KK_List_NoPawns[kk_index].wk;
        pos[1] = KK_List_NoPawns[kk_index].bk;
    }

    InitPermutationTables(piece_type_count, bishop_parity);

    if (!(IndexTable[eindex].PosFromIndex)(offset, pos)) {
        if (Verbose > 0)
            MyPrintf("Could not obtain position from ending %d, "
                     "offset " DEC_ZINDEX_FORMAT "\n",
                     eindex, offset);
        return 0;
    }

    if (piece_type_count[WHITE][PAWN] && piece_type_count[BLACK][PAWN]) {
        ep_square = GetEnPassant(pos, piece_types, n_pieces);
    }

    memset(board, 0, sizeof(board));

    for (int i = 0; i < n_pieces; i++)
        board[pos[i]] = piece_types[i];

    n_pieces =
        SetBoard(Board, board, side, ep_square, castle, half_move, full_move);

    return n_pieces;
}

static int ReadPositionList(char *pos, BOARD *Board, POSITION_DATA *pos_data) {
    char ending[64];

    int nwords = ReadPositionData(pos, pos_data);

    if (nwords < 8)
        return NEUTRAL;

    int side = pos_data->side;

    if (side == NEUTRAL)
        return NEUTRAL;

    strncpy(ending, pos_data->ending, sizeof(ending));

    int kk_index = pos_data->kk_index;
    ZINDEX offset = pos_data->offset;

#if 0
    int kk_index = pos_data->kk_index;
    ZINDEX offset = pos_data->offset;
#endif

    int n_pieces = GetBoardFromMB(Board, ending, side, kk_index, offset);

    if (n_pieces == 0)
        return NEUTRAL;

    int move_no = pos_data->move_no;

    Board->full_move = move_no;

    Board->result = pos_data->result;

    Board->score = pos_data->score;

    Board->promos = pos_data->promos;

    Board->game_num = pos_data->game_num;

    Board->zz_type = pos_data->zz_type;

    if (Verbose > 1)
        MyPrintf("game: %d  result: %d score: %d  zz_type: %d\n",
                 pos_data->game_num, Board->result, Board->score,
                 Board->zz_type);

    return side;
}

static int ConvertMB_to_YK(char *line) {
    POSITION_DATA pos_data;

    int nwords = ReadPositionData(line, &pos_data);

    if (nwords < 3) {
        MyPrintf("%s", line);
        return -1;
    }

    int piece_type_count[2][KING];

    int n_pieces = ParseEndingName(pos_data.ending, piece_type_count);

    bool pawns_present =
        piece_type_count[WHITE][PAWN] || piece_type_count[BLACK][PAWN];

    if (!pawns_present) {
        MyPrintf("%s", line);
        return 0;
    }

    int piece_types[MAX_PIECES];
    int bishop_parity[2] = {NONE, NONE};
    int pawn_file_type = FREE_PAWNS;

    int eindex = GetEndingType(piece_type_count, piece_types, bishop_parity,
                               pawn_file_type);

    if (eindex == -1) {
        MyPrintf("%s", line);
        return -1;
    }

    InitPermutationTables(piece_type_count, bishop_parity);

    int pos[MAX_PIECES];

    int kk_index = pos_data.kk_index;
    ZINDEX offset = pos_data.offset;

    if (!(IndexTable[eindex].PosFromIndex)(offset, pos)) {
        if (Verbose > 0) {
            MyPrintf("Could not obtain position from ending %d, "
                     "offset " DEC_ZINDEX_FORMAT "\n",
                     eindex, offset);
        }
        MyPrintf("%s", line);
        return -1;
    }

    int piece_types_yk[MAX_PIECES];

    int eindex_yk = GetEndingTypeYK(piece_type_count, piece_types_yk);

    if (eindex_yk == -1) {
        MyPrintf("%s", line);
        return -1;
    }

    int num_white_pawns = piece_type_count[WHITE][PAWN];
    int num_black_pawns = piece_type_count[BLACK][PAWN];
    int num_white_pieces = 0, num_black_pieces = 0;

    for (int p = KNIGHT; p < KING; p++) {
        num_white_pieces += piece_type_count[WHITE][p];
        num_black_pieces += piece_type_count[BLACK][p];
    }

    int pos_yk[MAX_PIECES];

    for (int wp = 0; wp < num_white_pieces; wp++) {
        pos_yk[2 + wp] = pos[2 + num_white_pawns + num_black_pawns + wp];
    }

    for (int wp = 0; wp < num_white_pawns; wp++) {
        pos_yk[2 + num_white_pieces + wp] = pos[2 + wp];
    }

    for (int bp = 0; bp < num_black_pieces; bp++) {
        pos_yk[2 + num_white_pieces + num_white_pawns + bp] =
            pos[2 + num_white_pawns + num_black_pawns + num_white_pieces + bp];
    }

    for (int bp = 0; bp < num_black_pawns; bp++) {
        pos_yk[2 + num_white_pieces + num_white_pawns + num_black_pieces + bp] =
            pos[2 + num_white_pawns + bp];
    }

    ZINDEX offset_yk = (IndexTable[eindex_yk].IndexFromPos)(pos_yk);

    char line_out[512];
    char *rest = line_out;

    strcpy(line_out, line);

    char *eptr = strtok_r(line_out, " \t\r", &rest);

    if (eptr == NULL) {
        MyPrintf("%s", line);
        return -1;
    }

    char *kptr = strtok_r(NULL, " \t\r", &rest);

    if (kptr == NULL) {
        MyPrintf("%s", line);
        return -1;
    }

    char *optr = strtok_r(NULL, " \t\r", &rest);

    if (optr == NULL) {
        MyPrintf("%s", line);
        return -1;
    }

    MyPrintf("%s %d " DEC_ZINDEX_FORMAT " %s", pos_data.ending, kk_index,
             offset_yk, rest);

    return 0;
}

static int ConvertYK_to_MB(char *line) {
    POSITION_DATA pos_data;

    int nwords = ReadPositionData(line, &pos_data);

    if (nwords < 3) {
        MyPrintf("%s", line);
        return -1;
    }

    int piece_type_count[2][KING];

    int n_pieces = ParseEndingName(pos_data.ending, piece_type_count);

    bool pawns_present =
        piece_type_count[WHITE][PAWN] || piece_type_count[BLACK][PAWN];

    if (!pawns_present) {
        MyPrintf("%s", line);
        return 0;
    }

    int piece_types_yk[MAX_PIECES];
    int bishop_parity[2] = {NONE, NONE};
    int pawn_file_type = FREE_PAWNS;

    int eindex_yk = GetEndingTypeYK(piece_type_count, piece_types_yk);

    if (eindex_yk == -1) {
        MyPrintf("%s", line);
        return -1;
    }

    InitPermutationTables(piece_type_count, bishop_parity);

    int pos_yk[MAX_PIECES];

    int kk_index = pos_data.kk_index;
    ZINDEX offset_yk = pos_data.offset;

    if (!(IndexTable[eindex_yk].PosFromIndex)(offset_yk, pos_yk)) {
        if (Verbose > 0) {
            MyPrintf("Could not obtain position from ending %d, "
                     "offset " DEC_ZINDEX_FORMAT "\n",
                     eindex_yk, offset_yk);
        }
        MyPrintf("%s", line);
        return -1;
    }

    int piece_types[MAX_PIECES];

    int eindex = GetEndingType(piece_type_count, piece_types, bishop_parity,
                               pawn_file_type);

    if (eindex == -1) {
        MyPrintf("%s", line);
        return -1;
    }

    int num_white_pawns = piece_type_count[WHITE][PAWN];
    int num_black_pawns = piece_type_count[BLACK][PAWN];
    int num_white_pieces = 0, num_black_pieces = 0;

    for (int p = KNIGHT; p < KING; p++) {
        num_white_pieces += piece_type_count[WHITE][p];
        num_black_pieces += piece_type_count[BLACK][p];
    }

    int pos[MAX_PIECES];

    for (int wp = 0; wp < num_white_pieces; wp++) {
        pos[2 + num_white_pawns + num_black_pawns + wp] = pos_yk[2 + wp];
    }

    for (int wp = 0; wp < num_white_pawns; wp++) {
        pos[2 + wp] = pos_yk[2 + num_white_pieces + wp];
    }

    for (int bp = 0; bp < num_black_pieces; bp++) {
        pos[2 + num_white_pawns + num_black_pawns + num_white_pieces + bp] =
            pos_yk[2 + num_white_pieces + num_white_pawns + bp];
    }

    for (int bp = 0; bp < num_black_pawns; bp++) {
        pos[2 + num_white_pawns + bp] =
            pos_yk[2 + num_white_pieces + num_white_pawns + num_black_pieces +
                   bp];
    }

    ZINDEX offset = (IndexTable[eindex].IndexFromPos)(pos);

    char line_out[512];

    strcpy(line_out, line);
    char *rest = line_out;

    char *eptr = strtok_r(line_out, " \t\r", &rest);

    if (eptr == NULL) {
        MyPrintf("%s", line);
        return -1;
    }

    char *kptr = strtok_r(NULL, " \t\r", &rest);

    if (kptr == NULL) {
        MyPrintf("%s", line);
        return -1;
    }

    char *optr = strtok_r(NULL, " \t\r", &rest);

    if (optr == NULL) {
        MyPrintf("%s", line);
        return -1;
    }

    MyPrintf("%s %d " DEC_ZINDEX_FORMAT " %s", pos_data.ending, kk_index,
             offset, rest);

    return 0;
}

static int ReadPosition(char *pos, BOARD *Board, char *title) {
    int legal, board[NSQUARES], ep_square = 0, castle = 0, half_move = 0,
                                full_move = 1;

    if (Verbose > 3) {
        MyPrintf("ReadPosition with: %s\n", pos);
    }
    if (strchr(pos, '/') != NULL) {
        if (Verbose > 3) {
            MyPrintf("ReadPosition: entering FEN scan\n");
        }
        legal = ScanFEN(pos, board, &ep_square, &castle, &half_move, &full_move,
                        title);
    } else {
        if (Verbose > 3) {
            MyPrintf("ReadPosition: entering non-FEN scan\n");
        }
        legal = ScanPosition(pos, board, &ep_square, &castle, title);
    }
    if (!UseEnPassant) {
        if (ep_square > 0) {
            if (Verbose > 1) {
                MyPrintf("Ignoring e.p.\n");
            }
            ep_square = 0;
        }
    }
    if (Verbose > 3) {
        MyPrintf("ReadPosition: legal from scan: %d\n", legal);
    }
    if (legal != NEUTRAL) {
        if (Verbose > 3) {
            MyPrintf("ReadPosition: Board scanned\n");
            DisplayRawBoard(board, "before SetBoard");
        }
        SetBoard(Board, board, legal, ep_square, castle, half_move, full_move);
        if (Verbose > 3) {
            MyPrintf("ReadPosition: Board scanned\n");
            DisplayBoard(Board, "after SetBoard");
        }
        if (IsInCheck(Board, OtherSide(legal)))
            legal = NEUTRAL;
    }
    return legal;
}

static void PrintPieceTypeCount(int types[2][KING], char *comment) {
    int piece;

    MyPrintf("W: ");
    for (piece = KING - 1; piece >= PAWN; piece--)
        MyPrintf("%c=%d  ", toupper(piece_char(piece)), types[0][piece]);
    MyPrintf("\nB: ");
    for (piece = KING - 1; piece >= PAWN; piece--)
        MyPrintf("%c=%d  ", toupper(piece_char(piece)), types[1][piece]);
    if (comment != NULL)
        MyPrintf(" %s", comment);
    MyPrintf("\n");
    MyFlush();
}

static void PrintPosition(int *pos, int *types, int n_pieces) {
    int i;
    for (i = 0; i < n_pieces; i++) {
        char color, piece;
        int sq = pos[i];

        if (types[i] > 0) {
            color = 'w';
            piece = piece_char(types[i]);
        } else {
            color = 'b';
            piece = piece_char(-types[i]);
        }

        MyPrintf("%c%c%c%d ", color, piece, 'a' + Column(sq), 1 + Row(sq));
    }
    MyPrintf("\n");
    MyFlush();
}

static void BoardToFEN(BOARD *Board, char *FEN_String) {
    char *pch;
    int sq, sqLast, i;
    int *board = Board->board;

    pch = FEN_String;
    sq = sqLast = SquareMake(NROWS - 1, 0);

    for (;;) {
        if (board[sq]) {
            if (sq != sqLast) {
                if (sq - sqLast >= 10) {
                    *pch++ = '0' + ((sq - sqLast) / 10);
                    *pch++ = '0' + ((sq - sqLast) % 10);
                } else
                    *pch++ = (char)(sq - sqLast + '0');
            }
            *pch++ = (board[sq] > 0) ? toupper(piece_char(board[sq]))
                                     : piece_char(-board[sq]);
            sqLast = sq + 1;
        }
        if (Column(sq) == (NCOLS - 1)) {
            if (sq >= sqLast) {
                if ((sq - sqLast + 1) >= 10) {
                    *pch++ = '0' + ((sq - sqLast + 1) / 10);
                    *pch++ = '0' + ((sq - sqLast + 1) % 10);
                } else
                    *pch++ = (char)(sq - sqLast + '1');
            }
            if (sq == SquareMake(0, NCOLS - 1))
                break;
            *pch++ = '/';
            sq = SquareMake(Row(sq) - 1, 0);
            sqLast = sq;
        } else
            sq++;
    }
    *pch++ = ' ';
    *pch++ = (Board->side == WHITE) ? 'w' : 'b';
    *pch++ = ' ';
    if (Board->castle & WK_CASTLE)
        *pch++ = 'K';
    if (Board->castle & WQ_CASTLE)
        *pch++ = 'Q';
    if (Board->castle & BK_CASTLE)
        *pch++ = 'k';
    if (Board->castle & BQ_CASTLE)
        *pch++ = 'q';
    if (Board->castle == 0)
        *pch++ = '-';
    *pch++ = ' ';
    if (Board->ep_square > 0) {
        int row_ep = 1 + Row(Board->ep_square);
        *pch++ = 'a' + Column(Board->ep_square);
        if (row_ep < 10) {
            *pch++ = '0' + row_ep;
        } else {
            *pch++ = '0' + (row_ep / 10);
            *pch++ = '0' + (row_ep % 10);
        }
    } else
        *pch++ = '-';
    *pch++ = ' ';
    sprintf(pch, "%d %d", Board->half_move, Board->full_move);
}

static void PrintEPD(BOARD *Board, char *comment) {
    char fen_string[256];
    BoardToFEN(Board, fen_string);
    if (comment != NULL)
        MyPrintf("%s %s\n", fen_string, comment);
    else
        MyPrintf("%s\n", fen_string);
    MyFlush();
}

/*
 * KK_Canonical computes the symmetry operation to transform wk_in and bk_in to
 * a "canonical" configuration when pawns are present.  The symmetry operation
 * index and transformed king positions are stored in sym, wk_in, and bk_in
 * respectively.
 *
 * The routine returns true if the position is legal, false otherwise
 */

bool KK_Canonical(int *wk_in, int *bk_in, int *sym) {
    int wk = *wk_in;
    int bk = *bk_in;
    int wk_row = Row(wk);
    int wk_col = Column(wk);
    int bk_row = Row(bk);
    int bk_col = Column(bk);

    // positions where kings are adjacent are illegal
    if (abs(wk_row - bk_row) <= 1 && abs(wk_col - bk_col) <= 1)
        return false;

    // try all symmetry operations on the two kings to find the canonical one
    for (int isym = 0; isym < NSYMMETRIES; isym++) {
        if (isym != IDENTITY && isym != REFLECT_V)
            continue;
        int *transform = Transforms[isym];
        int wk_trans = transform[wk];
        int bk_trans = transform[bk];
        int wk_trans_row = Row(wk_trans);
        int wk_trans_col = Column(wk_trans);
        int bk_trans_row = Row(bk_trans);
        int bk_trans_col = Column(bk_trans);
        bool sym_found = false;

        if (wk_trans_col < (NCOLS + 1) / 2) {
#if (NCOLS % 2)
            if (wk_trans_col == NCOLS / 2) {
                if (bk_trans_col <= NCOLS / 2)
                    sym_found = true;
            } else
                sym_found = true;
#else // even columns
            sym_found = true;
#endif
        }

        if (sym_found) {
            *wk_in = wk_trans;
            *bk_in = bk_trans;
            *sym = isym;
            return true;
        }
    }

    return false;
}

/*
 * KK_Canonical_NoPawns computes the symmetry operation to transform wk_in and
 * bk_in to a "canonical" configuration without pawns.  The symmetry operation
 * index and transformed king positions are stored in sym, wk_in, and bk_in
 * respectively.
 *
 * The routine returns true if the position is legal, false otherwise
 */

bool KK_Canonical_NoPawns(int *wk_in, int *bk_in, int *sym) {
    int wk = *wk_in;
    int bk = *bk_in;
    int wk_row = Row(wk);
    int wk_col = Column(wk);
    int bk_row = Row(bk);
    int bk_col = Column(bk);

    // positions where kings are adjacent are illegal
    if (abs(wk_row - bk_row) <= 1 && abs(wk_col - bk_col) <= 1)
        return false;

    // try all symmetry operations on the two kings to find the canonical one
    for (int isym = 0; isym < NSYMMETRIES; isym++) {
        int *transform = Transforms[isym];
        int wk_trans = transform[wk];
        int bk_trans = transform[bk];
        int wk_trans_row = Row(wk_trans);
        int wk_trans_col = Column(wk_trans);
        int bk_trans_row = Row(bk_trans);
        int bk_trans_col = Column(bk_trans);
        bool sym_found = false;

#if defined(SQUARE)
        if (wk_trans_row < (NROWS + 1) / 2 && wk_trans_col < (NCOLS + 1) / 2 &&
            wk_trans_row <= wk_trans_col) {
#if defined(ODD_SQUARE)
            if (wk_trans_row == wk_trans_col) {
                if (wk_trans_row == NROWS / 2) {
                    if (bk_trans_row <= NROWS / 2 &&
                        bk_trans_col <= NCOLS / 2 &&
                        bk_trans_row <= bk_trans_col)
                        sym_found = true;
                } else if (bk_trans_row <= bk_trans_col)
                    sym_found = true;
            } else {
                if (wk_trans_col == NCOLS / 2) {
                    if (bk_trans_col <= NCOLS / 2)
                        sym_found = true;
                } else
                    sym_found = true;
            }
#else  // not an odd square (an even square)
            if (wk_trans_row == wk_trans_col) {
                if (bk_trans_row <= bk_trans_col)
                    sym_found = true;
            } else
                sym_found = true;
#endif // even square
        }
#elif defined(RECTANGLE)
        if (wk_trans_row < (NROWS + 1) / 2 && wk_trans_col < (NCOLS + 1) / 2) {
#if defined(ODD_ROWS) && defined(ODD_COLUMNS)
            if (wk_trans_row < NROWS / 2 && wk_trans_col < NCOLS / 2)
                sym_found = true;
            else if (wk_trans_row == NROWS / 2 && wk_trans_col < NCOLS / 2) {
                if (bk_trans_row <= NROWS / 2)
                    sym_found = true;
            } else if (wk_trans_row < NROWS / 2 && wk_trans_col == NCOLS / 2) {
                if (bk_trans_col <= NCOLS / 2)
                    sym_found = true;
            } else if (wk_trans_row == NROWS / 2 && wk_trans_col == NCOLS / 2) {
                if (bk_trans_row <= NROWS / 2 && bk_trans_col <= NCOLS / 2)
                    sym_found = true;
            }
#elif defined(ODD_ROWS)
            if (wk_trans_row == NROWS / 2) {
                if (bk_trans_row <= NROWS / 2)
                    sym_found = true;
            } else
                sym_found = true;
#elif defined(ODD_COLUMNS)
            if (wk_trans_col == NCOLS / 2) {
                if (bk_trans_col <= NCOLS / 2)
                    sym_found = true;
            } else
                sym_found = true;
#else // even rows and columns
            sym_found = true;
#endif
        }
#endif // Rectangle

        if (sym_found) {
            *wk_in = wk_trans;
            *bk_in = bk_trans;
            *sym = isym;
            return true;
        }
    }

    return false;
}

static void InitTransforms() {
    int n_kings = 0;
    int n_kings_nopawns = 0;

    if (Verbose > 3) {
        MyPrintf("\nInitializing geometric transformation tables\n");
        MyFlush();
    }

    for (int row = 0; row < NROWS; row++) {
        for (int col = 0; col < NCOLS; col++) {
            int sq = SquareMake(row, col);
            Identity[sq] = sq;
            ReflectV[sq] = SquareMake(row, NCOLS - 1 - col);
            ReflectH[sq] = SquareMake(NROWS - 1 - row, col);
            ReflectVH[sq] = SquareMake(NROWS - 1 - row, NCOLS - 1 - col);
#if defined(SQUARE)
            ReflectD[sq] = SquareMake(col, row);
            ReflectDV[sq] = SquareMake(NCOLS - 1 - col, row);
            ReflectDH[sq] = SquareMake(col, NROWS - 1 - row);
            ReflectDVH[sq] = SquareMake(NCOLS - 1 - col, NROWS - 1 - row);
#endif
        }
    }

    for (int sq = 0; sq < NSQUARES; sq++) {
        for (int sym = 0; sym < NSYMMETRIES; sym++) {
            int *trans = Transforms[sym];
            int *inverse_trans = InverseTransforms[sym];
            if (inverse_trans[trans[sq]] != sq) {
                fprintf(stderr,
                        "for symmetry transformation %s, sq = %d, inverse of "
                        "trans = %d\n",
                        SymmetryName[sym], sq, inverse_trans[trans[sq]]);
                exit(-1);
            }
        }
    }

    if (Verbose > 3) {
        MyPrintf("Initializing king position tables\n");
        MyFlush();
    }

#if (NSQUARES > KK_TABLE_LIMIT)
    if (KK_Transform_Table != NULL) {
        fprintf(stderr,
                "KK_Transform_Table already initialzed in InitTransform\n");
        exit(1);
    }
    if (KK_Index_Table != NULL) {
        fprintf(stderr, "KK_Index_Table already initialzed in InitTransform\n");
        exit(1);
    }

    KK_Transform_Table = (int *)MyMalloc(NSQUARES * NSQUARES * sizeof(int));
    KK_Index_Table = (int *)MyMalloc(NSQUARES * NSQUARES * sizeof(int));

    if (KK_Transform_Table_NoPawns != NULL) {
        fprintf(
            stderr,
            "KK_Transform_Table_NoPawns already initialzed in InitTransform\n");
        exit(1);
    }
    if (KK_Index_Table_NoPawns != NULL) {
        fprintf(stderr,
                "KK_Index_Table_NoPawns already initialzed in InitTransform\n");
        exit(1);
    }

    KK_Transform_Table_NoPawns =
        (int *)MyMalloc(NSQUARES * NSQUARES * sizeof(int));
    KK_Index_Table_NoPawns = (int *)MyMalloc(NSQUARES * NSQUARES * sizeof(int));
#endif

    for (int wk = 0; wk < NSQUARES; wk++) {
        for (int bk = 0; bk < NSQUARES; bk++) {
            int wk_trans = wk, bk_trans = bk, sym;
            KK_Index_NoPawns(wk, bk) = -1;
            KK_Transform_NoPawns(wk, bk) = -1;
            if (!KK_Canonical_NoPawns(&wk_trans, &bk_trans, &sym))
                continue;
            KK_Transform_NoPawns(wk, bk) = sym;
            if (sym == IDENTITY) {
                assert(n_kings < N_KINGS_NOPAWNS);
                KK_List_NoPawns[n_kings_nopawns].wk = wk_trans;
                KK_List_NoPawns[n_kings_nopawns].bk = bk_trans;
                KK_Index_NoPawns(wk, bk) = n_kings_nopawns;
                n_kings_nopawns++;
            }
        }
    }

    assert(n_kings_nopawns == N_KINGS_NOPAWNS);

    for (int wk = 0; wk < NSQUARES; wk++) {
        for (int bk = 0; bk < NSQUARES; bk++) {
            int wk_trans = wk, bk_trans = bk, sym;
            KK_Index(wk, bk) = -1;
            KK_Transform(wk, bk) = -1;
            if (!KK_Canonical(&wk_trans, &bk_trans, &sym))
                continue;
            KK_Transform(wk, bk) = sym;
            if (sym == IDENTITY) {
                assert(n_kings < N_KINGS);
                KK_List[n_kings].wk = wk_trans;
                KK_List[n_kings].bk = bk_trans;
                KK_Index(wk, bk) = n_kings;
                n_kings++;
            }
        }
    }

    assert(n_kings == N_KINGS);

#if defined(NO_ATTACK_TABLES)
    if (Verbose > 3) {
        MyPrintf("Initializing tables for diagonals\n");
        MyFlush();
    }

    memset(DiagA1_H8_Table, 0, sizeof(DiagA1_H8_Table));

    for (int d = 0; d < (NROWS + NCOLS - 1); d++) {
        if (d < NROWS) {
            int row = NROWS - 1 - d;
            int col = 0;

            for (;;) {
                if (row < 0 || row >= NROWS || col < 0 || col >= NCOLS)
                    break;
                DiagA1_H8_Table[SquareMake(row, col)] = d;
                row++;
                col++;
            }
        } else {
            int row = 0;
            int col = d + 1 - NROWS;

            for (;;) {
                if (row < 0 || row >= NROWS || col < 0 || col >= NCOLS)
                    break;
                DiagA1_H8_Table[SquareMake(row, col)] = d;
                row++;
                col++;
            }
        }
    }

    memset(DiagA8_H1_Table, 0, sizeof(DiagA8_H1_Table));

    for (int d = 0; d < (NROWS + NCOLS - 1); d++) {
        if (d < NCOLS) {
            int row = 0;
            int col = d;

            for (;;) {
                if (row < 0 || row >= NROWS || col < 0 || col >= NCOLS)
                    break;
                DiagA8_H1_Table[SquareMake(row, col)] = d;
                row++;
                col--;
            }
        } else {
            int row = d + 1 - NCOLS;
            int col = NCOLS - 1;

            for (;;) {
                if (row < 0 || row >= NROWS || col < 0 || col >= NCOLS)
                    break;
                DiagA8_H1_Table[SquareMake(row, col)] = d;
                row++;
                col--;
            }
        }
    }
#endif

    if (Verbose > 3) {
        MyPrintf("Initializing parity tables\n");
        MyFlush();
    }

    int nw = 0, nb = 0;
    for (int row = 0; row < NROWS; row++) {
        for (int col = 0; col < NCOLS; col++) {
            int sq = SquareMake(row, col);
            IsWhiteSquare[sq] = false;
            // fix parity of bottom right hand corner to 0 "white"
            int parity = ((row & 1) ^ ((NCOLS - 1 - col) & 1));
            if (parity == 0)
                IsWhiteSquare[sq] = true;
            if (IsWhiteSquare[sq])
                WhiteSquares[nw++] = sq;
            else
                BlackSquares[nb++] = sq;
        }
    }

    if (nw != NUM_WHITE_SQUARES) {
        fprintf(stderr, "Found %d white squares, expected %d\n", nw,
                NUM_WHITE_SQUARES);
        exit(1);
    }

    if (nb != NUM_BLACK_SQUARES) {
        fprintf(stderr, "Found %d black squares, expected %d\n", nb,
                NUM_BLACK_SQUARES);
        exit(1);
    }

    for (int i = 0; i < nw; i++) {
        int sq = WhiteSquares[i];
        assert(WhiteSquares[sq / 2] == sq);
    }

    for (int i = 0; i < nb; i++) {
        int sq = BlackSquares[i];
        assert(BlackSquares[sq / 2] == sq);
    }
}

static void InitPieceStrengths() {
    memset(PieceStrengths, 0, sizeof(PieceStrengths));

    PieceStrengths[PAWN] = 1;
    PieceStrengths[KNIGHT] = 3;
    PieceStrengths[BISHOP] = 3;
    PieceStrengths[ROOK] = 5;
    PieceStrengths[QUEEN] = 9;
    PieceStrengths[ARCHBISHOP] = 7;
    PieceStrengths[CARDINAL] = 8;
    PieceStrengths[MAHARAJA] = 13;
}

static void InitMoves() {
    int sq, *sqp, sq2, direc;
    int i, n;

    for (sq = 0; sq < NSQUARES; sq++) {
        int row = Row(sq);
        int col = Column(sq);
        int row2, col2;
        n = 0;
        if (row < (NROWS - 2) && col < (NCOLS - 1))
            KnightMoves[sq][n++] = SquareMake(row + 2, col + 1);
        if (row < (NROWS - 2) && col >= 1)
            KnightMoves[sq][n++] = SquareMake(row + 2, col - 1);
        if (row < (NROWS - 1) && col < (NCOLS - 2))
            KnightMoves[sq][n++] = SquareMake(row + 1, col + 2);
        if (row < (NROWS - 1) && col >= 2)
            KnightMoves[sq][n++] = SquareMake(row + 1, col - 2);
        if (row >= 1 && col < (NCOLS - 2))
            KnightMoves[sq][n++] = SquareMake(row - 1, col + 2);
        if (row >= 1 && col >= 2)
            KnightMoves[sq][n++] = SquareMake(row - 1, col - 2);
        if (row >= 2 && col < (NCOLS - 1))
            KnightMoves[sq][n++] = SquareMake(row - 2, col + 1);
        if (row >= 2 && col >= 1)
            KnightMoves[sq][n++] = SquareMake(row - 2, col - 1);
        KnightMoves[sq][n] = -1;

        n = 0;
        for (col2 = col + 1; col2 < NCOLS; col2++)
            RookMoves[sq][0][n++] = SquareMake(row, col2);
        RookMoves[sq][0][n] = -1;
        n = 0;
        for (col2 = col - 1; col2 >= 0; col2--)
            RookMoves[sq][1][n++] = SquareMake(row, col2);
        RookMoves[sq][1][n] = -1;
        n = 0;
        for (row2 = row + 1; row2 < NROWS; row2++)
            RookMoves[sq][2][n++] = SquareMake(row2, col);
        RookMoves[sq][2][n] = -1;
        n = 0;
        for (row2 = row - 1; row2 >= 0; row2--)
            RookMoves[sq][3][n++] = SquareMake(row2, col);
        RookMoves[sq][3][n] = -1;

        n = 0;
        for (row2 = row + 1, col2 = col + 1; row2 < NROWS && col2 < NCOLS;
             row2++, col2++)
            BishopMoves[sq][0][n++] = SquareMake(row2, col2);
        BishopMoves[sq][0][n] = -1;
        n = 0;
        for (row2 = row + 1, col2 = col - 1; row2 < NROWS && col2 >= 0;
             row2++, col2--)
            BishopMoves[sq][1][n++] = SquareMake(row2, col2);
        BishopMoves[sq][1][n] = -1;
        n = 0;
        for (row2 = row - 1, col2 = col + 1; row2 >= 0 && col2 < NCOLS;
             row2--, col2++)
            BishopMoves[sq][2][n++] = SquareMake(row2, col2);
        BishopMoves[sq][2][n] = -1;
        n = 0;
        for (row2 = row - 1, col2 = col - 1; row2 >= 0 && col2 >= 0;
             row2--, col2--)
            BishopMoves[sq][3][n++] = SquareMake(row2, col2);
        BishopMoves[sq][3][n] = -1;

        n = 0;
        if (row < (NROWS - 1)) {
            if (col < (NCOLS - 1))
                KingMoves[sq][n++] = SquareMake(row + 1, col + 1);
            KingMoves[sq][n++] = SquareMake(row + 1, col);
            if (col >= 1)
                KingMoves[sq][n++] = SquareMake(row + 1, col - 1);
        }
        if (col < (NCOLS - 1))
            KingMoves[sq][n++] = SquareMake(row, col + 1);
        if (col >= 1)
            KingMoves[sq][n++] = SquareMake(row, col - 1);
        if (row >= 1) {
            if (col < (NCOLS - 1))
                KingMoves[sq][n++] = SquareMake(row - 1, col + 1);
            KingMoves[sq][n++] = SquareMake(row - 1, col);
            if (col >= 1)
                KingMoves[sq][n++] = SquareMake(row - 1, col - 1);
        }
        KingMoves[sq][n] = -1;
    }

    for (sq = 0; sq < NSQUARES; sq++) {
        int row_sq = Row(sq);
        int col_sq = Column(sq);
        for (sq2 = 0; sq2 < NSQUARES; sq2++) {
            BishopDirection[sq * NSQUARES + sq2] = -1;
            RookDirection[sq * NSQUARES + sq2] = -1;
            KnightAttack[sq * NSQUARES + sq2] = 0;
            KingAttack[sq * NSQUARES + sq2] = -1;
        }
        for (direc = 0; direc < 4; direc++) {
            sqp = BishopMoves[sq][direc];
            while ((sq2 = *sqp++) != -1)
                BishopDirection[sq * NSQUARES + sq2] = direc;
            sqp = RookMoves[sq][direc];
            while ((sq2 = *sqp++) != -1)
                RookDirection[sq * NSQUARES + sq2] = direc;
        }
        sqp = KnightMoves[sq];
        while ((sq2 = *sqp++) != -1)
            KnightAttack[sq * NSQUARES + sq2] = 1;
        sqp = KingMoves[sq];
        while ((sq2 = *sqp++) != -1) {
            int row_sq2 = Row(sq2);
            int col_sq2 = Column(sq2);
            if (row_sq2 == row_sq) {
                if (col_sq2 == (col_sq + 1)) {
                    KingAttack[sq * NSQUARES + sq2] = EE;
                } else if (col_sq2 == (col_sq - 1)) {
                    KingAttack[sq * NSQUARES + sq2] = WW;
                }
            } else if (row_sq2 == (row_sq + 1)) {
                if (col_sq2 == (col_sq + 1)) {
                    KingAttack[sq * NSQUARES + sq2] = NE;
                } else if (col_sq2 == col_sq) {
                    KingAttack[sq * NSQUARES + sq2] = NN;
                } else if (col_sq2 == (col_sq - 1)) {
                    KingAttack[sq * NSQUARES + sq2] = NW;
                }
            } else if (row_sq2 == (row_sq - 1)) {
                if (col_sq2 == (col_sq + 1)) {
                    KingAttack[sq * NSQUARES + sq2] = SE;
                } else if (col_sq2 == col_sq) {
                    KingAttack[sq * NSQUARES + sq2] = SS;
                } else if (col_sq2 == (col_sq - 1)) {
                    KingAttack[sq * NSQUARES + sq2] = SW;
                }
            }
        }
    }
}

static void InitParity() {
    int sq;

    if (Verbose > 2)
        MyPrintf("Initializing parity table\n");

    for (sq = 0; sq < NSQUARES; sq++) {
        ParityTable[sq] = (Row(sq) & 1) ^ (Column(sq) & 1);
        if (ParityTable[sq])
            WhiteSquare[sq / 2] = sq;
        else
            BlackSquare[sq / 2] = sq;
    }
}

static int GenPseudoLegalMoves(BOARD *Board, Move *move_list,
                               bool skip_pawn_promotion) {
    int nmoves = 0, ptype, direc, *sqp, dest, i;
    bool is_check, is_in_check;

    /* Generate legal moves
     * if skip_pawn_promotion is false, include promotion to pawn as a
     * move, for later egtb probing
     */

    if (Board->side == WHITE) {
        for (ptype = PAWN; ptype < KING; ptype++) {
            int locations[MAX_IDENT_PIECES];
            memcpy(locations, Board->piece_locations[WHITE][ptype],
                   sizeof(locations));
            int *pos = locations;
            for (i = 0; i < Board->piece_type_count[WHITE][ptype]; i++) {
                if (ptype == PAWN) {
                    // pawn pushes
                    dest = SquareMake(Row(pos[i]) + 1, Column(pos[i]));
                    if (Board->board[dest] == 0) {
                        move_list->from = pos[i];
                        move_list->to = dest;
                        move_list->piece_moved = PAWN;
                        move_list->flag = 0;
                        if (Row(dest) == PromoteRow[WHITE]) {
                            move_list->flag |= PROMOTION;
                            move_list->piece_promoted = PAWN;
                        }
                        if (1) {
                            if (Row(dest) != PromoteRow[WHITE]) {
                                move_list++;
                                nmoves++;
                            } else {
                                if (!skip_pawn_promotion) {
                                    move_list++;
                                    nmoves++;
                                }
                                int promo_type;
                                for (promo_type = PAWN + 1; promo_type < KING;
                                     promo_type++) {
                                    if (!((1 << promo_type) & Promotions))
                                        continue;
                                    move_list->from = pos[i];
                                    move_list->to = dest;
                                    move_list->piece_moved = PAWN;
                                    move_list->flag = PROMOTION;
                                    move_list->piece_promoted = promo_type;
                                    move_list++;
                                    nmoves++;
                                }
                            }
                        }

#if !defined(NO_DOUBLE_PAWN_MOVES)
                        if (Row(pos[i]) == StartRow[WHITE]) {
                            // double move from pawn starting position
                            dest = SquareMake(Row(pos[i]) + 2, Column(pos[i]));
                            if (Board->board[dest] == 0) {
                                move_list->from = pos[i];
                                move_list->to = dest;
                                move_list->piece_moved = PAWN;
                                move_list->flag = 0;
                                if (1) {
                                    move_list++;
                                    nmoves++;
                                }
                            }
                        }
#endif
                    }

                    // now pawn captures
                    if (Column(pos[i]) > 0) {
                        dest = SquareMake(Row(pos[i]) + 1, Column(pos[i]) - 1);
                        if (Board->board[dest] < 0 ||
                            (Board->ep_square > 0 &&
                             dest == Board->ep_square)) {
                            move_list->from = pos[i];
                            move_list->to = dest;
                            move_list->piece_moved = PAWN;
                            move_list->flag = CAPTURE;
                            if (Board->ep_square > 0 &&
                                dest == Board->ep_square) {
                                move_list->flag |= EN_PASSANT;
                                move_list->piece_captured = -PAWN;
                            } else {
                                move_list->piece_captured = Board->board[dest];
                            }
                            if (Row(dest) == PromoteRow[WHITE]) {
                                move_list->flag |= PROMOTION;
                                move_list->piece_promoted = PAWN;
                            }
                            if (1) {
                                if (Row(dest) != PromoteRow[WHITE]) {
                                    move_list++;
                                    nmoves++;
                                } else {
                                    if (!skip_pawn_promotion) {
                                        move_list++;
                                        nmoves++;
                                    }
                                    int promo_type;
                                    for (promo_type = PAWN + 1;
                                         promo_type < KING; promo_type++) {
                                        if (!(Promotions & (1 << promo_type)))
                                            continue;
                                        move_list->from = pos[i];
                                        move_list->to = dest;
                                        move_list->piece_moved = PAWN;
                                        move_list->flag = (CAPTURE | PROMOTION);
                                        move_list->piece_captured =
                                            Board->board[dest];
                                        move_list->piece_promoted = promo_type;
                                        move_list++;
                                        nmoves++;
                                    }
                                }
                            }
                        }
                    }

                    if (Column(pos[i]) < (NCOLS - 1)) {
                        dest = SquareMake(Row(pos[i]) + 1, Column(pos[i]) + 1);
                        if (Board->board[dest] < 0 ||
                            (Board->ep_square > 0 &&
                             dest == Board->ep_square)) {
                            move_list->from = pos[i];
                            move_list->to = dest;
                            move_list->piece_moved = PAWN;
                            move_list->flag = CAPTURE;
                            if (Board->ep_square > 0 &&
                                dest == Board->ep_square) {
                                move_list->flag |= EN_PASSANT;
                                move_list->piece_captured = -PAWN;
                            } else {
                                move_list->piece_captured = Board->board[dest];
                            }
                            if (Row(dest) == PromoteRow[WHITE]) {
                                move_list->flag |= PROMOTION;
                                move_list->piece_promoted = PAWN;
                            }
                            if (1) {
                                if (Row(dest) != PromoteRow[WHITE]) {
                                    move_list++;
                                    nmoves++;
                                } else {
                                    if (!skip_pawn_promotion) {
                                        move_list++;
                                        nmoves++;
                                    }
                                    int promo_type;
                                    for (promo_type = PAWN + 1;
                                         promo_type < KING; promo_type++) {
                                        if (!(Promotions & (1 << promo_type)))
                                            continue;
                                        move_list->from = pos[i];
                                        move_list->to = dest;
                                        move_list->piece_moved = PAWN;
                                        move_list->flag = (CAPTURE | PROMOTION);
                                        move_list->piece_captured =
                                            Board->board[dest];
                                        move_list->piece_promoted = promo_type;
                                        move_list++;
                                        nmoves++;
                                    }
                                }
                            }
                        }
                    }
                }

                if (ptype & KNIGHT) {
                    sqp = KnightMoves[pos[i]];
                    while ((dest = *sqp++) != -1) {
                        if (Board->board[dest] <= 0) {
                            move_list->from = pos[i];
                            move_list->to = dest;
                            move_list->piece_moved = ptype;
                            move_list->flag = 0;
                            if (Board->board[dest] < 0) {
                                move_list->flag |= CAPTURE;
                                move_list->piece_captured = Board->board[dest];
                            }
                            if (1) {
                                move_list++;
                                nmoves++;
                            }
                        }
                    }
                }

                if (ptype & BISHOP) {
                    for (direc = 0; direc < 4; direc++) {
                        sqp = BishopMoves[pos[i]][direc];
                        while ((dest = *sqp++) != -1) {
                            if (Board->board[dest] <= 0) {
                                move_list->from = pos[i];
                                move_list->to = dest;
                                move_list->piece_moved = ptype;
                                move_list->flag = 0;
                                if (Board->board[dest] < 0) {
                                    move_list->flag |= CAPTURE;
                                    move_list->piece_captured =
                                        Board->board[dest];
                                }
                                if (1) {
                                    move_list++;
                                    nmoves++;
                                }
                            }
                            if (Board->board[dest] != 0)
                                break;
                        }
                    }
                }

                if (ptype & ROOK) {
                    for (direc = 0; direc < 4; direc++) {
                        sqp = RookMoves[pos[i]][direc];
                        while ((dest = *sqp++) != -1) {
                            if (Board->board[dest] <= 0) {
                                move_list->from = pos[i];
                                move_list->to = dest;
                                move_list->piece_moved = ptype;
                                move_list->flag = 0;
                                if (Board->board[dest] < 0) {
                                    move_list->flag |= CAPTURE;
                                    move_list->piece_captured =
                                        Board->board[dest];
                                }
                                if (1) {
                                    move_list++;
                                    nmoves++;
                                }
                            }
                            if (Board->board[dest] != 0)
                                break;
                        }
                    }
                }
            }
        }

        sqp = KingMoves[Board->wkpos];
        while ((dest = *sqp++) != -1) {
            if (KingAttack[NSQUARES * dest + Board->bkpos] >= 0)
                continue;
            if (Board->board[dest] <= 0) {
                move_list->from = Board->wkpos;
                move_list->to = dest;
                move_list->piece_moved = KING;
                move_list->flag = 0;
                if (Board->board[dest] < 0) {
                    move_list->flag |= CAPTURE;
                    move_list->piece_captured = Board->board[dest];
                }
                if (1) {
                    move_list++;
                    nmoves++;
                }
            }
        }

        // King side castling
        if (Board->castle & WK_CASTLE) {
            bool castle_possible = true;
            if (Board->wkpos != SquareMake(0, king_orig_col)) {
                if (Verbose > 1) {
                    char label[64];
                    sprintf(label, "wkpos=%d, does not allow k-side castling",
                            Board->wkpos);
                    DisplayBoard(Board, label);
                }
                castle_possible = false;
            }
            if (ROOK != Board->board[SquareMake(0, grook_orig_col)]) {
                if (Verbose > 1) {
                    char label[64];
                    sprintf(label, "No rook on %d for k-side castling",
                            SquareMake(0, grook_orig_col));
                    DisplayBoard(Board, label);
                }
                castle_possible = false;
            }
            /* general test whether rook and king can move unimpeded */
            int kstart = min(king_orig_col, KING_GCASTLE_DEST_COL);
            int kend = max(king_orig_col, KING_GCASTLE_DEST_COL);
            int cstart =
                min(kstart, min(grook_orig_col, ROOK_GCASTLE_DEST_COL));
            int cend = max(kend, max(grook_orig_col, ROOK_GCASTLE_DEST_COL));
            for (i = cstart; i <= cend; i++) {
                if (i == king_orig_col || i == grook_orig_col)
                    continue;
                if (Board->board[SquareMake(0, i)] != 0) {
                    castle_possible = false;
                    break;
                }
            }
            if (castle_possible) {
                /* need to check king is not attacked on any squares
                 * it passes over, including starting and ending squares,
                 * but possible excluding the "g-rook" for Chess960
                 */
                move_list->from = Board->wkpos;
                move_list->piece_moved = KING;
                move_list->flag = 0;
                for (i = kstart; i <= kend; i++) {
                    if (i == grook_orig_col)
                        continue;
                    move_list->to = SquareMake(0, i);
                    MakeMove(Board, move_list);
                    is_in_check = IsInCheck(Board, WHITE);
                    UnMakeMove(Board, move_list);
                    if (is_in_check) {
                        castle_possible = false;
                        break;
                    }
                }

                if (castle_possible) {
                    move_list->flag = WK_CASTLE;
                    move_list->to = SquareMake(0, KING_GCASTLE_DEST_COL);
                    move_list++;
                    nmoves++;
                }
            }
        }

        // Queen side castling
        if (Board->castle & WQ_CASTLE) {
            bool castle_possible = true;
            if (Board->wkpos != SquareMake(0, king_orig_col)) {
                if (Verbose > 1) {
                    char label[64];
                    sprintf(label, "wkpos=%d, does not allow q-side castling",
                            Board->wkpos);
                    DisplayBoard(Board, label);
                }
                castle_possible = false;
            }
            if (ROOK != Board->board[SquareMake(0, crook_orig_col)]) {
                if (Verbose > 1) {
                    char label[64];
                    sprintf(label, "No rook on %d for q-side castling",
                            SquareMake(0, crook_orig_col));
                    DisplayBoard(Board, label);
                }
                castle_possible = false;
            }
            /* general test whether rook and king can move unimpeded */
            int kstart = min(king_orig_col, KING_CCASTLE_DEST_COL);
            int kend = max(king_orig_col, KING_CCASTLE_DEST_COL);
            int cstart =
                min(kstart, min(crook_orig_col, ROOK_CCASTLE_DEST_COL));
            int cend = max(kend, max(crook_orig_col, ROOK_CCASTLE_DEST_COL));
            for (i = cstart; i <= cend; i++) {
                if (i == king_orig_col || i == crook_orig_col)
                    continue;
                if (Board->board[SquareMake(0, i)] != 0) {
                    castle_possible = false;
                    break;
                }
            }
            if (castle_possible) {
                /* need to check king is not attacked on any squares
                 * it passes over, including starting and ending squares,
                 * but possible excluding the "c-rook" for Chess960
                 */
                move_list->from = Board->wkpos;
                move_list->piece_moved = KING;
                move_list->flag = 0;
                for (i = kstart; i <= kend; i++) {
                    if (i == crook_orig_col)
                        continue;
                    move_list->to = SquareMake(0, i);
                    MakeMove(Board, move_list);
                    is_in_check = IsInCheck(Board, WHITE);
                    UnMakeMove(Board, move_list);
                    if (is_in_check) {
                        castle_possible = false;
                        break;
                    }
                }

                if (castle_possible) {
                    move_list->flag = WQ_CASTLE;
                    move_list->to = SquareMake(0, KING_CCASTLE_DEST_COL);
                    move_list++;
                    nmoves++;
                }
            }
        }
    } else { // Black moves
        for (ptype = PAWN; ptype < KING; ptype++) {
            int locations[MAX_IDENT_PIECES];
            memcpy(locations, Board->piece_locations[BLACK][ptype],
                   sizeof(locations));
            int *pos = locations;
            for (i = 0; i < Board->piece_type_count[BLACK][ptype]; i++) {
                if (ptype == PAWN) {
                    // pawn pushes
                    dest = SquareMake(Row(pos[i]) - 1, Column(pos[i]));
                    if (Board->board[dest] == 0) {
                        move_list->from = pos[i];
                        move_list->to = dest;
                        move_list->piece_moved = -PAWN;
                        move_list->flag = 0;
                        if (Row(dest) == PromoteRow[BLACK]) {
                            move_list->flag |= PROMOTION;
                            move_list->piece_promoted = -PAWN;
                        }
                        if (1) {
                            if (Row(dest) != PromoteRow[BLACK]) {
                                move_list++;
                                nmoves++;
                            } else {
                                if (!skip_pawn_promotion) {
                                    move_list++;
                                    nmoves++;
                                }
                                int promo_type;
                                for (promo_type = PAWN + 1; promo_type < KING;
                                     promo_type++) {
                                    if (!((1 << promo_type) & Promotions))
                                        continue;
                                    move_list->from = pos[i];
                                    move_list->to = dest;
                                    move_list->piece_moved = -PAWN;
                                    move_list->flag = PROMOTION;
                                    move_list->piece_promoted = -promo_type;
                                    move_list++;
                                    nmoves++;
                                }
                            }
                        }

#if !defined(NO_DOUBLE_PAWN_MOVES)
                        if (Row(pos[i]) == StartRow[BLACK]) {
                            // double move from pawn starting position
                            dest = SquareMake(Row(pos[i]) - 2, Column(pos[i]));
                            if (Board->board[dest] == 0) {
                                move_list->from = pos[i];
                                move_list->to = dest;
                                move_list->piece_moved = -PAWN;
                                move_list->flag = 0;
                                if (1) {
                                    move_list++;
                                    nmoves++;
                                }
                            }
                        }
#endif
                    }

                    // now pawn captures
                    if (Column(pos[i]) > 0) {
                        dest = SquareMake(Row(pos[i]) - 1, Column(pos[i]) - 1);
                        if (Board->board[dest] > 0 ||
                            (Board->ep_square > 0 &&
                             dest == Board->ep_square)) {
                            move_list->from = pos[i];
                            move_list->to = dest;
                            move_list->piece_moved = -PAWN;
                            move_list->flag = CAPTURE;
                            if (Board->ep_square > 0 &&
                                dest == Board->ep_square) {
                                move_list->flag |= EN_PASSANT;
                                move_list->piece_captured = PAWN;
                            } else {
                                move_list->piece_captured = Board->board[dest];
                            }
                            if (Row(dest) == PromoteRow[BLACK]) {
                                move_list->flag |= PROMOTION;
                                move_list->piece_promoted = -PAWN;
                            }
                            if (1) {
                                if (Row(dest) != PromoteRow[BLACK]) {
                                    move_list++;
                                    nmoves++;
                                } else {
                                    if (!skip_pawn_promotion) {
                                        move_list++;
                                        nmoves++;
                                    }
                                    int promo_type;
                                    for (promo_type = PAWN + 1;
                                         promo_type < KING; promo_type++) {
                                        if (!(Promotions & (1 << promo_type)))
                                            continue;
                                        move_list->from = pos[i];
                                        move_list->to = dest;
                                        move_list->piece_moved = -PAWN;
                                        move_list->flag = (CAPTURE | PROMOTION);
                                        move_list->piece_captured =
                                            Board->board[dest];
                                        move_list->piece_promoted = -promo_type;
                                        move_list++;
                                        nmoves++;
                                    }
                                }
                            }
                        }
                    }

                    if (Column(pos[i]) < (NCOLS - 1)) {
                        dest = SquareMake(Row(pos[i]) - 1, Column(pos[i]) + 1);
                        if (Board->board[dest] > 0 ||
                            (Board->ep_square > 0 &&
                             dest == Board->ep_square)) {
                            move_list->from = pos[i];
                            move_list->to = dest;
                            move_list->piece_moved = -PAWN;
                            move_list->flag = CAPTURE;
                            if (Board->ep_square > 0 &&
                                dest == Board->ep_square) {
                                move_list->flag |= EN_PASSANT;
                                move_list->piece_captured = PAWN;
                            } else {
                                move_list->piece_captured = Board->board[dest];
                            }
                            if (Row(dest) == PromoteRow[BLACK]) {
                                move_list->flag |= PROMOTION;
                                move_list->piece_promoted = -PAWN;
                            }
                            if (1) {
                                if (Row(dest) != PromoteRow[BLACK]) {
                                    move_list++;
                                    nmoves++;
                                } else {
                                    if (!skip_pawn_promotion) {
                                        move_list++;
                                        nmoves++;
                                    }
                                    int promo_type;
                                    for (promo_type = PAWN + 1;
                                         promo_type < KING; promo_type++) {
                                        if (!(Promotions & (1 << promo_type)))
                                            continue;
                                        move_list->from = pos[i];
                                        move_list->to = dest;
                                        move_list->piece_moved = -PAWN;
                                        move_list->flag = (CAPTURE | PROMOTION);
                                        move_list->piece_captured =
                                            Board->board[dest];
                                        move_list->piece_promoted = -promo_type;
                                        move_list++;
                                        nmoves++;
                                    }
                                }
                            }
                        }
                    }
                }

                if (ptype & KNIGHT) {
                    sqp = KnightMoves[pos[i]];
                    while ((dest = *sqp++) != -1) {
                        if (Board->board[dest] >= 0) {
                            move_list->from = pos[i];
                            move_list->to = dest;
                            move_list->piece_moved = -ptype;
                            move_list->flag = 0;
                            if (Board->board[dest] > 0) {
                                move_list->flag |= CAPTURE;
                                move_list->piece_captured = Board->board[dest];
                            }
                            if (1) {
                                move_list++;
                                nmoves++;
                            }
                        }
                    }
                }

                if (ptype & BISHOP) {
                    for (direc = 0; direc < 4; direc++) {
                        sqp = BishopMoves[pos[i]][direc];
                        while ((dest = *sqp++) != -1) {
                            if (Board->board[dest] >= 0) {
                                move_list->from = pos[i];
                                move_list->to = dest;
                                move_list->piece_moved = -ptype;
                                move_list->flag = 0;
                                if (Board->board[dest] > 0) {
                                    move_list->flag |= CAPTURE;
                                    move_list->piece_captured =
                                        Board->board[dest];
                                }
                                if (1) {
                                    move_list++;
                                    nmoves++;
                                }
                            }
                            if (Board->board[dest] != 0)
                                break;
                        }
                    }
                }

                if (ptype & ROOK) {
                    for (direc = 0; direc < 4; direc++) {
                        sqp = RookMoves[pos[i]][direc];
                        while ((dest = *sqp++) != -1) {
                            if (Board->board[dest] >= 0) {
                                move_list->from = pos[i];
                                move_list->to = dest;
                                move_list->piece_moved = -ptype;
                                move_list->flag = 0;
                                if (Board->board[dest] > 0) {
                                    move_list->flag |= CAPTURE;
                                    move_list->piece_captured =
                                        Board->board[dest];
                                }
                                if (1) {
                                    move_list++;
                                    nmoves++;
                                }
                            }
                            if (Board->board[dest] != 0)
                                break;
                        }
                    }
                }
            }
        }

        sqp = KingMoves[Board->bkpos];
        while ((dest = *sqp++) != -1) {
            if (KingAttack[NSQUARES * dest + Board->wkpos] >= 0)
                continue;
            if (Board->board[dest] >= 0) {
                move_list->from = Board->bkpos;
                move_list->to = dest;
                move_list->piece_moved = -KING;
                move_list->flag = 0;
                if (Board->board[dest] > 0) {
                    move_list->flag |= CAPTURE;
                    move_list->piece_captured = Board->board[dest];
                }
                if (1) {
                    move_list++;
                    nmoves++;
                }
            }
        }

        // King side castling
        if (Board->castle & BK_CASTLE) {
            bool castle_possible = true;
            if (Board->bkpos != SquareMake(NROWS - 1, king_orig_col)) {
                if (Verbose > 1) {
                    char label[64];
                    sprintf(label, "bkpos=%d, does not allow k-side castling",
                            Board->bkpos);
                    DisplayBoard(Board, label);
                }
                castle_possible = false;
            }
            if (-ROOK != Board->board[SquareMake(NROWS - 1, grook_orig_col)]) {
                if (Verbose > 1) {
                    char label[64];
                    sprintf(label, "No rook on %d for k-side castling",
                            SquareMake(NROWS - 1, grook_orig_col));
                    DisplayBoard(Board, label);
                }
                castle_possible = false;
            }
            /* general test whether rook and king can move unimpeded */
            int kstart = min(king_orig_col, KING_GCASTLE_DEST_COL);
            int kend = max(king_orig_col, KING_GCASTLE_DEST_COL);
            int cstart =
                min(kstart, min(grook_orig_col, ROOK_GCASTLE_DEST_COL));
            int cend = max(kend, max(grook_orig_col, ROOK_GCASTLE_DEST_COL));
            for (i = cstart; i <= cend; i++) {
                if (i == king_orig_col || i == grook_orig_col)
                    continue;
                if (Board->board[SquareMake(NROWS - 1, i)] != 0) {
                    castle_possible = false;
                    break;
                }
            }
            if (castle_possible) {
                /* need to check king is not attacked on any squares
                 * it passes over, including starting and ending squares,
                 * but possible excluding the "g-rook" for Chess960
                 */
                move_list->from = Board->bkpos;
                move_list->piece_moved = -KING;
                move_list->flag = 0;
                for (i = kstart; i <= kend; i++) {
                    if (i == grook_orig_col)
                        continue;
                    move_list->to = SquareMake(NROWS - 1, i);
                    MakeMove(Board, move_list);
                    is_in_check = IsInCheck(Board, BLACK);
                    UnMakeMove(Board, move_list);
                    if (is_in_check) {
                        castle_possible = false;
                        break;
                    }
                }

                if (castle_possible) {
                    move_list->flag = BK_CASTLE;
                    move_list->to =
                        SquareMake(NROWS - 1, KING_GCASTLE_DEST_COL);
                    move_list++;
                    nmoves++;
                }
            }
        }

        // Queen side castling
        if (Board->castle & BQ_CASTLE) {
            bool castle_possible = true;
            if (Board->bkpos != SquareMake(NROWS - 1, king_orig_col)) {
                if (Verbose > 1) {
                    char label[64];
                    sprintf(label, "bkpos=%d, does not allow q-side castling",
                            Board->bkpos);
                    DisplayBoard(Board, label);
                }
                castle_possible = false;
            }
            if (-ROOK != Board->board[SquareMake(NROWS - 1, crook_orig_col)]) {
                if (Verbose > 1) {
                    char label[64];
                    sprintf(label, "No rook on %d for q-side castling",
                            SquareMake(NROWS - 1, crook_orig_col));
                    DisplayBoard(Board, label);
                }
                castle_possible = false;
            }
            /* general test whether rook and king can move unimpeded */
            int kstart = min(king_orig_col, KING_CCASTLE_DEST_COL);
            int kend = max(king_orig_col, KING_CCASTLE_DEST_COL);
            int cstart =
                min(kstart, min(crook_orig_col, ROOK_CCASTLE_DEST_COL));
            int cend = max(kend, max(crook_orig_col, ROOK_CCASTLE_DEST_COL));
            for (i = cstart; i <= cend; i++) {
                if (i == king_orig_col || i == crook_orig_col)
                    continue;
                if (Board->board[SquareMake(NROWS - 1, i)] != 0) {
                    castle_possible = false;
                    break;
                }
            }
            if (castle_possible) {
                /* need to check king is not attacked on any squares
                 * it passes over, including starting and ending squares,
                 * but possible excluding the "c-rook" for Chess960
                 */
                move_list->from = Board->bkpos;
                move_list->piece_moved = -KING;
                move_list->flag = 0;
                for (i = kstart; i <= kend; i++) {
                    if (i == crook_orig_col)
                        continue;
                    move_list->to = SquareMake(NROWS - 1, i);
                    MakeMove(Board, move_list);
                    is_in_check = IsInCheck(Board, BLACK);
                    UnMakeMove(Board, move_list);
                    if (is_in_check) {
                        castle_possible = false;
                        break;
                    }
                }

                if (castle_possible) {
                    move_list->flag = BQ_CASTLE;
                    move_list->to =
                        SquareMake(NROWS - 1, KING_CCASTLE_DEST_COL);
                    move_list++;
                    nmoves++;
                }
            }
        }
    }

    return nmoves;
}

static int GenLegalMoves(BOARD *Board, Move *move_list,
                         bool skip_pawn_promotion, bool all_promos) {
    int nmoves = 0, ptype, direc, *sqp, dest, i;
    unsigned int promos = Promotions;
    bool is_check, is_in_check;

    /* Generate legal moves
     * if skip_pawn_promotion is false, include promotion to pawn as a
     * move, for later egtb probing
     */

    if (all_promos || (SearchSubgamePromotions && Board->num_pieces < 8))
        promos = QRBN_PROMOTIONS;

    if (Board->side == WHITE) {
        for (ptype = PAWN; ptype < KING; ptype++) {
            int locations[MAX_IDENT_PIECES];
            memcpy(locations, Board->piece_locations[WHITE][ptype],
                   sizeof(locations));
            int *pos = locations;
            for (i = 0; i < Board->piece_type_count[WHITE][ptype]; i++) {
                if (ptype == PAWN) {
                    // pawn pushes
                    dest = SquareMake(Row(pos[i]) + 1, Column(pos[i]));
                    if (Board->board[dest] == 0) {
                        move_list->from = pos[i];
                        move_list->to = dest;
                        move_list->piece_moved = PAWN;
                        move_list->flag = 0;
                        if (Row(dest) == PromoteRow[WHITE]) {
                            move_list->flag |= PROMOTION;
                            move_list->piece_promoted = PAWN;
                        }
                        MakeMove(Board, move_list);
                        is_in_check = IsInCheck(Board, WHITE);
                        if (!is_in_check) {
                            if (IsInCheck(Board, BLACK))
                                move_list->flag |= CHECK;
                        }
                        UnMakeMove(Board, move_list);
                        if (!is_in_check) {
                            if (Row(dest) != PromoteRow[WHITE]) {
                                move_list++;
                                nmoves++;
                            } else {
                                if (!skip_pawn_promotion) {
                                    move_list++;
                                    nmoves++;
                                }
                                int promo_type;
                                for (promo_type = KING - 1; promo_type > PAWN;
                                     promo_type--) {
                                    if (!((1 << promo_type) & promos))
                                        continue;
                                    move_list->from = pos[i];
                                    move_list->to = dest;
                                    move_list->piece_moved = PAWN;
                                    move_list->flag = PROMOTION;
                                    move_list->piece_promoted = promo_type;
                                    MakeMove(Board, move_list);
                                    if (IsInCheck(Board, BLACK))
                                        move_list->flag |= CHECK;
                                    UnMakeMove(Board, move_list);
                                    move_list++;
                                    nmoves++;
                                }
                            }
                        }

#if !defined(NO_DOUBLE_PAWN_MOVES)
                        if (Row(pos[i]) == StartRow[WHITE]) {
                            // double move from pawn starting position
                            dest = SquareMake(Row(pos[i]) + 2, Column(pos[i]));
                            if (Board->board[dest] == 0) {
                                move_list->from = pos[i];
                                move_list->to = dest;
                                move_list->piece_moved = PAWN;
                                move_list->flag = 0;
                                MakeMove(Board, move_list);
                                is_in_check = IsInCheck(Board, WHITE);
                                if (!is_in_check) {
                                    if (IsInCheck(Board, BLACK))
                                        move_list->flag |= CHECK;
                                }
                                UnMakeMove(Board, move_list);
                                if (!is_in_check) {
                                    move_list++;
                                    nmoves++;
                                }
                            }
                        }
#endif
                    }

                    // now pawn captures
                    if (Column(pos[i]) > 0) {
                        dest = SquareMake(Row(pos[i]) + 1, Column(pos[i]) - 1);
                        if (Board->board[dest] < 0 ||
                            (Board->ep_square > 0 &&
                             dest == Board->ep_square)) {
                            move_list->from = pos[i];
                            move_list->to = dest;
                            move_list->piece_moved = PAWN;
                            move_list->flag = CAPTURE;
                            if (Board->ep_square > 0 &&
                                dest == Board->ep_square) {
                                move_list->flag |= EN_PASSANT;
                                move_list->piece_captured = -PAWN;
                            } else {
                                move_list->piece_captured = Board->board[dest];
                            }
                            if (Row(dest) == PromoteRow[WHITE]) {
                                move_list->flag |= PROMOTION;
                                move_list->piece_promoted = PAWN;
                            }
                            MakeMove(Board, move_list);
                            is_in_check = IsInCheck(Board, WHITE);
                            if (!is_in_check) {
                                if (IsInCheck(Board, BLACK))
                                    move_list->flag |= CHECK;
                            }
                            UnMakeMove(Board, move_list);
                            if (!is_in_check) {
                                if (Row(dest) != PromoteRow[WHITE]) {
                                    move_list++;
                                    nmoves++;
                                } else {
                                    if (!skip_pawn_promotion) {
                                        move_list++;
                                        nmoves++;
                                    }
                                    int promo_type;
                                    for (promo_type = PAWN + 1;
                                         promo_type < KING; promo_type++) {
                                        if (!(promos & (1 << promo_type)))
                                            continue;
                                        move_list->from = pos[i];
                                        move_list->to = dest;
                                        move_list->piece_moved = PAWN;
                                        move_list->flag = (CAPTURE | PROMOTION);
                                        move_list->piece_captured =
                                            Board->board[dest];
                                        move_list->piece_promoted = promo_type;
                                        MakeMove(Board, move_list);
                                        if (IsInCheck(Board, BLACK))
                                            move_list->flag |= CHECK;
                                        UnMakeMove(Board, move_list);
                                        move_list++;
                                        nmoves++;
                                    }
                                }
                            }
                        }
                    }

                    if (Column(pos[i]) < (NCOLS - 1)) {
                        dest = SquareMake(Row(pos[i]) + 1, Column(pos[i]) + 1);
                        if (Board->board[dest] < 0 ||
                            (Board->ep_square > 0 &&
                             dest == Board->ep_square)) {
                            move_list->from = pos[i];
                            move_list->to = dest;
                            move_list->piece_moved = PAWN;
                            move_list->flag = CAPTURE;
                            if (Board->ep_square > 0 &&
                                dest == Board->ep_square) {
                                move_list->flag |= EN_PASSANT;
                                move_list->piece_captured = -PAWN;
                            } else {
                                move_list->piece_captured = Board->board[dest];
                            }
                            if (Row(dest) == PromoteRow[WHITE]) {
                                move_list->flag |= PROMOTION;
                                move_list->piece_promoted = PAWN;
                            }
                            MakeMove(Board, move_list);
                            is_in_check = IsInCheck(Board, WHITE);
                            if (!is_in_check) {
                                if (IsInCheck(Board, BLACK))
                                    move_list->flag |= CHECK;
                            }
                            UnMakeMove(Board, move_list);
                            if (!is_in_check) {
                                if (Row(dest) != PromoteRow[WHITE]) {
                                    move_list++;
                                    nmoves++;
                                } else {
                                    if (!skip_pawn_promotion) {
                                        move_list++;
                                        nmoves++;
                                    }
                                    int promo_type;
                                    for (promo_type = PAWN + 1;
                                         promo_type < KING; promo_type++) {
                                        if (!(promos & (1 << promo_type)))
                                            continue;
                                        move_list->from = pos[i];
                                        move_list->to = dest;
                                        move_list->piece_moved = PAWN;
                                        move_list->flag = (CAPTURE | PROMOTION);
                                        move_list->piece_captured =
                                            Board->board[dest];
                                        move_list->piece_promoted = promo_type;
                                        MakeMove(Board, move_list);
                                        if (IsInCheck(Board, BLACK))
                                            move_list->flag |= CHECK;
                                        UnMakeMove(Board, move_list);
                                        move_list++;
                                        nmoves++;
                                    }
                                }
                            }
                        }
                    }
                }

                if (ptype & KNIGHT) {
                    sqp = KnightMoves[pos[i]];
                    while ((dest = *sqp++) != -1) {
                        if (Board->board[dest] <= 0) {
                            move_list->from = pos[i];
                            move_list->to = dest;
                            move_list->piece_moved = ptype;
                            move_list->flag = 0;
                            if (Board->board[dest] < 0) {
                                move_list->flag |= CAPTURE;
                                move_list->piece_captured = Board->board[dest];
                            }
                            MakeMove(Board, move_list);
                            is_in_check = IsInCheck(Board, WHITE);
                            if (!is_in_check) {
                                if (IsInCheck(Board, BLACK))
                                    move_list->flag |= CHECK;
                            }
                            UnMakeMove(Board, move_list);
                            if (!is_in_check) {
                                move_list++;
                                nmoves++;
                            }
                        }
                    }
                }

                if (ptype & BISHOP) {
                    for (direc = 0; direc < 4; direc++) {
                        sqp = BishopMoves[pos[i]][direc];
                        while ((dest = *sqp++) != -1) {
                            if (Board->board[dest] <= 0) {
                                move_list->from = pos[i];
                                move_list->to = dest;
                                move_list->piece_moved = ptype;
                                move_list->flag = 0;
                                if (Board->board[dest] < 0) {
                                    move_list->flag |= CAPTURE;
                                    move_list->piece_captured =
                                        Board->board[dest];
                                }
                                MakeMove(Board, move_list);
                                is_in_check = IsInCheck(Board, WHITE);
                                if (!is_in_check) {
                                    if (IsInCheck(Board, BLACK))
                                        move_list->flag |= CHECK;
                                }
                                UnMakeMove(Board, move_list);
                                if (!is_in_check) {
                                    move_list++;
                                    nmoves++;
                                }
                            }
                            if (Board->board[dest] != 0)
                                break;
                        }
                    }
                }

                if (ptype & ROOK) {
                    for (direc = 0; direc < 4; direc++) {
                        sqp = RookMoves[pos[i]][direc];
                        while ((dest = *sqp++) != -1) {
                            if (Board->board[dest] <= 0) {
                                move_list->from = pos[i];
                                move_list->to = dest;
                                move_list->piece_moved = ptype;
                                move_list->flag = 0;
                                if (Board->board[dest] < 0) {
                                    move_list->flag |= CAPTURE;
                                    move_list->piece_captured =
                                        Board->board[dest];
                                }
                                MakeMove(Board, move_list);
                                is_in_check = IsInCheck(Board, WHITE);
                                if (!is_in_check) {
                                    if (IsInCheck(Board, BLACK))
                                        move_list->flag |= CHECK;
                                }
                                UnMakeMove(Board, move_list);
                                if (!is_in_check) {
                                    move_list++;
                                    nmoves++;
                                }
                            }
                            if (Board->board[dest] != 0)
                                break;
                        }
                    }
                }
            }
        }

        sqp = KingMoves[Board->wkpos];
        while ((dest = *sqp++) != -1) {
            if (KingAttack[NSQUARES * dest + Board->bkpos] >= 0)
                continue;
            if (Board->board[dest] <= 0) {
                move_list->from = Board->wkpos;
                move_list->to = dest;
                move_list->piece_moved = KING;
                move_list->flag = 0;
                if (Board->board[dest] < 0) {
                    move_list->flag |= CAPTURE;
                    move_list->piece_captured = Board->board[dest];
                }
                MakeMove(Board, move_list);
                is_in_check = IsInCheck(Board, WHITE);
                if (!is_in_check) {
                    if (IsInCheck(Board, BLACK))
                        move_list->flag |= CHECK;
                }
                UnMakeMove(Board, move_list);
                if (!is_in_check) {
                    move_list++;
                    nmoves++;
                }
            }
        }

        // King side castling
        if (Board->castle & WK_CASTLE) {
            bool castle_possible = true;
            if (Board->wkpos != SquareMake(0, king_orig_col)) {
                if (Verbose > 1) {
                    char label[64];
                    sprintf(label, "wkpos=%d, does not allow k-side castling",
                            Board->wkpos);
                    DisplayBoard(Board, label);
                }
                castle_possible = false;
            }
            if (ROOK != Board->board[SquareMake(0, grook_orig_col)]) {
                if (Verbose > 1) {
                    char label[64];
                    sprintf(label, "No rook on %d for k-side castling",
                            SquareMake(0, grook_orig_col));
                    DisplayBoard(Board, label);
                }
                castle_possible = false;
            }
            /* general test whether rook and king can move unimpeded */
            int kstart = min(king_orig_col, KING_GCASTLE_DEST_COL);
            int kend = max(king_orig_col, KING_GCASTLE_DEST_COL);
            int cstart =
                min(kstart, min(grook_orig_col, ROOK_GCASTLE_DEST_COL));
            int cend = max(kend, max(grook_orig_col, ROOK_GCASTLE_DEST_COL));
            for (i = cstart; i <= cend; i++) {
                if (i == king_orig_col || i == grook_orig_col)
                    continue;
                if (Board->board[SquareMake(0, i)] != 0) {
                    castle_possible = false;
                    break;
                }
            }
            if (castle_possible) {
                /* need to check king is not attacked on any squares
                 * it passes over, including starting and ending squares,
                 * but possible excluding the "g-rook" for Chess960
                 */
                move_list->from = Board->wkpos;
                move_list->piece_moved = KING;
                move_list->flag = 0;
                for (i = kstart; i <= kend; i++) {
                    if (i == grook_orig_col)
                        continue;
                    move_list->to = SquareMake(0, i);
                    MakeMove(Board, move_list);
                    is_in_check = IsInCheck(Board, WHITE);
                    UnMakeMove(Board, move_list);
                    if (is_in_check) {
                        castle_possible = false;
                        break;
                    }
                }

                if (castle_possible) {
                    move_list->flag = WK_CASTLE;
                    move_list->to = SquareMake(0, KING_GCASTLE_DEST_COL);
                    MakeMove(Board, move_list);
                    if (IsInCheck(Board, BLACK))
                        move_list->flag |= CHECK;
                    UnMakeMove(Board, move_list);
                    move_list++;
                    nmoves++;
                }
            }
        }

        // Queen side castling
        if (Board->castle & WQ_CASTLE) {
            bool castle_possible = true;
            if (Board->wkpos != SquareMake(0, king_orig_col)) {
                if (Verbose > 1) {
                    char label[64];
                    sprintf(label, "wkpos=%d, does not allow q-side castling",
                            Board->wkpos);
                    DisplayBoard(Board, label);
                }
                castle_possible = false;
            }
            if (ROOK != Board->board[SquareMake(0, crook_orig_col)]) {
                if (Verbose > 1) {
                    char label[64];
                    sprintf(label, "No rook on %d for q-side castling",
                            SquareMake(0, crook_orig_col));
                    DisplayBoard(Board, label);
                }
                castle_possible = false;
            }
            /* general test whether rook and king can move unimpeded */
            int kstart = min(king_orig_col, KING_CCASTLE_DEST_COL);
            int kend = max(king_orig_col, KING_CCASTLE_DEST_COL);
            int cstart =
                min(kstart, min(crook_orig_col, ROOK_CCASTLE_DEST_COL));
            int cend = max(kend, max(crook_orig_col, ROOK_CCASTLE_DEST_COL));
            for (i = cstart; i <= cend; i++) {
                if (i == king_orig_col || i == crook_orig_col)
                    continue;
                if (Board->board[SquareMake(0, i)] != 0) {
                    castle_possible = false;
                    break;
                }
            }
            if (castle_possible) {
                /* need to check king is not attacked on any squares
                 * it passes over, including starting and ending squares,
                 * but possible excluding the "c-rook" for Chess960
                 */
                move_list->from = Board->wkpos;
                move_list->piece_moved = KING;
                move_list->flag = 0;
                for (i = kstart; i <= kend; i++) {
                    if (i == crook_orig_col)
                        continue;
                    move_list->to = SquareMake(0, i);
                    MakeMove(Board, move_list);
                    is_in_check = IsInCheck(Board, WHITE);
                    UnMakeMove(Board, move_list);
                    if (is_in_check) {
                        castle_possible = false;
                        break;
                    }
                }

                if (castle_possible) {
                    move_list->flag = WQ_CASTLE;
                    move_list->to = SquareMake(0, KING_CCASTLE_DEST_COL);
                    MakeMove(Board, move_list);
                    if (IsInCheck(Board, BLACK))
                        move_list->flag |= CHECK;
                    UnMakeMove(Board, move_list);
                    move_list++;
                    nmoves++;
                }
            }
        }
    } else { // Black moves
        for (ptype = PAWN; ptype < KING; ptype++) {
            int locations[MAX_IDENT_PIECES];
            memcpy(locations, Board->piece_locations[BLACK][ptype],
                   sizeof(locations));
            int *pos = locations;
            for (i = 0; i < Board->piece_type_count[BLACK][ptype]; i++) {
                if (ptype == PAWN) {
                    // pawn pushes
                    dest = SquareMake(Row(pos[i]) - 1, Column(pos[i]));
                    if (Board->board[dest] == 0) {
                        move_list->from = pos[i];
                        move_list->to = dest;
                        move_list->piece_moved = -PAWN;
                        move_list->flag = 0;
                        if (Row(dest) == PromoteRow[BLACK]) {
                            move_list->flag |= PROMOTION;
                            move_list->piece_promoted = -PAWN;
                        }
                        MakeMove(Board, move_list);
                        is_in_check = IsInCheck(Board, BLACK);
                        if (!is_in_check) {
                            if (IsInCheck(Board, WHITE))
                                move_list->flag |= CHECK;
                        }
                        UnMakeMove(Board, move_list);
                        if (!is_in_check) {
                            if (Row(dest) != PromoteRow[BLACK]) {
                                move_list++;
                                nmoves++;
                            } else {
                                if (!skip_pawn_promotion) {
                                    move_list++;
                                    nmoves++;
                                }
                                int promo_type;
                                for (promo_type = PAWN + 1; promo_type < KING;
                                     promo_type++) {
                                    if (!((1 << promo_type) & promos))
                                        continue;
                                    move_list->from = pos[i];
                                    move_list->to = dest;
                                    move_list->piece_moved = -PAWN;
                                    move_list->flag = PROMOTION;
                                    move_list->piece_promoted = -promo_type;
                                    MakeMove(Board, move_list);
                                    if (IsInCheck(Board, WHITE))
                                        move_list->flag |= CHECK;
                                    UnMakeMove(Board, move_list);
                                    move_list++;
                                    nmoves++;
                                }
                            }
                        }

#if !defined(NO_DOUBLE_PAWN_MOVES)
                        if (Row(pos[i]) == StartRow[BLACK]) {
                            // double move from pawn starting position
                            dest = SquareMake(Row(pos[i]) - 2, Column(pos[i]));
                            if (Board->board[dest] == 0) {
                                move_list->from = pos[i];
                                move_list->to = dest;
                                move_list->piece_moved = -PAWN;
                                move_list->flag = 0;
                                MakeMove(Board, move_list);
                                is_in_check = IsInCheck(Board, BLACK);
                                if (!is_in_check) {
                                    if (IsInCheck(Board, WHITE))
                                        move_list->flag |= CHECK;
                                }
                                UnMakeMove(Board, move_list);
                                if (!is_in_check) {
                                    move_list++;
                                    nmoves++;
                                }
                            }
                        }
#endif
                    }

                    // now pawn captures
                    if (Column(pos[i]) > 0) {
                        dest = SquareMake(Row(pos[i]) - 1, Column(pos[i]) - 1);
                        if (Board->board[dest] > 0 ||
                            (Board->ep_square > 0 &&
                             dest == Board->ep_square)) {
                            move_list->from = pos[i];
                            move_list->to = dest;
                            move_list->piece_moved = -PAWN;
                            move_list->flag = CAPTURE;
                            if (Board->ep_square > 0 &&
                                dest == Board->ep_square) {
                                move_list->flag |= EN_PASSANT;
                                move_list->piece_captured = PAWN;
                            } else {
                                move_list->piece_captured = Board->board[dest];
                            }
                            if (Row(dest) == PromoteRow[BLACK]) {
                                move_list->flag |= PROMOTION;
                                move_list->piece_promoted = -PAWN;
                            }
                            MakeMove(Board, move_list);
                            is_in_check = IsInCheck(Board, BLACK);
                            if (!is_in_check) {
                                if (IsInCheck(Board, WHITE))
                                    move_list->flag |= CHECK;
                            }
                            UnMakeMove(Board, move_list);
                            if (!is_in_check) {
                                if (Row(dest) != PromoteRow[BLACK]) {
                                    move_list++;
                                    nmoves++;
                                } else {
                                    if (!skip_pawn_promotion) {
                                        move_list++;
                                        nmoves++;
                                    }
                                    int promo_type;
                                    for (promo_type = PAWN + 1;
                                         promo_type < KING; promo_type++) {
                                        if (!(promos & (1 << promo_type)))
                                            continue;
                                        move_list->from = pos[i];
                                        move_list->to = dest;
                                        move_list->piece_moved = -PAWN;
                                        move_list->flag = (CAPTURE | PROMOTION);
                                        move_list->piece_captured =
                                            Board->board[dest];
                                        move_list->piece_promoted = -promo_type;
                                        MakeMove(Board, move_list);
                                        if (IsInCheck(Board, WHITE))
                                            move_list->flag |= CHECK;
                                        UnMakeMove(Board, move_list);
                                        move_list++;
                                        nmoves++;
                                    }
                                }
                            }
                        }
                    }

                    if (Column(pos[i]) < (NCOLS - 1)) {
                        dest = SquareMake(Row(pos[i]) - 1, Column(pos[i]) + 1);
                        if (Board->board[dest] > 0 ||
                            (Board->ep_square > 0 &&
                             dest == Board->ep_square)) {
                            move_list->from = pos[i];
                            move_list->to = dest;
                            move_list->piece_moved = -PAWN;
                            move_list->flag = CAPTURE;
                            if (Board->ep_square > 0 &&
                                dest == Board->ep_square) {
                                move_list->flag |= EN_PASSANT;
                                move_list->piece_captured = PAWN;
                            } else {
                                move_list->piece_captured = Board->board[dest];
                            }
                            if (Row(dest) == PromoteRow[BLACK]) {
                                move_list->flag |= PROMOTION;
                                move_list->piece_promoted = -PAWN;
                            }
                            MakeMove(Board, move_list);
                            is_in_check = IsInCheck(Board, BLACK);
                            if (!is_in_check) {
                                if (IsInCheck(Board, WHITE))
                                    move_list->flag |= CHECK;
                            }
                            UnMakeMove(Board, move_list);
                            if (!is_in_check) {
                                if (Row(dest) != PromoteRow[BLACK]) {
                                    move_list++;
                                    nmoves++;
                                } else {
                                    if (!skip_pawn_promotion) {
                                        move_list++;
                                        nmoves++;
                                    }
                                    int promo_type;
                                    for (promo_type = PAWN + 1;
                                         promo_type < KING; promo_type++) {
                                        if (!(promos & (1 << promo_type)))
                                            continue;
                                        move_list->from = pos[i];
                                        move_list->to = dest;
                                        move_list->piece_moved = -PAWN;
                                        move_list->flag = (CAPTURE | PROMOTION);
                                        move_list->piece_captured =
                                            Board->board[dest];
                                        move_list->piece_promoted = -promo_type;
                                        MakeMove(Board, move_list);
                                        if (IsInCheck(Board, WHITE))
                                            move_list->flag |= CHECK;
                                        UnMakeMove(Board, move_list);
                                        move_list++;
                                        nmoves++;
                                    }
                                }
                            }
                        }
                    }
                }

                if (ptype & KNIGHT) {
                    sqp = KnightMoves[pos[i]];
                    while ((dest = *sqp++) != -1) {
                        if (Board->board[dest] >= 0) {
                            move_list->from = pos[i];
                            move_list->to = dest;
                            move_list->piece_moved = -ptype;
                            move_list->flag = 0;
                            if (Board->board[dest] > 0) {
                                move_list->flag |= CAPTURE;
                                move_list->piece_captured = Board->board[dest];
                            }
                            MakeMove(Board, move_list);
                            is_in_check = IsInCheck(Board, BLACK);
                            if (!is_in_check) {
                                if (IsInCheck(Board, WHITE))
                                    move_list->flag |= CHECK;
                            }
                            UnMakeMove(Board, move_list);
                            if (!is_in_check) {
                                move_list++;
                                nmoves++;
                            }
                        }
                    }
                }

                if (ptype & BISHOP) {
                    for (direc = 0; direc < 4; direc++) {
                        sqp = BishopMoves[pos[i]][direc];
                        while ((dest = *sqp++) != -1) {
                            if (Board->board[dest] >= 0) {
                                move_list->from = pos[i];
                                move_list->to = dest;
                                move_list->piece_moved = -ptype;
                                move_list->flag = 0;
                                if (Board->board[dest] > 0) {
                                    move_list->flag |= CAPTURE;
                                    move_list->piece_captured =
                                        Board->board[dest];
                                }
                                MakeMove(Board, move_list);
                                is_in_check = IsInCheck(Board, BLACK);
                                if (!is_in_check) {
                                    if (IsInCheck(Board, WHITE))
                                        move_list->flag |= CHECK;
                                }
                                UnMakeMove(Board, move_list);
                                if (!is_in_check) {
                                    move_list++;
                                    nmoves++;
                                }
                            }
                            if (Board->board[dest] != 0)
                                break;
                        }
                    }
                }

                if (ptype & ROOK) {
                    for (direc = 0; direc < 4; direc++) {
                        sqp = RookMoves[pos[i]][direc];
                        while ((dest = *sqp++) != -1) {
                            if (Board->board[dest] >= 0) {
                                move_list->from = pos[i];
                                move_list->to = dest;
                                move_list->piece_moved = -ptype;
                                move_list->flag = 0;
                                if (Board->board[dest] > 0) {
                                    move_list->flag |= CAPTURE;
                                    move_list->piece_captured =
                                        Board->board[dest];
                                }
                                MakeMove(Board, move_list);
                                is_in_check = IsInCheck(Board, BLACK);
                                if (!is_in_check) {
                                    if (IsInCheck(Board, WHITE))
                                        move_list->flag |= CHECK;
                                }
                                UnMakeMove(Board, move_list);
                                if (!is_in_check) {
                                    move_list++;
                                    nmoves++;
                                }
                            }
                            if (Board->board[dest] != 0)
                                break;
                        }
                    }
                }
            }
        }

        sqp = KingMoves[Board->bkpos];
        while ((dest = *sqp++) != -1) {
            if (KingAttack[NSQUARES * dest + Board->wkpos] >= 0)
                continue;
            if (Board->board[dest] >= 0) {
                move_list->from = Board->bkpos;
                move_list->to = dest;
                move_list->piece_moved = -KING;
                move_list->flag = 0;
                if (Board->board[dest] > 0) {
                    move_list->flag |= CAPTURE;
                    move_list->piece_captured = Board->board[dest];
                }
                MakeMove(Board, move_list);
                is_in_check = IsInCheck(Board, BLACK);
                if (!is_in_check) {
                    if (IsInCheck(Board, WHITE))
                        move_list->flag |= CHECK;
                }
                UnMakeMove(Board, move_list);
                if (!is_in_check) {
                    move_list++;
                    nmoves++;
                }
            }
        }

        // King side castling
        if (Board->castle & BK_CASTLE) {
            bool castle_possible = true;
            if (Board->bkpos != SquareMake(NROWS - 1, king_orig_col)) {
                if (Verbose > 1) {
                    char label[64];
                    sprintf(label, "bkpos=%d, does not allow k-side castling",
                            Board->bkpos);
                    DisplayBoard(Board, label);
                }
                castle_possible = false;
            }
            if (-ROOK != Board->board[SquareMake(NROWS - 1, grook_orig_col)]) {
                if (Verbose > 1) {
                    char label[64];
                    sprintf(label, "No rook on %d for k-side castling",
                            SquareMake(NROWS - 1, grook_orig_col));
                    DisplayBoard(Board, label);
                }
                castle_possible = false;
            }
            /* general test whether rook and king can move unimpeded */
            int kstart = min(king_orig_col, KING_GCASTLE_DEST_COL);
            int kend = max(king_orig_col, KING_GCASTLE_DEST_COL);
            int cstart =
                min(kstart, min(grook_orig_col, ROOK_GCASTLE_DEST_COL));
            int cend = max(kend, max(grook_orig_col, ROOK_GCASTLE_DEST_COL));
            for (i = cstart; i <= cend; i++) {
                if (i == king_orig_col || i == grook_orig_col)
                    continue;
                if (Board->board[SquareMake(NROWS - 1, i)] != 0) {
                    castle_possible = false;
                    break;
                }
            }
            if (castle_possible) {
                /* need to check king is not attacked on any squares
                 * it passes over, including starting and ending squares,
                 * but possible excluding the "g-rook" for Chess960
                 */
                move_list->from = Board->bkpos;
                move_list->piece_moved = -KING;
                move_list->flag = 0;
                for (i = kstart; i <= kend; i++) {
                    if (i == grook_orig_col)
                        continue;
                    move_list->to = SquareMake(NROWS - 1, i);
                    MakeMove(Board, move_list);
                    is_in_check = IsInCheck(Board, BLACK);
                    UnMakeMove(Board, move_list);
                    if (is_in_check) {
                        castle_possible = false;
                        break;
                    }
                }

                if (castle_possible) {
                    move_list->flag = BK_CASTLE;
                    move_list->to =
                        SquareMake(NROWS - 1, KING_GCASTLE_DEST_COL);
                    MakeMove(Board, move_list);
                    if (IsInCheck(Board, WHITE))
                        move_list->flag |= CHECK;
                    UnMakeMove(Board, move_list);
                    move_list++;
                    nmoves++;
                }
            }
        }

        // Queen side castling
        if (Board->castle & BQ_CASTLE) {
            bool castle_possible = true;
            if (Board->bkpos != SquareMake(NROWS - 1, king_orig_col)) {
                if (Verbose > 1) {
                    char label[64];
                    sprintf(label, "bkpos=%d, does not allow q-side castling",
                            Board->bkpos);
                    DisplayBoard(Board, label);
                }
                castle_possible = false;
            }
            if (-ROOK != Board->board[SquareMake(NROWS - 1, crook_orig_col)]) {
                if (Verbose > 1) {
                    char label[64];
                    sprintf(label, "No rook on %d for q-side castling",
                            SquareMake(NROWS - 1, crook_orig_col));
                    DisplayBoard(Board, label);
                }
                castle_possible = false;
            }
            /* general test whether rook and king can move unimpeded */
            int kstart = min(king_orig_col, KING_CCASTLE_DEST_COL);
            int kend = max(king_orig_col, KING_CCASTLE_DEST_COL);
            int cstart =
                min(kstart, min(crook_orig_col, ROOK_CCASTLE_DEST_COL));
            int cend = max(kend, max(crook_orig_col, ROOK_CCASTLE_DEST_COL));
            for (i = cstart; i <= cend; i++) {
                if (i == king_orig_col || i == crook_orig_col)
                    continue;
                if (Board->board[SquareMake(NROWS - 1, i)] != 0) {
                    castle_possible = false;
                    break;
                }
            }
            if (castle_possible) {
                /* need to check king is not attacked on any squares
                 * it passes over, including starting and ending squares,
                 * but possible excluding the "c-rook" for Chess960
                 */
                move_list->from = Board->bkpos;
                move_list->piece_moved = -KING;
                move_list->flag = 0;
                for (i = kstart; i <= kend; i++) {
                    if (i == crook_orig_col)
                        continue;
                    move_list->to = SquareMake(NROWS - 1, i);
                    MakeMove(Board, move_list);
                    is_in_check = IsInCheck(Board, BLACK);
                    UnMakeMove(Board, move_list);
                    if (is_in_check) {
                        castle_possible = false;
                        break;
                    }
                }

                if (castle_possible) {
                    move_list->flag = BQ_CASTLE;
                    move_list->to =
                        SquareMake(NROWS - 1, KING_CCASTLE_DEST_COL);
                    MakeMove(Board, move_list);
                    if (IsInCheck(Board, WHITE))
                        move_list->flag |= CHECK;
                    UnMakeMove(Board, move_list);
                    move_list++;
                    nmoves++;
                }
            }
        }
    }

    return nmoves;
}

static int GenLegalCaptures(BOARD *Board, Move *move_list,
                            bool skip_pawn_promotion, bool all_promos) {
    int nmoves = 0, ptype, direc, *sqp, dest, i;
    unsigned int promos = Promotions;
    bool is_check, is_in_check;

    /* Generate legal captures
     * if skip_pawn_promotion is false, include promotion to pawn as a
     * move, for later egtb probing
     */

    if (all_promos || (Board->num_pieces < 8 && SearchSubgamePromotions))
        promos = QRBN_PROMOTIONS;

    if (Board->side == WHITE) {
        for (ptype = PAWN; ptype < KING; ptype++) {
            int locations[MAX_IDENT_PIECES];
            memcpy(locations, Board->piece_locations[WHITE][ptype],
                   sizeof(locations));
            int *pos = locations;
            for (i = 0; i < Board->piece_type_count[WHITE][ptype]; i++) {
                if (ptype == PAWN) {

                    // now pawn captures
                    if (Column(pos[i]) > 0) {
                        dest = SquareMake(Row(pos[i]) + 1, Column(pos[i]) - 1);
                        if (Board->board[dest] < 0 ||
                            (Board->ep_square > 0 &&
                             dest == Board->ep_square)) {
                            move_list->from = pos[i];
                            move_list->to = dest;
                            move_list->piece_moved = PAWN;
                            move_list->flag = CAPTURE;
                            if (Board->ep_square > 0 &&
                                dest == Board->ep_square) {
                                move_list->flag |= EN_PASSANT;
                                move_list->piece_captured = -PAWN;
                            } else {
                                move_list->piece_captured = Board->board[dest];
                            }
                            if (Row(dest) == PromoteRow[WHITE]) {
                                move_list->flag |= PROMOTION;
                                move_list->piece_promoted = PAWN;
                            }
                            MakeMove(Board, move_list);
                            is_in_check = IsInCheck(Board, WHITE);
                            if (!is_in_check) {
                                if (IsInCheck(Board, BLACK))
                                    move_list->flag |= CHECK;
                            }
                            UnMakeMove(Board, move_list);
                            if (!is_in_check) {
                                if (Row(dest) != PromoteRow[WHITE]) {
                                    move_list++;
                                    nmoves++;
                                } else {
                                    if (!skip_pawn_promotion) {
                                        move_list++;
                                        nmoves++;
                                    }
                                    int promo_type;
                                    for (promo_type = PAWN + 1;
                                         promo_type < KING; promo_type++) {
                                        if (!(promos & (1 << promo_type)))
                                            continue;
                                        move_list->from = pos[i];
                                        move_list->to = dest;
                                        move_list->piece_moved = PAWN;
                                        move_list->flag = (CAPTURE | PROMOTION);
                                        move_list->piece_captured =
                                            Board->board[dest];
                                        move_list->piece_promoted = promo_type;
                                        MakeMove(Board, move_list);
                                        if (IsInCheck(Board, BLACK))
                                            move_list->flag |= CHECK;
                                        UnMakeMove(Board, move_list);
                                        move_list++;
                                        nmoves++;
                                    }
                                }
                            }
                        }
                    }

                    if (Column(pos[i]) < (NCOLS - 1)) {
                        dest = SquareMake(Row(pos[i]) + 1, Column(pos[i]) + 1);
                        if (Board->board[dest] < 0 ||
                            (Board->ep_square > 0 &&
                             dest == Board->ep_square)) {
                            move_list->from = pos[i];
                            move_list->to = dest;
                            move_list->piece_moved = PAWN;
                            move_list->flag = CAPTURE;
                            if (Board->ep_square > 0 &&
                                dest == Board->ep_square) {
                                move_list->flag |= EN_PASSANT;
                                move_list->piece_captured = -PAWN;
                            } else {
                                move_list->piece_captured = Board->board[dest];
                            }
                            if (Row(dest) == PromoteRow[WHITE]) {
                                move_list->flag |= PROMOTION;
                                move_list->piece_promoted = PAWN;
                            }
                            MakeMove(Board, move_list);
                            is_in_check = IsInCheck(Board, WHITE);
                            if (!is_in_check) {
                                if (IsInCheck(Board, BLACK))
                                    move_list->flag |= CHECK;
                            }
                            UnMakeMove(Board, move_list);
                            if (!is_in_check) {
                                if (Row(dest) != PromoteRow[WHITE]) {
                                    move_list++;
                                    nmoves++;
                                } else {
                                    if (!skip_pawn_promotion) {
                                        move_list++;
                                        nmoves++;
                                    }
                                    int promo_type;
                                    for (promo_type = PAWN + 1;
                                         promo_type < KING; promo_type++) {
                                        if (!(promos & (1 << promo_type)))
                                            continue;
                                        move_list->from = pos[i];
                                        move_list->to = dest;
                                        move_list->piece_moved = PAWN;
                                        move_list->flag = (CAPTURE | PROMOTION);
                                        move_list->piece_captured =
                                            Board->board[dest];
                                        move_list->piece_promoted = promo_type;
                                        MakeMove(Board, move_list);
                                        if (IsInCheck(Board, BLACK))
                                            move_list->flag |= CHECK;
                                        UnMakeMove(Board, move_list);
                                        move_list++;
                                        nmoves++;
                                    }
                                }
                            }
                        }
                    }
                }

                if (ptype & KNIGHT) {
                    sqp = KnightMoves[pos[i]];
                    while ((dest = *sqp++) != -1) {
                        if (Board->board[dest] < 0) {
                            move_list->from = pos[i];
                            move_list->to = dest;
                            move_list->piece_moved = ptype;
                            move_list->flag = CAPTURE;
                            move_list->piece_captured = Board->board[dest];
                            MakeMove(Board, move_list);
                            is_in_check = IsInCheck(Board, WHITE);
                            if (!is_in_check) {
                                if (IsInCheck(Board, BLACK))
                                    move_list->flag |= CHECK;
                            }
                            UnMakeMove(Board, move_list);
                            if (!is_in_check) {
                                move_list++;
                                nmoves++;
                            }
                        }
                    }
                }

                if (ptype & BISHOP) {
                    for (direc = 0; direc < 4; direc++) {
                        sqp = BishopMoves[pos[i]][direc];
                        while ((dest = *sqp++) != -1) {
                            if (Board->board[dest] < 0) {
                                move_list->from = pos[i];
                                move_list->to = dest;
                                move_list->piece_moved = ptype;
                                move_list->flag = CAPTURE;
                                move_list->piece_captured = Board->board[dest];
                                MakeMove(Board, move_list);
                                is_in_check = IsInCheck(Board, WHITE);
                                if (!is_in_check) {
                                    if (IsInCheck(Board, BLACK))
                                        move_list->flag |= CHECK;
                                }
                                UnMakeMove(Board, move_list);
                                if (!is_in_check) {
                                    move_list++;
                                    nmoves++;
                                }
                            }
                            if (Board->board[dest] != 0)
                                break;
                        }
                    }
                }

                if (ptype & ROOK) {
                    for (direc = 0; direc < 4; direc++) {
                        sqp = RookMoves[pos[i]][direc];
                        while ((dest = *sqp++) != -1) {
                            if (Board->board[dest] < 0) {
                                move_list->from = pos[i];
                                move_list->to = dest;
                                move_list->piece_moved = ptype;
                                move_list->flag = CAPTURE;
                                move_list->piece_captured = Board->board[dest];
                                MakeMove(Board, move_list);
                                is_in_check = IsInCheck(Board, WHITE);
                                if (!is_in_check) {
                                    if (IsInCheck(Board, BLACK))
                                        move_list->flag |= CHECK;
                                }
                                UnMakeMove(Board, move_list);
                                if (!is_in_check) {
                                    move_list++;
                                    nmoves++;
                                }
                            }
                            if (Board->board[dest] != 0)
                                break;
                        }
                    }
                }
            }
        }

        sqp = KingMoves[Board->wkpos];
        while ((dest = *sqp++) != -1) {
            if (KingAttack[NSQUARES * dest + Board->bkpos] >= 0)
                continue;
            if (Board->board[dest] < 0) {
                move_list->from = Board->wkpos;
                move_list->to = dest;
                move_list->piece_moved = KING;
                move_list->flag = CAPTURE;
                move_list->piece_captured = Board->board[dest];
                MakeMove(Board, move_list);
                is_in_check = IsInCheck(Board, WHITE);
                if (!is_in_check) {
                    if (IsInCheck(Board, BLACK))
                        move_list->flag |= CHECK;
                }
                UnMakeMove(Board, move_list);
                if (!is_in_check) {
                    move_list++;
                    nmoves++;
                }
            }
        }
    } else { // Black moves
        for (ptype = PAWN; ptype < KING; ptype++) {
            int locations[MAX_IDENT_PIECES];
            memcpy(locations, Board->piece_locations[BLACK][ptype],
                   sizeof(locations));
            int *pos = locations;
            for (i = 0; i < Board->piece_type_count[BLACK][ptype]; i++) {
                if (ptype == PAWN) {

                    // now pawn captures
                    if (Column(pos[i]) > 0) {
                        dest = SquareMake(Row(pos[i]) - 1, Column(pos[i]) - 1);
                        if (Board->board[dest] > 0 ||
                            (Board->ep_square > 0 &&
                             dest == Board->ep_square)) {
                            move_list->from = pos[i];
                            move_list->to = dest;
                            move_list->piece_moved = -PAWN;
                            move_list->flag = CAPTURE;
                            if (Board->ep_square > 0 &&
                                dest == Board->ep_square) {
                                move_list->flag |= EN_PASSANT;
                                move_list->piece_captured = PAWN;
                            } else {
                                move_list->piece_captured = Board->board[dest];
                            }
                            if (Row(dest) == PromoteRow[BLACK]) {
                                move_list->flag |= PROMOTION;
                                move_list->piece_promoted = -PAWN;
                            }
                            MakeMove(Board, move_list);
                            is_in_check = IsInCheck(Board, BLACK);
                            if (!is_in_check) {
                                if (IsInCheck(Board, WHITE))
                                    move_list->flag |= CHECK;
                            }
                            UnMakeMove(Board, move_list);
                            if (!is_in_check) {
                                if (Row(dest) != PromoteRow[BLACK]) {
                                    move_list++;
                                    nmoves++;
                                } else {
                                    if (!skip_pawn_promotion) {
                                        move_list++;
                                        nmoves++;
                                    }
                                    int promo_type;
                                    for (promo_type = PAWN + 1;
                                         promo_type < KING; promo_type++) {
                                        if (!(promos & (1 << promo_type)))
                                            continue;
                                        move_list->from = pos[i];
                                        move_list->to = dest;
                                        move_list->piece_moved = -PAWN;
                                        move_list->flag = (CAPTURE | PROMOTION);
                                        move_list->piece_captured =
                                            Board->board[dest];
                                        move_list->piece_promoted = -promo_type;
                                        MakeMove(Board, move_list);
                                        if (IsInCheck(Board, WHITE))
                                            move_list->flag |= CHECK;
                                        UnMakeMove(Board, move_list);
                                        move_list++;
                                        nmoves++;
                                    }
                                }
                            }
                        }
                    }

                    if (Column(pos[i]) < (NCOLS - 1)) {
                        dest = SquareMake(Row(pos[i]) - 1, Column(pos[i]) + 1);
                        if (Board->board[dest] > 0 ||
                            (Board->ep_square > 0 &&
                             dest == Board->ep_square)) {
                            move_list->from = pos[i];
                            move_list->to = dest;
                            move_list->piece_moved = -PAWN;
                            move_list->flag = CAPTURE;
                            if (Board->ep_square > 0 &&
                                dest == Board->ep_square) {
                                move_list->flag |= EN_PASSANT;
                                move_list->piece_captured = PAWN;
                            } else {
                                move_list->piece_captured = Board->board[dest];
                            }
                            if (Row(dest) == PromoteRow[BLACK]) {
                                move_list->flag |= PROMOTION;
                                move_list->piece_promoted = -PAWN;
                            }
                            MakeMove(Board, move_list);
                            is_in_check = IsInCheck(Board, BLACK);
                            if (!is_in_check) {
                                if (IsInCheck(Board, WHITE))
                                    move_list->flag |= CHECK;
                            }
                            UnMakeMove(Board, move_list);
                            if (!is_in_check) {
                                if (Row(dest) != PromoteRow[BLACK]) {
                                    move_list++;
                                    nmoves++;
                                } else {
                                    if (!skip_pawn_promotion) {
                                        move_list++;
                                        nmoves++;
                                    }
                                    int promo_type;
                                    for (promo_type = PAWN + 1;
                                         promo_type < KING; promo_type++) {
                                        if (!(promos & (1 << promo_type)))
                                            continue;
                                        move_list->from = pos[i];
                                        move_list->to = dest;
                                        move_list->piece_moved = -PAWN;
                                        move_list->flag = (CAPTURE | PROMOTION);
                                        move_list->piece_captured =
                                            Board->board[dest];
                                        move_list->piece_promoted = -promo_type;
                                        MakeMove(Board, move_list);
                                        if (IsInCheck(Board, WHITE))
                                            move_list->flag |= CHECK;
                                        UnMakeMove(Board, move_list);
                                        move_list++;
                                        nmoves++;
                                    }
                                }
                            }
                        }
                    }
                }

                if (ptype & KNIGHT) {
                    sqp = KnightMoves[pos[i]];
                    while ((dest = *sqp++) != -1) {
                        if (Board->board[dest] > 0) {
                            move_list->from = pos[i];
                            move_list->to = dest;
                            move_list->piece_moved = -ptype;
                            move_list->flag = CAPTURE;
                            move_list->piece_captured = Board->board[dest];
                            MakeMove(Board, move_list);
                            is_in_check = IsInCheck(Board, BLACK);
                            if (!is_in_check) {
                                if (IsInCheck(Board, WHITE))
                                    move_list->flag |= CHECK;
                            }
                            UnMakeMove(Board, move_list);
                            if (!is_in_check) {
                                move_list++;
                                nmoves++;
                            }
                        }
                    }
                }

                if (ptype & BISHOP) {
                    for (direc = 0; direc < 4; direc++) {
                        sqp = BishopMoves[pos[i]][direc];
                        while ((dest = *sqp++) != -1) {
                            if (Board->board[dest] > 0) {
                                move_list->from = pos[i];
                                move_list->to = dest;
                                move_list->piece_moved = -ptype;
                                move_list->flag = CAPTURE;
                                move_list->piece_captured = Board->board[dest];
                                MakeMove(Board, move_list);
                                is_in_check = IsInCheck(Board, BLACK);
                                if (!is_in_check) {
                                    if (IsInCheck(Board, WHITE))
                                        move_list->flag |= CHECK;
                                }
                                UnMakeMove(Board, move_list);
                                if (!is_in_check) {
                                    move_list++;
                                    nmoves++;
                                }
                            }
                            if (Board->board[dest] != 0)
                                break;
                        }
                    }
                }

                if (ptype & ROOK) {
                    for (direc = 0; direc < 4; direc++) {
                        sqp = RookMoves[pos[i]][direc];
                        while ((dest = *sqp++) != -1) {
                            if (Board->board[dest] > 0) {
                                move_list->from = pos[i];
                                move_list->to = dest;
                                move_list->piece_moved = -ptype;
                                move_list->flag = CAPTURE;
                                move_list->piece_captured = Board->board[dest];
                                MakeMove(Board, move_list);
                                is_in_check = IsInCheck(Board, BLACK);
                                if (!is_in_check) {
                                    if (IsInCheck(Board, WHITE))
                                        move_list->flag |= CHECK;
                                }
                                UnMakeMove(Board, move_list);
                                if (!is_in_check) {
                                    move_list++;
                                    nmoves++;
                                }
                            }
                            if (Board->board[dest] != 0)
                                break;
                        }
                    }
                }
            }
        }

        sqp = KingMoves[Board->bkpos];
        while ((dest = *sqp++) != -1) {
            if (KingAttack[NSQUARES * dest + Board->wkpos] >= 0)
                continue;
            if (Board->board[dest] > 0) {
                move_list->from = Board->bkpos;
                move_list->to = dest;
                move_list->piece_moved = -KING;
                move_list->flag = CAPTURE;
                move_list->piece_captured = Board->board[dest];
                MakeMove(Board, move_list);
                is_in_check = IsInCheck(Board, BLACK);
                if (!is_in_check) {
                    if (IsInCheck(Board, WHITE))
                        move_list->flag |= CHECK;
                }
                UnMakeMove(Board, move_list);
                if (!is_in_check) {
                    move_list++;
                    nmoves++;
                }
            }
        }
    }

    return nmoves;
}

static int GenLegalFromPseudoMoves(BOARD *curr_pos, Move *move_list,
                                   Move *pseudo_move_list, int npseudo_moves) {
    int nmoves = 0;

    for (int i = 0; i < npseudo_moves; i++) {
        MakeMove(curr_pos, &pseudo_move_list[i]);
        if (!IsInCheck(curr_pos, OtherSide(curr_pos->side))) {
            memcpy(&move_list[nmoves], &pseudo_move_list[i], sizeof(Move));
            nmoves++;
        }
        UnMakeMove(curr_pos, &pseudo_move_list[i]);
    }
    return nmoves;
}

static int GetMBPosition(BOARD *Board, int *mb_position, int *parity,
                         int *pawn_file_type) {
    int loc = 0, color, type, i;
    int bishops_on_white_squares[2] = {0, 0};
    int bishops_on_black_squares[2] = {0, 0};

    mb_position[loc++] = Board->wkpos;
    mb_position[loc++] = Board->bkpos;

    for (color = WHITE; color <= BLACK; color++) {
        int *pos = Board->piece_locations[color][PAWN];
        for (int i = 0; i < Board->piece_type_count[color][PAWN]; i++) {
            mb_position[loc] = pos[i];
            if (UseEnPassant && Board->ep_square > 0) {
                if (color == WHITE &&
                    SquareMake(Row(pos[i]) - 1, Column(pos[i])) ==
                        Board->ep_square)
                    mb_position[loc] = SquareMake(0, Column(pos[i]));
                if (color == BLACK &&
                    SquareMake(Row(pos[i]) + 1, Column(pos[i])) ==
                        Board->ep_square)
                    mb_position[loc] = SquareMake(NROWS - 1, Column(pos[i]));
            }
            loc++;
        }
    }

    *pawn_file_type = FREE_PAWNS;

    if (Board->piece_type_count[WHITE][PAWN] == 1 &&
        Board->piece_type_count[BLACK][PAWN] == 1) {
        if (Column(mb_position[2]) == Column(mb_position[3])) {
            if (mb_position[3] == mb_position[2] + NCOLS)
                *pawn_file_type = BP_11_PAWNS;
            else if (mb_position[3] > mb_position[2])
                *pawn_file_type = OP_11_PAWNS;
        }
    } else if (Board->piece_type_count[WHITE][PAWN] == 2 &&
               Board->piece_type_count[BLACK][PAWN] == 1) {
        int op21 =
            N2_1_Opposing_Index(mb_position[4], mb_position[3], mb_position[2]);
        if (op21 != -1)
            *pawn_file_type = OP_21_PAWNS;
    } else if (Board->piece_type_count[WHITE][PAWN] == 1 &&
               Board->piece_type_count[BLACK][PAWN] == 2) {
        int op12 =
            N1_2_Opposing_Index(mb_position[4], mb_position[3], mb_position[2]);
        if (op12 != -1)
            *pawn_file_type = OP_12_PAWNS;
    } else if (Board->piece_type_count[WHITE][PAWN] == 2 &&
               Board->piece_type_count[BLACK][PAWN] == 2) {
        ZINDEX dp22 = IndexDP22(mb_position);
        if (Verbose > 3) {
            MyPrintf("GetMBPosition: wp1 = %c%d wp2 = %c%d bp1 = %c%d bp2 = "
                     "%c%d, sp22 = " DEC_ZINDEX_FORMAT "\n",
                     'a' + Column(mb_position[2]), 1 + Row(mb_position[2]),
                     'a' + Column(mb_position[3]), 1 + Row(mb_position[3]),
                     'a' + Column(mb_position[4]), 1 + Row(mb_position[4]),
                     'a' + Column(mb_position[5]), 1 + Row(mb_position[5]),
                     dp22);
        }
        if (dp22 != ALL_ONES)
            *pawn_file_type = DP_22_PAWNS;
        else {
            int op22 = N2_2_Opposing_Index(mb_position[5], mb_position[4],
                                           mb_position[3], mb_position[2]);
            if (op22 != -1)
                *pawn_file_type = OP_22_PAWNS;
        }
    } else if (Board->piece_type_count[WHITE][PAWN] == 3 &&
               Board->piece_type_count[BLACK][PAWN] == 1) {
        int op31 = N3_1_Opposing_Index(mb_position[5], mb_position[4],
                                       mb_position[3], mb_position[2]);
        if (op31 != -1)
            *pawn_file_type = OP_31_PAWNS;
    } else if (Board->piece_type_count[WHITE][PAWN] == 1 &&
               Board->piece_type_count[BLACK][PAWN] == 3) {
        int op13 = N1_3_Opposing_Index(mb_position[5], mb_position[4],
                                       mb_position[3], mb_position[2]);
        if (op13 != -1)
            *pawn_file_type = OP_13_PAWNS;
    } else if (Board->piece_type_count[WHITE][PAWN] == 4 &&
               Board->piece_type_count[BLACK][PAWN] == 1) {
        if ((Column(mb_position[6]) == Column(mb_position[2]) &&
             mb_position[2] < mb_position[6]) ||
            (Column(mb_position[6]) == Column(mb_position[3]) &&
             mb_position[3] < mb_position[6]) ||
            (Column(mb_position[6]) == Column(mb_position[4]) &&
             mb_position[4] < mb_position[6]) ||
            (Column(mb_position[6]) == Column(mb_position[5]) &&
             mb_position[5] < mb_position[6])) {
            *pawn_file_type = OP_41_PAWNS;
        }
    } else if (Board->piece_type_count[WHITE][PAWN] == 1 &&
               Board->piece_type_count[BLACK][PAWN] == 4) {
        if ((Column(mb_position[2]) == Column(mb_position[3]) &&
             mb_position[2] < mb_position[3]) ||
            (Column(mb_position[2]) == Column(mb_position[4]) &&
             mb_position[2] < mb_position[4]) ||
            (Column(mb_position[2]) == Column(mb_position[5]) &&
             mb_position[2] < mb_position[5]) ||
            (Column(mb_position[2]) == Column(mb_position[6]) &&
             mb_position[2] < mb_position[6])) {
            *pawn_file_type = OP_14_PAWNS;
        }
    } else if (Board->piece_type_count[WHITE][PAWN] == 3 &&
               Board->piece_type_count[BLACK][PAWN] == 2) {
        if ((Column(mb_position[5]) == Column(mb_position[2]) &&
             mb_position[2] < mb_position[5]) ||
            (Column(mb_position[5]) == Column(mb_position[3]) &&
             mb_position[3] < mb_position[5]) ||
            (Column(mb_position[5]) == Column(mb_position[4]) &&
             mb_position[4] < mb_position[5]) ||
            (Column(mb_position[6]) == Column(mb_position[2]) &&
             mb_position[2] < mb_position[6]) ||
            (Column(mb_position[6]) == Column(mb_position[3]) &&
             mb_position[3] < mb_position[6]) ||
            (Column(mb_position[6]) == Column(mb_position[4]) &&
             mb_position[4] < mb_position[6])) {
            *pawn_file_type = OP_32_PAWNS;
        }
    } else if (Board->piece_type_count[WHITE][PAWN] == 2 &&
               Board->piece_type_count[BLACK][PAWN] == 3) {
        if ((Column(mb_position[2]) == Column(mb_position[4]) &&
             mb_position[2] < mb_position[4]) ||
            (Column(mb_position[2]) == Column(mb_position[5]) &&
             mb_position[2] < mb_position[5]) ||
            (Column(mb_position[2]) == Column(mb_position[6]) &&
             mb_position[2] < mb_position[6]) ||
            (Column(mb_position[3]) == Column(mb_position[4]) &&
             mb_position[3] < mb_position[4]) ||
            (Column(mb_position[3]) == Column(mb_position[5]) &&
             mb_position[3] < mb_position[5]) ||
            (Column(mb_position[3]) == Column(mb_position[6]) &&
             mb_position[3] < mb_position[6])) {
            *pawn_file_type = OP_23_PAWNS;
        }
    } else if (Board->piece_type_count[WHITE][PAWN] == 3 &&
               Board->piece_type_count[BLACK][PAWN] == 3) {
        if ((Column(mb_position[5]) == Column(mb_position[2]) &&
             mb_position[2] < mb_position[5]) ||
            (Column(mb_position[5]) == Column(mb_position[3]) &&
             mb_position[3] < mb_position[5]) ||
            (Column(mb_position[5]) == Column(mb_position[4]) &&
             mb_position[4] < mb_position[5]) ||
            (Column(mb_position[6]) == Column(mb_position[2]) &&
             mb_position[2] < mb_position[6]) ||
            (Column(mb_position[6]) == Column(mb_position[3]) &&
             mb_position[3] < mb_position[6]) ||
            (Column(mb_position[6]) == Column(mb_position[4]) &&
             mb_position[4] < mb_position[6]) ||
            (Column(mb_position[7]) == Column(mb_position[2]) &&
             mb_position[2] < mb_position[7]) ||
            (Column(mb_position[7]) == Column(mb_position[3]) &&
             mb_position[3] < mb_position[7]) ||
            (Column(mb_position[7]) == Column(mb_position[4]) &&
             mb_position[4] < mb_position[7])) {
            *pawn_file_type = OP_33_PAWNS;
        }
    } else if (Board->piece_type_count[WHITE][PAWN] == 4 &&
               Board->piece_type_count[BLACK][PAWN] == 2) {
        if ((Column(mb_position[6]) == Column(mb_position[2]) &&
             mb_position[2] < mb_position[6]) ||
            (Column(mb_position[6]) == Column(mb_position[3]) &&
             mb_position[3] < mb_position[6]) ||
            (Column(mb_position[6]) == Column(mb_position[4]) &&
             mb_position[4] < mb_position[6]) ||
            (Column(mb_position[6]) == Column(mb_position[5]) &&
             mb_position[5] < mb_position[6]) ||
            (Column(mb_position[7]) == Column(mb_position[2]) &&
             mb_position[2] < mb_position[7]) ||
            (Column(mb_position[7]) == Column(mb_position[3]) &&
             mb_position[3] < mb_position[7]) ||
            (Column(mb_position[7]) == Column(mb_position[4]) &&
             mb_position[4] < mb_position[7]) ||
            (Column(mb_position[7]) == Column(mb_position[5]) &&
             mb_position[5] < mb_position[7])) {
            *pawn_file_type = OP_42_PAWNS;
        }
    } else if (Board->piece_type_count[WHITE][PAWN] == 2 &&
               Board->piece_type_count[BLACK][PAWN] == 4) {
        if ((Column(mb_position[4]) == Column(mb_position[2]) &&
             mb_position[2] < mb_position[4]) ||
            (Column(mb_position[4]) == Column(mb_position[3]) &&
             mb_position[3] < mb_position[4]) ||
            (Column(mb_position[5]) == Column(mb_position[2]) &&
             mb_position[2] < mb_position[5]) ||
            (Column(mb_position[5]) == Column(mb_position[3]) &&
             mb_position[3] < mb_position[5]) ||
            (Column(mb_position[6]) == Column(mb_position[2]) &&
             mb_position[2] < mb_position[6]) ||
            (Column(mb_position[6]) == Column(mb_position[3]) &&
             mb_position[3] < mb_position[6]) ||
            (Column(mb_position[7]) == Column(mb_position[2]) &&
             mb_position[2] < mb_position[7]) ||
            (Column(mb_position[7]) == Column(mb_position[3]) &&
             mb_position[3] < mb_position[7])) {
            *pawn_file_type = OP_24_PAWNS;
        }
    }
    for (color = WHITE; color <= BLACK; color++) {
        for (type = KING - 1; type >= KNIGHT; type--) {
            int *pos = Board->piece_locations[color][type];
            for (i = 0; i < Board->piece_type_count[color][type]; i++) {
                mb_position[loc] = pos[i];
                if (type == BISHOP) {
                    if (IsWhiteSquare[pos[i]]) {
                        bishops_on_white_squares[color]++;
                    }
                }
                loc++;
            }
        }
        bishops_on_black_squares[color] =
            Board->piece_type_count[color][BISHOP] -
            bishops_on_white_squares[color];
    }

    // for Board with even number of squares can switch "white" and "black"
#if ((NROWS % 2) == 0) || ((NCOLS % 2) == 0)
    if ((bishops_on_black_squares[WHITE] > bishops_on_white_squares[WHITE]) ||
        ((bishops_on_black_squares[WHITE] == bishops_on_white_squares[WHITE]) &&
         (bishops_on_black_squares[BLACK] > bishops_on_white_squares[BLACK]))) {
        SWAP(bishops_on_white_squares[WHITE], bishops_on_black_squares[WHITE]);
        SWAP(bishops_on_white_squares[BLACK], bishops_on_black_squares[BLACK]);
    }
#endif

    *parity = 1000 * bishops_on_white_squares[WHITE] +
              100 * bishops_on_black_squares[WHITE] +
              10 * bishops_on_white_squares[BLACK] +
              bishops_on_black_squares[BLACK];

    if (loc != Board->num_pieces) {
        MyPrintf("GetMBPosition: Bad number of pieces: %d, expected %d", loc,
                 Board->num_pieces);
        DisplayBoard(Board, NULL);
        exit(1);
    }

    return loc;
}

static int GetYKPosition(BOARD *Board, int *yk_position) {
    int loc = 0;

    yk_position[loc++] = Board->wkpos;
    yk_position[loc++] = Board->bkpos;

    for (int color = WHITE; color <= BLACK; color++) {
        for (int type = KING - 1; type >= PAWN; type--) {
            int *pos = Board->piece_locations[color][type];
            for (int i = 0; i < Board->piece_type_count[color][type]; i++) {
                yk_position[loc] = pos[i];
                if (type == PAWN && UseEnPassant && Board->ep_square > 0) {
                    if (color == WHITE &&
                        SquareMake(Row(pos[i]) - 1, Column(pos[i])) ==
                            Board->ep_square)
                        yk_position[loc] = SquareMake(0, Column(pos[i]));

                    if (color == BLACK &&
                        SquareMake(Row(pos[i]) + 1, Column(pos[i])) ==
                            Board->ep_square)
                        yk_position[loc] =
                            SquareMake(NROWS - 1, Column(pos[i]));
                }
                loc++;
            }
        }
    }

    if (loc != Board->num_pieces) {
        MyPrintf("Bad number of pieces in GetYKPosition: %d, expected %d", loc,
                 Board->num_pieces);
        DisplayBoard(Board, NULL);
        exit(1);
    }

    return loc;
}

static ZINDEX GetMBIndex(int *mb_pos, int npieces, bool pawns_present,
                         IndexType *eptr, int *kindex, ZINDEX *offset) {
    if (eptr == NULL) {
        *kindex = -1;
        *offset = ALL_ONES;
        return ALL_ONES;
    }

    int wk = mb_pos[0];
    int bk = mb_pos[1];

    int sym = IDENTITY;

    if (pawns_present)
        sym = KK_Transform(wk, bk);
    else
        sym = KK_Transform_NoPawns(wk, bk);

    if (Verbose > 3) {
        MyPrintf("GetMBIndex: For wk=%c%d bk=%c%d, pawns present=%d, transform "
                 "needed (%d) = %s\n",
                 'a' + Column(wk), 1 + Row(wk), 'a' + Column(bk), 1 + Row(bk),
                 pawns_present, sym, SymmetryName[sym]);
    }

    int *transform = Transforms[sym];

    for (int i = 0; i < npieces; i++) {
        mb_pos[i] = transform[mb_pos[i]];
    }

    wk = mb_pos[0];
    bk = mb_pos[1];

    *offset = (eptr->IndexFromPos)(mb_pos);

    if (Verbose > 3) {
        MyPrintf("GetMBIndex: index = %I64u\n", *offset);
    }

    bool flipped = false;

    if (pawns_present)
        GetFlipFunction(wk, bk, &flipped, &transform);
    else
        GetFlipFunctionNoPawns(wk, bk, &flipped, &transform);

    if (flipped) {
        int tmp_pos[MAX_PIECES];
        for (int i = 0; i < npieces; i++) {
            tmp_pos[i] = transform[mb_pos[i]];
        }
        ZINDEX offset_t = (eptr->IndexFromPos)(tmp_pos);
        if (offset_t < *offset) {
            *offset = offset_t;
            memcpy(mb_pos, tmp_pos, npieces * sizeof(tmp_pos[0]));
        }
    }

    if (pawns_present)
        *kindex = KK_Index(wk, bk);
    else
        *kindex = KK_Index_NoPawns(wk, bk);

    return 0;
}

static int GetYKIndex(int *yk_pos, int npieces, bool pawns_present,
                      IndexType *eptr, int *kindex, ZINDEX *offset) {
    if (eptr == NULL) {
        *kindex = -1;
        *offset = ALL_ONES;
        return -1;
    }

    int sym = IDENTITY;

    int wk = yk_pos[0];
    int bk = yk_pos[1];

    if (pawns_present)
        sym = KK_Transform(wk, bk);
    else
        sym = KK_Transform_NoPawns(wk, bk);

    if (Verbose > 3) {
        MyPrintf("GetYKIndex: For wk=%c%d bk=%c%d, pawns present=%d, transform "
                 "needed (%d) = %s\n",
                 'a' + Column(wk), 1 + Row(wk), 'a' + Column(bk), 1 + Row(bk),
                 pawns_present, sym, SymmetryName[sym]);
    }

    int *transform = Transforms[sym];

    for (int i = 0; i < npieces; i++) {
        yk_pos[i] = transform[yk_pos[i]];
    }

    wk = yk_pos[0];
    bk = yk_pos[1];

    *offset = (eptr->IndexFromPos)(yk_pos);

    if (Verbose > 3) {
        MyPrintf("GetYKIndex: index before flip = %lu\n", *offset);
    }

    bool flipped = false;

    if (pawns_present)
        GetFlipFunction(wk, bk, &flipped, &transform);
    else
        GetFlipFunctionNoPawns(wk, bk, &flipped, &transform);

    if (flipped) {
        int tmp_pos[MAX_PIECES];
        for (int i = 0; i < npieces; i++) {
            tmp_pos[i] = transform[yk_pos[i]];
        }
        ZINDEX offset_t = (eptr->IndexFromPos)(tmp_pos);
        if (offset_t < *offset) {
            *offset = offset_t;
            memcpy(yk_pos, tmp_pos, npieces * sizeof(tmp_pos[0]));
        }
    }

    if (Verbose > 3) {
        MyPrintf("GetYKIndex: index after flip = " DEC_ZINDEX_FORMAT "\n",
                 *offset);
    }

    if (pawns_present)
        *kindex = KK_Index(wk, bk);
    else
        *kindex = KK_Index_NoPawns(wk, bk);

    if (Verbose > 3) {
        MyPrintf("GetYKIndex: kk_index = %lu\n", *kindex);
    }

    return 0;
}

static bool IsB22Ending(BOARD *Board) {
    if (Board->piece_type_count[WHITE][BISHOP] == 4) {
        int nb = 0, nw = 0;
        for (int i = 0; i < Board->piece_type_count[WHITE][BISHOP]; i++) {
            if (ParityTable[Board->piece_locations[WHITE][BISHOP][i]])
                nw++;
            else
                nb++;
        }
        if (nw == 2 && nb == 2)
            return true;
    }

    if (Board->piece_type_count[BLACK][BISHOP] == 4) {
        int nb = 0, nw = 0;
        for (int i = 0; i < Board->piece_type_count[BLACK][BISHOP]; i++) {
            if (ParityTable[Board->piece_locations[BLACK][BISHOP][i]])
                nw++;
            else
                nb++;
        }
        if (nw == 2 && nb == 2)
            return true;
    }

    return false;
}

static int ParseBoard(int *board, int ep_square, int count[2][KING], int *pos,
                      int *types) {
    int color, piece, ep_pawn = -1;
    int sq, n_pieces, loc;
    int PieceLocation[2][KING + 1];

    memset(count, 0, 2 * KING * sizeof(count[0][0]));

    n_pieces = 2;

    for (sq = 0; sq < NSQUARES; sq++) {
        if (board[sq] > 0) {
            if (board[sq] != KING) {
                n_pieces++;
                count[WHITE][board[sq]]++;
            }
        } else if (board[sq] < 0) {
            if (board[sq] != -KING) {
                n_pieces++;
                count[BLACK][-board[sq]]++;
            }
        }
    }

    if (n_pieces > MAX_PIECES)
        return n_pieces;

    loc = 0;
    PieceLocation[WHITE][KING] = loc++;
    PieceLocation[BLACK][KING] = loc++;

    for (color = WHITE; color <= BLACK; color++) {
        for (piece = KING - 1; piece >= PAWN; piece--) {
            PieceLocation[color][piece] = loc;
            loc += count[color][piece];
        }
    }

    memset(pos, 0, MAX_PIECES * sizeof(pos[0]));
    memset(types, 0, MAX_PIECES * sizeof(types[0]));

    if (ep_square != 0) {
        int ep_row = Row(ep_square);
        int ep_col = Column(ep_square);
        if (ep_row == 2)
            ep_pawn = SquareMake(ep_row + 1, ep_col);
        else if (ep_row == (NROWS - 3))
            ep_pawn = SquareMake(ep_row - 1, ep_col);
    }

    for (sq = 0; sq < NSQUARES; sq++) {
        if (board[sq] == 0)
            continue;
        if (board[sq] > 0) {
            color = WHITE;
            piece = board[sq];
        } else {
            color = BLACK;
            piece = -board[sq];
        }

        types[PieceLocation[color][piece]] = board[sq];
        if (piece == PAWN && sq == ep_pawn) {
            if (color == WHITE)
                pos[PieceLocation[color][piece]++] = SquareMake(0, Column(sq));
            else
                pos[PieceLocation[color][piece]++] =
                    SquareMake(NROWS - 1, Column(sq));
        } else
            pos[PieceLocation[color][piece]++] = sq;
    }

    return n_pieces;
}

static void GetYKBaseFileName(int count[2][KING], int side, char *fname) {
    fname[0] = 'k';
    int len = 1;
    for (int piece = KING - 1; piece >= PAWN; piece--) {
        for (int i = len; i < len + count[WHITE][piece]; i++)
            fname[i] = piece_char(piece);
        len += count[WHITE][piece];
    }
    fname[len++] = 'k';
    for (int piece = KING - 1; piece >= PAWN; piece--) {
        for (int i = len; i < len + count[BLACK][piece]; i++)
            fname[i] = piece_char(piece);
        len += count[BLACK][piece];
    }
    for (int i = len; i < MAX_PIECES_YK; i++)
        fname[i] = '_';
    len = MAX_PIECES_YK;
    if (side == WHITE)
        fname[len++] = 'w';
    else
        fname[len++] = 'b';
    fname[len] = '\0';
}

static file OpenMBFile(char *ending, int kk_index, int bishop_parity[2],
                       int pawn_file_type, int side, bool high_dtz) {
    char path[1024];

    if (Verbose > 3) {
        MyPrintf(
            "OpenMBFile: ending=%s, kk_index=%d, w_parity = %s, b_parity = %s, "
            "pawn_file_type = %d, side = %s, NumPaths = %d, high_dtz = %d\n",
            ending, kk_index,
            ((bishop_parity[WHITE] == NONE)
                 ? "none"
                 : ((bishop_parity[WHITE] == EVEN) ? "even" : "odd")),
            ((bishop_parity[BLACK] == NONE)
                 ? "none"
                 : ((bishop_parity[BLACK] == EVEN) ? "even" : "odd")),
            pawn_file_type, ColorName(side), NumPaths, high_dtz);
    }

    for (int i = 0; i < NumPaths; i++) {
        char dirname[65];
        if (bishop_parity[WHITE] == NONE && bishop_parity[BLACK] == NONE) {
            if (pawn_file_type == BP_11_PAWNS)
                snprintf(dirname, sizeof(dirname) - 1, "%s_bp1", ending);
            else if (pawn_file_type == OP_11_PAWNS)
                snprintf(dirname, sizeof(dirname) - 1, "%s_op1", ending);
            else if (pawn_file_type == OP_21_PAWNS)
                snprintf(dirname, sizeof(dirname) - 1, "%s_op21", ending);
            else if (pawn_file_type == OP_12_PAWNS)
                snprintf(dirname, sizeof(dirname) - 1, "%s_op12", ending);
            else if (pawn_file_type == DP_22_PAWNS)
                snprintf(dirname, sizeof(dirname) - 1, "%s_dp2", ending);
            else if (pawn_file_type == OP_22_PAWNS)
                snprintf(dirname, sizeof(dirname) - 1, "%s_op22", ending);
            else if (pawn_file_type == OP_31_PAWNS)
                snprintf(dirname, sizeof(dirname) - 1, "%s_op31", ending);
            else if (pawn_file_type == OP_13_PAWNS)
                snprintf(dirname, sizeof(dirname) - 1, "%s_op13", ending);
            else if (pawn_file_type == OP_41_PAWNS)
                snprintf(dirname, sizeof(dirname) - 1, "%s_op41", ending);
            else if (pawn_file_type == OP_14_PAWNS)
                snprintf(dirname, sizeof(dirname) - 1, "%s_op14", ending);
            else if (pawn_file_type == OP_32_PAWNS)
                snprintf(dirname, sizeof(dirname) - 1, "%s_op32", ending);
            else if (pawn_file_type == OP_23_PAWNS)
                snprintf(dirname, sizeof(dirname) - 1, "%s_op23", ending);
            else if (pawn_file_type == OP_33_PAWNS)
                snprintf(dirname, sizeof(dirname) - 1, "%s_op33", ending);
            else if (pawn_file_type == OP_42_PAWNS)
                snprintf(dirname, sizeof(dirname) - 1, "%s_op42", ending);
            else if (pawn_file_type == OP_24_PAWNS)
                snprintf(dirname, sizeof(dirname) - 1, "%s_op24", ending);
            else
                snprintf(dirname, sizeof(dirname) - 1, "%s", ending);
        } else if (bishop_parity[WHITE] != NONE &&
                   bishop_parity[BLACK] == NONE) {
            snprintf(dirname, sizeof(dirname) - 1, "%s_%s", ending,
                     ((bishop_parity[WHITE] == EVEN) ? "wbe" : "wbo"));
        } else if (bishop_parity[WHITE] == NONE &&
                   bishop_parity[BLACK] != NONE) {
            snprintf(dirname, sizeof(dirname) - 1, "%s_%s", ending,
                     ((bishop_parity[BLACK] == EVEN) ? "bbe" : "bbo"));
        } else {
            assert(bishop_parity[WHITE] != NONE &&
                   bishop_parity[BLACK] != NONE);
            snprintf(dirname, sizeof(dirname) - 1, "%s_%s_%s", ending,
                     ((bishop_parity[WHITE] == EVEN) ? "wbe" : "wbo"),
                     ((bishop_parity[BLACK] == EVEN) ? "bbe" : "bbo"));
        }

        snprintf(path, sizeof(path) - 1, "%s%c%s_out%c%s_%c_%d.%s", TbPaths[i],
                 DELIMITER[0], dirname, DELIMITER[0], ending,
                 (side == WHITE ? 'w' : 'b'), kk_index,
                 (high_dtz ? "hi" : "mb"));
        if (Verbose > 3) {
            MyPrintf("File name for path %d: %s...", i, path);
        }
        file fptr = f_open(path, "rb");
        if (fptr != NULL) {
            if (Verbose > 3) {
                MyPrintf("opened successfully\n");
            }
            return fptr;
        } else {
            if (Verbose > 3) {
                MyPrintf("not found\n");
            }
        }
    }
    return NULL;
}

static file OpenYKFile(char *base_name) {
    char path[1024];

    if (Verbose > 3) {
        MyPrintf("OpenYKFile: ending=%s\n", base_name);
    }

    for (int i = 0; i < NumPaths; i++) {
        snprintf(path, sizeof(path) - 1, "%s%c%s", TbPaths[i], DELIMITER[0],
                 base_name);
        if (Verbose > 3) {
            MyPrintf("YK File name for path %d: %s...", i, path);
        }
        file fptr = f_open(path, "rb");
        if (fptr != NULL) {
            if (Verbose > 3) {
                MyPrintf("opened successfully\n");
            }
            return fptr;
        } else {
            if (Verbose > 3) {
                MyPrintf("not found\n");
            }
        }
    }
    return NULL;
}

static int GetMBInfo(BOARD *Board, MB_INFO *mb_info) {
    mb_info->num_parities = 0;
    mb_info->pawn_file_type = FREE_PAWNS;

    if (Board->num_pieces > MAX_PIECES_MB) {
        if (Verbose > 4) {
            MyPrintf("GetMBInfo: too many pieces for database %d\n",
                     Board->num_pieces);
        }
        return TOO_MANY_PIECES;
    }

    memcpy(mb_info->piece_type_count, Board->piece_type_count,
           sizeof(Board->piece_type_count));

    int bishop_parity[2] = {NONE, NONE};

    InitPermutationTables(mb_info->piece_type_count, bishop_parity);

    mb_info->num_pieces = Board->num_pieces;

    GetMBPosition(Board, mb_info->mb_position, &mb_info->parity,
                  &mb_info->pawn_file_type);

    memset(mb_info->mb_piece_types, 0, sizeof(mb_info->mb_piece_types));

    int eindex = GetEndingType(Board->piece_type_count, mb_info->mb_piece_types,
                               bishop_parity, FREE_PAWNS);

    if (Verbose > 4) {
        MyPrintf("GetMBInfo: eindex without parities or blocked pawns = %d\n",
                 eindex);
        MyPrintf("GetMBInfo: etype = %d\n", IndexTable[eindex].etype);
        MyPrintf("GetMBInfo: sub_type = %d\n", IndexTable[eindex].sub_type);
        MyPrintf("GetMBInfo: pawn file type = %s\n",
                 OpExtensionName[mb_info->pawn_file_type]);
    }

    int kk_index_blocked = -1;
    if (eindex >= 0) {
        memcpy(mb_info->parity_index[0].bishop_parity, bishop_parity,
               sizeof(bishop_parity));
        mb_info->parity_index[0].eptr = &IndexTable[eindex];
        mb_info->num_parities++;
        InitPermutationTables(mb_info->piece_type_count, bishop_parity);
        // check whether we can also probe blocked/opposing pawn data

        if (mb_info->pawn_file_type == OP_11_PAWNS ||
            mb_info->pawn_file_type == BP_11_PAWNS) {
            eindex = GetEndingType(Board->piece_type_count, NULL, bishop_parity,
                                   OP_11_PAWNS);
            if (eindex >= 0) {
                if (Verbose > 4) {
                    MyPrintf(
                        "GetMBInfo: eindex with 1 opposing pawn pair = %d\n",
                        eindex);
                }
                mb_info->eptr_op_11 = &IndexTable[eindex];
                GetMBIndex(mb_info->mb_position, mb_info->num_pieces, true,
                           mb_info->eptr_op_11, &kk_index_blocked,
                           &mb_info->index_op_11);
            } else {
                if (Verbose > 4) {
                    MyPrintf("GetMBInfo: no eindex for 1 opposing pawn pair\n");
                }
                mb_info->eptr_op_11 = NULL;
                mb_info->index_op_11 = ALL_ONES;
            }
        }

        if (mb_info->pawn_file_type == BP_11_PAWNS) {
            eindex = GetEndingType(Board->piece_type_count, NULL, bishop_parity,
                                   BP_11_PAWNS);
            if (eindex >= 0) {
                if (Verbose > 4) {
                    MyPrintf("GetMBInfo: eindex with 1 pair of blocking pawns "
                             "= %d\n",
                             eindex);
                }
                mb_info->eptr_bp_11 = &IndexTable[eindex];
                GetMBIndex(mb_info->mb_position, mb_info->num_pieces, true,
                           mb_info->eptr_bp_11, &kk_index_blocked,
                           &mb_info->index_bp_11);
            } else {
                if (Verbose > 4) {
                    MyPrintf("GetMBInfo: no eindex for 1 blocking pawn pair\n");
                }
                mb_info->eptr_bp_11 = NULL;
                mb_info->index_bp_11 = ALL_ONES;
            }
        }

        if (mb_info->pawn_file_type == OP_21_PAWNS) {
            eindex = GetEndingType(Board->piece_type_count, NULL, bishop_parity,
                                   OP_21_PAWNS);
            if (eindex >= 0) {
                if (Verbose > 4) {
                    MyPrintf("GetMBInfo: eindex with 2 vs 1 pawn, one opposing "
                             "pair = %d\n",
                             eindex);
                }
                mb_info->eptr_op_21 = &IndexTable[eindex];
                GetMBIndex(mb_info->mb_position, mb_info->num_pieces, true,
                           mb_info->eptr_op_21, &kk_index_blocked,
                           &mb_info->index_op_21);
            } else {
                if (Verbose > 4) {
                    MyPrintf("GetMBInfo: no eindex for 2 vs 1 pawn, 1 opposing "
                             "pawn pair\n");
                }
                mb_info->eptr_op_21 = NULL;
                mb_info->index_op_21 = ALL_ONES;
            }
        }

        if (mb_info->pawn_file_type == OP_12_PAWNS) {
            eindex = GetEndingType(Board->piece_type_count, NULL, bishop_parity,
                                   OP_12_PAWNS);
            if (eindex >= 0) {
                if (Verbose > 4) {
                    MyPrintf("GetMBInfo: eindex with 1 vs 2 pawns, one "
                             "opposing pair = %d\n",
                             eindex);
                }
                mb_info->eptr_op_12 = &IndexTable[eindex];
                GetMBIndex(mb_info->mb_position, mb_info->num_pieces, true,
                           mb_info->eptr_op_12, &kk_index_blocked,
                           &mb_info->index_op_12);
            } else {
                if (Verbose > 4) {
                    MyPrintf("GetMBInfo: no eindex for 1 vs 2 pawns, 1 "
                             "opposing pawn pair\n");
                }
                mb_info->eptr_op_12 = NULL;
                mb_info->index_op_12 = ALL_ONES;
            }
        }

        if (mb_info->pawn_file_type == OP_22_PAWNS ||
            mb_info->pawn_file_type == DP_22_PAWNS) {
            eindex = GetEndingType(Board->piece_type_count, NULL, bishop_parity,
                                   OP_22_PAWNS);
            if (eindex >= 0) {
                if (Verbose > 4) {
                    MyPrintf("GetMBInfo: eindex with pawn pairs, with 1 "
                             "opposing pairs = %d\n",
                             eindex);
                }
                mb_info->eptr_op_22 = &IndexTable[eindex];
                GetMBIndex(mb_info->mb_position, mb_info->num_pieces, true,
                           mb_info->eptr_op_22, &kk_index_blocked,
                           &mb_info->index_op_22);
                if (Verbose > 4) {
                    MyPrintf("GetMBInfo: kk_index_blocked = %d, index "
                             "= " DEC_ZINDEX_FORMAT "\n",
                             kk_index_blocked, mb_info->index_op_22);
                }
            } else {
                mb_info->eptr_op_22 = NULL;
                mb_info->index_op_22 = ALL_ONES;
            }
        }

        if (mb_info->pawn_file_type == DP_22_PAWNS) {
            eindex = GetEndingType(Board->piece_type_count, NULL, bishop_parity,
                                   DP_22_PAWNS);
            if (eindex >= 0) {
                if (Verbose > 4) {
                    MyPrintf(
                        "GetMBInfo: eindex with 2 opposing pawn pairs = %d\n",
                        eindex);
                }
                mb_info->eptr_dp_22 = &IndexTable[eindex];
                GetMBIndex(mb_info->mb_position, mb_info->num_pieces, true,
                           mb_info->eptr_dp_22, &kk_index_blocked,
                           &mb_info->index_dp_22);
                if (Verbose > 4) {
                    MyPrintf("GetMBInfo: kk_index_blocked = %d, index "
                             "= " DEC_ZINDEX_FORMAT "\n",
                             kk_index_blocked, mb_info->index_dp_22);
                }
            } else {
                mb_info->eptr_dp_22 = NULL;
                mb_info->index_dp_22 = ALL_ONES;
            }
        }

        if (mb_info->pawn_file_type == OP_31_PAWNS) {
            eindex = GetEndingType(Board->piece_type_count, NULL, bishop_parity,
                                   OP_31_PAWNS);
            if (eindex >= 0) {
                if (Verbose > 4) {
                    MyPrintf("GetMBInfo: eindex with 3 vs 1 pawn, one opposing "
                             "pair = %d\n",
                             eindex);
                }
                mb_info->eptr_op_31 = &IndexTable[eindex];
                GetMBIndex(mb_info->mb_position, mb_info->num_pieces, true,
                           mb_info->eptr_op_31, &kk_index_blocked,
                           &mb_info->index_op_31);
            } else {
                if (Verbose > 4) {
                    MyPrintf("GetMBInfo: no eindex for 3 vs 1 pawn, 1 opposing "
                             "pawn pair\n");
                }
                mb_info->eptr_op_31 = NULL;
                mb_info->index_op_31 = ALL_ONES;
            }
        }

        if (mb_info->pawn_file_type == OP_13_PAWNS) {
            eindex = GetEndingType(Board->piece_type_count, NULL, bishop_parity,
                                   OP_13_PAWNS);
            if (eindex >= 0) {
                if (Verbose > 4) {
                    MyPrintf("GetMBInfo: eindex with 1 vs 3 pawns, one "
                             "opposing pair = %d\n",
                             eindex);
                }
                mb_info->eptr_op_13 = &IndexTable[eindex];
                GetMBIndex(mb_info->mb_position, mb_info->num_pieces, true,
                           mb_info->eptr_op_13, &kk_index_blocked,
                           &mb_info->index_op_13);
            } else {
                if (Verbose > 4) {
                    MyPrintf("GetMBInfo: no eindex for 1 vs 3 pawns, 1 "
                             "opposing pawn pair\n");
                }
                mb_info->eptr_op_13 = NULL;
                mb_info->index_op_13 = ALL_ONES;
            }
        }

        if (mb_info->pawn_file_type == OP_41_PAWNS) {
            eindex = GetEndingType(Board->piece_type_count, NULL, bishop_parity,
                                   OP_41_PAWNS);
            if (eindex >= 0) {
                if (Verbose > 4) {
                    MyPrintf("GetMBInfo: eindex with 4 vs 1 pawn, one opposing "
                             "pair = %d\n",
                             eindex);
                }
                mb_info->eptr_op_41 = &IndexTable[eindex];
                GetMBIndex(mb_info->mb_position, mb_info->num_pieces, true,
                           mb_info->eptr_op_41, &kk_index_blocked,
                           &mb_info->index_op_41);
            } else {
                if (Verbose > 4) {
                    MyPrintf("GetMBInfo: no eindex for 4 vs 1 pawn, 1 opposing "
                             "pawn pair\n");
                }
                mb_info->eptr_op_41 = NULL;
                mb_info->index_op_41 = ALL_ONES;
            }
        }

        if (mb_info->pawn_file_type == OP_14_PAWNS) {
            eindex = GetEndingType(Board->piece_type_count, NULL, bishop_parity,
                                   OP_14_PAWNS);
            if (eindex >= 0) {
                if (Verbose > 4) {
                    MyPrintf("GetMBInfo: eindex with 1 vs 4 pawns, one "
                             "opposing pair = %d\n",
                             eindex);
                }
                mb_info->eptr_op_14 = &IndexTable[eindex];
                GetMBIndex(mb_info->mb_position, mb_info->num_pieces, true,
                           mb_info->eptr_op_14, &kk_index_blocked,
                           &mb_info->index_op_14);
            } else {
                if (Verbose > 4) {
                    MyPrintf("GetMBInfo: no eindex for 1 vs 4 pawns, 1 "
                             "opposing pawn pair\n");
                }
                mb_info->eptr_op_14 = NULL;
                mb_info->index_op_14 = ALL_ONES;
            }
        }

        if (mb_info->pawn_file_type == OP_32_PAWNS) {
            eindex = GetEndingType(Board->piece_type_count, NULL, bishop_parity,
                                   OP_32_PAWNS);
            if (eindex >= 0) {
                if (Verbose > 4) {
                    MyPrintf("GetMBInfo: eindex with 3 vs 2 pawn, one opposing "
                             "pair = %d\n",
                             eindex);
                }
                mb_info->eptr_op_32 = &IndexTable[eindex];
                GetMBIndex(mb_info->mb_position, mb_info->num_pieces, true,
                           mb_info->eptr_op_32, &kk_index_blocked,
                           &mb_info->index_op_32);
            } else {
                if (Verbose > 4) {
                    MyPrintf("GetMBInfo: no eindex for 3 vs 2 pawn, 1 opposing "
                             "pawn pair\n");
                }
                mb_info->eptr_op_32 = NULL;
                mb_info->index_op_32 = ALL_ONES;
            }
        }

        if (mb_info->pawn_file_type == OP_23_PAWNS) {
            eindex = GetEndingType(Board->piece_type_count, NULL, bishop_parity,
                                   OP_23_PAWNS);
            if (eindex >= 0) {
                if (Verbose > 4) {
                    MyPrintf("GetMBInfo: eindex with 2 vs 3 pawns, one "
                             "opposing pair = %d\n",
                             eindex);
                }
                mb_info->eptr_op_23 = &IndexTable[eindex];
                GetMBIndex(mb_info->mb_position, mb_info->num_pieces, true,
                           mb_info->eptr_op_23, &kk_index_blocked,
                           &mb_info->index_op_23);
            } else {
                if (Verbose > 4) {
                    MyPrintf("GetMBInfo: no eindex for 2 vs 3 pawns, 1 "
                             "opposing pawn pair\n");
                }
                mb_info->eptr_op_23 = NULL;
                mb_info->index_op_23 = ALL_ONES;
            }
        }

        if (mb_info->pawn_file_type == OP_33_PAWNS) {
            eindex = GetEndingType(Board->piece_type_count, NULL, bishop_parity,
                                   OP_33_PAWNS);
            if (eindex >= 0) {
                if (Verbose > 4) {
                    MyPrintf("GetMBInfo: eindex with 3 vs 3 pawn, one opposing "
                             "pair = %d\n",
                             eindex);
                }
                mb_info->eptr_op_33 = &IndexTable[eindex];
                GetMBIndex(mb_info->mb_position, mb_info->num_pieces, true,
                           mb_info->eptr_op_33, &kk_index_blocked,
                           &mb_info->index_op_33);
            } else {
                if (Verbose > 4) {
                    MyPrintf("GetMBInfo: no eindex for 3 vs 3 pawn, 1 opposing "
                             "pawn pair\n");
                }
                mb_info->eptr_op_33 = NULL;
                mb_info->index_op_33 = ALL_ONES;
            }
        }

        if (mb_info->pawn_file_type == OP_42_PAWNS) {
            eindex = GetEndingType(Board->piece_type_count, NULL, bishop_parity,
                                   OP_42_PAWNS);
            if (eindex >= 0) {
                if (Verbose > 4) {
                    MyPrintf("GetMBInfo: eindex with 4 vs 2 pawn, one opposing "
                             "pair = %d\n",
                             eindex);
                }
                mb_info->eptr_op_42 = &IndexTable[eindex];
                GetMBIndex(mb_info->mb_position, mb_info->num_pieces, true,
                           mb_info->eptr_op_42, &kk_index_blocked,
                           &mb_info->index_op_42);
            } else {
                if (Verbose > 4) {
                    MyPrintf("GetMBInfo: no eindex for 4 vs 2 pawn, 1 opposing "
                             "pawn pair\n");
                }
                mb_info->eptr_op_42 = NULL;
                mb_info->index_op_42 = ALL_ONES;
            }
        }

        if (mb_info->pawn_file_type == OP_24_PAWNS) {
            eindex = GetEndingType(Board->piece_type_count, NULL, bishop_parity,
                                   OP_24_PAWNS);
            if (eindex >= 0) {
                if (Verbose > 4) {
                    MyPrintf("GetMBInfo: eindex with 2 vs 4 pawn, one opposing "
                             "pair = %d\n",
                             eindex);
                }
                mb_info->eptr_op_24 = &IndexTable[eindex];
                GetMBIndex(mb_info->mb_position, mb_info->num_pieces, true,
                           mb_info->eptr_op_24, &kk_index_blocked,
                           &mb_info->index_op_24);
            } else {
                if (Verbose > 4) {
                    MyPrintf("GetMBInfo: no eindex for 2 vs 4 pawn, 1 opposing "
                             "pawn pair\n");
                }
                mb_info->eptr_op_24 = NULL;
                mb_info->index_op_24 = ALL_ONES;
            }
        }
    }

    bool pawns_present = mb_info->piece_type_count[WHITE][PAWN] ||
                         mb_info->piece_type_count[BLACK][PAWN];

    // now check possible parity-constrained index tables
    // consider only subsets where parity is "odd" or "even"
    // only do this for pawnless endings

    if (!pawns_present) {
        int w_parity = mb_info->parity / 100;

        if (w_parity == 20 || w_parity == 2 || w_parity == 30 || w_parity == 3)
            bishop_parity[WHITE] = EVEN;
        else if (w_parity == 11 || w_parity == 21 || w_parity == 12)
            bishop_parity[WHITE] = ODD;

        int b_parity = mb_info->parity % 100;

        if (b_parity == 20 || b_parity == 2 || b_parity == 30 || b_parity == 3)
            bishop_parity[BLACK] = EVEN;
        else if (b_parity == 11 || b_parity == 21 || b_parity == 12)
            bishop_parity[BLACK] = ODD;

            // for odd-sized boards, can't do any parities for triples, and only
            // odd parities for doubles

#if (NROWS % 2) && (NCOLS % 2)
        if (bishop_parity[WHITE] != NONE) {
            if (mb_info->piece_type_count[WHITE][BISHOP] >= 3 ||
                bishop_parity[WHITE] == EVEN)
                bishop_parity[WHITE] = NONE;
        }

        if (bishop_parity[BLACK] != NONE) {
            if (mb_info->piece_type_count[BLACK][BISHOP] >= 3 ||
                bishop_parity[BLACK] == EVEN)
                bishop_parity[BLACK] = NONE;
        }
#endif
    }

    // if there are no parities, no need to check further

    if (bishop_parity[WHITE] == NONE && bishop_parity[BLACK] == NONE) {
        if (mb_info->num_parities == 0)
            return ETYPE_NOT_MAPPED;
        GetMBIndex(mb_info->mb_position, mb_info->num_pieces, pawns_present,
                   mb_info->parity_index[0].eptr, &mb_info->kk_index,
                   &mb_info->parity_index[0].index);
        if (kk_index_blocked != -1) {
            if (kk_index_blocked != mb_info->kk_index) {
                fprintf(stderr,
                        "GetMBInfo: kk_index_blocked = %d, kk_index = %d\n",
                        kk_index_blocked, mb_info->kk_index);
            }
        }
    }

    // now gather index for specific bishop parity

    eindex =
        GetEndingType(Board->piece_type_count, NULL, bishop_parity, FREE_PAWNS);

    if (Verbose > 4) {
        MyPrintf("GetMBInfo: eindex %d with white bishop parity %s, black "
                 "bishop parity %s\n",
                 eindex,
                 ((bishop_parity[WHITE] == NONE)
                      ? "none"
                      : ((bishop_parity[WHITE] == EVEN) ? "even" : "odd")),
                 ((bishop_parity[BLACK] == NONE)
                      ? "none"
                      : ((bishop_parity[BLACK] == EVEN) ? "even" : "odd")));
    }

    if (eindex >= 0) {
        memcpy(mb_info->parity_index[mb_info->num_parities].bishop_parity,
               bishop_parity, sizeof(bishop_parity));
        mb_info->parity_index[mb_info->num_parities].eptr = &IndexTable[eindex];
        mb_info->num_parities++;
        InitPermutationTables(mb_info->piece_type_count, bishop_parity);
    }

    // if both white and black have parity constraints, can add cases where only
    // one side is constrained

    if (bishop_parity[WHITE] != NONE && bishop_parity[BLACK] != NONE) {
        int sub_bishop_parity[2];
        sub_bishop_parity[WHITE] = bishop_parity[WHITE];
        sub_bishop_parity[BLACK] = NONE;

        eindex = GetEndingType(Board->piece_type_count, NULL, sub_bishop_parity,
                               FREE_PAWNS);

        if (Verbose > 4) {
            MyPrintf("GetMBInfo: eindex %d with white bishop parity %s\n",
                     eindex, (bishop_parity[WHITE] == EVEN) ? "even" : "odd");
        }

        if (eindex >= 0) {
            memcpy(mb_info->parity_index[mb_info->num_parities].bishop_parity,
                   sub_bishop_parity, sizeof(sub_bishop_parity));
            mb_info->parity_index[mb_info->num_parities].eptr =
                &IndexTable[eindex];
            mb_info->num_parities++;
            InitPermutationTables(mb_info->piece_type_count, sub_bishop_parity);
        }

        sub_bishop_parity[WHITE] = NONE;
        sub_bishop_parity[BLACK] = bishop_parity[BLACK];

        eindex = GetEndingType(Board->piece_type_count, NULL, sub_bishop_parity,
                               FREE_PAWNS);

        if (Verbose > 4) {
            MyPrintf("GetMBInfo: eindex %d with black bishop parity %s\n",
                     eindex, (bishop_parity[BLACK] == EVEN) ? "even" : "odd");
        }

        if (eindex >= 0) {
            memcpy(mb_info->parity_index[mb_info->num_parities].bishop_parity,
                   sub_bishop_parity, sizeof(sub_bishop_parity));
            mb_info->parity_index[mb_info->num_parities].eptr =
                &IndexTable[eindex];
            mb_info->num_parities++;
            InitPermutationTables(mb_info->piece_type_count, sub_bishop_parity);
        }
    }

    if (mb_info->num_parities == 0) {
        if (Verbose > 4) {
            MyPrintf("GetMBInfo: Could not map to index type, for piece type "
                     "counts:\n");
            PrintPieceTypeCount(Board->piece_type_count, NULL);
        }
        return ETYPE_NOT_MAPPED;
    }

    GetMBIndex(mb_info->mb_position, mb_info->num_pieces, pawns_present,
               mb_info->parity_index[0].eptr, &mb_info->kk_index,
               &mb_info->parity_index[0].index);

    for (int i = 1; i < mb_info->num_parities; i++) {
        int kk_index;
        GetMBIndex(mb_info->mb_position, mb_info->num_pieces, pawns_present,
                   mb_info->parity_index[i].eptr, &kk_index,
                   &mb_info->parity_index[i].index);
        assert(kk_index == mb_info->kk_index);
    }

    return 0;
}

static int LookupScoreInDatabase(BOARD *Board) {
    if (Verbose > 4) {
        MyPrintf("Entering LookupScoreInDatabase\n");
    }

    if (Board->num_pieces == 2)
        return DRAW;

    if (Board->num_pieces == 3) {
        if (Board->piece_type_count[WHITE][BISHOP] ||
            Board->piece_type_count[WHITE][KNIGHT] ||
            Board->piece_type_count[BLACK][BISHOP] ||
            Board->piece_type_count[BLACK][KNIGHT]) {
            Board->score = DRAW;
            Board->zz_type = NO_MZUG;
            return DRAW;
        }
    }

    if (Board->castle && !IgnoreCastle) {
        Board->score = UNKNOWN;
        Board->zz_type = UNKNOWN;
        return UNKNOWN;
    }

    MB_INFO mb_info;

    int result = GetMBInfo(Board, &mb_info);

    if (Verbose > 3) {
        MyPrintf("LookupScoreInDatabase: from GetMBInfo\n");
        MyPrintf("  result: %d\n", result);
        MyPrintf("  kk_index: %d\n", mb_info.kk_index);
        MyPrintf("  parity: %d\n", mb_info.parity);
        MyPrintf("  n_parities: %d\n", mb_info.num_parities);
        MyPrintf("  pawn_file_type: %d\n", mb_info.pawn_file_type);
        if (mb_info.pawn_file_type == OP_11_PAWNS ||
            mb_info.pawn_file_type == BP_11_PAWNS)
            MyPrintf("  index 1 opposing pair: " DEC_ZINDEX_FORMAT "\n",
                     mb_info.index_op_11);
        if (mb_info.pawn_file_type == BP_11_PAWNS)
            MyPrintf("  index 1 blocking pair: " DEC_ZINDEX_FORMAT "\n",
                     mb_info.index_bp_11);
        if (mb_info.pawn_file_type == OP_21_PAWNS)
            MyPrintf(
                "   index 2 vs 1 pawns, 1 opposing pair: " DEC_ZINDEX_FORMAT
                "\n",
                mb_info.index_op_21);
        if (mb_info.pawn_file_type == OP_12_PAWNS)
            MyPrintf(
                "   index 1 vs 2 pawns, 1 opposing pair: " DEC_ZINDEX_FORMAT
                "\n",
                mb_info.index_op_12);
        if (mb_info.pawn_file_type == DP_22_PAWNS)
            MyPrintf("  index 2 opposing pairs: " DEC_ZINDEX_FORMAT "\n",
                     mb_info.index_dp_22);
        if (mb_info.pawn_file_type == OP_22_PAWNS)
            MyPrintf("  index 2 pawn pairs, 1 opposing pair: " DEC_ZINDEX_FORMAT
                     "\n",
                     mb_info.index_op_22);
        if (mb_info.pawn_file_type == OP_31_PAWNS)
            MyPrintf(
                "   index 3 vs 1 pawns, 1 opposing pair: " DEC_ZINDEX_FORMAT
                "\n",
                mb_info.index_op_31);
        if (mb_info.pawn_file_type == OP_13_PAWNS)
            MyPrintf(
                "   index 1 vs 3 pawns, 1 opposing pair: " DEC_ZINDEX_FORMAT
                "\n",
                mb_info.index_op_13);
        for (int i = 0; i < mb_info.num_parities; i++) {
            MyPrintf("  index %d: " DEC_ZINDEX_FORMAT "\n", i,
                     mb_info.parity_index[i].index);
        }
    }

    if (result < 0) {
        if (Verbose > 3) {
            MyPrintf("LookupScoreInDatabase: returning negative result %d\n",
                     result);
            MyFlush();
        }
        return result;
    }

    POSITION_DB pos;
    char ending[16];

    GetEndingName(Board->piece_type_count, ending);

    strcpy(pos.ending, ending);
    pos.kk_index = mb_info.kk_index;
    pos.offset = mb_info.parity_index[0].index;
    pos.side = Board->side;

    POSITION_DB *pptr = (POSITION_DB *)bsearch(
        &pos, PositionDB, NumDBPositions, sizeof(POSITION_DB), db_pos_compar);

    if (pptr != NULL) {
        if (Verbose > 3) {
            MyPrintf(
                "LookupScoreInDatabase: found position, score=%d, promos=%d\n",
                pptr->score, pptr->promos);
            MyFlush();
        }
        Board->score = pptr->score;
        Board->zz_type = pptr->zz_type;
        Board->promos = pptr->promos;
        return pptr->score;
    }

    // if not found, try flipped position

    BOARD flipped_board;

    memcpy(&flipped_board, Board, sizeof(BOARD));

    FlipBoard(&flipped_board);

    result = GetMBInfo(&flipped_board, &mb_info);

    if (Verbose > 3) {
        MyPrintf("LookupScoreInDatabase: trying flipped ending\n");
        MyFlush();
    }

    if (result < 0) {
        if (Verbose > 3) {
            MyPrintf("LookupScoreInDatabase: returning negative result after "
                     "flip\n");
            MyFlush();
        }
        return result;
    }

    GetEndingName(Board->piece_type_count, ending);

    strcpy(pos.ending, ending);
    pos.kk_index = mb_info.kk_index;
    pos.offset = mb_info.parity_index[0].index;
    pos.side = flipped_board.side;

    pptr = (POSITION_DB *)bsearch(&pos, PositionDB, NumDBPositions,
                                  sizeof(POSITION_DB), db_pos_compar);

    if (pptr != NULL) {
        if (Verbose > 3) {
            MyPrintf("LookupScoreInDatabase: found position after flipping, "
                     "score=%d, promos=%d\n",
                     pptr->score, pptr->promos);
            MyFlush();
        }
        Board->score = pptr->score;
        Board->zz_type = pptr->zz_type;
        Board->promos = pptr->promos;
        return pptr->score;
    }

    if (Verbose > 3) {
        MyPrintf("LookupScoreInDatabase: returning unknown\n");
        MyFlush();
    }

    Board->score = UNKNOWN;
    Board->zz_type = UNKNOWN;
    return UNKNOWN;
}

static int GetYKInfo(BOARD *Board, YK_INFO *yk_info) {
    if (Board->num_pieces > MAX_PIECES_YK) {
        if (Verbose > 3) {
            MyPrintf("GetYKInfo: too many pieces for database %d\n",
                     Board->num_pieces);
        }
        return TOO_MANY_PIECES;
    }

    memcpy(yk_info->piece_type_count, Board->piece_type_count,
           sizeof(Board->piece_type_count));

    int bishop_parity[2] = {NONE, NONE};

    InitPermutationTables(yk_info->piece_type_count, bishop_parity);

    yk_info->num_pieces = Board->num_pieces;

    GetYKPosition(Board, yk_info->yk_position);

    memset(yk_info->yk_piece_types, 0, sizeof(yk_info->yk_piece_types));

    int eindex =
        GetEndingTypeYK(Board->piece_type_count, yk_info->yk_piece_types);

    if (Verbose > 3) {
        MyPrintf("GetYKInfo: eindex = %d\n", eindex);
    }

    if (eindex < 0)
        return ETYPE_NOT_MAPPED;

    bool pawns_present = yk_info->piece_type_count[WHITE][PAWN] ||
                         yk_info->piece_type_count[BLACK][PAWN];

    yk_info->eptr = &IndexTable[eindex];

    GetYKIndex(yk_info->yk_position, yk_info->num_pieces, pawns_present,
               yk_info->eptr, &yk_info->kk_index, &yk_info->index);

    return 0;
}

/*
 * GetMBResult:
 *
 * returns a negative number if some kind of error is encountered,
 * UNRESOLVED if wtm does not win, or btm does not lose.
 *
 * A value of UNRESOLVED will require probing of a "flipped" position to
 * resolve whether wtm does not win is a draw or loss for white, or btm
 * does not lose is a draw or win for black.
 *
 * Other positive integers indicate the number of moves for a win for wtm,
 * or the number of moves for a loss for btm (using the DTC metric).
 *
 * A checkmate is loss in 0
 */

static int GetMBCacheResult(MB_INFO *mb_info, int side) {
    // check whether file for ending is already opened

    int file_index = -1;
    FILE_CACHE *fcache;
    ZINDEX index = ALL_ONES;
    int n;

    for (n = 0; n < num_cached_files[side]; n++) {
        int np = cached_file_lru[n][side];
        fcache = &FileCache[np][side];
        if (fcache->kk_index != mb_info->kk_index)
            continue;
        if (memcmp(fcache->piece_type_count, mb_info->piece_type_count,
                   sizeof(fcache->piece_type_count)))
            continue;

        bool found_parity = false;
        for (int i = 0; i < mb_info->num_parities; i++) {
            if (fcache->pawn_file_type != FREE_PAWNS)
                continue;
            if ((fcache->bishop_parity[WHITE] == NONE ||
                 (fcache->bishop_parity[WHITE] ==
                  mb_info->parity_index[i].bishop_parity[WHITE])) &&
                (fcache->bishop_parity[BLACK] == NONE ||
                 (fcache->bishop_parity[BLACK] ==
                  mb_info->parity_index[i].bishop_parity[BLACK]))) {
                found_parity = true;
                index = mb_info->parity_index[i].index;
                break;
            }
        }

        // if not found, try possible blocked/opposing pawn files

        if (!found_parity) {
            if ((mb_info->pawn_file_type == OP_11_PAWNS ||
                 mb_info->pawn_file_type == BP_11_PAWNS) &&
                fcache->pawn_file_type == OP_11_PAWNS) {
                if (mb_info->index_op_11 != ALL_ONES) {
                    index = mb_info->index_op_11;
                    continue;
                }
            }
            if (mb_info->pawn_file_type == BP_11_PAWNS) {
                if (mb_info->index_bp_11 != ALL_ONES &&
                    fcache->pawn_file_type == BP_11_PAWNS) {
                    index = mb_info->index_bp_11;
                    continue;
                }
            }
            if (mb_info->pawn_file_type == OP_21_PAWNS) {
                if (mb_info->index_op_21 != ALL_ONES &&
                    fcache->pawn_file_type == OP_21_PAWNS) {
                    index = mb_info->index_op_21;
                    continue;
                }
            }
            if (mb_info->pawn_file_type == OP_12_PAWNS) {
                if (mb_info->index_op_12 != ALL_ONES &&
                    fcache->pawn_file_type == OP_12_PAWNS) {
                    index = mb_info->index_op_12;
                    continue;
                }
            }
            if (mb_info->pawn_file_type == OP_22_PAWNS ||
                mb_info->pawn_file_type == DP_22_PAWNS) {
                if (mb_info->index_op_22 != ALL_ONES &&
                    fcache->pawn_file_type == OP_22_PAWNS) {
                    index = mb_info->index_op_22;
                    continue;
                }
            }
            if (mb_info->pawn_file_type == DP_22_PAWNS) {
                if (mb_info->index_dp_22 != ALL_ONES &&
                    fcache->pawn_file_type == DP_22_PAWNS) {
                    index = mb_info->index_dp_22;
                    continue;
                }
            }
            if (mb_info->pawn_file_type == OP_31_PAWNS) {
                if (mb_info->index_op_31 != ALL_ONES &&
                    fcache->pawn_file_type == OP_31_PAWNS) {
                    index = mb_info->index_op_31;
                    continue;
                }
            }
            if (mb_info->pawn_file_type == OP_13_PAWNS) {
                if (mb_info->index_op_13 != ALL_ONES &&
                    fcache->pawn_file_type == OP_13_PAWNS) {
                    index = mb_info->index_op_13;
                    continue;
                }
            }
        }

        file_index = np;
        break;
    }

    // file pointer is not cached, return

    if (file_index == -1) {
        return UNKNOWN;
    }

    int b_index = index / fcache->header.block_size;

    // if block is not cached, return

    if (b_index != fcache->block_index) {
        return UNKNOWN;
    }

    // move cached file to front of queue so it is tried first next time

    if (n > 0) {
        for (int i = n; i > 0; i--) {
            cached_file_lru[i][side] = cached_file_lru[i - 1][side];
        }
    }

    cached_file_lru[0][side] = file_index;

    int result = fcache->block[index % fcache->header.block_size];

    // if result is 254 check whether larger results are cached
    if (result == 254 && fcache->header.max_depth > 254) {
        file_index = -1;
        FILE_CACHE_HIGH_DTZ *fcache_high_dtz;

        for (n = 0; n < num_cached_files_high_dtz[side]; n++) {
            int np = cached_file_high_dtz_lru[n][side];
            fcache_high_dtz = &FileCacheHighDTZ[np][side];
            if (fcache_high_dtz->kk_index != mb_info->kk_index)
                continue;
            if (memcmp(fcache_high_dtz->piece_type_count,
                       mb_info->piece_type_count,
                       sizeof(fcache_high_dtz->piece_type_count)))
                continue;
            bool found_parity = false;
            for (int i = 0; i < mb_info->num_parities; i++) {
                if (fcache_high_dtz->pawn_file_type != FREE_PAWNS)
                    continue;
                if ((fcache_high_dtz->bishop_parity[WHITE] == NONE ||
                     (fcache_high_dtz->bishop_parity[WHITE] ==
                      mb_info->parity_index[i].bishop_parity[WHITE])) &&
                    (fcache_high_dtz->bishop_parity[BLACK] == NONE ||
                     (fcache_high_dtz->bishop_parity[BLACK] ==
                      mb_info->parity_index[i].bishop_parity[BLACK]))) {
                    found_parity = true;
                    index = mb_info->parity_index[i].index;
                    break;
                }
            }

            // if not found, check any possible blocked pawn files

            if (!found_parity) {
                if ((mb_info->pawn_file_type == OP_11_PAWNS ||
                     mb_info->pawn_file_type == BP_11_PAWNS) &&
                    fcache_high_dtz->pawn_file_type == OP_11_PAWNS) {
                    if (mb_info->index_op_11 != ALL_ONES) {
                        index = mb_info->index_op_11;
                        continue;
                    }
                }

                if (mb_info->pawn_file_type == BP_11_PAWNS &&
                    fcache_high_dtz->pawn_file_type == BP_11_PAWNS) {
                    if (mb_info->index_bp_11 != ALL_ONES) {
                        index = mb_info->index_bp_11;
                        continue;
                    }
                }

                if (mb_info->pawn_file_type == OP_21_PAWNS &&
                    fcache_high_dtz->pawn_file_type == OP_21_PAWNS) {
                    if (mb_info->index_op_21 != ALL_ONES) {
                        index = mb_info->index_op_21;
                        continue;
                    }
                }

                if (mb_info->pawn_file_type == OP_12_PAWNS &&
                    fcache_high_dtz->pawn_file_type == OP_12_PAWNS) {
                    if (mb_info->index_op_12 != ALL_ONES) {
                        index = mb_info->index_op_12;
                        continue;
                    }
                }

                if ((mb_info->pawn_file_type == OP_22_PAWNS ||
                     mb_info->pawn_file_type == DP_22_PAWNS) &&
                    fcache_high_dtz->pawn_file_type == OP_22_PAWNS) {
                    if (mb_info->index_op_22 != ALL_ONES) {
                        index = mb_info->index_op_22;
                        continue;
                    }
                }

                if (mb_info->pawn_file_type == DP_22_PAWNS &&
                    fcache_high_dtz->pawn_file_type == DP_22_PAWNS) {
                    if (mb_info->index_dp_22 != ALL_ONES) {
                        index = mb_info->index_dp_22;
                        continue;
                    }
                }

                if (mb_info->pawn_file_type == OP_31_PAWNS &&
                    fcache_high_dtz->pawn_file_type == OP_31_PAWNS) {
                    if (mb_info->index_op_31 != ALL_ONES) {
                        index = mb_info->index_op_31;
                        continue;
                    }
                }

                if (mb_info->pawn_file_type == OP_13_PAWNS &&
                    fcache_high_dtz->pawn_file_type == OP_13_PAWNS) {
                    if (mb_info->index_op_13 != ALL_ONES) {
                        index = mb_info->index_op_13;
                        continue;
                    }
                }
            }

            file_index = np;
            break;
        }

        // file pointer is not cached, return

        if (file_index == -1) {
            return UNKNOWN;
        }

        // if index is outside range of all indices with depth > 254, depth must
        // be 254
        if (index < fcache_high_dtz->starting_index[0] ||
            index > fcache_high_dtz
                        ->starting_index[fcache_high_dtz->header.num_blocks]) {
            CacheHits++;
            return 254;
        }

        // if no block is cached, return unknown
        if (fcache_high_dtz->block_index == -1)
            return UNKNOWN;

        ULONG n_per_block = (fcache_high_dtz->header.n_elements +
                             fcache_high_dtz->header.num_blocks - 1) /
                            fcache_high_dtz->header.num_blocks;

        if (fcache_high_dtz->block_index ==
            fcache_high_dtz->header.num_blocks - 1) {
            ULONG rem = fcache_high_dtz->header.n_elements % n_per_block;
            if (rem != 0)
                n_per_block = rem;
        }

        // check whether index is in range of cached block.  If not, return
        // unknown

        HIGH_DTZ *hptr = (HIGH_DTZ *)fcache_high_dtz->block;
        if (index < hptr[0].index || index > hptr[n_per_block - 1].index)
            return UNKNOWN;

        // now perform binary search on block that may contain score
        // corresponding to index
        HIGH_DTZ key;
        key.index = index;

        HIGH_DTZ *kptr =
            (HIGH_DTZ *)bsearch(&key, fcache_high_dtz->block, n_per_block,
                                sizeof(HIGH_DTZ), high_dtz_compar);

        CacheHits++;

        // if no score > 254 exists, score must be = 254
        if (kptr == NULL)
            return 254;
        else
            return kptr->score;
    } else {
        CacheHits++;
        if (result == 255)
            result = UNRESOLVED;
    }

    return result;
}

static int GetYKCacheResult(YK_INFO *yk_info, int side) {
    // check whether file for ending is already opened

    int file_index = -1;
    FILE_CACHE_YK *fcache;

    for (int n = 0; n < num_cached_files_yk[side]; n++) {
        int np = cached_file_lru_yk[n][side];
        fcache = &FileCacheYK[np][side];
        if (memcmp(fcache->piece_type_count, yk_info->piece_type_count,
                   sizeof(fcache->piece_type_count)))
            continue;

        file_index = np;
        break;
    }

    // file pointer is not cached, return

    if (file_index == -1) {
        return UNKNOWN;
    }

    int kk_index = yk_info->kk_index;

    if (yk_info->index > 0xeffffff) {
        return UNKNOWN;
    }

    ULONG offset = yk_info->index;

    bool pawns_present = yk_info->piece_type_count[WHITE][PAWN] ||
                         yk_info->piece_type_count[BLACK][PAWN];

    int num_kk = N_KINGS_NOPAWNS;
    if (pawns_present) {
        num_kk = N_KINGS;
    }

    int sub_blocks = fcache->num_blocks / num_kk;
    int sub_index = offset / fcache->block_size;

    int b_index = kk_index * sub_blocks + sub_index;

    // if block is not cached, return

    if (b_index != fcache->block_index) {
        return UNKNOWN;
    }

    // move cached file to front of queue so it is tried first next time

    if (file_index > 0) {
        for (int i = file_index; i > 0; i--) {
            cached_file_lru[i][side] = cached_file_lru[i - 1][side];
        }
    }

    cached_file_lru[0][side] = file_index;

    int result = fcache->block[offset % fcache->block_size];

    // if result is 254 check whether larger results are cached
    if (result == 254 && fcache->max_depth > 254) {
        if (fcache->block_high == NULL)
            return UNKNOWN;
        HDATA tdata;
        tdata.kindex = yk_info->kk_index;
        tdata.offset = yk_info->index;
        HDATA *tptr =
            (HDATA *)bsearch(&tdata, fcache->block_high, fcache->num_high_dtc,
                             sizeof(HDATA), CompareHigh);
        if (tptr != NULL) {
            CacheHits++;
            return tptr->dtc;
        }
    } else {
        CacheHits++;
        if (result == 255)
            result = UNRESOLVED;
    }

    return result;
}

/*
 * GetMBResult:
 *
 * returns a negative number if some kind of error is encountered,
 * UNRESOLVED if wtm does not win, or btm does not lose.
 *
 * A value of UNRESOLVED will require probing of a "flipped" position to
 * resolve whether wtm does not win is a draw or loss for white, or btm
 * does not lose is a draw or win for black.
 *
 * Other positive integers indicate the number of moves for a win for wtm,
 * or the number of moves for a loss for btm (using the DTC metric).
 *
 * A checkmate is loss in 0
 */

static int GetYKResult(BOARD *Board, INDEX_DATA *ind) {
    YK_INFO yk_info;
    FILE_CACHE_YK *fcache = NULL;
    bool cache_hit = true;

    if (Verbose > 3) {
        MyPrintf("GetYKResult: Entering with number of pieces: %d\n",
                 Board->num_pieces);
    }

    if (Board->num_pieces > MAX_PIECES_YK) {
        return TOO_MANY_PIECES;
    }

    int result = GetYKInfo(Board, &yk_info);

    if (Verbose > 3) {
        MyPrintf("GetYKResult: from GetYKInfo\n");
        MyPrintf("  result: %d\n", result);
        MyPrintf("  kk_index: %d\n", yk_info.kk_index);
        MyPrintf("  offset: " DEC_ZINDEX_FORMAT "\n", yk_info.index);
    }

    if (yk_info.index > 0xefffffff) {
        if (Verbose > 3) {
            MyPrintf("GetYKResult: offset " DEC_ZINDEX_FORMAT
                     " larger than 64^5\n",
                     yk_info.index);
        }
        return BAD_ZONE_SIZE;
    }

    ind->kk_index = yk_info.kk_index;
    ind->index = yk_info.index;

    if (result < 0)
        return result;

    int side = Board->side;

    // check whether file for ending is already opened

    if (Verbose > 3) {
        MyPrintf("GetYKResult: scanning %d cached files\n",
                 num_cached_files_yk[side]);
    }

    int file_index = -1;
    for (int n = 0; n < num_cached_files_yk[side]; n++) {
        int np = cached_file_lru_yk[n][side];
        fcache = &FileCacheYK[np][side];
        if (memcmp(fcache->piece_type_count, yk_info.piece_type_count,
                   sizeof(fcache->piece_type_count))) {
            continue;
        }

        file_index = np;
        // move file to front of queue so it is tried first next time
        if (n > 0) {
            for (int i = n; i > 0; i--) {
                cached_file_lru_yk[i][side] = cached_file_lru_yk[i - 1][side];
            }
        }
        cached_file_lru_yk[0][side] = file_index;
        break;
    }

    if (Verbose > 3) {
        MyPrintf("GetYKResult: file_index from scan: %d\n", file_index);
    }

    // if file pointer is not cached, need to open new file

    if (file_index == -1) {
        cache_hit = false;
        char ending[64];
        GetEndingName(yk_info.piece_type_count, ending);
        if (Verbose > 3) {
            MyPrintf("GetYKResult: Ending Name: %s\n", ending);
        }

        char base_name[64];
        GetYKBaseFileName(yk_info.piece_type_count, side, base_name);
        strcat(base_name, ".yk");

        file file_yk = OpenYKFile(base_name);

        if (file_yk == NULL) {
            if (Verbose > 2) {
                MyPrintf("Could not open YK file %s\n", base_name);
            }
            return YK_FILE_MISSING;
        }

        if (num_cached_files_yk[side] < MAX_FILES_YK) {
            file_index = num_cached_files_yk[side];
            num_cached_files_yk[side]++;
        } else
            file_index = cached_file_lru_yk[MAX_FILES_YK - 1][side];

        fcache = &FileCacheYK[file_index][side];

        if (fcache->fp != NULL) {
            if (Verbose > 2) {
                MyPrintf("Closing previous file\n");
            }
            f_close(fcache->fp);
        }

        fcache->fp = file_yk;

        BYTE header[YK_HEADER_SIZE];
        if (f_read(&header, YK_HEADER_SIZE, file_yk, 0) != YK_HEADER_SIZE) {
            if (Verbose > 2) {
                MyPrintf("Could not read %d header bytes for yk file %s\n",
                         sizeof(header), base_name);
            }
            f_close(file_yk);
            return HEADER_READ_ERROR;
        }

        ULONG block_size, num_blocks;

        memcpy(&block_size, &header[0], 4);
        memcpy(&num_blocks, &header[4], 4);

        if (num_blocks > fcache->max_num_blocks) {
            if (fcache->max_num_blocks > 0) {
                MyFree(fcache->offsets,
                       (fcache->max_num_blocks + 1) * sizeof(INDEX));
            }
            fcache->max_num_blocks = num_blocks;
            fcache->offsets =
                (INDEX *)MyMalloc((fcache->max_num_blocks + 1) * sizeof(INDEX));
            if (fcache->offsets == NULL) {
                if (Verbose > 2) {
                    MyPrintf("Could not allocate %lu offsets\n",
                             fcache->max_num_blocks + 1);
                }
                return OFFSET_ALLOC_ERROR;
            }
        }

        fcache->num_blocks = num_blocks;

        if (f_read(fcache->offsets, (num_blocks + 1) * sizeof(INDEX), file_yk,
                   YK_HEADER_SIZE) != (num_blocks + 1) * sizeof(INDEX)) {
            if (Verbose > 2) {
                MyPrintf("Could not read %d offsets from %s\n", num_blocks + 1,
                         base_name);
            }
            f_close(file_yk);
            return OFFSET_READ_ERROR;
        }

        fcache->block_size = block_size;

        BYTE archive_id;

        memcpy(&archive_id, &header[23], 1);

        if (archive_id == NO_COMPRESSION_YK)
            fcache->compression_method = NO_COMPRESSION;
        else if (archive_id == ZLIB_YK)
            fcache->compression_method = ZLIB;
        else if (archive_id == ZSTD_YK)
            fcache->compression_method = ZSTD;
        else if (archive_id == LZMA_YK) {
            fprintf(stderr, "LZMA decompression not supported\n");
            exit(1);
        } else if (archive_id == BZIP_YK) {
            fprintf(stderr, "BZIP decompression not supported\n");
            exit(1);
        } else {
            fprintf(stderr, "Unknown decompression method %d in YK file %s\n",
                    archive_id, base_name);
            exit(1);
        }

        int max_depth;

        memcpy(&max_depth, &header[32], 4);

        fcache->max_depth = max_depth;

        if (max_depth > 254) {
            unsigned int num_high_dtc_low, num_high_dtc_high;

            memcpy(&num_high_dtc_low, &header[36], 4);
            memcpy(&num_high_dtc_high, &header[40], 4);
            INDEX num_high_dtc =
                (((INDEX)(num_high_dtc_high)) << 32) + num_high_dtc_low;
            fcache->num_high_dtc = num_high_dtc;
        }

        memcpy(fcache->piece_type_count, yk_info.piece_type_count,
               sizeof(yk_info.piece_type_count));

        fcache->block_index = -1;

        // move file index to front of queue
        if (num_cached_files_yk[side] > 1) {
            for (int i = num_cached_files_yk[side] - 1; i > 0; i--) {
                cached_file_lru_yk[i][side] = cached_file_lru_yk[i - 1][side];
            }
        }
        cached_file_lru_yk[0][side] = file_index;
    }

    if (Verbose > 4) {
        MyPrintf(
            "Searching YK index %lu block index %lu, header block size %lu\n",
            ind->index, fcache->block_index, fcache->block_size);
    }

    bool pawns_present = yk_info.piece_type_count[WHITE][PAWN] ||
                         yk_info.piece_type_count[BLACK][PAWN];

    int num_kk = N_KINGS_NOPAWNS;
    if (pawns_present) {
        num_kk = N_KINGS;
    }

    int sub_blocks = fcache->num_blocks / num_kk;
    int sub_index = ind->index / fcache->block_size;

    int b_index = ind->kk_index * sub_blocks + sub_index;

    if (b_index != fcache->block_index) {
        cache_hit = false;
        ULONG length = fcache->offsets[b_index + 1] - fcache->offsets[b_index];
        if (Verbose > 4) {
            MyPrintf(
                "Reading compressed block size %lu at offset " DEC_INDEX_FORMAT
                " block index %lu\n",
                length, fcache->offsets[b_index], b_index);
        }
        if (length > CompressionBufferSize) {
            if (CompressionBuffer != NULL) {
                MyFree(CompressionBuffer, CompressionBufferSize);
            }
            CompressionBufferSize = length;
            CompressionBuffer = (BYTE *)MyMalloc(CompressionBufferSize);
            if (CompressionBuffer == NULL) {
                fprintf(stderr,
                        "Could not allocate CompressionBuffer size %lu\n",
                        CompressionBufferSize);
                exit(1);
            }
        }
        f_read(CompressionBuffer, length, fcache->fp, fcache->offsets[b_index]);
        ULONG tmp_zone_size = fcache->block_size;
        if (tmp_zone_size > fcache->max_block_size) {
            if (fcache->block != NULL) {
                MyFree(fcache->block, fcache->max_block_size);
            }
            fcache->max_block_size = tmp_zone_size;
            fcache->block = (BYTE *)MyMalloc(tmp_zone_size);
            if (fcache->block == NULL) {
                fprintf(stderr, "Could not allocate zone block size %lu\n",
                        fcache->max_block_size);
                exit(1);
            }
        }
        MyUncompress(fcache->block, &tmp_zone_size, CompressionBuffer, length,
                     fcache->compression_method);
        assert(tmp_zone_size == fcache->block_size);
        fcache->block_index = b_index;
    }

    result = fcache->block[ind->index % fcache->block_size];

    if (Verbose > 4) {
        MyPrintf("YK result from database: %d\n", result);
    }

    if (cache_hit)
        CacheHits++;
    else
        DBHits++;

    if (result == 254) {
        if (fcache->max_depth == 254)
            return result;
        if (fcache->block_high == NULL) {
            // High dtc file (.__) not yet initialized?
            if (fcache->fp_high == NULL) {
                char base_name[64];
                GetYKBaseFileName(yk_info.piece_type_count, side, base_name);
                strcat(base_name, ".__");

                file file_yk = OpenYKFile(base_name);

                if (file_yk == NULL) {
                    if (Verbose > 2) {
                        MyPrintf("Could not open YK file %s\n", base_name);
                    }
                    return HIGH_DTZ_MISSING;
                }

                fcache->fp_high = file_yk;
            }
            if (fcache->num_high_dtc > 0) {
                fcache->block_high =
                    (HDATA *)MyMalloc(fcache->num_high_dtc * sizeof(HDATA));
                if (fcache->block_high == NULL) {
                    if (Verbose > 2) {
                        MyPrintf("Could not allocate " DEC_INDEX_FORMAT
                                 " high DTC scores\n",
                                 fcache->num_high_dtc);
                    }
                    return HIGH_DTZ_MISSING;
                }
                INDEX nread = f_read(fcache->block_high,
                                     fcache->num_high_dtc * sizeof(HDATA),
                                     fcache->fp_high, 0);
                if (nread != fcache->num_high_dtc * sizeof(HDATA)) {
                    if (Verbose > 2) {
                        MyPrintf("Read " DEC_INDEX_FORMAT
                                 " bytes from high DTC file, "
                                 "expected " DEC_INDEX_FORMAT "\n",
                                 nread, fcache->num_high_dtc * sizeof(HDATA));
                    }
                    return HIGH_DTZ_MISSING;
                }
                qsort(fcache->block_high, fcache->num_high_dtc, sizeof(HDATA),
                      CompareHigh);
            } else {
                return HIGH_DTZ_MISSING;
            }
        }
        HDATA tdata;
        tdata.kindex = yk_info.kk_index;
        tdata.offset = yk_info.index;
        HDATA *tptr =
            (HDATA *)bsearch(&tdata, fcache->block_high, fcache->num_high_dtc,
                             sizeof(HDATA), CompareHigh);
        if (tptr != NULL) {
            DBHits++;
            return tptr->dtc;
        }
    } else if (result == 255) {
        result = UNRESOLVED;
    }

    return result;
}

static int GetMBResult(BOARD *Board, INDEX_DATA *ind) {
    if (Verbose > 3) {
        MyPrintf("Entering GetMBResult with number of pieces: %d\n",
                 Board->num_pieces);
    }

    if (YKFormatOnly) {
        INDEX_DATA ind_yk;
        return GetYKResult(Board, &ind_yk);
    }

    MB_INFO mb_info;
    FILE_CACHE *fcache;
    bool cache_hit = true;

    int result = GetMBInfo(Board, &mb_info);

    if (Verbose > 3) {
        MyPrintf("GetMBResult: from GetMBInfo\n");
        MyPrintf("  result: %d\n", result);
        MyPrintf("  kk_index: %d\n", mb_info.kk_index);
        MyPrintf("  parity: %d\n", mb_info.parity);
        MyPrintf("  n_parities: %d\n", mb_info.num_parities);
        MyPrintf("  number of pieces: %d\n", mb_info.num_pieces);
        MyPrintf("  pawn_file_type: %s\n",
                 OpExtensionName[mb_info.pawn_file_type]);
        if (mb_info.pawn_file_type == OP_11_PAWNS ||
            mb_info.pawn_file_type == BP_11_PAWNS)
            MyPrintf("  index 1 opposing pair: " DEC_ZINDEX_FORMAT "\n",
                     mb_info.index_op_11);
        if (mb_info.pawn_file_type == BP_11_PAWNS)
            MyPrintf("  index 1 blocking pair: " DEC_ZINDEX_FORMAT "\n",
                     mb_info.index_bp_11);
        if (mb_info.pawn_file_type == OP_21_PAWNS)
            MyPrintf(
                "   index 2 vs 1 pawns, 1 opposing pair: " DEC_ZINDEX_FORMAT
                "\n",
                mb_info.index_op_21);
        if (mb_info.pawn_file_type == OP_12_PAWNS)
            MyPrintf(
                "   index 1 vs 2 pawns, 1 opposing pair: " DEC_ZINDEX_FORMAT
                "\n",
                mb_info.index_op_12);
        if (mb_info.pawn_file_type == OP_22_PAWNS ||
            mb_info.pawn_file_type == DP_22_PAWNS)
            MyPrintf("  index 2 pawn pairs, 1 opposing pair: " DEC_ZINDEX_FORMAT
                     "\n",
                     mb_info.index_op_22);
        if (mb_info.pawn_file_type == DP_22_PAWNS)
            MyPrintf("  index 2 opposing pairs: " DEC_ZINDEX_FORMAT "\n",
                     mb_info.index_dp_22);
        if (mb_info.pawn_file_type == OP_31_PAWNS)
            MyPrintf(
                "   index 3 vs 1 pawns, 1 opposing pair: " DEC_ZINDEX_FORMAT
                "\n",
                mb_info.index_op_31);
        if (mb_info.pawn_file_type == OP_13_PAWNS)
            MyPrintf(
                "   index 1 vs 3 pawns, 1 opposing pair: " DEC_ZINDEX_FORMAT
                "\n",
                mb_info.index_op_13);
        if (mb_info.pawn_file_type == OP_41_PAWNS)
            MyPrintf(
                "   index 4 vs 1 pawns, 1 opposing pair: " DEC_ZINDEX_FORMAT
                "\n",
                mb_info.index_op_41);
        if (mb_info.pawn_file_type == OP_14_PAWNS)
            MyPrintf(
                "   index 1 vs 4 pawns, 1 opposing pair: " DEC_ZINDEX_FORMAT
                "\n",
                mb_info.index_op_14);
        if (mb_info.pawn_file_type == OP_32_PAWNS)
            MyPrintf(
                "   index 3 vs 2 pawns, 1 opposing pair: " DEC_ZINDEX_FORMAT
                "\n",
                mb_info.index_op_32);
        if (mb_info.pawn_file_type == OP_23_PAWNS)
            MyPrintf(
                "   index 2 vs 3 pawns, 1 opposing pair: " DEC_ZINDEX_FORMAT
                "\n",
                mb_info.index_op_23);
        if (mb_info.pawn_file_type == OP_33_PAWNS)
            MyPrintf(
                "   index 3 vs 3 pawns, 1 opposing pair: " DEC_ZINDEX_FORMAT
                "\n",
                mb_info.index_op_33);
        if (mb_info.pawn_file_type == OP_42_PAWNS)
            MyPrintf(
                "   index 4 vs 2 pawns, 1 opposing pair: " DEC_ZINDEX_FORMAT
                "\n",
                mb_info.index_op_42);
        if (mb_info.pawn_file_type == OP_24_PAWNS)
            MyPrintf(
                "   index 2 vs 4 pawns, 1 opposing pair: " DEC_ZINDEX_FORMAT
                "\n",
                mb_info.index_op_24);
        for (int i = 0; i < mb_info.num_parities; i++) {
            MyPrintf("  index %d: " DEC_ZINDEX_FORMAT "\n", i,
                     mb_info.parity_index[i].index);
        }
    }

    ind->kk_index = mb_info.kk_index;
    ind->index = mb_info.parity_index[0].index;

    if (result < 0) {
        if (Verbose > 3) {
            MyPrintf("GetMBResult: returning with negative results %d\n",
                     result);
        }
        return result;
    }

    int side = Board->side;

    // check whether file for ending is already opened

    if (Verbose > 3) {
        MyPrintf("GetMBResult: scanning %d cached files\n",
                 num_cached_files[side]);
    }

    int file_index = -1;
    for (int n = 0; n < num_cached_files[side]; n++) {
        int np = cached_file_lru[n][side];
        fcache = &FileCache[np][side];
        if (fcache->kk_index != mb_info.kk_index)
            continue;
        if (memcmp(fcache->piece_type_count, mb_info.piece_type_count,
                   sizeof(fcache->piece_type_count)))
            continue;
        bool found_parity = false;
        for (int i = 0; i < mb_info.num_parities; i++) {
            if (fcache->pawn_file_type != FREE_PAWNS)
                continue;
            if ((fcache->bishop_parity[WHITE] == NONE ||
                 (fcache->bishop_parity[WHITE] ==
                  mb_info.parity_index[i].bishop_parity[WHITE])) &&
                (fcache->bishop_parity[BLACK] == NONE ||
                 (fcache->bishop_parity[BLACK] ==
                  mb_info.parity_index[i].bishop_parity[BLACK]))) {
                found_parity = true;
                ind->index = mb_info.parity_index[i].index;
                if (Verbose > 4) {
                    MyPrintf("Found index " DEC_ZINDEX_FORMAT
                             " parity index %d\n",
                             ind->index, i);
                }
                break;
            }
        }

        // if not found, check whether a blocked/opposing pawn file is
        // applicable

        bool found_pawn_file = false;

        if (!found_parity) {
            if ((mb_info.pawn_file_type == OP_11_PAWNS ||
                 mb_info.pawn_file_type == BP_11_PAWNS) &&
                fcache->pawn_file_type == OP_11_PAWNS) {
                if (mb_info.index_op_11 != ALL_ONES) {
                    ind->index = mb_info.index_op_11;
                    if (Verbose > 4) {
                        MyPrintf("GetMBResult: Found index " DEC_ZINDEX_FORMAT
                                 " for 1 opposing pawn pair\n",
                                 ind->index);
                    }
                    found_pawn_file = true;
                }
            }

            if (!found_pawn_file && mb_info.pawn_file_type == BP_11_PAWNS &&
                fcache->pawn_file_type == BP_11_PAWNS) {
                if (mb_info.index_bp_11 != ALL_ONES) {
                    ind->index = mb_info.index_bp_11;
                    if (Verbose > 4) {
                        MyPrintf("GetMBResult: Found index " DEC_ZINDEX_FORMAT
                                 " for 1 blocking pawn pair\n",
                                 ind->index);
                    }
                    found_pawn_file = true;
                }
            }

            if (!found_pawn_file && mb_info.pawn_file_type == OP_21_PAWNS &&
                fcache->pawn_file_type == OP_21_PAWNS) {
                if (mb_info.index_op_21 != ALL_ONES) {
                    ind->index = mb_info.index_op_21;
                    if (Verbose > 4) {
                        MyPrintf("GetMBResult: Found index " DEC_ZINDEX_FORMAT
                                 " for 2 vs 1 pawn, 1 opposing pawn pair\n",
                                 ind->index);
                    }
                    found_pawn_file = true;
                }
            }

            if (!found_pawn_file && mb_info.pawn_file_type == OP_12_PAWNS &&
                fcache->pawn_file_type == OP_12_PAWNS) {
                if (mb_info.index_op_12 != ALL_ONES) {
                    ind->index = mb_info.index_op_12;
                    if (Verbose > 4) {
                        MyPrintf("GetMBResult: Found index " DEC_ZINDEX_FORMAT
                                 " for 1 vs 2 pawns, 1 opposing pawn pair\n",
                                 ind->index);
                    }
                    found_pawn_file = true;
                }
            }

            if (!found_pawn_file &&
                (mb_info.pawn_file_type == OP_22_PAWNS ||
                 mb_info.pawn_file_type == DP_22_PAWNS) &&
                fcache->pawn_file_type == OP_22_PAWNS) {
                if (mb_info.index_op_22 != ALL_ONES) {
                    ind->index = mb_info.index_op_22;
                    if (Verbose > 4) {
                        MyPrintf("GetMBResult: Found index " DEC_ZINDEX_FORMAT
                                 " for 2 pawn pairs, 1 opposing pawn pair\n",
                                 ind->index);
                    }
                    found_pawn_file = true;
                }
            }

            if (!found_pawn_file && mb_info.pawn_file_type == DP_22_PAWNS &&
                fcache->pawn_file_type == DP_22_PAWNS) {
                if (mb_info.index_dp_22 != ALL_ONES) {
                    ind->index = mb_info.index_dp_22;
                    if (Verbose > 4) {
                        MyPrintf("GetMBResult: Found index " DEC_ZINDEX_FORMAT
                                 " for 2 opposing pawn pairs\n",
                                 ind->index);
                    }
                    found_pawn_file = true;
                }
            }

            if (!found_pawn_file && mb_info.pawn_file_type == OP_31_PAWNS &&
                fcache->pawn_file_type == OP_31_PAWNS) {
                if (mb_info.index_op_31 != ALL_ONES) {
                    ind->index = mb_info.index_op_31;
                    if (Verbose > 4) {
                        MyPrintf("GetMBResult: Found index " DEC_ZINDEX_FORMAT
                                 " for 3 vs 1 pawn, 1 opposing pawn pair\n",
                                 ind->index);
                    }
                    found_pawn_file = true;
                }
            }

            if (!found_pawn_file && mb_info.pawn_file_type == OP_13_PAWNS &&
                fcache->pawn_file_type == OP_13_PAWNS) {
                if (mb_info.index_op_13 != ALL_ONES) {
                    ind->index = mb_info.index_op_13;
                    if (Verbose > 4) {
                        MyPrintf("GetMBResult: Found index " DEC_ZINDEX_FORMAT
                                 " for 1 vs 3 pawns, 1 opposing pawn pair\n",
                                 ind->index);
                    }
                    found_pawn_file = true;
                }
            }

            if (!found_pawn_file && mb_info.pawn_file_type == OP_41_PAWNS &&
                fcache->pawn_file_type == OP_41_PAWNS) {
                if (mb_info.index_op_41 != ALL_ONES) {
                    ind->index = mb_info.index_op_41;
                    if (Verbose > 4) {
                        MyPrintf("GetMBResult: Found index " DEC_ZINDEX_FORMAT
                                 " for 4 vs 1 pawn, 1 opposing pawn pair\n",
                                 ind->index);
                    }
                    found_pawn_file = true;
                }
            }

            if (!found_pawn_file && mb_info.pawn_file_type == OP_14_PAWNS &&
                fcache->pawn_file_type == OP_14_PAWNS) {
                if (mb_info.index_op_14 != ALL_ONES) {
                    ind->index = mb_info.index_op_14;
                    if (Verbose > 4) {
                        MyPrintf("GetMBResult: Found index " DEC_ZINDEX_FORMAT
                                 " for 1 vs 4 pawns, 1 opposing pawn pair\n",
                                 ind->index);
                    }
                    found_pawn_file = true;
                }
            }

            if (!found_pawn_file && mb_info.pawn_file_type == OP_32_PAWNS &&
                fcache->pawn_file_type == OP_32_PAWNS) {
                if (mb_info.index_op_32 != ALL_ONES) {
                    ind->index = mb_info.index_op_32;
                    if (Verbose > 4) {
                        MyPrintf("GetMBResult: Found index " DEC_ZINDEX_FORMAT
                                 " for 3 vs 2 pawn, 1 opposing pawn pair\n",
                                 ind->index);
                    }
                    found_pawn_file = true;
                }
            }

            if (!found_pawn_file && mb_info.pawn_file_type == OP_23_PAWNS &&
                fcache->pawn_file_type == OP_23_PAWNS) {
                if (mb_info.index_op_23 != ALL_ONES) {
                    ind->index = mb_info.index_op_23;
                    if (Verbose > 4) {
                        MyPrintf("GetMBResult: Found index " DEC_ZINDEX_FORMAT
                                 " for 2 vs 3 pawns, 1 opposing pawn pair\n",
                                 ind->index);
                    }
                    found_pawn_file = true;
                }
            }

            if (!found_pawn_file && mb_info.pawn_file_type == OP_33_PAWNS &&
                fcache->pawn_file_type == OP_33_PAWNS) {
                if (mb_info.index_op_33 != ALL_ONES) {
                    ind->index = mb_info.index_op_33;
                    if (Verbose > 4) {
                        MyPrintf("GetMBResult: Found index " DEC_ZINDEX_FORMAT
                                 " for 3 vs 3 pawn, 1 opposing pawn pair\n",
                                 ind->index);
                    }
                    found_pawn_file = true;
                }
            }

            if (!found_pawn_file && mb_info.pawn_file_type == OP_42_PAWNS &&
                fcache->pawn_file_type == OP_42_PAWNS) {
                if (mb_info.index_op_42 != ALL_ONES) {
                    ind->index = mb_info.index_op_42;
                    if (Verbose > 4) {
                        MyPrintf("GetMBResult: Found index " DEC_ZINDEX_FORMAT
                                 " for 4 vs 2 pawn, 1 opposing pawn pair\n",
                                 ind->index);
                    }
                    found_pawn_file = true;
                }
            }

            if (!found_pawn_file && mb_info.pawn_file_type == OP_24_PAWNS &&
                fcache->pawn_file_type == OP_24_PAWNS) {
                if (mb_info.index_op_24 != ALL_ONES) {
                    ind->index = mb_info.index_op_24;
                    if (Verbose > 4) {
                        MyPrintf("GetMBResult: Found index " DEC_ZINDEX_FORMAT
                                 " for 2 vs 4 pawn, 1 opposing pawn pair\n",
                                 ind->index);
                    }
                    found_pawn_file = true;
                }
            }
        }

        if (!found_pawn_file) {
            if (Verbose > 3) {
                MyPrintf("GetMBResult: Could not find pawn file in cache\n");
            }
            continue;
        } else {
            if (Verbose > 3) {
                MyPrintf("GetMBResult: Found pawn file in cache\n");
            }
        }

        file_index = np;
        // move file to front of queue so it is tried first next time
        if (n > 0) {
            for (int i = n; i > 0; i--) {
                cached_file_lru[i][side] = cached_file_lru[i - 1][side];
            }
        }
        cached_file_lru[0][side] = file_index;
        break;
    }

    // if file pointer is not cached, need to open new file

    if (Verbose > 3) {
        if (file_index == -1)
            MyPrintf("GetMBResult: file_index from scan: %d so need to open "
                     "new one\n",
                     file_index);
        else
            MyPrintf("GetMBResult: file_index from scan: %d so can use "
                     "existing one\n",
                     file_index);
    }

    if (file_index == -1) {
        cache_hit = false;
        char ending[64];
        GetEndingName(mb_info.piece_type_count, ending);
        if (Verbose > 3) {
            MyPrintf("GetMBResult: Ending Name when searching for file: %s\n",
                     ending);
        }

        file file_mb = NULL;
        int bishop_parity[2] = {NONE, NONE};
        for (int i = 0; i < mb_info.num_parities; i++) {
            file_mb = OpenMBFile(ending, mb_info.kk_index,
                                 mb_info.parity_index[i].bishop_parity,
                                 FREE_PAWNS, side, false);
            if (file_mb != NULL) {
                memcpy(bishop_parity, mb_info.parity_index[i].bishop_parity,
                       sizeof(bishop_parity));
                ind->index = mb_info.parity_index[i].index;
                break;
            }
        }

        // now try any blocked pawn files

        if (file_mb == NULL) {
            if (Verbose > 3)
                MyPrintf("GetMBResult: could not find file in bishop parity "
                         "search\n");
        }

        int pawn_file_type = FREE_PAWNS;

        if (file_mb == NULL) {
            if (Verbose > 3) {
                MyPrintf("GetMBResult: Searching file with pawn file type %s\n",
                         OpExtensionName[mb_info.pawn_file_type]);
            }
            if ((mb_info.pawn_file_type == OP_11_PAWNS ||
                 mb_info.pawn_file_type == BP_11_PAWNS) &&
                mb_info.index_op_11 != ALL_ONES) {
                file_mb = OpenMBFile(ending, mb_info.kk_index, bishop_parity,
                                     OP_11_PAWNS, side, false);
                ind->index = mb_info.index_op_11;
                pawn_file_type = OP_11_PAWNS;
            }
            if (file_mb == NULL && mb_info.pawn_file_type == BP_11_PAWNS &&
                mb_info.index_bp_11 != ALL_ONES) {
                file_mb = OpenMBFile(ending, mb_info.kk_index, bishop_parity,
                                     BP_11_PAWNS, side, false);
                ind->index = mb_info.index_bp_11;
                pawn_file_type = BP_11_PAWNS;
            }
            if (file_mb == NULL && mb_info.pawn_file_type == OP_21_PAWNS &&
                mb_info.index_op_21 != ALL_ONES) {
                file_mb = OpenMBFile(ending, mb_info.kk_index, bishop_parity,
                                     OP_21_PAWNS, side, false);
                ind->index = mb_info.index_op_21;
                pawn_file_type = OP_21_PAWNS;
            }
            if (file_mb == NULL && mb_info.pawn_file_type == OP_12_PAWNS &&
                mb_info.index_op_12 != ALL_ONES) {
                file_mb = OpenMBFile(ending, mb_info.kk_index, bishop_parity,
                                     OP_12_PAWNS, side, false);
                ind->index = mb_info.index_op_12;
                pawn_file_type = OP_12_PAWNS;
            }
            if (file_mb == NULL &&
                (mb_info.pawn_file_type == OP_22_PAWNS ||
                 mb_info.pawn_file_type == DP_22_PAWNS) &&
                mb_info.index_op_22 != ALL_ONES) {
                file_mb = OpenMBFile(ending, mb_info.kk_index, bishop_parity,
                                     OP_22_PAWNS, side, false);
                ind->index = mb_info.index_op_22;
                pawn_file_type = OP_22_PAWNS;
            }
            if (file_mb == NULL && mb_info.pawn_file_type == DP_22_PAWNS &&
                mb_info.index_dp_22 != ALL_ONES) {
                file_mb = OpenMBFile(ending, mb_info.kk_index, bishop_parity,
                                     DP_22_PAWNS, side, false);
                ind->index = mb_info.index_dp_22;
                pawn_file_type = DP_22_PAWNS;
            }
            if (file_mb == NULL && mb_info.pawn_file_type == OP_31_PAWNS &&
                mb_info.index_op_31 != ALL_ONES) {
                file_mb = OpenMBFile(ending, mb_info.kk_index, bishop_parity,
                                     OP_31_PAWNS, side, false);
                ind->index = mb_info.index_op_31;
                pawn_file_type = OP_31_PAWNS;
            }
            if (file_mb == NULL && mb_info.pawn_file_type == OP_13_PAWNS &&
                mb_info.index_op_13 != ALL_ONES) {
                file_mb = OpenMBFile(ending, mb_info.kk_index, bishop_parity,
                                     OP_13_PAWNS, side, false);
                ind->index = mb_info.index_op_13;
                pawn_file_type = OP_13_PAWNS;
            }
            if (file_mb == NULL && mb_info.pawn_file_type == OP_41_PAWNS &&
                mb_info.index_op_41 != ALL_ONES) {
                file_mb = OpenMBFile(ending, mb_info.kk_index, bishop_parity,
                                     OP_41_PAWNS, side, false);
                ind->index = mb_info.index_op_41;
                pawn_file_type = OP_41_PAWNS;
            }
            if (file_mb == NULL && mb_info.pawn_file_type == OP_14_PAWNS &&
                mb_info.index_op_14 != ALL_ONES) {
                file_mb = OpenMBFile(ending, mb_info.kk_index, bishop_parity,
                                     OP_14_PAWNS, side, false);
                ind->index = mb_info.index_op_14;
                pawn_file_type = OP_14_PAWNS;
            }
            if (file_mb == NULL && mb_info.pawn_file_type == OP_32_PAWNS &&
                mb_info.index_op_32 != ALL_ONES) {
                file_mb = OpenMBFile(ending, mb_info.kk_index, bishop_parity,
                                     OP_32_PAWNS, side, false);
                ind->index = mb_info.index_op_32;
                pawn_file_type = OP_32_PAWNS;
            }
            if (file_mb == NULL && mb_info.pawn_file_type == OP_23_PAWNS &&
                mb_info.index_op_23 != ALL_ONES) {
                file_mb = OpenMBFile(ending, mb_info.kk_index, bishop_parity,
                                     OP_23_PAWNS, side, false);
                ind->index = mb_info.index_op_23;
                pawn_file_type = OP_23_PAWNS;
            }
            if (file_mb == NULL && mb_info.pawn_file_type == OP_33_PAWNS &&
                mb_info.index_op_33 != ALL_ONES) {
                file_mb = OpenMBFile(ending, mb_info.kk_index, bishop_parity,
                                     OP_33_PAWNS, side, false);
                ind->index = mb_info.index_op_33;
                pawn_file_type = OP_33_PAWNS;
            }
            if (file_mb == NULL && mb_info.pawn_file_type == OP_42_PAWNS &&
                mb_info.index_op_42 != ALL_ONES) {
                file_mb = OpenMBFile(ending, mb_info.kk_index, bishop_parity,
                                     OP_42_PAWNS, side, false);
                ind->index = mb_info.index_op_42;
                pawn_file_type = OP_42_PAWNS;
            }
            if (file_mb == NULL && mb_info.pawn_file_type == OP_24_PAWNS &&
                mb_info.index_op_24 != ALL_ONES) {
                file_mb = OpenMBFile(ending, mb_info.kk_index, bishop_parity,
                                     OP_24_PAWNS, side, false);
                ind->index = mb_info.index_op_24;
                pawn_file_type = OP_24_PAWNS;
            }
            if (file_mb == NULL) {
                if (Verbose > 3) {
                    MyPrintf("GetMBResult: Did not find file for %s, returning "
                             "file missing\n",
                             OpExtensionName[mb_info.pawn_file_type]);
                }
                if (!MBFormatOnly) {
                    INDEX_DATA ind_yk;
                    return GetYKResult(Board, &ind_yk);
                }
                return MB_FILE_MISSING;
            } else {
                if (Verbose > 3)
                    MyPrintf("GetMBResult: Found file for %s\n",
                             OpExtensionName[mb_info.pawn_file_type]);
            }
        }

        if (num_cached_files[side] < MAX_FILES) {
            file_index = num_cached_files[side];
            num_cached_files[side]++;
        } else
            file_index = cached_file_lru[MAX_FILES - 1][side];

        fcache = &FileCache[file_index][side];

        if (fcache->fp != NULL) {
            f_close(fcache->fp);
        }

        fcache->fp = file_mb;
        f_read(&fcache->header, sizeof(HEADER), fcache->fp, 0);

        if (fcache->header.num_blocks > fcache->max_num_blocks) {
            if (fcache->max_num_blocks > 0) {
                MyFree(fcache->offsets,
                       (fcache->max_num_blocks + 1) * sizeof(INDEX));
            }
            fcache->max_num_blocks = fcache->header.num_blocks;
            fcache->offsets =
                (INDEX *)MyMalloc((fcache->max_num_blocks + 1) * sizeof(INDEX));
        }

        f_read(fcache->offsets, (fcache->header.num_blocks + 1) * sizeof(INDEX),
               fcache->fp, sizeof(HEADER));

        fcache->kk_index = mb_info.kk_index;
        memcpy(fcache->piece_type_count, mb_info.piece_type_count,
               sizeof(mb_info.piece_type_count));
        memcpy(fcache->bishop_parity, bishop_parity, sizeof(bishop_parity));
        fcache->pawn_file_type = pawn_file_type;
        fcache->block_index = -1;

        // move file index to front of queue
        if (num_cached_files[side] > 1) {
            for (int i = num_cached_files[side] - 1; i > 0; i--) {
                cached_file_lru[i][side] = cached_file_lru[i - 1][side];
            }
        }
        cached_file_lru[0][side] = file_index;
    }

    if (Verbose > 3) {
        MyPrintf(
            "GetMBResult: Searching result for %s, index " DEC_ZINDEX_FORMAT
            " block index %lu, header block size %lu\n",
            fcache->header.basename, ind->index, fcache->block_index,
            fcache->header.block_size);
    }

    int b_index = ind->index / fcache->header.block_size;

    if (b_index != fcache->block_index) {
        cache_hit = false;
        ULONG length = fcache->offsets[b_index + 1] - fcache->offsets[b_index];
        if (Verbose > 4) {
            MyPrintf("GetMBResult: Reading compressed block size %lu at "
                     "offset " DEC_INDEX_FORMAT " block index %lu\n",
                     length, fcache->offsets[b_index], b_index);
        }
        if (length > CompressionBufferSize) {
            if (CompressionBuffer != NULL) {
                MyFree(CompressionBuffer, CompressionBufferSize);
            }
            CompressionBufferSize = length;
            CompressionBuffer = (BYTE *)MyMalloc(CompressionBufferSize);
        }
        f_read(CompressionBuffer, length, fcache->fp, fcache->offsets[b_index]);
        ULONG tmp_zone_size = fcache->header.block_size;
        if (tmp_zone_size > fcache->max_block_size) {
            if (fcache->block != NULL) {
                MyFree(fcache->block, fcache->max_block_size);
            }
            fcache->max_block_size = tmp_zone_size;
            fcache->block = (BYTE *)MyMalloc(tmp_zone_size);
        }
        MyUncompress(fcache->block, &tmp_zone_size, CompressionBuffer, length,
                     fcache->header.compression_method);
        assert(tmp_zone_size == fcache->header.block_size);
        fcache->block_index = b_index;
    }

    result = fcache->block[ind->index % fcache->header.block_size];

    if (Verbose > 3) {
        MyPrintf("GetMBResult: Result from database: %d\n", result);
    }

    // check for results with > 254 moves if possible

    if (result == 254 && fcache->header.max_depth > 254) {
        file_index = -1;
        FILE_CACHE_HIGH_DTZ *fcache_high_dtz = NULL;
        ULONG n_per_block = 0;

        for (int n = 0; n < num_cached_files_high_dtz[side]; n++) {
            int np = cached_file_high_dtz_lru[n][side];
            fcache_high_dtz = &FileCacheHighDTZ[np][side];
            if (fcache_high_dtz->kk_index != mb_info.kk_index)
                continue;
            if (memcmp(fcache_high_dtz->piece_type_count,
                       mb_info.piece_type_count,
                       sizeof(fcache_high_dtz->piece_type_count)))
                continue;
            bool found_parity = false;
            for (int i = 0; i < mb_info.num_parities; i++) {
                if (fcache_high_dtz->pawn_file_type != FREE_PAWNS)
                    continue;
                if ((fcache_high_dtz->bishop_parity[WHITE] == NONE ||
                     (fcache_high_dtz->bishop_parity[WHITE] ==
                      mb_info.parity_index[i].bishop_parity[WHITE])) &&
                    (fcache_high_dtz->bishop_parity[BLACK] == NONE ||
                     (fcache_high_dtz->bishop_parity[BLACK] ==
                      mb_info.parity_index[i].bishop_parity[BLACK]))) {
                    found_parity = true;
                    ind->index = mb_info.parity_index[i].index;
                    break;
                }
            }

            // if not found, check whether a blocked pawn file is applicable

            int pawn_file_type = FREE_PAWNS;

            if (!found_parity) {
                bool found_pawn_file = false;
                if ((mb_info.pawn_file_type == OP_11_PAWNS ||
                     mb_info.pawn_file_type == BP_11_PAWNS) &&
                    fcache_high_dtz->pawn_file_type == OP_11_PAWNS) {
                    if (mb_info.index_op_11 != ALL_ONES) {
                        ind->index = mb_info.index_op_11;
                        pawn_file_type = OP_11_PAWNS;
                        found_pawn_file = true;
                    }
                }

                if (!found_pawn_file && mb_info.pawn_file_type == BP_11_PAWNS &&
                    fcache_high_dtz->pawn_file_type == BP_11_PAWNS) {
                    if (mb_info.index_bp_11 != ALL_ONES) {
                        ind->index = mb_info.index_bp_11;
                        pawn_file_type = BP_11_PAWNS;
                        found_pawn_file = true;
                    }
                }

                if (!found_pawn_file && mb_info.pawn_file_type == OP_21_PAWNS &&
                    fcache_high_dtz->pawn_file_type == OP_21_PAWNS) {
                    if (mb_info.index_op_21 != ALL_ONES) {
                        ind->index = mb_info.index_op_21;
                        pawn_file_type = OP_21_PAWNS;
                        found_pawn_file = true;
                    }
                }

                if (!found_pawn_file && mb_info.pawn_file_type == OP_12_PAWNS &&
                    fcache_high_dtz->pawn_file_type == OP_12_PAWNS) {
                    if (mb_info.index_op_12 != ALL_ONES) {
                        ind->index = mb_info.index_op_12;
                        pawn_file_type = OP_12_PAWNS;
                        found_pawn_file = true;
                    }
                }

                if (!found_pawn_file &&
                    (mb_info.pawn_file_type == OP_22_PAWNS ||
                     mb_info.pawn_file_type == DP_22_PAWNS) &&
                    fcache_high_dtz->pawn_file_type == OP_22_PAWNS) {
                    if (mb_info.index_op_22 != ALL_ONES) {
                        ind->index = mb_info.index_op_22;
                        pawn_file_type = OP_22_PAWNS;
                        found_pawn_file = true;
                    }
                }

                if (!found_pawn_file && mb_info.pawn_file_type == DP_22_PAWNS &&
                    fcache_high_dtz->pawn_file_type == DP_22_PAWNS) {
                    if (mb_info.index_dp_22 != ALL_ONES) {
                        ind->index = mb_info.index_dp_22;
                        pawn_file_type = DP_22_PAWNS;
                        found_pawn_file = true;
                    }
                }

                if (!found_pawn_file && mb_info.pawn_file_type == OP_31_PAWNS &&
                    fcache_high_dtz->pawn_file_type == OP_31_PAWNS) {
                    if (mb_info.index_op_31 != ALL_ONES) {
                        ind->index = mb_info.index_op_31;
                        pawn_file_type = OP_31_PAWNS;
                        found_pawn_file = true;
                    }
                }

                if (!found_pawn_file && mb_info.pawn_file_type == OP_13_PAWNS &&
                    fcache_high_dtz->pawn_file_type == OP_13_PAWNS) {
                    if (mb_info.index_op_13 != ALL_ONES) {
                        ind->index = mb_info.index_op_13;
                        pawn_file_type = OP_13_PAWNS;
                        found_pawn_file = true;
                    }
                }

                if (!found_pawn_file && mb_info.pawn_file_type == OP_41_PAWNS &&
                    fcache_high_dtz->pawn_file_type == OP_41_PAWNS) {
                    if (mb_info.index_op_41 != ALL_ONES) {
                        ind->index = mb_info.index_op_41;
                        pawn_file_type = OP_41_PAWNS;
                        found_pawn_file = true;
                    }
                }

                if (!found_pawn_file && mb_info.pawn_file_type == OP_14_PAWNS &&
                    fcache_high_dtz->pawn_file_type == OP_14_PAWNS) {
                    if (mb_info.index_op_14 != ALL_ONES) {
                        ind->index = mb_info.index_op_14;
                        pawn_file_type = OP_14_PAWNS;
                        found_pawn_file = true;
                    }
                }

                if (!found_pawn_file && mb_info.pawn_file_type == OP_32_PAWNS &&
                    fcache_high_dtz->pawn_file_type == OP_32_PAWNS) {
                    if (mb_info.index_op_32 != ALL_ONES) {
                        ind->index = mb_info.index_op_32;
                        pawn_file_type = OP_32_PAWNS;
                        found_pawn_file = true;
                    }
                }

                if (!found_pawn_file && mb_info.pawn_file_type == OP_23_PAWNS &&
                    fcache_high_dtz->pawn_file_type == OP_23_PAWNS) {
                    if (mb_info.index_op_23 != ALL_ONES) {
                        ind->index = mb_info.index_op_23;
                        pawn_file_type = OP_23_PAWNS;
                        found_pawn_file = true;
                    }
                }

                if (!found_pawn_file && mb_info.pawn_file_type == OP_33_PAWNS &&
                    fcache_high_dtz->pawn_file_type == OP_33_PAWNS) {
                    if (mb_info.index_op_33 != ALL_ONES) {
                        ind->index = mb_info.index_op_33;
                        pawn_file_type = OP_33_PAWNS;
                        found_pawn_file = true;
                    }
                }

                if (!found_pawn_file && mb_info.pawn_file_type == OP_42_PAWNS &&
                    fcache_high_dtz->pawn_file_type == OP_42_PAWNS) {
                    if (mb_info.index_op_42 != ALL_ONES) {
                        ind->index = mb_info.index_op_42;
                        pawn_file_type = OP_42_PAWNS;
                        found_pawn_file = true;
                    }
                }

                if (!found_pawn_file && mb_info.pawn_file_type == OP_24_PAWNS &&
                    fcache_high_dtz->pawn_file_type == OP_24_PAWNS) {
                    if (mb_info.index_op_24 != ALL_ONES) {
                        ind->index = mb_info.index_op_24;
                        pawn_file_type = OP_24_PAWNS;
                        found_pawn_file = true;
                    }
                }

                if (!found_pawn_file)
                    continue;
            }

            file_index = np;
            // move file to front of queue so it is tried first next time
            if (n > 0) {
                for (int i = n; i > 0; i--) {
                    cached_file_high_dtz_lru[i][side] =
                        cached_file_high_dtz_lru[i - 1][side];
                }
            }
            cached_file_high_dtz_lru[0][side] = file_index;
            break;
        }

        if (Verbose > 3) {
            MyPrintf("GetMBResult: file_index for high dtz from scan: %d\n",
                     file_index);
        }

        // if file pointer is not cached, need to open new file

        if (file_index == -1) {
            cache_hit = false;
            char ending[64];
            GetEndingName(mb_info.piece_type_count, ending);

            file file_mb = NULL;
            int bishop_parity[2] = {NONE, NONE};
            for (int i = 0; i < mb_info.num_parities; i++) {
                file_mb = OpenMBFile(ending, mb_info.kk_index,
                                     mb_info.parity_index[i].bishop_parity, 0,
                                     side, true);
                if (file_mb != NULL) {
                    memcpy(bishop_parity, mb_info.parity_index[i].bishop_parity,
                           sizeof(bishop_parity));
                    ind->index = mb_info.parity_index[i].index;
                    break;
                }
            }

            // now try any blocked pawn files

            int pawn_file_type = FREE_PAWNS;

            if (file_mb == NULL) {
                if ((mb_info.pawn_file_type == OP_11_PAWNS ||
                     mb_info.pawn_file_type == BP_11_PAWNS) &&
                    mb_info.index_op_11 != ALL_ONES) {
                    file_mb =
                        OpenMBFile(ending, mb_info.kk_index, bishop_parity,
                                   OP_11_PAWNS, side, true);
                    pawn_file_type = OP_11_PAWNS;
                }
                if (file_mb == NULL && mb_info.pawn_file_type == BP_11_PAWNS &&
                    mb_info.index_bp_11 != ALL_ONES) {
                    file_mb =
                        OpenMBFile(ending, mb_info.kk_index, bishop_parity,
                                   BP_11_PAWNS, side, true);
                    pawn_file_type = BP_11_PAWNS;
                }
                if (file_mb == NULL && mb_info.pawn_file_type == OP_21_PAWNS &&
                    mb_info.index_op_21 != ALL_ONES) {
                    file_mb =
                        OpenMBFile(ending, mb_info.kk_index, bishop_parity,
                                   OP_21_PAWNS, side, true);
                    pawn_file_type = OP_21_PAWNS;
                }
                if (file_mb == NULL && mb_info.pawn_file_type == OP_12_PAWNS &&
                    mb_info.index_op_12 != ALL_ONES) {
                    file_mb =
                        OpenMBFile(ending, mb_info.kk_index, bishop_parity,
                                   OP_12_PAWNS, side, true);
                    pawn_file_type = OP_12_PAWNS;
                }
                if (file_mb == NULL &&
                    (mb_info.pawn_file_type == OP_22_PAWNS ||
                     mb_info.pawn_file_type == DP_22_PAWNS) &&
                    mb_info.index_op_22 != ALL_ONES) {
                    file_mb =
                        OpenMBFile(ending, mb_info.kk_index, bishop_parity,
                                   OP_22_PAWNS, side, true);
                    pawn_file_type = OP_22_PAWNS;
                }
                if (file_mb == NULL && mb_info.pawn_file_type == DP_22_PAWNS &&
                    mb_info.index_dp_22 != ALL_ONES) {
                    file_mb =
                        OpenMBFile(ending, mb_info.kk_index, bishop_parity,
                                   DP_22_PAWNS, side, true);
                    pawn_file_type = DP_22_PAWNS;
                }
                if (file_mb == NULL && mb_info.pawn_file_type == OP_31_PAWNS &&
                    mb_info.index_op_31 != ALL_ONES) {
                    file_mb =
                        OpenMBFile(ending, mb_info.kk_index, bishop_parity,
                                   OP_31_PAWNS, side, true);
                    pawn_file_type = OP_31_PAWNS;
                }
                if (file_mb == NULL && mb_info.pawn_file_type == OP_13_PAWNS &&
                    mb_info.index_op_13 != ALL_ONES) {
                    file_mb =
                        OpenMBFile(ending, mb_info.kk_index, bishop_parity,
                                   OP_13_PAWNS, side, true);
                    pawn_file_type = OP_13_PAWNS;
                }
                if (file_mb == NULL && mb_info.pawn_file_type == OP_41_PAWNS &&
                    mb_info.index_op_41 != ALL_ONES) {
                    file_mb =
                        OpenMBFile(ending, mb_info.kk_index, bishop_parity,
                                   OP_41_PAWNS, side, true);
                    pawn_file_type = OP_41_PAWNS;
                }
                if (file_mb == NULL && mb_info.pawn_file_type == OP_14_PAWNS &&
                    mb_info.index_op_14 != ALL_ONES) {
                    file_mb =
                        OpenMBFile(ending, mb_info.kk_index, bishop_parity,
                                   OP_14_PAWNS, side, true);
                    pawn_file_type = OP_14_PAWNS;
                }
                if (file_mb == NULL && mb_info.pawn_file_type == OP_32_PAWNS &&
                    mb_info.index_op_32 != ALL_ONES) {
                    file_mb =
                        OpenMBFile(ending, mb_info.kk_index, bishop_parity,
                                   OP_32_PAWNS, side, true);
                    pawn_file_type = OP_32_PAWNS;
                }
                if (file_mb == NULL && mb_info.pawn_file_type == OP_23_PAWNS &&
                    mb_info.index_op_23 != ALL_ONES) {
                    file_mb =
                        OpenMBFile(ending, mb_info.kk_index, bishop_parity,
                                   OP_23_PAWNS, side, true);
                    pawn_file_type = OP_23_PAWNS;
                }
                if (file_mb == NULL && mb_info.pawn_file_type == OP_33_PAWNS &&
                    mb_info.index_op_33 != ALL_ONES) {
                    file_mb =
                        OpenMBFile(ending, mb_info.kk_index, bishop_parity,
                                   OP_33_PAWNS, side, true);
                    pawn_file_type = OP_33_PAWNS;
                }
                if (file_mb == NULL && mb_info.pawn_file_type == OP_42_PAWNS &&
                    mb_info.index_op_42 != ALL_ONES) {
                    file_mb =
                        OpenMBFile(ending, mb_info.kk_index, bishop_parity,
                                   OP_42_PAWNS, side, true);
                    pawn_file_type = OP_42_PAWNS;
                }
                if (file_mb == NULL && mb_info.pawn_file_type == OP_24_PAWNS &&
                    mb_info.index_op_24 != ALL_ONES) {
                    file_mb =
                        OpenMBFile(ending, mb_info.kk_index, bishop_parity,
                                   OP_24_PAWNS, side, true);
                    pawn_file_type = OP_24_PAWNS;
                }
                if (file_mb == NULL)
                    return HIGH_DTZ_MISSING;
            }

            if (num_cached_files_high_dtz[side] < MAX_FILES_HIGH_DTZ) {
                file_index = num_cached_files_high_dtz[side];
                num_cached_files_high_dtz[side]++;
            } else
                file_index =
                    cached_file_high_dtz_lru[MAX_FILES_HIGH_DTZ - 1][side];

            fcache_high_dtz = &FileCacheHighDTZ[file_index][side];

            if (fcache_high_dtz->fp != NULL) {
                f_close(fcache_high_dtz->fp);
            }

            fcache_high_dtz->fp = file_mb;
            f_read(&fcache_high_dtz->header, sizeof(HEADER),
                   fcache_high_dtz->fp, 0);

            if (fcache_high_dtz->header.list_element_size != sizeof(HIGH_DTZ)) {
                if (Verbose > 1) {
                    MyPrintf("GetMBResult: High DTZ size for %s, side=%s, "
                             "kk_index=%d not consistent; not read\n",
                             ending, ColorName(side), mb_info.kk_index);
                }
                return HIGH_DTZ_MISSING;
            }

            if (fcache_high_dtz->header.num_blocks >
                fcache_high_dtz->max_num_blocks) {
                if (fcache_high_dtz->max_num_blocks > 0) {
                    MyFree(fcache_high_dtz->offsets,
                           (fcache_high_dtz->max_num_blocks + 1) *
                               sizeof(INDEX));
                    MyFree(fcache_high_dtz->starting_index,
                           (fcache_high_dtz->max_num_blocks + 1) *
                               sizeof(ZINDEX));
                }
                fcache_high_dtz->max_num_blocks =
                    fcache_high_dtz->header.num_blocks;
                fcache_high_dtz->offsets = (INDEX *)MyMalloc(
                    (fcache_high_dtz->max_num_blocks + 1) * sizeof(INDEX));
                fcache_high_dtz->starting_index = (ZINDEX *)MyMalloc(
                    (fcache_high_dtz->max_num_blocks + 1) * sizeof(ZINDEX));
            }

            INDEX l_offset = sizeof(HEADER);
            f_read(fcache_high_dtz->offsets,
                   (fcache_high_dtz->header.num_blocks + 1) * sizeof(INDEX),
                   fcache_high_dtz->fp, l_offset);
            l_offset +=
                (fcache_high_dtz->header.num_blocks + 1) * sizeof(INDEX);
            f_read(fcache_high_dtz->starting_index,
                   (fcache_high_dtz->header.num_blocks + 1) * sizeof(ZINDEX),
                   fcache_high_dtz->fp, l_offset);
            ULONG block_size = fcache_high_dtz->header.block_size;
            if (block_size > fcache_high_dtz->max_block_size) {
                if (fcache_high_dtz->max_block_size > 0) {
                    MyFree(fcache_high_dtz->block,
                           fcache_high_dtz->max_block_size);
                }
                fcache_high_dtz->max_block_size = block_size;
                fcache_high_dtz->block = (BYTE *)MyMalloc(block_size);
            }
            fcache_high_dtz->block_index = -1;
            fcache_high_dtz->kk_index = mb_info.kk_index;
            memcpy(fcache_high_dtz->piece_type_count, mb_info.piece_type_count,
                   sizeof(mb_info.piece_type_count));
            memcpy(fcache_high_dtz->bishop_parity, bishop_parity,
                   sizeof(bishop_parity));
            fcache_high_dtz->pawn_file_type = pawn_file_type;

            if (num_cached_files_high_dtz[side] > 1) {
                for (int i = num_cached_files_high_dtz[side] - 1; i > 0; i--) {
                    cached_file_high_dtz_lru[i][side] =
                        cached_file_high_dtz_lru[i - 1][side];
                }
            }
            cached_file_high_dtz_lru[0][side] = file_index;
        }

        // if index is outside range of all indices with depth > 254, depth is
        // 254
        if (ind->index < fcache_high_dtz->starting_index[0] ||
            ind->index >
                fcache_high_dtz
                    ->starting_index[fcache_high_dtz->header.num_blocks]) {
            if (cache_hit)
                CacheHits++;
            else
                DBHits++;
            return 254;
        }

        n_per_block = fcache_high_dtz->header.block_size /
                      fcache_high_dtz->header.list_element_size;

        // check whether index is in range of block that may already be cached
        if (fcache_high_dtz->block_index != -1) {
            HIGH_DTZ *hptr = (HIGH_DTZ *)fcache_high_dtz->block;
            ULONG n_per_block_cached = n_per_block;
            if (fcache_high_dtz->block_index ==
                fcache_high_dtz->header.num_blocks - 1) {
                ULONG rem = fcache_high_dtz->header.n_elements % n_per_block;
                if (rem != 0)
                    n_per_block_cached = rem;
            }
            if (ind->index < hptr[0].index ||
                ind->index > hptr[n_per_block_cached - 1].index)
                fcache_high_dtz->block_index = -1;
        }

        // if block is not cached, need to read it from file
        if (fcache_high_dtz->block_index == -1) {
            cache_hit = false;
            // do binary search to find which block of indices the depth would
            // be
            int l = 0, r = fcache_high_dtz->header.num_blocks;
            while (l < r) {
                int m = (l + r) / 2;
                if (fcache_high_dtz->starting_index[m] < ind->index)
                    l = m + 1;
                else
                    r = m;
            }
            if ((l == fcache_high_dtz->header.num_blocks) ||
                (fcache_high_dtz->starting_index[l] > ind->index))
                l--;
            // now read block from file
            fcache_high_dtz->block_index = l;
            ULONG length =
                fcache_high_dtz->offsets[fcache_high_dtz->block_index + 1] -
                fcache_high_dtz->offsets[fcache_high_dtz->block_index];
            if (length > CompressionBufferSize) {
                if (CompressionBuffer != NULL) {
                    MyFree(CompressionBuffer, CompressionBufferSize);
                }
                CompressionBufferSize = length;
                CompressionBuffer = (BYTE *)MyMalloc(CompressionBufferSize);
            }
            f_read(CompressionBuffer, length, fcache_high_dtz->fp,
                   fcache_high_dtz->offsets[fcache_high_dtz->block_index]);
            ULONG n_per_block_cached = n_per_block;
            if (fcache_high_dtz->block_index ==
                fcache_high_dtz->header.num_blocks - 1) {
                ULONG rem = fcache_high_dtz->header.n_elements % n_per_block;
                if (rem != 0)
                    n_per_block_cached = rem;
            }
            ULONG tmp_zone_size = n_per_block_cached * sizeof(HIGH_DTZ);
            MyUncompress((BYTE *)fcache_high_dtz->block, &tmp_zone_size,
                         CompressionBuffer, length,
                         fcache_high_dtz->header.compression_method);
            assert(tmp_zone_size == n_per_block_cached * sizeof(HIGH_DTZ));
        }

        // now perform binary search on block that may contain score
        // corresponding to index
        HIGH_DTZ key;
        key.index = ind->index;

        ULONG n_per_block_cached = n_per_block;
        if (fcache_high_dtz->block_index ==
            fcache_high_dtz->header.num_blocks - 1) {
            ULONG rem = fcache_high_dtz->header.n_elements % n_per_block;
            if (rem != 0)
                n_per_block_cached = rem;
        }

        HIGH_DTZ *kptr = (HIGH_DTZ *)bsearch(&key, fcache_high_dtz->block,
                                             n_per_block_cached,
                                             sizeof(HIGH_DTZ), high_dtz_compar);

        if (kptr == NULL)
            result = 254;
        else
            result = kptr->score;
    } else {
        if (result == 255)
            result = UNRESOLVED;
    }

    if (cache_hit)
        CacheHits++;
    else
        DBHits++;

    if (!MBFormatOnly && result == UNKNOWN) {
        INDEX_DATA ind_yk;
        result = GetYKResult(Board, &ind_yk);
    }

    if (Verbose > 3) {
        MyPrintf("GetMBResult: Returning with final result %d\n", result);
    }

    return result;
}

/*
 * ScorePosition returns either the following constants:
 *
 * UNKNOWN, LOST, DRAW, NOT_LOST, NOT_WON, WON, high_dtz
 * or a positive integer n denoting "won in n", or a negative integer
 * denoting "lost in n".  high_dtz means the position is won (or
 * lost) in 254 or more moves, but the exact number could not be
 * obtained (usually due to a missing database.)
 *
 * The input position is assumed to be legal.
 */

static int ScorePosition(BOARD *BoardIn, INDEX_DATA *index) {
    if (Verbose > 4) {
        MyPrintf("Entering ScorePosition\n");
    }

    if (BoardIn->num_pieces == 2)
        return DRAW;

    if (BoardIn->num_pieces == 3) {
        if (BoardIn->piece_type_count[WHITE][BISHOP] ||
            BoardIn->piece_type_count[WHITE][KNIGHT] ||
            BoardIn->piece_type_count[BLACK][BISHOP] ||
            BoardIn->piece_type_count[BLACK][KNIGHT]) {
            return DRAW;
        }
    }

    if (BoardIn->num_pieces > MAX_PIECES_MB ||
        (BoardIn->castle && !IgnoreCastle))
        return UNKNOWN;

    BOARD Board;

    memcpy(&Board, BoardIn, sizeof(Board));

    // make the stronger side white to reduce chance of having
    // to probe "flipped" position

    bool flipped = false;

    if (Board.strength_w < Board.strength_b) {
        FlipBoard(&Board);
        flipped = true;
    }

    if (Verbose > 4) {
        DisplayBoard(BoardIn, "ScorePosition: Board before flip");
        DisplayBoard(&Board, "ScorePosition: Board after flip");
    }

    int result = GetMBResult(&Board, index);

    if (Verbose > 3) {
        char label[64];
        sprintf(label, "ScorePosition: MB result: %d", result);
        DisplayBoard(&Board, label);
    }

    // if we have definite result, can return right away

    if (!(result < 0 || result == UNRESOLVED)) {
        if ((Board.side == WHITE) || result == LOST || result == WON ||
            result == HIGH_DTZ_MISSING) {
            if (Verbose > 3) {
                MyPrintf("ScorePosition: Returning %d\n", result);
            }
            return result;
        }
        if (Verbose > 3) {
            MyPrintf("ScorePosition: Returning %d\n", -result);
        }
        return -result;
    }

    // if one side has no pieces, there is no flipped database,
    // so UNRESOLVED becomes draw

    if (Board.nblack == 0) {
        if (result < 0)
            return UNKNOWN;
        else if (result == UNRESOLVED) {
            return DRAW;
        } else {
            if (Verbose > 1) {
                char label[64];
                sprintf(label, "Unexpected result %d", result);
                DisplayBoard(&Board, label);
            }
        }
    }

    FlipBoard(&Board);

    INDEX_DATA index2;

    int result_flipped = GetMBResult(&Board, &index2);

    if (Verbose > 3) {
        char label[64];
        sprintf(label, "ScorePosition: result=%d result_flipped=%d", result,
                result_flipped);
        DisplayBoard(&Board, label);
    }

    if (result_flipped == WON || result_flipped == LOST ||
        result_flipped == HIGH_DTZ_MISSING)
        return result_flipped;
    else if (result_flipped >= 0 && result_flipped != UNRESOLVED) {
        if (Board.side == WHITE)
            return result_flipped;
        else
            return -result_flipped;
    } else if (result_flipped < 0 && result < 0)
        return UNKNOWN;
    else if (result_flipped == UNRESOLVED && result == UNRESOLVED) {
        return DRAW;
    } else if (result == UNRESOLVED && result_flipped < 0) {
        if (Board.side == WHITE)
            return NOT_LOST;
        else
            return NOT_WON;
    } else if (result < 0 && result_flipped == UNRESOLVED) {
        if (Board.side == WHITE)
            return NOT_WON;
        else
            return NOT_LOST;
    } else {
        if (Verbose > 1) {
            char label[64];
            sprintf(
                label,
                "ScorePosition: Unexpected result=%d, flipped=%d combination",
                result, result_flipped);
            DisplayBoard(&Board, label);
        }
    }

    return UNKNOWN;
}

static int GenMoveScores(BOARD *Board, Move *move_list,
                         bool skip_pawn_promotion, bool try_all_promos) {
    MB_INFO mb_info;
    YK_INFO yk_info;
    int num_unresolved = 0;
    bool try_list[MAX_MOVES];
    INDEX_DATA idata;

    int nmoves =
        GenLegalMoves(Board, move_list, skip_pawn_promotion, try_all_promos);

    int xside = OtherSide(Board->side);

    for (int i = 0; i < nmoves; i++) {
        try_list[i] = true;
        MakeMove(Board, &move_list[i]);

        // first check whether it is a possible EGTB ending

        int valid_info = 0;

        if (!YKFormatOnly) {
            valid_info = GetMBInfo(Board, &mb_info);
        } else {
            valid_info = GetYKInfo(Board, &yk_info);
        }

        if (valid_info != 0) {
            try_list[i] = false;
            move_list[i].score = UNKNOWN;
            UnMakeMove(Board, &move_list[i]);
            continue;
        }

        move_list[i].metric = DTC;

        if (!YKFormatOnly)
            move_list[i].score = GetMBCacheResult(&mb_info, xside);
        else
            move_list[i].score = GetYKCacheResult(&yk_info, xside);

        if (!MBFormatOnly && move_list[i].score == UNKNOWN) {
            if (GetYKInfo(Board, &yk_info) == 0) {
                move_list[i].score = GetYKCacheResult(&yk_info, xside);
            }
        }

        if (xside == BLACK && move_list[i].score != UNKNOWN &&
            move_list[i].score != UNRESOLVED && move_list[i].score > 0 &&
            move_list[i].score != HIGH_DTZ_MISSING)
            move_list[i].score = -move_list[i].score;
        if (move_list[i].score != UNKNOWN && move_list[i].score != UNRESOLVED &&
            move_list[i].score != HIGH_DTZ_MISSING)
            try_list[i] = false;
        else
            num_unresolved++;
        UnMakeMove(Board, &move_list[i]);
    }

    while (num_unresolved > 0) {
        bool run_full = true;
        for (int i = 0; i < nmoves; i++) {
            if (try_list[i]) {
                MakeMove(Board, &move_list[i]);
                if (run_full) {
                    move_list[i].score = ScorePosition(Board, &idata);
                    move_list[i].metric = DTC;
                    run_full = false;
                    try_list[i] = false;
                    num_unresolved--;
                } else {
                    if (!YKFormatOnly) {
                        GetMBInfo(Board, &mb_info);
                        move_list[i].score = GetMBCacheResult(&mb_info, xside);
                    } else {
                        GetYKInfo(Board, &yk_info);
                        move_list[i].score = GetYKCacheResult(&yk_info, xside);
                    }

                    if (!MBFormatOnly && move_list[i].score == UNKNOWN) {
                        if (GetYKInfo(Board, &yk_info) == 0) {
                            move_list[i].score =
                                GetYKCacheResult(&yk_info, xside);
                        }
                    }
                    if (xside == BLACK && move_list[i].score != UNKNOWN &&
                        move_list[i].score != UNRESOLVED &&
                        move_list[i].score > 0 &&
                        move_list[i].score != HIGH_DTZ_MISSING)
                        move_list[i].score = -move_list[i].score;
                    if (move_list[i].score != UNKNOWN &&
                        move_list[i].score != UNRESOLVED &&
                        move_list[i].score != HIGH_DTZ_MISSING) {
                        try_list[i] = false;
                        num_unresolved--;
                    }
                }
                UnMakeMove(Board, &move_list[i]);
            }
        }
    }

    return nmoves;
}

static void PrintPGNHeader(BOARD *Board, int score, char *result) {
    int nbishops, i, j, wb[2], bb[2];
    char fen_string[256], score_str[32];
    struct tm *loctime;

    time_t curtime = time(NULL);

    loctime = localtime(&curtime);

    memset(wb, 0, sizeof(wb));
    memset(bb, 0, sizeof(bb));

    for (i = 0; i < NSQUARES; i++) {
        int row = Row(i);
        int col = Column(i);
        int parity = 1;
        if (IsWhiteSquare[i])
            parity = 0;
        if (Board->board[i] == BISHOP)
            wb[parity]++;
        if (Board->board[i] == -BISHOP)
            bb[parity]++;
    }

    nbishops = wb[0] + wb[1] + bb[0] + bb[1];

#if !defined(ODD_EDGES)
    if (wb[0] < wb[1] || (wb[0] == wb[1] && bb[0] < bb[1])) {
        SWAP(wb[0], wb[1])
        SWAP(bb[0], bb[1])
    }
#endif

    if (score == DRAW)
        sprintf(score_str, "=");
    else if (score == NOT_WON)
        sprintf(score_str, "-=");
    else if (score == NOT_LOST)
        sprintf(score_str, "=+");
    else if (score == WON)
        sprintf(score_str, "+");
    else if (score == LOST)
        sprintf(score_str, "-");
    else if (score == UNKNOWN)
        sprintf(score_str, "?");
    else if (score > 0)
        sprintf(score_str, "+%d", score);
    else if (score <= 0)
        sprintf(score_str, "-%d", -score);

    MyPrintf("[Event \"%c %s\"]\n", "WB"[Board->side], score_str);
    MyPrintf("[Date \"%02d.%02d.%02d\"]\n", 1900 + loctime->tm_year,
             1 + loctime->tm_mon, loctime->tm_mday);
    MyPrintf("[Site \"?\"]\n");
    MyPrintf("[Round \"?\"]\n");
    MyPrintf("[White \"");
    for (i = KING - 1; i >= PAWN; i--) {
        for (j = 0; j < Board->piece_type_count[WHITE][i]; j++)
            MyPrintf("%c", toupper(piece_char(i)));
    }
    if (nbishops >= MIN_BISHOPS_FOR_PARITY && wb[0] + wb[1] > 0) {
        MyPrintf("-%01d%01d", wb[0], wb[1]);
    }
    MyPrintf("\"]\n");
    MyPrintf("[Black \"");
    for (i = KING - 1; i >= PAWN; i--) {
        for (j = 0; j < Board->piece_type_count[BLACK][i]; j++)
            MyPrintf("%c", toupper(piece_char(i)));
    }
    if (nbishops >= MIN_BISHOPS_FOR_PARITY && bb[0] + bb[1] > 0) {
        MyPrintf("-%01d%01d", bb[0], bb[1]);
    }
    MyPrintf("\"]\n");
    if (result != NULL) {
        if (score == DRAW) {
            sprintf(result, "1/2-1/2");
        } else if (score == NOT_WON || score == NOT_LOST || score == UNKNOWN) {
            sprintf(result, "*");
        } else {
            if ((score > 0 && Board->side == WHITE) ||
                (score <= 0 && Board->side == BLACK)) {
                sprintf(result, "1-0");
            } else {
                sprintf(result, "0-1");
            }
        }
        MyPrintf("[Result \"%s\"]\n", result);
    } else {
        MyPrintf("[Result *]\n");
    }
    MyPrintf("[SetUp \"1\"]\n");
    BoardToFEN(Board, fen_string);
    MyPrintf("[FEN \"%s\"]\n\n", fen_string);
    MyFlush();
}

static void FindBestLine(BOARD *Board, bool mark_mzugs, bool mark_best,
                         bool mark_squeeze) {
    Move move_list[MAX_MOVES];
    int ply = 0, nmoves, good_moves, best_move, best_index, i, j, len;
    int score, side_in;
    bool first_move = true;
    INDEX_DATA index, orig_index;
    bool in_check, flip = false, mzug = false, repeated_pos = false,
                   capture_seen = false;
    bool phase_change_seen = false, metric = DTC;
    char fen_string[256], prev_move[16], move_string[16];
    char result[8];

    index.metric = metric;

    score = ScorePosition(Board, &index);

    metric = index.metric;

    if (score == DRAW) {
        PrintPGNHeader(Board, score, result);
        MyPrintf("1/2-1/2\n\n");
        return;
    }

    if (score == UNKNOWN || score == ILLEGAL || score == NOT_WON ||
        score == NOT_LOST) {
        PrintPGNHeader(Board, score, result);
        MyPrintf("*\n\n");
        if (Verbose > 1)
            PrintEPD(Board, " c0 \"Cannot create play\"");
        return;
    }

    side_in = Board->side;

    in_check = IsInCheck(Board, Board->side);

    orig_index.kk_index = -1;
    orig_index.index = ALL_ONES;

    if (!in_check) {
        memcpy(&orig_index, &index, sizeof(INDEX_DATA));
    }

    if (!in_check && mark_mzugs && (score == LOST || score <= 0)) {
        Board->side = OtherSide(Board->side);
        int xscore = ScorePosition(Board, &index);
        if (xscore == DRAW || xscore == NOT_WON || xscore == LOST ||
            xscore <= 0) {
            mzug = true;
        }
        Board->side = OtherSide(Board->side);
    }

    PrintPGNHeader(Board, score, result);

    InitPGNOutput();

    if (mzug) {
        OutputPGNString("{zz}");
        mzug = false;
    }

    prev_move[0] = 0;
    ply = 0;
    first_move = true;

    if ((Board->side == WHITE && (score <= 0 || score == LOST)) ||
        (Board->side == BLACK && (score > 0 || score == WON))) {
        if (Verbose > 1)
            MyPrintf("Flipped because side = %d, score = %d\n", Board->side,
                     score);
        flip = true;
    }

    capture_seen = false;
    phase_change_seen = false;

    if (flip) {
        FlipBoard(Board);
        if (!in_check) {
            score = ScorePosition(Board, &index);
            // side_in = Board->side;
            memcpy(&orig_index, &index, sizeof(INDEX_DATA));
        }
    }

    for (;;) {
        nmoves = GenMoveScores(Board, move_list, true, true);

        if (flip) {
            FlipBoard(Board);
            for (i = 0; i < nmoves; i++)
                FlipMove(&move_list[i]);
        }

    final_move:
        move_string[0] = 0;

        if (first_move) {
            sprintf(move_string, "%d%s", Board->full_move,
                    ((Board->side == WHITE) ? "." : "..."));
            first_move = false;
        }

        if (ply > 0) {
            if ((side_in == WHITE && !(ply & 1)) ||
                (side_in == BLACK && (ply & 1))) {
                sprintf(move_string, "%d.", Board->full_move);
            }
        }

        if (move_string[0]) {
            OutputPGNString(move_string);
        }

        if (prev_move[0] != 0) {
            OutputPGNString(prev_move);
            if (mzug) {
                OutputPGNString("{zz}");
            }
            if (mark_squeeze && repeated_pos) {
                OutputPGNString("{rr}");
            }
            prev_move[0] = 0;
            ply++;
        }

        if (nmoves == 0 || (StopAtTransition && phase_change_seen))
            break;

        if (Board->full_move > MaxLineLength)
            break;

        if (nmoves == 1) {
            best_index = 0;
            goto show_move;
        }

        if (score > 0 && score != LOST) {
            good_moves = 0;
            best_index = 0;
            best_move = -16384;
            int biggest_capture = 0;
            //	    fprintf(stderr,"MSB: nmoves=%d\n", nmoves);
            //	    DisplayBoard(Board, "MSB: Initial position");
            for (i = 0; i < nmoves; i++) {
                int xscore = move_list[i].score;
                //		fprintf(stderr,"MSB: move %d xscore %d\n", i,
                //xscore);
                if ((move_list[i].flag & PROMOTION) &&
                    abs(move_list[i].piece_promoted) == PAWN)
                    continue;
                Move reply_list[MAX_MOVES];
                int nreplies;
                MakeMove(Board, &move_list[i]);
                //		fprintf(stderr,"MSB: Board after move %d\n", i);
                //		DisplayBoard(Board, "MSB after move");
                nreplies = GenLegalMoves(Board, reply_list, true, true);
                in_check = IsInCheck(Board, Board->side);
                //		fprintf(stderr,"MSB: nreplies %d, in_check %d\n",
                //nreplies, in_check);
                UnMakeMove(Board, &move_list[i]);
                if (nreplies == 0) {
                    if (in_check) {
                        good_moves++;
                        best_move = 1024;
                        best_index = i;
                        move_list[i].flag |= MATE;
                    } else {
                        move_list[i].flag = DRAW;
                    }
                    continue;
                }

                if (xscore == UNKNOWN) {
                    if (Verbose > 2) {
                        GetMoveString(&move_list[i], move_string, false);
                        MyPrintf("Move %s not resolved\n", move_string);
                    }
                    continue;
                }

                if (xscore == DRAW || xscore == NOT_WON || xscore == NOT_LOST ||
                    xscore == UNRESOLVED || xscore == ILLEGAL) {
                    //		    fprintf(stderr,"MSB: for xscore=%d, continue\n",
                    //xscore);
                    continue;
                }

                if (xscore <= 0 || xscore == LOST)
                    good_moves++;
                if (score == 1) {
                    if ((move_list[i].flag & (CAPTURE | PROMOTION)) ||
                        (metric == DTZ &&
                         abs(move_list[i].piece_moved) == PAWN)) {
                        if (xscore < 0 && xscore + 1024 > best_move) {
                            best_move = xscore + 1024;
                            best_index = i;
                        }
                    }
                    if (xscore == 0 || xscore == LOST) {
                        if (best_move < 0) {
                            best_move = 0;
                            best_index = i;
                        } else if (best_move == 0) {
                            if (move_list[i].flag & (CAPTURE | PROMOTION)) {
                                int material =
                                    abs(move_list[i].piece_captured) +
                                    abs(move_list[i].piece_promoted);
                                if (material > biggest_capture) {
                                    biggest_capture = material;
                                    best_index = i;
                                }
                            } else if (metric == DTZ &&
                                       abs(move_list[i].piece_moved) == PAWN) {
                                best_index = i;
                            }
                        }
                    }
                } else {
                    bool pchange =
                        (move_list[i].flag & (CAPTURE | PROMOTION)) ||
                        (metric == DTZ &&
                         abs(move_list[i].piece_moved) == PAWN);
                    //		    fprintf(stderr,"MSB: pchange=%d\n",
                    //pchange); 		    fprintf(stderr,"MSB: xscore=%d, -(score - 1)=%d,
                    //best_move=%d\n", xscore, -(score - 1), best_move);
                    if (!pchange && xscore == -(score - 1) &&
                        best_move != xscore) {
                        best_move = xscore;
                        best_index = i;
                        //			fprintf(stderr,"MSB: changing
                        //best_move to %d\n", best_move);
                    }
                    //		    else {
                    //			fprintf(stderr,"MSB: leaving best_move at %d\n",
                    //best_move);
                    //		    }
                }
            }
            if (best_move == -16384) {
                if (Verbose > 2)
                    MyPrintf("Winning line terminated early\n");
                nmoves = 0;
                goto final_move;
            }

            if (nmoves > 1) {
                int nbest = 0;
                if (good_moves == 1)
                    move_list[best_index].flag |= UNIQUE;
                for (i = 0; i < nmoves; i++) {
                    if ((move_list[i].flag & PROMOTION) &&
                        (abs(move_list[i].piece_promoted) == PAWN))
                        continue;
                    if (move_list[i].score == move_list[best_index].score)
                        nbest++;
                }
                if (mark_best && nbest == 1)
                    move_list[best_index].flag |= BEST;
            }
        } else {
            best_index = 0;
            best_move = -16384;
            for (i = 0; i < nmoves; i++) {
                if ((move_list[i].flag & PROMOTION) &&
                    (abs(move_list[i].piece_promoted) == PAWN))
                    continue;
                int xscore = move_list[i].score;
                if (xscore == UNKNOWN || xscore == NOT_LOST ||
                    xscore == ILLEGAL) {
                    continue;
                }
                if (xscore == DRAW || xscore == NOT_WON ||
                    xscore == UNRESOLVED || xscore == LOST || xscore <= 0) {
                    char msg[64];
                    GetMoveString(&move_list[i], move_string, false);
                    sprintf(msg, "Move %s seems to save lost position\n",
                            move_string);
                    PrintEPD(Board, msg);
                    exit(1);
                }
                if (score == 0) {
                    if ((move_list[i].flag & (CAPTURE | PROMOTION)) ||
                        (metric == DTZ &&
                         abs(move_list[i].piece_moved) == PAWN)) {
                        if (xscore == WON) {
                            if (best_move <= -1024) {
                                best_move = -1023;
                                best_index = i;
                            }
                        } else if (xscore - 1024 > best_move) {
                            best_move = xscore - 1024;
                            best_index = i;
                        }
                    }
                } else if (score == -1 &&
                           !(move_list[i].flag & (CAPTURE | PROMOTION))) {
                    Move reply_list[MAX_MOVES];
                    char mstring[64];
                    int nreplies, best_index_x = 0, best_move_x = -16384,
                                  biggest_capture = 0;
                    MakeMove(Board, &move_list[i]);
                    if (flip) {
                        FlipBoard(Board);
                    }
                    nreplies = GenMoveScores(Board, reply_list, true, true);
                    if (flip) {
                        FlipBoard(Board);
                        for (j = 0; j < nreplies; j++)
                            FlipMove(&reply_list[j]);
                    }
                    for (j = 0; j < nreplies; j++) {
                        int xscore_x = reply_list[j].score;
                        Move reply_list_x[MAX_MOVES];
                        int nreplies_x;
                        MakeMove(Board, &reply_list[j]);
                        nreplies_x =
                            GenLegalMoves(Board, reply_list_x, true, true);
                        in_check = IsInCheck(Board, Board->side);
                        UnMakeMove(Board, &reply_list[j]);
                        if (nreplies_x == 0) {
                            if (in_check) {
                                best_move_x = 1024;
                                best_index_x = j;
                                reply_list[j].flag |= MATE;
                            } else {
                                reply_list[j].flag = DRAW;
                            }
                            continue;
                        }
                        if (xscore_x == UNKNOWN) {
                            if (Verbose > 2) {
                                GetMoveString(&reply_list[j], move_string,
                                              false);
                                MyPrintf("Move %s not resolved\n", move_string);
                            }
                            continue;
                        }

                        if (xscore_x == DRAW || xscore_x == NOT_WON ||
                            xscore_x == NOT_LOST || xscore_x == LOST ||
                            xscore_x == UNRESOLVED || xscore_x == ILLEGAL) {
                            continue;
                        }

                        if ((reply_list[j].flag & (CAPTURE | PROMOTION)) ||
                            (metric == DTZ &&
                             abs(reply_list[j].piece_moved) == PAWN)) {
                            if (xscore_x < 0 && xscore_x + 1024 > best_move_x) {
                                best_move_x = xscore_x + 1024;
                                best_index_x = j;
                            }
                        }
                        if (xscore_x == 0) {
                            if (best_move_x < 0) {
                                best_move_x = 0;
                                best_index_x = j;
                            } else if (best_move_x == 0) {
                                if (reply_list[j].flag &
                                    (CAPTURE | PROMOTION)) {
                                    int material =
                                        abs(reply_list[j].piece_captured) +
                                        abs(reply_list[j].piece_promoted);
                                    if (material > biggest_capture) {
                                        biggest_capture = material;
                                        best_index_x = j;
                                    }
                                } else if (metric == DTZ &&
                                           abs(reply_list[j].piece_moved) ==
                                               PAWN) {
                                    best_index_x = j;
                                }
                            }
                        }
                    }
                    UnMakeMove(Board, &move_list[i]);
                    if (best_move_x == -16384)
                        continue;
                    if (-best_move_x >= best_move) {
                        best_move = -best_move_x;
                        best_index = i;
                    }
                } else {
                    if (!((move_list[i].flag & (CAPTURE | PROMOTION)) ||
                          (metric == DTZ &&
                           abs(move_list[i].piece_moved) == PAWN)) &&
                        xscore == -score && xscore > best_move) {
                        best_move = xscore;
                        best_index = i;
                    }
                }
            }

            if (best_move == -16384) {
                if (Verbose > 2)
                    MyPrintf("Losing line terminated early\n");
                nmoves = 0;
                goto final_move;
            }
        }
    show_move:

        GetShortMoveString(&move_list[best_index], move_list, nmoves,
                           move_string);

        if (move_list[best_index].flag & (CAPTURE | PROMOTION))
            capture_seen = true;
        phase_change_seen = capture_seen;
        if ((metric == DTZ || StopAtPawnMove) &&
            abs(move_list[best_index].piece_moved) == PAWN)
            phase_change_seen = true;

        strcpy(prev_move, move_string);

        MakeMove(Board, &move_list[best_index]);

        if (flip) {
            FlipBoard(Board);
        }

        score = ScorePosition(Board, &index);

        in_check = IsInCheck(Board, Board->side);

        mzug = false;
        repeated_pos = false;

        if (Board->side == OtherSide(side_in) &&
            orig_index.kk_index == index.kk_index &&
            orig_index.index == index.index) {
            repeated_pos = true;
        }

        if (score == UNKNOWN || score == DRAW || score == ILLEGAL ||
            score == UNRESOLVED || score == NOT_WON || score == NOT_LOST) {
            if (Verbose > 2)
                MyPrintf("Line terminated early\n");
            nmoves = 0;
            goto final_move;
        }

        if (!in_check && mark_mzugs && (score == LOST || score <= 0)) {
            Board->side = OtherSide(Board->side);
            int xscore = ScorePosition(Board, &index);
            if (xscore == NOT_WON || xscore == DRAW || xscore == LOST ||
                xscore <= 0)
                mzug = true;
            Board->side = OtherSide(Board->side);
        }
    }

    OutputPGNString(result);
    OutputPGNString("\n");
    FlushPGNOutput();
}

static void EvaluateEnding(BOARD *Board, bool other_side, bool best_move) {
    int score, xscore;
    char msg[1024], score_str[64], xscore_str[32];
    INDEX_DATA index;

    score = ScorePosition(Board, &index);

    if (score == UNKNOWN)
        sprintf(msg, "score unknown");
    else if (score == ILLEGAL)
        sprintf(msg, "illegal");
    else if (score == NOT_WON)
        sprintf(msg, "not won");
    else if (score == NOT_LOST)
        sprintf(msg, "not lost");
    else if (score == DRAW)
        sprintf(msg, "draw");
    else if (score == WON)
        sprintf(msg, "won in ?");
    else if (score == LOST)
        sprintf(msg, "lost in ?");
    else if (score > 0)
        sprintf(msg, "won in %d", score);
    else if (score <= 0)
        sprintf(msg, "lost in %d", -score);

    sprintf(score_str, "%s %s", SideTm(Board->side), msg);

    if (other_side) {
        Board->side = OtherSide(Board->side);
        xscore = ScorePosition(Board, &index);

        if (xscore == UNKNOWN)
            sprintf(xscore_str, "score unknown");
        else if (xscore == ILLEGAL)
            sprintf(xscore_str, "illegal");
        else if (xscore == NOT_WON)
            sprintf(xscore_str, "not won");
        else if (xscore == NOT_LOST)
            sprintf(xscore_str, "not lost");
        else if (xscore == DRAW)
            sprintf(xscore_str, "draw");
        else if (xscore == WON)
            sprintf(xscore_str, "won in ?");
        else if (xscore == LOST)
            sprintf(xscore_str, "lost in ?");
        else if (xscore > 0)
            sprintf(xscore_str, "won in %d", xscore);
        else if (xscore <= 0)
            sprintf(xscore_str, "lost in %d", -xscore);

        sprintf(&score_str[strlen(score_str)], "; %s %s", SideTm(Board->side),
                xscore_str);
        Board->side = OtherSide(Board->side);
    }

    if (OutputPGNFormat) {
        PrintPGNHeader(Board, score, NULL);
        InitPGNOutput();
    }

    memset(msg, 0, sizeof(msg));

    int moves_seen = 0, nmoves = 0;
    Move move_list[MAX_MOVES];

    if (best_move && (score != UNKNOWN && score != ILLEGAL)) {
        bool flip = false;

        if (Board->side == WHITE &&
            (score == NOT_LOST || score <= 0 || score == LOST))
            flip = true;

        if (Board->side == BLACK && (score == NOT_WON || score == WON ||
                                     (score > 0 && score != DRAW &&
                                      score != NOT_LOST && score != LOST)))
            flip = true;

        if (flip) {
            FlipBoard(Board);
        }

        nmoves = GenMoveScores(Board, move_list, true, true);

        if (flip) {
            FlipBoard(Board);
            for (int i = 0; i < nmoves; i++)
                FlipMove(&move_list[i]);
        }

        if (nmoves == 0)
            goto moves_done;

        for (int i = 0; i < nmoves; i++) {
            Move reply_list[MAX_MOVES];
            move_list[i].score = RetrogradeResult(move_list[i].score);
            MakeMove(Board, &move_list[i]);
            int nreplies = GenLegalMoves(Board, reply_list, true, true);
            bool in_check2 = IsInCheck(Board, Board->side);
            UnMakeMove(Board, &move_list[i]);
            if (nreplies == 0) {
                if (in_check2) {
                    move_list[i].score = CHECK_MATE;
                    move_list[i].flag |= MATE;
                } else {
                    move_list[i].score = DRAW;
                }
            }
        }

        qsort(move_list, nmoves, sizeof(move_list[0]), MoveCompare);

        moves_seen = 1;

#if 0
	for(int i = 0; i < nmoves; i++) {
	    char mstring[64];
	    GetShortMoveString(&move_list[i], move_list, nmoves, mstring);
	    MyPrintf("%d: %s %d\n", i, mstring, move_list[i].score);
	}
#endif

        int score_type = 0;
        if (IsWinningScore(move_list[0].score))
            score_type = 1;
        else if (IsLosingScore(move_list[0].score))
            score_type = -1;

        int score_type_0 = 0;
        if (IsWinningScore(score))
            score_type_0 = 1;
        else if (IsLosingScore(score))
            score_type_0 = -1;

        if (score_type_0 != score_type) {
            OutputPGNString("{Probably unresolved subgame}");
            moves_seen = 0;
            goto moves_done;
        }

        if (nmoves > 1) {
            for (int i = 1; i < nmoves; i++) {
                int score_type_2 = 0;
                if (IsWinningScore(move_list[i].score))
                    score_type_2 = 1;
                else if (IsLosingScore(move_list[i].score))
                    score_type_2 = -1;
                if (score_type != score_type_2)
                    break;
                else
                    moves_seen = i + 1;
            }

            if (moves_seen == 1)
                move_list[0].flag |= UNIQUE;

            if (UniqueMovesOnly && (moves_seen != 1)) {
                moves_seen = 0;
                goto moves_done;
            }
        }

        strcat(msg, "bm");

        for (int i = 0; i < moves_seen; i++) {
            char move_string[64];
            strcat(msg, " ");
            GetShortMoveString(&move_list[i], move_list, nmoves, move_string);
            strcat(msg, move_string);
        }

        strcat(msg, "; ");
    }

moves_done:

    if (OutputPGNFormat) {
        if (moves_seen > 0) {
            char move_string[64];
            GetShortMoveString(&move_list[0], move_list, nmoves, move_string);
            char out_string[128];
            sprintf(out_string, "%d%s %s", Board->full_move,
                    ((Board->side == WHITE) ? "." : "..."), move_string);
            if (IsWinningScore(move_list[0].score)) {
                if (Board->side == WHITE)
                    strcat(out_string, " $20");
                else
                    strcat(out_string, " $21");
                if (move_list[0].score != WON) {
                    sprintf(move_string, " {+%d}", move_list[0].score);
                    strcat(out_string, move_string);
                }
            } else if (IsDrawnScore(move_list[0].score))
                strcat(out_string, " $10");
            OutputPGNString(out_string);
            for (int i = 1; i < moves_seen; i++) {
                GetShortMoveString(&move_list[i], move_list, nmoves,
                                   move_string);
                sprintf(out_string, "%d%s %s", Board->full_move,
                        ((Board->side == WHITE) ? "." : "..."), move_string);
                if (IsWinningScore(move_list[i].score)) {
                    if (Board->side == WHITE)
                        strcat(out_string, " $20");
                    else
                        strcat(out_string, " $21");
                    if (move_list[i].score != WON) {
                        sprintf(move_string, " {+%d}", move_list[i].score);
                        strcat(out_string, move_string);
                    }
                } else if (IsDrawnScore(move_list[i].score))
                    strcat(out_string, " $10");
                OutputPGNString(" (");
                OutputPGNString(out_string);
                OutputPGNString(")");
            }
        }

        OutputPGNString(" *");
        FlushPGNOutput();
        MyPrintf("\n");
    } else {
        sprintf(&msg[strlen(msg)], "c0 \"%s\"", score_str);
        PrintEPD(Board, msg);
    }
}

static void PrintHelpScreen() {
    MyPrintf(
        "\nCommands:\n\n"
        "q[uit]    - Exit\n"
        "[#]       - Play move number # from list of optimal ones\n"
        "            (hitting ENTER is equivalent to entering 0, the best "
        "move)\n"
        "- [#]     - Go back # ply (default 1)\n"
        "+ [#]     - Go forward # ply (default 1)\n"
        "m [#]     - Go to ply # (default 0)\n"
        "on|off    - Turn evaluation of all moves from position on or off\n"
        "f         - Flip side to move\n"
        "n         - Go to the next position in the input file\n"
        "p         - Go to the previous position in the input file\n"
        "g #       - Go to the #th position in the input file\n"
        "\"title\"   - Set title of position\n"
        "help      - Print this screen\n\n");
    MyFlush();
}

static bool Dynamic = false;

static void PlayPosition(char *pos_file, bool single_pos) {
    FILE *fin;
    BOARD Board;
    Move move_list[MAX_MOVES], move_stack[MAX_STACK];
    int npos = 1, nlines = 0, side, nmoves, ep_square;
    INDEX_DATA index;
    int score, xscore, i, j;
    int curr_stack = 0, max_stack = 0;
    char line[MAX_LINE], label[64], xlabel[64], title[MAX_LINE], *sptr;
    bool pos_found = false, flipped = false, in_check;
    static bool eval_moves = true;

    if (Verbose > 3) {
        MyPrintf("Entering PlayPosition\n");
    }

    if (!single_pos) {
        if ((sptr = strchr(pos_file, SEPARATOR[0])) != NULL) {
            sscanf(sptr + 1, "%lu", &npos);
            if (npos <= 0)
                npos = 1;
            *sptr = '\0';
        }
    }

next_pos:
    nlines = 0;
    pos_found = false;

    if (!single_pos) {
        if ((fin = fopen(pos_file, "r")) == NULL) {
            MyPrintf("Could not open %s for reading\n", pos_file);
            MyFlush();
            exit(1);
        }

        memset(line, 0, sizeof(line));
        side = WHITE;

        while (fgets(line, MAX_LINE - 1, fin) != NULL) {
            BOARD tmp_board;
            int tmp_side;
            title[0] = '\0';
            tmp_side = ReadPosition(line, &tmp_board, title);
            if (tmp_side != NEUTRAL) {
                memcpy(&Board, &tmp_board, sizeof(Board));
                pos_found = true;
                nlines++;
            }
            if (nlines == npos)
                break;
        }
        fclose(fin);
    } else {
        if (Dynamic) {
            memset(line, 0, sizeof(line));
            do {
                fgets(line, MAX_LINE - 1, stdin);
            } while (line[0] == '\0');
            pos_file = &line[0];
        }
        title[0] = '\0';
        side = ReadPosition(pos_file, &Board, title);
        if (side != NEUTRAL)
            pos_found = true;
    }

    if (!pos_found) {
        MyPrintf("No positions found\n");
        MyFlush();
        return;
    }

    if (Verbose > 3)
        DisplayBoard(&Board, "PlayPosition: initial board");

    if (npos > nlines)
        npos = nlines;

    curr_stack = max_stack = 0;
repeat:
    score = ScorePosition(&Board, &index);

    if (score == ILLEGAL) {
        MyPrintf("Illegal position encountered\n");
        MyPrintf("->%s\n", line);
        MyFlush();
        return;
    }

    in_check = IsInCheck(&Board, Board.side);

    flipped = false;

    if (side == WHITE && (score == NOT_LOST || score <= 0 || score == LOST))
        flipped = true;

    if (side == BLACK &&
        (score == NOT_WON || score == WON ||
         (score > 0 && score != DRAW && score != NOT_LOST && score != LOST)))
        flipped = true;

    if (flipped) {
        FlipBoard(&Board);
    }

    if (eval_moves)
        nmoves = GenMoveScores(&Board, move_list, true, true);
    else {
        nmoves = GenLegalMoves(&Board, move_list, true, true);
        for (i = 0; i < nmoves; i++)
            move_list[i].score = UNKNOWN;
    }

    if (flipped) {
        FlipBoard(&Board);
        for (i = 0; i < nmoves; i++)
            FlipMove(&move_list[i]);
    }

    if (Verbose > 4) {
        MyPrintf("Number of moves: %d\n", nmoves);
        for (i = 0; i < nmoves; i++) {
            MyPrintf("   from=%02o to=%02o score=%d\n", move_list[i].from,
                     move_list[i].to, move_list[i].score);
        }
    }

    if (nmoves == 0) {
        if (in_check) {
            if (score != UNKNOWN && score != 0 && score != LOST) {
                MyPrintf("Checkmate, but DB has score %d\n", score);
                MyFlush();
                return;
            }
            score = CHECK_MATE;
        } else {
            if (score != UNKNOWN && score != DRAW && score != NOT_WON &&
                score != NOT_LOST) {
                MyPrintf("Stalemate, but DB has score %d\n", score);
                MyFlush();
                return;
            }
            score = STALE_MATE;
        }
    }

    if (score == UNKNOWN)
        sprintf(label, "?");
    else if (score == DRAW)
        sprintf(label, "=");
    else if (score == STALE_MATE)
        sprintf(label, "=");
    else if (score == CHECK_MATE)
        sprintf(label, "Mate");
    else if (score == NOT_WON)
        sprintf(label, "Not Won");
    else if (score == NOT_LOST)
        sprintf(label, "Not Lost");
    else if (score == WON)
        sprintf(label, "Won");
    else if (score == LOST)
        sprintf(label, "Lost");
    else if (score <= 0)
        sprintf(label, "%d", score);
    else
        sprintf(label, "+%d", score);

    xscore = ILLEGAL;
    if (!in_check) {
        INDEX_DATA index2;
        Board.side = OtherSide(Board.side);
        xscore = ScorePosition(&Board, &index2);
        if (Verbose > 4) {
            MyPrintf("ScorePosition: xscore = %d\n", xscore);
        }
        Board.side = OtherSide(Board.side);
    }

    if (xscore == ILLEGAL)
        sprintf(xlabel, "/");
    else if (xscore == UNKNOWN)
        sprintf(xlabel, "/?");
    else if (xscore == DRAW)
        sprintf(xlabel, "/=");
    else if (xscore == NOT_WON)
        sprintf(xlabel, "/Not Won");
    else if (xscore == NOT_LOST)
        sprintf(xlabel, "/Not Lost");
    else if (xscore == WON)
        sprintf(xlabel, "/Won");
    else if (xscore == LOST)
        sprintf(xlabel, "/Lost");
    else if (xscore <= 0)
        sprintf(xlabel, "/%d", xscore);
    else if (xscore > 0)
        sprintf(xlabel, "/+%d", xscore);

    strcat(label, xlabel);

    if (title[0] != '\0') {
        MyPrintf("\n%s", title);
    }

    MyPrintf("\nPosition[%d], Ply %d, KK %d, Offset " DEC_ZINDEX_FORMAT, npos,
             curr_stack, index.kk_index, index.index);
    DisplayBoard(&Board, label);

    for (i = 0; i < nmoves; i++) {
        Move reply_list[MAX_MOVES];
        int nreplies;
        bool in_check2;
        move_list[i].score = RetrogradeResult(move_list[i].score);
        if ((move_list[i].flag & PROMOTION) &&
            (abs(move_list[i].piece_promoted) == PAWN))
            continue;
        MakeMove(&Board, &move_list[i]);
        nreplies = GenLegalMoves(&Board, reply_list, true, true);
        in_check2 = IsInCheck(&Board, Board.side);
        UnMakeMove(&Board, &move_list[i]);
        if (nreplies == 0) {
            if (in_check2) {
                move_list[i].score = CHECK_MATE;
                move_list[i].flag |= MATE;
            } else {
                move_list[i].score = DRAW;
            }
        }
    }

    qsort(move_list, nmoves, sizeof(move_list[0]), MoveCompare);

    for (i = 0; i < (nmoves + 3) / 3; i++) {
        for (j = 0; j < 3; j++) {
            int k = j * ((nmoves + 3) / 3) + i;
            if (k < nmoves) {
                Move *mptr = &move_list[k];
                char move_string[16], score_string[16];

                GetMoveString(mptr, move_string, false);

                if (j > 0)
                    MyPrintf("    ");
#if (NROWS < 10)
                MyPrintf("%2d %-10s", k, move_string);
#else
                MyPrintf("%2d %-12s", k, move_string);
#endif
                if (mptr->score == UNKNOWN)
                    sprintf(score_string, "?");
                else if (mptr->score == DRAW)
                    sprintf(score_string, "Draw");
                else if (mptr->score == NOT_LOST)
                    sprintf(score_string, "=+");
                else if (mptr->score == NOT_WON)
                    sprintf(score_string, "=-");
                else if (mptr->score == LOST)
                    sprintf(score_string, " -");
                else if (mptr->score == WON)
                    sprintf(score_string, " +");
                else if (mptr->score == CHECK_MATE)
                    sprintf(score_string, "Mate");
                else {
                    sprintf(score_string, "%c%c%d",
                            (mptr->flag & (CAPTURE | PROMOTION)) ? '*' : ' ',
                            (mptr->score >= 0) ? '+' : '-',
                            (mptr->score >= 0) ? mptr->score : -mptr->score);
                }
                MyPrintf("%6s", score_string);
            }
        }
        MyPrintf("\n");
    }

prompt:
    memset(line, 0, sizeof(line));
    MyPrintf("\n[help, [#], +, -, m [#], on|off, f, n, p, g #, \"title\", "
             "q[uit]]) ");
    MyFlush();
    do {
        fgets(line, MAX_LINE - 1, stdin);
    } while (line[0] == 0);

    if (strchr(line, '"')) {
        char *pptr = strchr(line, '"');
        pptr++;
        j = 0;
        while (*pptr != '"' && *pptr != '\0') {
            title[j++] = *pptr++;
        }
        title[j] = '\0';
    } else if (!strncmp(line, "quit", 1)) {
        MyPrintf("exiting play\n");
        return;
    } else if (!strncmp(line, "on", 2)) {
        MyPrintf("turning move evaluation on\n");
        eval_moves = true;
    } else if (!strncmp(line, "of", 2)) {
        MyPrintf("turning move evaluation off\n");
        eval_moves = false;
    } else if (strchr(line, 'h')) {
        PrintHelpScreen();
        goto prompt;
    } else if (strchr(line, '-')) {
        int ns;
        if (sscanf(strchr(line, '-') + 1, "%d", &ns) != 1)
            ns = 1;
        MyPrintf("rewinding %d ply\n", ns);
        while (curr_stack > 0 && --ns >= 0) {
            UnMakeMove(&Board, &move_stack[--curr_stack]);
        }
    } else if (strchr(line, '+')) {
        int ns;
        if (sscanf(strchr(line, '+') + 1, "%d", &ns) != 1)
            ns = 1;
        MyPrintf("advancing %d ply\n", ns);
        while (curr_stack < max_stack && --ns >= 0) {
            MakeMove(&Board, &move_stack[curr_stack++]);
        }
    } else if (strchr(line, 'f')) {
        if (in_check)
            MyPrintf("\nIn Check, can't flip\n");
        else {
            MyPrintf("flipping side to move\n");
            curr_stack = max_stack = 0;
            Board.side = OtherSide(Board.side);
            Board.ep_square = 0;
            goto repeat;
        }
    } else if (strchr(line, 'n')) {
        npos++;
        MyPrintf("load next position from file\n");
        goto next_pos;
    } else if (strchr(line, 'p')) {
        if (npos > 1)
            npos--;
        MyPrintf("load previous position from file\n");
        goto next_pos;
    } else if (strchr(line, 'g')) {
        sscanf(strchr(line, 'g') + 1, "%d", &npos);
        MyPrintf("go to position %d\n", npos);
        goto next_pos;
    } else if (strchr(line, 'm')) {
        int nply;
        if (sscanf(strchr(line, 'm') + 1, "%d", &nply) != 1)
            nply = 0;
        MyPrintf("go to ply %d\n", nply);
        if (nply < curr_stack) {
            int ns = curr_stack - nply;
            while (--ns >= 0) {
                UnMakeMove(&Board, &move_stack[--curr_stack]);
            }
        } else if (nply > curr_stack) {
            int ns;
            if (max_stack < nply)
                nply = max_stack;
            ns = nply - curr_stack;
            while (--ns >= 0) {
                MakeMove(&Board, &move_stack[curr_stack++]);
            }
        }
    } else {
        int move_no;
        int n = sscanf(line, "%d", &move_no);

        if (n <= 0)
            move_no = 0;
        else {
            if (move_no < 0 || move_no >= nmoves)
                move_no = -1;
        }
        if (nmoves <= 0)
            move_no = -1;
        if (move_no >= 0) {
            if ((move_list[move_no].flag == PROMOTION) &&
                (abs(move_list[move_no].piece_promoted) == PAWN)) {
                MyPrintf("Cannot promote to pawn\n");
                goto repeat;
            }
            MyPrintf("play move %d\n", move_no);
            if (curr_stack == MAX_STACK) {
                memmove(&move_stack[0], &move_stack[1],
                        (MAX_STACK - 1) * sizeof(Move));
                curr_stack--;
            }
            memcpy(&move_stack[curr_stack++], &move_list[move_no],
                   sizeof(Move));
            max_stack = curr_stack;
            MakeMove(&Board, &move_list[move_no]);
        }
    }

    goto repeat;
}

static void EvaluateBleicher(char *pos, int side_in) {
    BOARD Board;
    int side, score, i, nmoves, nmoves_x;
    Move move_list[MAX_MOVES];
    INDEX_DATA index;
    bool in_check, flipped, pawns_present = false;

    side = ReadPosition(pos, &Board, NULL);

    if (side == NEUTRAL) {
        MyPrintf("INVALID\n");
        return;
    }

    if (side_in != Board.side) {
        Board.ep_square = 0;
        Board.side = side_in;
    }

    score = ScorePosition(&Board, &index);

    if (score == ILLEGAL) {
        MyPrintf("INVALID\n");
        return;
    }

    MyPrintf("SUCCESS\n");

    flipped = false;

    if (side == WHITE && (score == NOT_LOST || score == LOST || score <= 0))
        flipped = true;

    if (side == BLACK &&
        (score == NOT_WON || score == WON ||
         (score > 0 && score != DRAW && score != NOT_LOST && score != LOST)))
        flipped = true;

    if (flipped) {
        FlipBoard(&Board);
    }

    nmoves = GenMoveScores(&Board, move_list, true, true);

    if (flipped) {
        FlipBoard(&Board);
        for (i = 0; i < nmoves; i++)
            FlipMove(&move_list[i]);
    }

    if (nmoves == 0) {
        in_check = IsInCheck(&Board, Board.side);
        if (in_check)
            MyPrintf("%d\n", BLEICHER_MATED);
        else
            MyPrintf("%d\n", BLEICHER_DRAW);
        MyPrintf("MOVES:%d\n", nmoves);
        return;
    }

    if (score == UNKNOWN || score == NOT_WON || score == NOT_LOST) {
        MyPrintf("%d\n", BLEICHER_NOT_FOUND);
    } else if (score == DRAW)
        MyPrintf("%d\n", BLEICHER_DRAW);
    else if (score == WON)
        MyPrintf("%d\n", BLEICHER_WON);
    else if (score == LOST)
        MyPrintf("%d\n", BLEICHER_LOST);
    else
        MyPrintf("%d\n", score);

    nmoves_x = nmoves;

    for (i = 0; i < nmoves; i++) {
        Move reply_list[MAX_MOVES];
        int nreplies;
        bool in_check_x;
        if ((move_list[i].flag & PROMOTION) &&
            (abs(move_list[i].piece_promoted) == PAWN)) {
            nmoves_x--;
            continue;
        }
        MakeMove(&Board, &move_list[i]);
        nreplies = GenLegalMoves(&Board, reply_list, true, true);
        in_check_x = IsInCheck(&Board, Board.side);
        UnMakeMove(&Board, &move_list[i]);
        if (nreplies == 0) {
            if (in_check_x) {
                move_list[i].score = CHECK_MATE;
                move_list[i].flag |= MATE;
            } else {
                move_list[i].score = DRAW;
            }
        }
    }

    MyPrintf("MOVES:%d\n", nmoves_x);

    qsort(move_list, nmoves, sizeof(move_list[0]), MoveCompare);

    for (i = nmoves - 1; i >= 0; i--) {
        Move *mptr = &move_list[i];
        char move_string[16], score_string[16];

        if ((mptr->flag & PROMOTION) && (abs(mptr->piece_promoted) == PAWN))
            continue;

        GetMoveStringBleicher(mptr, move_string);

        MyPrintf("%s:", move_string);

        if (mptr->score == UNKNOWN || mptr->score == NOT_WON ||
            mptr->score == NOT_LOST)
            MyPrintf("%d\n", BLEICHER_NOT_FOUND);
        else if (mptr->score == DRAW || mptr->score == STALE_MATE)
            MyPrintf("%d\n", BLEICHER_DRAW);
        else if (mptr->score == CHECK_MATE)
            MyPrintf("%d\n", BLEICHER_MATED);
        else if (mptr->score == LOST)
            MyPrintf("%d\n", BLEICHER_LOST);
        else if (mptr->score == WON)
            MyPrintf("%d\n", BLEICHER_WON);
        else
            MyPrintf("%d\n", mptr->score);
    }
}

typedef struct {
    int piece_types[2][KING];
    unsigned int count;
} FREQ_TABLE;

static FREQ_TABLE *EndingFreqTable = NULL;

static int compare_freq(const void *a, const void *b) {
    const FREQ_TABLE *x = (const FREQ_TABLE *)a;
    const FREQ_TABLE *y = (const FREQ_TABLE *)b;

    return y->count - x->count;
}

static void AddEndingToStats(BOARD *Board) {
    int piece_types[2][KING];
    int index;

    if (Board->strength_w < Board->strength_b) {
        memcpy(piece_types[0], Board->piece_type_count[1], KING * sizeof(int));
        memcpy(piece_types[1], Board->piece_type_count[0], KING * sizeof(int));
    } else {
        memcpy(piece_types, Board->piece_type_count, sizeof(piece_types));
    }

    index = ending_stat_index(piece_types);

    EndingStats[index]++;
}

static void CompileEndingStats() {
    int n = 0, i;

    MyPrintf("Total number of positions: %u\n", num_positions);

    for (i = 0; i < num_total_endings; i++) {
        if (EndingStats[i] > 0) {
            n++;
        }
    }

    EndingFreqTable = (FREQ_TABLE *)MyMalloc(n * sizeof(FREQ_TABLE));

    n = 0;
    for (i = 0; i < num_total_endings; i++) {
        if (EndingStats[i] > 0) {
            GetEndingFromIndex(EndingFreqTable[n].piece_types, i);
            EndingFreqTable[n++].count = EndingStats[i];
        }
    }

    qsort(EndingFreqTable, n, sizeof(FREQ_TABLE), compare_freq);

    for (i = 3; i <= 8; i++) {
        MyPrintf("\nEndings with %d pieces\n", i);
        for (int j = 0; j < n; j++) {
            int len, piece;
            char ending[64];

            len = GetEndingName(EndingFreqTable[j].piece_types, ending);

            if (len == i)
                MyPrintf("%s  %d\n", ending, EndingFreqTable[j].count);
        }
    }
}

static void PrintSummary() {
    MyPrintf("Fopen: %lu   Fclose: %lu"
             "   Fread: " DEC_INDEX_FORMAT "   Fwrite: " DEC_INDEX_FORMAT "\n",
             FilesOpened, FilesClosed, FileReads, FileWrites);
    MyPrintf("Alloc: %lu   Free: %lu\n", MemoryAllocated, MemoryFreed);
    MyPrintf("Database hits: %lu  Cache hits: %lu\n", DBHits, CacheHits);
    MyFlush();
}

static bool FirstMove = true;
static bool EGTBWritten = false;
static bool PrevEGTBWritten = false;
static bool ContainsEGTBEvaluation = false;

static int max_tag_line = 0;

PGNToken ParseTagList(PGNToken symbol, FILE *fp, ParseType *yylval,
                      BOARD *start_pos) {
    bool setup_seen = false;
    bool fen_seen = false;
    bool tag_seen = false;
    int result = UNKNOWN;

    if (Verbose > 3)
        MyPrintf("Starting to parse tags\n");

    // for TAG lines, don't limit line length

    int save_line_width = LineWidth;
    LineWidth = MAX_PGN_OUTPUT;

    while (symbol == TAG) {
        int tag_index = yylval->tag_index;
        if (Verbose > 3)
            MyPrintf("Saw tag index %d\n", tag_index);
        tag_seen = true;
        symbol = GetNextToken(fp, yylval);
        if (symbol == STRING) {
            if (tag_index == VARIANT_TAG) {
                if (strstr(yylval->string_info, "960")) {
                    Chess960Game = true;
                }
            }
            if (tag_index == SETUP_TAG) {
                int setup_value = 0;
                if (sscanf(yylval->string_info, "%d", &setup_value) == 1) {
                    if (setup_value != 0) {
                        setup_seen = true;
                    }
                }
            }
            if (tag_index == FEN_TAG) {
                BOARD tmp_board;
                int legal;
                if (!setup_seen) {
                    if (Verbose > 1 || CheckSyntaxOnly) {
                        MyPrintf("Saw FEN tag without SetUp tag, game %u\n",
                                 num_games);
                    }
                }
                legal = ReadPosition(yylval->string_info, &tmp_board, NULL);
                if (legal != NEUTRAL) {
                    memcpy(start_pos, &tmp_board, sizeof(BOARD));
                    if (Verbose > 3)
                        DisplayBoard(start_pos, "Board in TagList");
                    fen_seen = true;
                } else {
                    if (CheckSyntaxOnly || Verbose > 1) {
                        MyPrintf("Could not scan FEN from %s, game %u\n",
                                 yylval->string_info, num_games);
                    }
                }
            }
            if (tag_index == RESULT_TAG) {
                if (!strcmp(yylval->string_info, "1-0"))
                    result = WON;
                else if (!strcmp(yylval->string_info, "1/2-1/2"))
                    result = DRAW;
                else if (!strcmp(yylval->string_info, "0-1"))
                    result = LOST;
                if (Verbose > 3)
                    MyPrintf("Saw result integer %d\n", result);
            }
            /*
            if(tag_index == WHITE_TAG) {
                printf("%s - ",yylval->string_info);
                }
            if(tag_index == BLACK_TAG) {
            printf("%s\n", yylval->string_info);
            }
            */
            /*
            if(tag_index == ANNOTATOR_TAG) {
                AnnotatorSeen = true;
                AnnotatorLine = NumOutputLines;
            }
            */
            if ((!CheckSyntaxOnly && !PositionDatabase &&
                 !PrintBadlyPlayedPositions) ||
                Verbose > 3) {
                char output[512];
                if (tag_index == ANNOTATOR_TAG) {
                    AnnotatorSeen = true;
                    AnnotatorLine = NumOutputLines;
                }
                sprintf(output, "[%s \"%s\"]", TagList[tag_index],
                        yylval->string_info);
                if (Verbose > 3)
                    MyPrintf("Sending %s to output cache\n", output);
                OutputPGNStringCashed(output);
                FlushPGNOutputCashed();
            }
        } else {
            if (CheckSyntaxOnly || Verbose > 1) {
                MyPrintf("Expected STRING tag %d, got %d for %s game %u\n",
                         STRING, symbol, yylval->string_info, num_games);
            }
        }
        symbol = GetNextToken(fp, yylval);
        while (symbol == STRING)
            symbol = GetNextToken(fp, yylval);
    }

    LastTagLine = NumOutputLines;

    // restore maximum line length
    LineWidth = save_line_width;

    if (Verbose > 3)
        MyPrintf("Last line with tags: %d\n", LastTagLine);

    start_pos->result = result;

    while (symbol == COMMENT) {
        if (!CheckSyntaxOnly && !PositionDatabase &&
            !PrintBadlyPlayedPositions) {
            if (Verbose > 3)
                MyPrintf("Comment before moves: %s\n", yylval->string_info);
            OutputPGNCommentCashed(yylval->string_info);
            FlushPGNOutputCashed();
        }
        symbol = GetNextToken(fp, yylval);
    }

    return symbol;
}

static int ProcessPosition(BOARD *pos, bool evaluate, bool best_capture,
                           bool mark_mzugs) {
    MB_INFO mb_info;
    char ending[16];
    int changed = 0;
    char score_string[64], zz_string[64];
    INDEX_DATA idata;

    if (!IgnoreCastle && pos->castle)
        return 0;

    if (pos->num_pieces > MaximumNumberOfPieces ||
        pos->num_pieces < MinimumNumberOfPieces)
        return 0;

    int len = GetEndingName(pos->piece_type_count, ending);

    if (len > MaximumNumberOfPieces || len < MinimumNumberOfPieces) {
        fprintf(stderr, "Inconsistent length for %s\n", ending);
        exit(1);
    }

    if (GetMBInfo(pos, &mb_info) < 0) {
        if (Verbose > 1) {
            fprintf(stderr, "Could not map ending %s\n", ending);
        }
        return 0;
    }

    int result = pos->result;

    if (evaluate) {
        int game_num = pos->game_num;
        int score = pos->score;
        if (score == UNKNOWN || score == NOT_WON || score == NOT_LOST) {
            int score_new = UNKNOWN;
            if (best_capture) {
                Move move_list[MAX_MOVES];
                int ncaptures = GenLegalCaptures(pos, move_list, true, true);

                for (int i = 0; i < ncaptures; i++) {
                    MakeMove(pos, &move_list[i]);
                    int tmp_score = ScorePosition(pos, &idata);
                    UnMakeMove(pos, &move_list[i]);
                    tmp_score = RetrogradeResult(tmp_score);
                    if (tmp_score == UNKNOWN || tmp_score == UNRESOLVED ||
                        tmp_score == ILLEGAL || tmp_score == LOST ||
                        tmp_score == NOT_WON)
                        continue;
                    if (tmp_score == STALE_MATE || tmp_score == DRAW ||
                        tmp_score == NOT_LOST)
                        score_new = NOT_LOST;
                    else if (tmp_score == WON || tmp_score > 0) {
                        score_new = 1;
                        break;
                    }
                }
            } else {
                score_new = ScorePosition(pos, &idata);
            }

            if (score != score_new) {
                if ((score == NOT_WON && score_new == NOT_LOST) ||
                    (score == NOT_LOST && score_new == NOT_WON)) {
                    score = DRAW;
                    changed = 1;
                } else if (score == UNKNOWN ||
                           (score_new != UNKNOWN &&
                            (score == NOT_WON || score == NOT_LOST))) {
                    score = score_new;
                    changed = 1;
                }
                pos->score = score;
            }
        }
    }

    if (mark_mzugs) {
        int game_num = pos->game_num;
        int score = pos->score;
        int zz_type = pos->zz_type;

        if (zz_type == UNKNOWN) {
            if (score == WON ||
                (score > 0 && score != DRAW && score != NOT_WON &&
                 score != LOST && score != UNKNOWN) ||
                IsInCheck(pos, pos->side)) {
                pos->zz_type = NO_MZUG;
            } else {
                int score_other = UNKNOWN;
                pos->side = OtherSide(pos->side);
                score_other = ScorePosition(pos, &idata);
                pos->side = OtherSide(pos->side);
                if (score_other != UNKNOWN) {
                    if (score == LOST || score <= 0) {
                        if (score_other == DRAW || score_other == NOT_WON)
                            zz_type = MINUS_EQUAL;
                        else if (score_other == LOST || score_other <= 0)
                            zz_type = MINUS_PLUS;
                        else
                            zz_type = NO_MZUG;
                    } else if (score == DRAW || score == NOT_WON) {
                        if (score_other == LOST || score_other <= 0)
                            zz_type = EQUAL_PLUS;
                        else
                            zz_type = NO_MZUG;
                    }

                    if (zz_type != UNKNOWN) {
                        pos->zz_type = zz_type;
                        changed = 1;
                    }
                }
            }
        }
    }

    int game_num = pos->game_num;

    if (!evaluate && !mark_mzugs) {
        game_num = num_games;
    }

    int promos = QRBN_PROMOTIONS;

    if (FlagRestrictedPromotions && mb_info.pawn_file_type == FREE_PAWNS &&
        pos->num_pieces == 8) {
        RESTRICTED_PROMOTION *rptr, ref;
        strcpy(ref.ending, ending);
        rptr = (RESTRICTED_PROMOTION *)bsearch(
            &ref, RestrictedPromotionList, NumRestrictedPromotionEndings,
            sizeof(RestrictedPromotionList[0]), restricted_promo_compar);
        if (rptr != NULL)
            promos = rptr->promos;
    }

    POSITION_DATA pos_data;

    strncpy(pos_data.ending, ending, sizeof(pos_data.ending));
    pos_data.kk_index = mb_info.kk_index;
    pos_data.offset = mb_info.parity_index[0].index;
    pos_data.side = pos->side;
    pos_data.promos = promos;
    pos_data.game_num = game_num;
    pos_data.move_no = (RAV_level == 0 ? pos->full_move : -1);
    pos_data.result = result;
    pos_data.score = pos->score;
    pos_data.zz_type = pos->zz_type;

    WritePositionData(&pos_data, 9);

    return changed;
}

static int EvaluatePosition(BOARD *board, bool best_capture, bool mark_mzugs) {
    return ProcessPosition(board, true, best_capture, mark_mzugs);
}

PGNToken ParseMoveList(PGNToken symbol, FILE *fin, ParseType *yylval,
                       BOARD *prev_pos, BOARD *curr_pos) {
    while (symbol == MOVE_NUMBER || symbol == MOVE) {
        if (symbol == MOVE_NUMBER) {
            if (Verbose > 4) {
                MyPrintf("\n");
                for (int i = 0; i < 3 * RAV_level; i++)
                    MyPrintf(" ");
                MyPrintf("Saw move number: %d\n", yylval->move_number);
            }
            symbol = GetNextToken(fin, yylval);
        }

        if (symbol == MOVE) {
            Move move_list[MAX_MOVES], pseudo_move_list[MAX_MOVES], move;
            int nmoves, npseudo_moves;

            if (Verbose > 4) {
                MyPrintf("\n");
                for (int i = 0; i < 3 * RAV_level; i++)
                    MyPrintf(" ");
                MyPrintf("Saw move: %s\n", yylval->string_info);
            }

            npseudo_moves =
                GenPseudoLegalMoves(curr_pos, pseudo_move_list, true);
            nmoves = GenLegalFromPseudoMoves(curr_pos, move_list,
                                             pseudo_move_list, npseudo_moves);

            if (Verbose > 5) {
                MyPrintf("# pseudo-legal: %d,  legal: %d\n", npseudo_moves,
                         nmoves);
            }

            if (!DecodeMove(yylval->string_info, &move, move_list, nmoves)) {
                fprintf(stderr, "Could not match move %s, game %u\n",
                        yylval->string_info, num_games);
                fprintf(stderr, "Candidate moves (%d):\n", nmoves);
                for (int i = 0; i < nmoves; i++) {
                    char move_string[64];
                    GetMoveString(&move_list[i], move_string, true);
                    fprintf(stderr, "%d: %s\n", i, move_string);
                }
                exit(1);
            }

            memcpy(prev_pos, curr_pos, sizeof(BOARD));

            if (Verbose > 5) {
                char comment[64];
                sprintf(comment, "Before move %s", yylval->string_info);
                DisplayBoard(prev_pos, comment);
            }

            if (!CheckSyntaxOnly && !PositionDatabase &&
                !PrintBadlyPlayedPositions) {
                char move_string[64];
                if (curr_pos->side == WHITE || FirstMove) {
                    sprintf(move_string, "%d%s", curr_pos->full_move,
                            (curr_pos->side == WHITE) ? "." : "...");
                    OutputPGNStringCashed(move_string);
                }
            }

#if 0
	    /* check for null move */
	    if(move.from == 0 && move.to == 0) {
		if(IsInCheck(curr_pos, curr_pos->side)) {
		    DisplayBoard(curr_pos, "null move not possible");
		    exit(1);
		}
		if(curr_pos->side == BLACK)
		   curr_pos->full_move++;
		curr_pos->side = OtherSide(curr_pos->side);
	    }
	    else {
#endif
            if (Verbose > 6) {
                MyPrintf("move from: %d to: %d flag: %d pieve_moved: %d\n",
                         move.from, move.to, move.flag, move.piece_moved);
            }
            MakeMove(curr_pos, &move);
#if 0
	    }
#endif

            curr_pos->score = UNKNOWN;
            curr_pos->zz_type = UNKNOWN;

            if (InsertComments || AddEGTBComments ||
                PrintBadlyPlayedPositions) {
                LookupScoreInDatabase(curr_pos);
            }

            if (PositionDatabase) {
                if (AnnotateVariations || RAV_level == 0) {
                    ProcessPosition(curr_pos, false, false, false);
                }
            }

            bool is_right_number_of_pieces =
                (curr_pos->num_pieces >= MinimumNumberOfPieces) &&
                (curr_pos->num_pieces <= MaximumNumberOfPieces);

            if (!CheckSyntaxOnly && !PositionDatabase &&
                !PrintBadlyPlayedPositions) {
                if (IsInCheck(curr_pos, curr_pos->side))
                    move.flag |= CHECK;
                char move_string[64];
                if (move.flag & CHECK) {
                    Move reply_list[MAX_MOVES];
                    int nreplies;
                    nreplies = GenLegalMoves(curr_pos, reply_list, true, true);
                    if (nreplies == 0)
                        move.flag |= MATE;
                }
                GetShortMoveString(&move, pseudo_move_list, npseudo_moves,
                                   move_string);
                OutputPGNStringCashed(move_string);
                if (InsertComments && is_right_number_of_pieces) {
                    if (curr_pos->zz_type != UNKNOWN &&
                        curr_pos->zz_type != NO_MZUG) {
                        OutputPGNCommentCashed("zz");
                    }
                }
            }

            FirstMove = false;

            if (CheckSyntaxOnly && (RAV_level == 0 || AnnotateVariations) &&
                is_right_number_of_pieces) {
                num_positions++;
                if (curr_pos->num_pieces <= 8) {
                    AddEndingToStats(curr_pos);
                }
            }

            if (Verbose > 5) {
                char comment[64];
                strncpy(comment, yylval->string_info, 64);
                sprintf(comment, "After move %s", yylval->string_info);
                DisplayBoard(curr_pos, comment);
            }

            symbol = GetNextToken(fin, yylval);

            while (symbol == CHECK_SYMBOL) {
                symbol = GetNextToken(fin, yylval);
            }

            while (symbol == COMMENT) {
                if (!CheckSyntaxOnly && !PositionDatabase &&
                    !PrintBadlyPlayedPositions)
                    OutputPGNCommentCashed(yylval->string_info);
                symbol = GetNextToken(fin, yylval);
            }

            while (symbol == NAG) {
                if (Verbose > 4) {
                    MyPrintf("\n");
                    for (int i = 0; i < 3 * RAV_level; i++)
                        MyPrintf(" ");
                    MyPrintf("Saw NAG: %s\n", yylval->string_info);
                }
                if (!CheckSyntaxOnly && !PositionDatabase &&
                    !PrintBadlyPlayedPositions) {
                    OutputPGNStringCashed(yylval->string_info);
                }
                symbol = GetNextToken(fin, yylval);
            }

            while (symbol == COMMENT) {
                if (!CheckSyntaxOnly && !PositionDatabase &&
                    !PrintBadlyPlayedPositions)
                    OutputPGNCommentCashed(yylval->string_info);
                symbol = GetNextToken(fin, yylval);
            }

            bool is_prior_right_number_of_pieces =
                (prev_pos->num_pieces >= MinimumNumberOfPieces) &&
                (prev_pos->num_pieces <= MaximumNumberOfPieces);

            if ((InsertComments || AddEGTBComments ||
                 PrintBadlyPlayedPositions) &&
                (RAV_level == 0 || AnnotateVariations)) {
                bool phase_change = false;
                if (memcmp(curr_pos->piece_type_count,
                           prev_pos->piece_type_count,
                           sizeof(curr_pos->piece_type_count)))
                    phase_change = true;
                int move_score = EvaluateMove(prev_pos->score, curr_pos->score,
                                              phase_change);
                if ((phase_change || !EGTBWritten) &&
                    curr_pos->score != UNKNOWN) {
                    if (InsertComments && is_right_number_of_pieces) {
                        char score_string[16];
                        ScoreToString(curr_pos->score, score_string);
                        char comment[64];
                        if (curr_pos->promos != QRBN_PROMOTIONS &&
                            curr_pos->promos != 0) {
                            char promos[5];
                            int n = 0;
                            if (curr_pos->promos & (1 << (QUEEN)))
                                promos[n++] = 'q';
                            if (curr_pos->promos & (1 << (ROOK)))
                                promos[n++] = 'r';
                            if (curr_pos->promos & (1 << (BISHOP)))
                                promos[n++] = 'b';
                            if (curr_pos->promos & (1 << (KNIGHT)))
                                promos[n++] = 'n';
                            promos[n] = '\0';
                            sprintf(comment, "%s_%s_%d (%s)", Annotator, promos,
                                    curr_pos->num_pieces, score_string);
                        } else
                            sprintf(comment, "%s_%d (%s)", Annotator,
                                    curr_pos->num_pieces, score_string);
                        OutputPGNCommentCashed(comment);
                        EGTBWritten = true;
                        ContainsEGTBEvaluation = true;
                    }
                }
                if (IsResultChangingMove(move_score) ||
                    (DepthDelta > 0 && abs(move_score) >= DepthDelta)) {
                    if (InsertComments && is_prior_right_number_of_pieces) {
                        char move_string[16];
                        MoveScoreToString(move_score, move_string);
                        char score_string[16];
                        ScoreToString(curr_pos->score, score_string);
                        char comment[64];
                        if (curr_pos->promos != QRBN_PROMOTIONS &&
                            curr_pos->promos != 0) {
                            char promos[5];
                            int n = 0;
                            if (curr_pos->promos & (1 << (QUEEN)))
                                promos[n++] = 'q';
                            if (curr_pos->promos & (1 << (ROOK)))
                                promos[n++] = 'r';
                            if (curr_pos->promos & (1 << (BISHOP)))
                                promos[n++] = 'b';
                            if (curr_pos->promos & (1 << (KNIGHT)))
                                promos[n++] = 'n';
                            promos[n] = '\0';
                            sprintf(comment, "%s_%s_%d %s (%s)", Annotator,
                                    promos, prev_pos->num_pieces, move_string,
                                    score_string);
                        } else
                            sprintf(comment, "%s_%d %s (%s)", Annotator,
                                    prev_pos->num_pieces, move_string,
                                    score_string);
                        OutputPGNCommentCashed(comment);
                    }
                    if (PrintBadlyPlayedPositions)
                        PrintEPD(curr_pos, NULL);
                    if (RAV_level == 0 && is_prior_right_number_of_pieces) {
                        if (IsResultChangingMove(move_score)) {
                            if (prev_pos->num_pieces > MaxBadEnding)
                                MaxBadEnding = prev_pos->num_pieces;
                            if (NumBadMovesPGN < MAX_GAME_MOVES) {
                                ScoreListPGN[NumBadMovesPGN].score = move_score;
                                ScoreListPGN[NumBadMovesPGN].move_no =
                                    prev_pos->full_move;
                                ScoreListPGN[NumBadMovesPGN].side =
                                    prev_pos->side;
                                NumBadMovesPGN++;
                            }
                        } else {
                            if (NumDeltaMovesPGN < MAX_GAME_MOVES) {
                                ScoreListDeltaPGN[NumDeltaMovesPGN].score =
                                    move_score;
                                ScoreListDeltaPGN[NumDeltaMovesPGN].move_no =
                                    prev_pos->full_move;
                                ScoreListDeltaPGN[NumDeltaMovesPGN].side =
                                    prev_pos->side;
                                NumDeltaMovesPGN++;
                            }
                        }
                    } else {
                        if (IsResultChangingMove(move_score) &&
                            is_prior_right_number_of_pieces) {
                            if (prev_pos->num_pieces > MaxBadVariation) {
                                MaxBadVariation = prev_pos->num_pieces;
                            }
                        }
                    }
                }
            }

            while (symbol == RAV_START) {
                FirstMove = true;
                PrevEGTBWritten = EGTBWritten;
                EGTBWritten = false;

                RAV_level++;

                symbol = GetNextToken(fin, yylval);

                if (!CheckSyntaxOnly && !PositionDatabase &&
                    !PrintBadlyPlayedPositions)
                    OutputPGNStringCashed("(");

                while (symbol == COMMENT) {
                    if (!CheckSyntaxOnly && !PositionDatabase &&
                        !PrintBadlyPlayedPositions)
                        OutputPGNCommentCashed(yylval->string_info);
                    symbol = GetNextToken(fin, yylval);
                }

                if (symbol == RAV_END) {
                    if (!CheckSyntaxOnly && !PositionDatabase &&
                        !PrintBadlyPlayedPositions)
                        OutputPGNStringCashed(")");
                    symbol = GetNextToken(fin, yylval);
                    RAV_level--;
                    EGTBWritten = PrevEGTBWritten;
                    continue;
                }

                if (symbol == MOVE || symbol == MOVE_NUMBER) {
                    BOARD new_prev_pos, new_curr_pos;
                    if (Verbose > 4) {
                        MyPrintf("\n");
                        for (int i = 0; i < 3 * RAV_level; i++)
                            MyPrintf(" ");
                        MyPrintf("Saw start of variation\n");
                    }

                    memcpy(&new_curr_pos, prev_pos, sizeof(BOARD));

                    if (RAV_level > max_rav_level) {
                        max_rav_level = RAV_level;
                        max_rav_game = num_games;
                    }

                    symbol = ParseMoveList(symbol, fin, yylval, &new_prev_pos,
                                           &new_curr_pos);
                } else {
                    fprintf(stderr,
                            "Variation not followed by move in game %u\n",
                            num_games);
                    FlushPGNOutputCashed();
                    exit(1);
                }

                while (symbol != RAV_END) {
                    fprintf(
                        stderr,
                        "Unexpected  %s in input, expected end of variation\n",
                        yylval->string_info);
                    symbol = GetNextToken(fin, yylval);
                    FlushPGNOutputCashed();
                }

                if (symbol != RAV_END) {
                    fprintf(stderr, "Unmatched variation in game %u\n",
                            num_games);
                    FlushPGNOutputCashed();
                    exit(1);
                } else {
                    FirstMove = true;
                    EGTBWritten = PrevEGTBWritten;
                    if (!CheckSyntaxOnly && !PositionDatabase &&
                        !PrintBadlyPlayedPositions)
                        OutputPGNStringCashed(")");
                }

                RAV_level--;
                symbol = GetNextToken(fin, yylval);
                while (symbol == COMMENT) {
                    if (!CheckSyntaxOnly && !PositionDatabase &&
                        !PrintBadlyPlayedPositions)
                        OutputPGNCommentCashed(yylval->string_info);
                    symbol = GetNextToken(fin, yylval);
                }
            }
        }
    }
    return symbol;
}

PGNToken ParseGame(PGNToken symbol, FILE *fin, ParseType *yylval,
                   BOARD *curr_pos, bool best_capture) {
    BOARD start_pos, prev_pos;
    int legal;

    RAV_level = 0;
    EGTBWritten = false;
    ContainsEGTBEvaluation = false;

#if 0
    if(!CheckSyntaxOnly && !PositionDatabase)
#endif
    InitCashedPGNOutput();

    legal =
        ReadPosition("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
                     curr_pos, NULL);

    if (legal == NEUTRAL) {
        fprintf(stderr, "Could not initialize starting position\n");
        exit(1);
    }

    memcpy(&start_pos, curr_pos, sizeof(start_pos));

    king_orig_col = KING_ORIG_COL_TRADITIONAL;
    crook_orig_col = CROOK_ORIG_COL_TRADITIONAL;
    grook_orig_col = GROOK_ORIG_COL_TRADITIONAL;

    Chess960Game = false;

    symbol = ParseGamePrefix(symbol, fin, yylval);
    symbol = ParseTagList(symbol, fin, yylval, curr_pos);

    if (Chess960Game && !Chess960) {
        if (Verbose > 1)
            MyPrintf("Skipping Chess 960 game %d\n", num_games);
        symbol = SkipToNextGame(symbol, fin, yylval);
        return symbol;
    }

    if (Verbose > 3)
        MyPrintf("Line number for pre-game score comments: %d\n",
                 NumOutputLines);

    bool is_right_number_of_pieces =
        (curr_pos->num_pieces >= MinimumNumberOfPieces) &&
        (curr_pos->num_pieces <= MaximumNumberOfPieces);

    if (CheckSyntaxOnly) {
        if (is_right_number_of_pieces) {
            num_positions++;
        }
    }

    curr_pos->score = UNKNOWN;
    curr_pos->zz_type = UNKNOWN;

    if (InsertComments || AddEGTBComments || PrintBadlyPlayedPositions) {
        LookupScoreInDatabase(curr_pos);
    }

    if (PositionDatabase) {
        ProcessPosition(curr_pos, false, best_capture, false);
    }

    if (Verbose > 3) {
        char comment[64];
        sprintf(comment, "Board after Tags, game %u", num_games);
        DisplayBoard(curr_pos, comment);
    }

    FirstMove = true;

    if (InsertComments && is_right_number_of_pieces) {
        if (curr_pos->zz_type != UNKNOWN && curr_pos->zz_type != NO_MZUG) {
            OutputPGNCommentCashed("zz");
        }
        // always evaluate starting position if possible
        if (curr_pos->score != UNKNOWN) {
            char score_string[16];
            ScoreToString(curr_pos->score, score_string);
            char comment[64];
            if (curr_pos->promos != QRBN_PROMOTIONS && curr_pos->promos != 0) {
                char promos[5];
                int n = 0;
                if (curr_pos->promos & (1 << (QUEEN)))
                    promos[n++] = 'q';
                if (curr_pos->promos & (1 << (ROOK)))
                    promos[n++] = 'r';
                if (curr_pos->promos & (1 << (BISHOP)))
                    promos[n++] = 'b';
                if (curr_pos->promos & (1 << (KNIGHT)))
                    promos[n++] = 'n';
                promos[n] = '\0';
                sprintf(comment, "%s_%s_%d (%s)", Annotator, promos,
                        curr_pos->num_pieces, score_string);
            } else
                sprintf(comment, "%s_%d (%s)", Annotator, curr_pos->num_pieces,
                        score_string);
            OutputPGNCommentCashed(comment);
            EGTBWritten = true;
            ContainsEGTBEvaluation = true;
        }
    }

    symbol = ParseMoveList(symbol, fin, yylval, &prev_pos, curr_pos);

    if (symbol == TERMINATING_RESULT) {
        if (Verbose > 1) {
            MyPrintf("\nSaw terminating result %s, game %u\n",
                     yylval->string_info, num_games);
        }
        is_right_number_of_pieces =
            (curr_pos->num_pieces >= MinimumNumberOfPieces) &&
            (curr_pos->num_pieces <= MaximumNumberOfPieces);
        if ((InsertComments || AddEGTBComments) && is_right_number_of_pieces) {
            int sip1 = UNKNOWN;
            if (curr_pos->result == DRAW)
                sip1 = DRAW;
            else if (curr_pos->result == WON) {
                if (curr_pos->side == BLACK)
                    sip1 = WON;
                else
                    sip1 = LOST;
            } else if (curr_pos->result == LOST) {
                if (curr_pos->side == WHITE)
                    sip1 = WON;
                else
                    sip1 = LOST;
            }
            int move_score = EvaluateMove(curr_pos->score, sip1, false);
            if (IsResultChangingMove(move_score)) {
                if (InsertComments) {
                    char move_string[16];
                    MoveScoreToString(move_score, move_string);
                    char comment[64];
                    if (curr_pos->promos != QRBN_PROMOTIONS &&
                        curr_pos->promos != 0) {
                        char promos[5];
                        int n = 0;
                        if (curr_pos->promos & (1 << (QUEEN)))
                            promos[n++] = 'q';
                        if (curr_pos->promos & (1 << (ROOK)))
                            promos[n++] = 'r';
                        if (curr_pos->promos & (1 << (BISHOP)))
                            promos[n++] = 'b';
                        if (curr_pos->promos & (1 << (KNIGHT)))
                            promos[n++] = 'n';
                        promos[n] = '\0';
                        sprintf(comment, "%s_%s_%d (%s)", Annotator, promos,
                                curr_pos->num_pieces, move_string);
                    } else
                        sprintf(comment, "%s_%d (%s)", Annotator,
                                curr_pos->num_pieces, move_string);
                    OutputPGNCommentCashed(comment);
                }
                if (NumBadMovesPGN < MAX_GAME_MOVES) {
                    ScoreListPGN[NumBadMovesPGN].move_no = 999;
                    ScoreListPGN[NumBadMovesPGN].score = move_score;
                    NumBadMovesPGN++;
                }
            }
        }
        if (!CheckSyntaxOnly && !PositionDatabase &&
            !PrintBadlyPlayedPositions) {
            OutputPGNStringCashed(yylval->string_info);
            FlushPGNOutputCashed();
        }
    }

    if (!CheckSyntaxOnly && !PositionDatabase && !PrintBadlyPlayedPositions &&
        (ContainsEGTBEvaluation || !TBGamesOnly))
        OutputCashedPGN();

    return symbol;
}

static void Usage(char *prog_name, bool show_version) {
    if (show_version) {
        MyPrintf("%s version %s"
#if defined(USE_64_BIT)
                 ", using 64 bit indices"
#endif
#if defined(NO_DOUBLE_PAWN_MOVES)
                 ", pawns can't make a double step"
#endif
#if (NROWS != 8 || NCOLS != 8)
                 "\nUsing %dx%d board\n"
#endif
                 "\n(C) 2019-2024 Marc Bourzutschky\n\n",
                 prog_name, Version
#if (NROWS != 8) || (NCOLS != 8)
                 ,
                 NCOLS, NROWS
#endif
        );
    }
    MyPrintf(
        "Usage: %s [options] [pos_file[%cn] | -f pos_string]\n"
        "\n"
        "Valid options:\n"
        " -r               - read positions from standard input\n"
        " -q               - interactively play through position\n"
        " -p[elprsSz!] [l] - show best line. For -pz, also label mzugs with "
        "{zz}:\n"
        "                    for !, use !! for only, and ! for best DTZ moves\n"
        "                    for l, read maximum line length from argument\n"
        "                    for p, use strict PGN format ($1 for !, $3 for "
        "!!)\n"
        "                    for r, mark repeats of the original position with "
        "switched sides with {rr}\n"
        "                    for e, use EG format ('S' for 'N', no space after "
        "'.')\n"
        "                    for s, stop list after transition to capture sub "
        "game (or pawn move with S)\n"
        " -a[v][options]     process PGN pos_file (for v parse variations as "
        "well). Valid options:\n"
        "     s            - check syntax only and provide positions stats\n"
        "     h[m-n]       - create position file (for later reading with -o)\n"
        "                    m-n number of pieces between m and n (default %d "
        "and %d)\n"
        "     [aioc]       - create PGN with comments, based on data from -o "
        "file\n"
        "                    for a, add annotator to PGN (either EGTB or from "
        "-t)\n"
        "                    for o, add position valuation summary to header "
        "from -o file\n"
        "                    for i, insert scores from db file from -o file\n"
        "                    for c, only include games with at least one TB "
        "position\n"
        "     e            - print EPD with incorrectly played positions in "
        "pos_file\n"
        " -h[sczu][m|y][r] - process position file\n"
        "                    for c, evaluate best captures only\n"
        "                    for r, identify possible cyclic zugzwang pairs\n"
        "                    for z, label zugzwangs\n"
        "                    for u, tag restricted promotions from -ul file\n"
        "                    for s, sort position list for best caching\n"
        "                    for m, convert YK indices to MB format\n"
        "                    for y, convert MB indices to YK format\n"
        " -d dirs          - search TBs in specified directories\n"
        "                    (directories are separated by ;)\n"
        " -yk              - Read YK files only\n"
        " -mb              - Read MB files only\n"
        " -o file          - read position evaluation from file for -ao "
        "option\n"
        " -oo[s] file      - extract bad moves from position file\n"
        "                    for oos, provide additional statistics\n"
        " -g[o] delta      - flag moves with depth differences >= delta\n"
        "                    for o, also add position summary to header in -ao "
        "option\n"
        " -m[p[u]]         - find best move (for p, output in PGN rather than "
        "EGTB format)\n"
        "                    for pu, only keep positions with unique moves\n"
        " -s               - evaluate for both white and black to move\n"
        " -l[[a] file]     - echo output to file (if -la append to file)\n"
        " -t annotator     - set annotator (for -aa option), default %s\n"
        " -e               - output in Bleicher format\n"
        " -ul file         - read endings with restricted promotions from "
        "file\n"
        " -u[x] promo      - allowed promotions (q, r, b, n, qn,...; default "
        "qrbn)\n"
        "                    ux: use all promotions for endings with < 8 "
        "pieces\n"
        " -n               - exclude en passant\n"
        " -c[f]            - include castling rights (will return unknown from "
        "egtb)\n"
        "                    (for cf, include Chess960 castling)\n"
        " -w n             - use output line width n (for -a and -p options), "
        "default %d\n"
        " -v[n]            - verbosity level, 0,1,2,..., default %d\n"
        "\n"
        "If %cn is given, only the n-th position is evaluated.\n"
        "Positions can be FEN, or of form wka1 bkc1 wqg3...\n"
        "\n",
        prog_name, SEPARATOR[0], MinimumNumberOfPieces, MaximumNumberOfPieces,
        Annotator, LineWidth, Verbose, SEPARATOR[0]);
    fflush(stdout);
}

/*
 * append paths given in TbDirs to existing paths
 */
static int InitPaths() {
    char path[1024], *pptr;
    int num_paths = NumPaths, i;

    /* store each path separately for later reference */

    pptr = TbDirs;

    for (;;) {
        for (i = 0; pptr[i] != '\0' && pptr[i] != ',' && pptr[i] != ';'
#if !defined(_WIN32) && !defined(_WIN64) && !defined(__MWERKS__)
                    && pptr[i] != ':'
#endif
             ;
             i++) {
            path[i] = pptr[i];
        }
        path[i] = '\0';

        /* check whether we already have this path */

        bool found = false;

        for (int j = 0; j < num_paths - 1; j++) {
            if (!strcmp(path, TbPaths[j])) {
                found = true;
                break;
            }
        }

        if (!found && strlen(path) > 0 && num_paths < MAX_PATHS) {
            strcpy(TbPaths[num_paths++], path);
        }

        pptr += i;
        if (*pptr == '\0')
            break;
        pptr++;
    }

    /* have at least one path */

    if (num_paths == 0) {
        TbPaths[0][0] = '.';
        TbPaths[0][1] = '\0';
        num_paths = 1;
    }

    return num_paths;
}

int main(int argc, char *argv[]) {
    FILE *fin;
    time_t tm_start, tm_finish;
    unsigned npos, pos_count, num_studies = 0;
    BOARD Board;
    int side;
    bool best_move = false, other_side = false, single_pos = false;
    bool best_line = false, mark_mzugs = false, mark_squeeze = false,
         query = false;
    bool mark_best = false, SinglePos = false;
    bool best_capture = false;
    bool process_positions = false;
    bool convert_from_mb_to_yk = false;
    bool convert_from_yk_to_mb = false;
    bool sort_list = false;
    bool comment_games = false;
    bool mark_cz_zugs = false;
    bool show_bad_games = false;
    bool extra_statistics = false;
    bool restricted_promotions = false;
    bool bleicher_format = false;
    char line[MAX_LINE + 1], *sptr, *pos_string;

#if defined(UCI)
#if (NROWS != 8) || (NCOLS != 8)
#error UCI engine only works on 8x8 board
#endif

    IgnoreCastle = false;
    Verbose = 0;

    int MultiPV = 1;
    bool first_call = true;
    char *engine = "YKMB";
    char *white_space = " \t\r\n";
    char *StartFEN = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
    setbuf(stdout, NULL);
    side = ReadPosition(StartFEN, &Board, NULL);
    assert(side != NEUTRAL);

    InitTransforms();
    InitMoves();
    InitParity();
    InitPieceStrengths();

#if defined(UCI_LOG)
    FILE *fout = fopen("f:\\baseykmb\\new_mb\\uci_ykmb.txt", "w");
#endif

    for (;;) {
        if (fgets(line, MAX_LINE, stdin) == NULL) {
            break;
        }
#if defined(UCI_LOG)
        fprintf(fout, "Saw command %s\n", line);
#endif
        char *token, *rest;
        token = strtok_r(line, white_space, &rest);
#ifndef NDEBUG
        printf("Saw token %s\n", token);
#endif
        if (strcmp(token, "quit") == 0)
            break;
        else if (strcmp(token, "uci") == 0) {
            printf("id name %s\n", engine);
            printf("id author Marc Bourzutschky\n");
            printf("option name MultiPV type spin default 1 min 1 max 500\n");
            printf("option name NalimovPath type string default <empty>\n");
            printf("option name GaviotaTbPath type string default <empty>\n");
            printf("option name SyzygyPath type string default <empty>\n");
            printf("uciok\n");
        } else if (strcmp(token, "isready") == 0)
            printf("readyok\n");
        else if (strcmp(token, "setoption") == 0) {
            // eat "name" token
            char *name = strtok_r(NULL, white_space, &rest);
            if (name == NULL) {
#ifndef NDEBUG
                printf("Could not read option\n");
#endif
                continue;
            }
            char *option = strtok_r(NULL, white_space, &rest);
            if (option == NULL) {
#ifndef NDEBUG
                printf("Could not read option\n");
#endif
                continue;
            }
            // eat "value" token
            char *value = strtok_r(NULL, white_space, &rest);
            if (value == NULL) {
#ifndef NDEBUG
                printf("Could not read option value\n");
#endif
                continue;
            }
            value = strtok_r(NULL, white_space, &rest);
            if (value == NULL) {
#ifndef NDEBUG
                printf("Could not read option value\n");
#endif
                continue;
            }
            if (strcmp(option, "MultiPV") == 0) {
                if (sscanf(value, "%d", &MultiPV) != 1) {
#ifndef NDEBUG
                    printf("Could not read option value for MultiPV\n");
#endif
                    continue;
                }
                if (MultiPV < 1)
                    MultiPV = 1;
#ifndef NDEBUG
                printf("Setting MultiPV to %d\n", MultiPV);
#endif
            } else if (strcmp(option, "NalimovPath") == 0) {
                TbDirs = value;
#if defined(UCI_LOG)
                fprintf(fout, "Adding Nalimov from %s to %d existing paths\n",
                        TbDirs, NumPaths);
#endif
                NumPaths = InitPaths();
#if defined(UCI_LOG)
                for (int i = 0; i < NumPaths; i++) {
                    fprintf(fout, "path %d: %s\n", i + 1, TbPaths[i]);
                }
#endif
#ifndef NDEBUG
                printf("Initializing tb paths to %s, number of paths %d\n",
                       value, NumPaths);
#endif
            } else if (strcmp(option, "GaviotaTbPath") == 0) {
                TbDirs = value;
#if defined(UCI_LOG)
                fprintf(fout, "Adding Gaviota from %s to %d existing paths\n",
                        TbDirs, NumPaths);
#endif
                NumPaths = InitPaths();
#if defined(UCI_LOG)
                for (int i = 0; i < NumPaths; i++) {
                    fprintf(fout, "path %d: %s\n", i + 1, TbPaths[i]);
                }
#endif
#ifndef NDEBUG
                printf("Initializing tb paths to %s, number of paths %d\n",
                       value, NumPaths);
#endif
            } else if (strcmp(option, "SyzygyPath") == 0) {
                TbDirs = value;
#if defined(UCI_LOG)
                fprintf(fout, "Adding Syzygy from %s to %d existing paths\n",
                        TbDirs, NumPaths);
#endif
                NumPaths = InitPaths();
#if defined(UCI_LOG)
                for (int i = 0; i < NumPaths; i++) {
                    fprintf(fout, "path %d: %s\n", i + 1, TbPaths[i]);
                }
#endif
#ifndef NDEBUG
                printf("Initializing tb paths to %s, number of paths %d\n",
                       value, NumPaths);
#endif
            } else {
#ifndef NDEBUG
                printf("unsupported option %s\n", option);
#endif
                continue;
            }
        } else if (strcmp(token, "position") == 0) {
            char *pos = strtok_r(NULL, white_space, &rest);
            if (pos == NULL) {
#ifndef NDEBUG
                printf("Could not read position\n");
#endif
                continue;
            }
            if (strcmp(pos, "startpos") == 0) {
                ReadPosition(StartFEN, &Board, NULL);
            } else if (strcmp(pos, "fen") == 0) {
                char fen_string[1024];
                pos = strtok_r(NULL, white_space, &rest);
                if (pos == NULL) {
#ifndef NDEBUG
                    printf("Could not read first element of FEN\n");
#endif
                    continue;
                }
                sprintf(fen_string, "%s ", pos);
                pos = strtok_r(NULL, white_space, &rest);
                if (pos == NULL) {
#ifndef NDEBUG
                    printf("Could not read second element of FEN\n");
#endif
                    continue;
                }
                strcat(fen_string, pos);
                strcat(fen_string, " ");
                pos = strtok_r(NULL, white_space, &rest);
                if (pos == NULL) {
#ifndef NDEBUG
                    printf("Could not read third element of FEN\n");
#endif
                    continue;
                }
                strcat(fen_string, pos);
                strcat(fen_string, " ");
                pos = strtok_r(NULL, white_space, &rest);
                if (pos == NULL) {
#ifndef NDEBUG
                    printf("Could not read fourth element of FEN\n");
#endif
                    continue;
                }
                strcat(fen_string, pos);
                strcat(fen_string, " ");
                pos = strtok_r(NULL, white_space, &rest);
                if (pos == NULL) {
#ifndef NDEBUG
                    printf("Could not read fifth element of FEN\n");
#endif
                    continue;
                }
                strcat(fen_string, pos);
                strcat(fen_string, " ");
                pos = strtok_r(NULL, white_space, &rest);
                if (pos == NULL) {
#ifndef NDEBUG
                    printf("Could not read sixth element of FEN\n");
#endif
                    continue;
                }
                strcat(fen_string, pos);
                BOARD tmp_board;
#if defined(UCI_LOG)
                fprintf(fout, "Parsing FEN string %s\n", fen_string);
#endif
                side = ReadPosition(fen_string, &tmp_board, NULL);

                if (side == NEUTRAL) {
#ifndef NDEBUG
                    printf("Could not parse FEN string %s\n", fen_string);
#endif
                    continue;
                }
                memcpy(&Board, &tmp_board, sizeof(Board));
            } else {
#ifndef NDEBUG
                printf("Invalid position position specifier %s\n", pos);
#endif
                continue;
            }

            char *move = strtok_r(NULL, white_space, &rest);
            if (move == NULL)
                continue;
            if (strcmp(move, "moves") == 0) {
                move = strtok_r(NULL, white_space, &rest);
                while (move != NULL) {
#if defined(UCI_LOG)
                    fprintf(fout, "applying move %s\n", move);
#endif
                    MakeMoveUCI(&Board, move);
                    move = strtok_r(NULL, white_space, &rest);
                }
            }
            char fen_string[512];
            BoardToFEN(&Board, fen_string);
#if defined(UCI_LOG)
            fprintf(fout, "Final FEN: %s\n", fen_string);
#endif
        } else if (strcmp(token, "d") == 0) {
            DisplayBoard(&Board, NULL);
            INDEX_DATA index;
            memset(&index, 0, sizeof(index));
            int score = ScorePosition(&Board, &index);
            if (score != UNKNOWN) {
                char score_str[16];
                if (score == DRAW || score == STALE_MATE)
                    sprintf(score_str, "draw");
                else if (score == WON)
                    sprintf(score_str, "won");
                else if (score == LOST)
                    sprintf(score_str, "lost");
                else if (score == NOT_LOST)
                    sprintf(score_str, "not lost");
                else if (score == NOT_WON)
                    sprintf(score_str, "not won");
                else
                    sprintf(score_str, "%+d", score);
                printf("Position in YKMB database, kk_index=%d "
                       "offset= " DEC_ZINDEX_FORMAT " score=%s\n",
                       index.kk_index, index.index, score_str);
            }
        } else if (strcmp(token, "go") == 0) {
            if (first_call) {
                InitCaches();
                first_call = false;
            }
#if defined(UCI_LOG)
            fprintf(fout, "Received go command\n");
#endif

            char fen_string[512];
            BoardToFEN(&Board, fen_string);
#if defined(UCI_LOG)
            fprintf(fout, "FEN input for go: %s\n", fen_string);
#endif
            Move move_list[MAX_MOVES];
            INDEX_DATA index;

            DBHits = 0;
            CacheHits = 0;
            unsigned long time_taken = clock();

            int score = ScorePosition(&Board, &index);

#if defined(UCI_LOG)
            fprintf(fout, "Score Position: %d\n", score);
#endif

            bool in_check = IsInCheck(&Board, Board.side);

            bool flipped = false;

            if (Board.side == WHITE &&
                (score == NOT_LOST || score <= 0 || score == LOST))
                flipped = true;

            if (Board.side == BLACK && (score == NOT_WON || score == WON ||
                                        (score > 0 && score != DRAW &&
                                         score != NOT_LOST && score != LOST)))
                flipped = true;

            if (flipped) {
                FlipBoard(&Board);
            }

            int nmoves = GenMoveScores(&Board, move_list, true, true);

            if (flipped) {
                FlipBoard(&Board);
                for (int i = 0; i < nmoves; i++)
                    FlipMove(&move_list[i]);
            }

            if (nmoves == 0) {
                if (in_check) {
                    printf("info depth 0 mate 0\n");
                } else {
                    printf("info depth 0 cp 0\n");
                }
                printf("bestmove (none)\n");
                continue;
            }

            for (int i = 0; i < nmoves; i++) {
                Move reply_list[MAX_MOVES];
                int nreplies;
                bool in_check2;
                move_list[i].score = RetrogradeResult(move_list[i].score);
                if ((move_list[i].flag & PROMOTION) &&
                    (abs(move_list[i].piece_promoted) == PAWN))
                    continue;
                MakeMove(&Board, &move_list[i]);
                nreplies = GenLegalMoves(&Board, reply_list, true, true);
                in_check2 = IsInCheck(&Board, Board.side);
                UnMakeMove(&Board, &move_list[i]);
                if (nreplies == 0) {
                    if (in_check2) {
                        move_list[i].score = CHECK_MATE;
                        move_list[i].flag |= MATE;
                    } else {
                        move_list[i].score = DRAW;
                    }
                } else {
                    if ((move_list[i].flag & (CAPTURE | PROMOTION)) &&
                        IsWinningScore(move_list[i].score))
                        move_list[i].score = 1;
                }
            }

            qsort(move_list, nmoves, sizeof(move_list[0]), MoveCompare);

            time_taken = 1000 * (clock() - time_taken) / CLOCKS_PER_SEC;
            ULONG nodes = CacheHits + DBHits;
            ULONG nps = nodes / max(1, time_taken / 1000);

            char move[6];

            for (int i = 0; i < min(MultiPV, nmoves); i++) {
                MoveToUCI(&move_list[i], move);
                char score_str[32];
                if (move_list[i].score == DRAW)
                    sprintf(score_str, "cp 0");
                else if (move_list[i].score == NOT_WON)
                    sprintf(score_str, "cp 0 upperbound");
                else if (move_list[i].score == NOT_LOST)
                    sprintf(score_str, "cp 0 lowerbound");
                else if (move_list[i].score == WON)
                    sprintf(score_str, "cp 10000");
                else if (move_list[i].score == LOST)
                    sprintf(score_str, "cp -10000");
                else if (move_list[i].score == UNKNOWN ||
                         move_list[i].score == UNRESOLVED)
                    sprintf(score_str, "cp -9999");
                else {
                    if (move_list[i].score == CHECK_MATE)
                        sprintf(score_str, "mate 1");
                    else if (move_list[i].score == 0)
                        sprintf(score_str, "cp 25000");
                    else {
                        sprintf(score_str, "mate %d", move_list[i].score);
                    }
                }
#if defined(UCI_LOG)
                fprintf(fout,
                        "info depth 1 seldepth 1 multipv %d time %d score %s "
                        "nodes %lu nps %lu tbhits %lu pv %s\n",
                        i + 1, time_taken, score_str, nodes, nps, DBHits, move);
#endif

                printf("info depth 1 seldepth 1 multipv %d time %d score %s "
                       "nodes %lu npos %lu tbhits %lu pv %s\n",
                       i + 1, time_taken, score_str, nodes, nps, DBHits, move);
            }
            MoveToUCI(&move_list[0], move);
            printf("bestmove %s\n", move);
#if defined(UCI_LOG)
            fprintf(fout, "bestmove %s\n", move);
#endif
        }
    }
#if defined(UCI_LOG)
    fclose(fout);
#endif

#else // not UCI
    if (sizeof(INDEX) != 8) {
        MyPrintf("index must have 8 bytes\n");
        fflush(stdout);
        exit(1);
    }

    for (int iarg = 1; iarg < argc;) {
        if (argv[iarg][0] == '-') {
            switch (argv[iarg][1]) {
            case 'a':
            case 'A':
                comment_games = true;
                if (strchr(&(argv[iarg][2]), 'v') ||
                    strchr(&(argv[iarg][2]), 'V'))
                    AnnotateVariations = true;
                if (strchr(&(argv[iarg][2]), 's') ||
                    strchr(&(argv[iarg][2]), 'S'))
                    CheckSyntaxOnly = true;
                if (strchr(&(argv[iarg][2]), 'c') ||
                    strchr(&(argv[iarg][2]), 'C'))
                    best_capture = true;
                if (strchr(&(argv[iarg][2]), 'o') ||
                    strchr(&(argv[iarg][2]), 'O'))
                    AddEGTBComments = true;
                if (strchr(&(argv[iarg][2]), 'h') ||
                    strchr(&(argv[iarg][2]), 'H'))
                    PositionDatabase = true;
                if (strchr(&(argv[iarg][2]), 'i') ||
                    strchr(&(argv[iarg][2]), 'I'))
                    InsertComments = true;
                if (strchr(&(argv[iarg][2]), 'a') ||
                    strchr(&(argv[iarg][2]), 'A'))
                    AddAnnotator = true;
                if (strchr(&(argv[iarg][2]), 'e') ||
                    strchr(&(argv[iarg][2]), 'E'))
                    PrintBadlyPlayedPositions = true;
                if (strchr(&(argv[iarg][2]), 'c') ||
                    strchr(&(argv[iarg][2]), 'C'))
                    TBGamesOnly = true;
                sptr = strchr(&(argv[iarg][2]), '-');
                if (sptr != NULL) {
                    sptr--;
                    while (isdigit(*sptr)) {
                        sptr--;
                    }
                    sptr++;
                    if (sscanf(sptr, "%d", &MinimumNumberOfPieces) != 1) {
                        fprintf(stderr,
                                "Could not read minimum number of pieces\n");
                        exit(1);
                    }
                    sptr = strchr(sptr, '-');
                    if (sptr == NULL) {
                        fprintf(stderr, "Internal error parsing maximum number "
                                        "of pieces\n");
                        exit(1);
                    }
                    sptr++;
                    if (sscanf(sptr, "%d", &MaximumNumberOfPieces) != 1) {
                        fprintf(stderr,
                                "Could not read maximum number of pieces\n");
                        exit(1);
                    }
                }
                break;
            case 'm':
            case 'M':
                if ((argv[iarg][2] == 'b') || (argv[iarg][2] == 'B')) {
                    MBFormatOnly = true;
                } else {
                    best_move = true;
                    if (strchr(&(argv[iarg][2]), 'p') ||
                        strchr(&(argv[iarg][2]), 'P'))
                        OutputPGNFormat = true;
                    if (strchr(&(argv[iarg][2]), 'u') ||
                        strchr(&(argv[iarg][2]), 'U'))
                        UniqueMovesOnly = true;
                }
                break;
            case 's':
            case 'S':
                other_side = true;
                break;
            case 'y':
            case 'Y':
                if ((argv[iarg][2] == 'K') || (argv[iarg][2] == 'k')) {
                    YKFormatOnly = true;
                }
                break;
            case 'h':
            case 'H':
                process_positions = true;
                if (strchr(&(argv[iarg][2]), 'c') ||
                    strchr(&(argv[iarg][2]), 'C'))
                    best_capture = true;
                if (strchr(&(argv[iarg][2]), 's') ||
                    strchr(&(argv[iarg][2]), 'S'))
                    sort_list = true;
                if (strchr(&(argv[iarg][2]), 'z') ||
                    strchr(&(argv[iarg][2]), 'Z'))
                    mark_mzugs = true;
                if (strchr(&(argv[iarg][2]), 'r') ||
                    strchr(&(argv[iarg][2]), 'R'))
                    mark_cz_zugs = true;
                if (strchr(&(argv[iarg][2]), 'y') ||
                    strchr(&(argv[iarg][2]), 'Y'))
                    convert_from_mb_to_yk = true;
                if (strchr(&(argv[iarg][2]), 'm') ||
                    strchr(&(argv[iarg][2]), 'M'))
                    convert_from_yk_to_mb = true;
                if (strchr(&(argv[iarg][2]), 'u') ||
                    strchr(&(argv[iarg][2]), 'U'))
                    restricted_promotions = true;
                break;
            case 'e':
            case 'E':
                bleicher_format = true;
                break;
            case 'p':
            case 'P':
                best_line = true;
                if (strchr(&(argv[iarg][2]), 'z') ||
                    strchr(&(argv[iarg][2]), 'Z'))
                    mark_mzugs = true;
                if (strchr(&(argv[iarg][2]), 'r') ||
                    strchr(&(argv[iarg][2]), 'R'))
                    mark_squeeze = true;
                if (strchr(&(argv[iarg][2]), '!'))
                    mark_best = true;
                if (strchr(&(argv[iarg][2]), 'p') ||
                    strchr(&(argv[iarg][2]), 'P'))
                    StrictPGN = true;
                if (strchr(&(argv[iarg][2]), 'e')) {
                    EGFormat = true;
                    GermanKnight = true;
                }
                if (strchr(&(argv[iarg][2]), 'E'))
                    EGFormat = true;
                if (strchr(&(argv[iarg][2]), 's'))
                    StopAtTransition = true;
                if (strchr(&(argv[iarg][2]), 'S')) {
                    StopAtTransition = true;
                    StopAtPawnMove = true;
                }
                if (strchr(&(argv[iarg][2]), 'l') ||
                    strchr(&(argv[iarg][2]), 'L')) {
                    if (iarg + 1 < argc) {
                        if (sscanf(argv[iarg + 1], "%d", &MaxLineLength) != 1) {
                            fprintf(
                                stderr,
                                "Could not read maximum line length from %s\n",
                                argv[iarg + 1]);
                            goto error;
                        }
                        memmove(argv + iarg, argv + iarg + 1,
                                (argc - iarg) * sizeof(char *));
                        argc--;
                    } else {
                        fprintf(stderr,
                                "Need maximum length as additional argument "
                                "for %s\n",
                                argv[iarg]);
                        goto error;
                    }
                }
                break;
            case 'q':
            case 'Q':
                query = true;
                break;
            case 'r':
            case 'R':
                Dynamic = true;
                SinglePos = true;
                break;
            case 'n':
            case 'N':
                UseEnPassant = false;
                break;
            case 'c':
            case 'C':
                IgnoreCastle = false;
                if (strchr(&(argv[iarg][2]), 'f') ||
                    strchr(&(argv[iarg][2]), 'F'))
                    Chess960 = true;
                break;
            case 'l':
            case 'L':
                if (iarg + 1 < argc) {
                    flog =
                        fopen(argv[iarg + 1], argv[iarg][2] == 'a' ? "a" : "w");
                    if (flog == NULL) {
                        MyPrintf("Could not open log file %s\n",
                                 argv[iarg + 1]);
                        fflush(stdout);
                        exit(1);
                    }
                    memmove(argv + iarg, argv + iarg + 1,
                            (argc - iarg) * sizeof(char *));
                    argc--;
                    break;
                } else
                    goto error;
            case 'g':
            case 'G':
                if (iarg + 1 < argc) {
                    if (argv[iarg][2] == 'o' || argv[iarg][2] == 'O')
                        AddEGTBDepthComments = true;
                    sscanf(argv[iarg + 1], "%d", &DepthDelta);
                    memmove(argv + iarg, argv + iarg + 1,
                            (argc - iarg) * sizeof(char *));
                    argc--;
                    break;
                } else
                    goto error;
            case 'u':
            case 'U':
                if (argv[iarg][2] == 'l' || argv[iarg][2] == 'L') {
                    if (iarg + 1 < argc) {
                        FlagRestrictedPromotions = true;
                        RestrictedPromotionFile = argv[iarg + 1];
                        memmove(argv + iarg, argv + iarg + 1,
                                (argc - iarg) * sizeof(char *));
                        argc--;
                        break;
                    } else
                        goto error;
                } else if (iarg + 1 < argc) {
                    Promotions = 0;
                    for (int i = 0; i < strlen(argv[iarg + 1]); i++) {
                        int pi = GetPiece(argv[iarg + 1][i]);
                        Promotions |= (1 << pi);
                    }
                    if (Promotions != QRBN_PROMOTIONS)
                        SearchAllPromotions = false;
                    SearchSubgamePromotions = false;
                    if (strchr(argv[iarg], 'x') || strchr(argv[iarg], 'X'))
                        SearchSubgamePromotions = true;
                    memmove(argv + iarg, argv + iarg + 1,
                            (argc - iarg) * sizeof(char *));
                    argc--;
                    break;
                } else
                    goto error;
            case 'v':
            case 'V':
                sscanf(&argv[iarg][2], "%d", &Verbose);
                if (Verbose > 0)
                    SummaryStats = true;
                break;
            case 'w':
            case 'W':
                if (iarg + 1 < argc) {
                    sscanf(argv[iarg + 1], "%d", &LineWidth);
                    if (LineWidth > MAX_PGN_OUTPUT) {
                        fprintf(stderr, "Line length must be <= %d\n",
                                MAX_PGN_OUTPUT);
                        exit(1);
                    }
                    memmove(argv + iarg, argv + iarg + 1,
                            (argc - iarg) * sizeof(char *));
                    argc--;
                    break;
                } else
                    goto error;
            case 'd':
            case 'D':
                if (iarg + 1 < argc) {
                    TbDirs = argv[iarg + 1];
                    memmove(argv + iarg, argv + iarg + 1,
                            (argc - iarg) * sizeof(char *));
                    argc--;
                    break;
                } else
                    goto error;
            case 't':
            case 'T':
                if (iarg + 1 < argc) {
                    Annotator = argv[iarg + 1];
                    memmove(argv + iarg, argv + iarg + 1,
                            (argc - iarg) * sizeof(char *));
                    argc--;
                    break;
                } else
                    goto error;
            case 'o':
            case 'O':
                if (iarg + 1 < argc) {
                    if (argv[iarg][2] == 'o' || argv[iarg][2] == 'O') {
                        show_bad_games = true;
                        if (argv[iarg][3] == 's' || argv[iarg][3] == 'S') {
                            extra_statistics = true;
                        }
                    }
                    ScoreFile = argv[iarg + 1];
                    memmove(argv + iarg, argv + iarg + 1,
                            (argc - iarg) * sizeof(char *));
                    argc--;
                    break;
                } else
                    goto error;
            case 'f':
            case 'F':
                SinglePos = true;
                if (iarg + 1 < argc) {
                    pos_string = argv[iarg + 1];
                    memmove(argv + iarg, argv + iarg + 1,
                            (argc - iarg) * sizeof(char *));
                    argc--;
                    break;
                } else
                    goto error;
            case '?':
                Usage(argv[0], true);
                exit(0);
            default:
            error:
                MyPrintf("Illegal option '%s'\n", argv[iarg]);
                Usage(argv[0], false);
                exit(1);
            }
            memmove(argv + iarg, argv + iarg + 1,
                    (argc - iarg) * sizeof(char *));
            argc--;
        } else
            iarg++;
    }

    if (MBFormatOnly && YKFormatOnly) {
        MyPrintf("Cannot specify both YK and MB format only\n");
        exit(1);
    }

    if (bleicher_format) {
        if (query) {
            MyPrintf("Cannot use -q with Bleicher format\n");
            exit(1);
        }
        if (!SinglePos) {
            MyPrintf("Need to use -f option for Bleicher format\n");
            exit(1);
        }
    }

    if (!SinglePos && !show_bad_games) {
        if (argc != 2) {
            Usage(argv[0], true);
            exit(0);
        }
        pos_string = argv[1];
    }

    NumPaths = InitPaths();

#if !defined(NO_ZSTD)
    ZSTD_DecompressionContext = ZSTD_createDCtx();
#endif

    if (Verbose > 1) {
        MyPrintf("Checking %d paths:\n", NumPaths);
        for (int i = 0; i < NumPaths; i++) {
            MyPrintf("%s\n", TbPaths[i]);
        }
        MyPrintf("\n");
    }

    if (RestrictedPromotionFile != NULL) {
        FILE *fpromos;
        if ((fpromos = fopen(RestrictedPromotionFile, "r")) == NULL) {
            MyPrintf("Could not open resticed promotion file %s for reading\n",
                     RestrictedPromotionFile);
            MyFlush();
            exit(1);
        }

        // read endings with restricted promotions into list

        NumRestrictedPromotionEndings = 0;

        for (int i = 0; i < 2; i++) {
            int n = 0;
            while (fgets(line, MAX_LINE, fpromos) != NULL) {
                char ending[MAX_LINE], promos[MAX_LINE];
                if (sscanf(line, "%s %s", ending, promos) != 2)
                    continue;
                if (strlen(ending) > 16 || strlen(ending) < 2)
                    continue;
                int uprom = 0;
                for (int j = 0; j < strlen(promos); j++) {
                    if (promos[j] == 'q' || promos[j] == 'Q')
                        uprom |= (1 << (QUEEN));
                    else if (promos[j] == 'r' || promos[j] == 'R')
                        uprom |= (1 << (ROOK));
                    else if (promos[j] == 'b' || promos[j] == 'B')
                        uprom |= (1 << (BISHOP));
                    else if (promos[j] == 'n' || promos[j] == 'N')
                        uprom |= (1 << (KNIGHT));
                }
                if (uprom == 0 || uprom == QRBN_PROMOTIONS)
                    continue;

                if (i == 0) {
                    NumRestrictedPromotionEndings++;
                    continue;
                }

                if (n < NumRestrictedPromotionEndings) {
                    if (Verbose > 2) {
                        MyPrintf("Ending %s has promotions restriced to %s\n",
                                 ending, promos);
                        MyFlush();
                    }
                    strncpy(RestrictedPromotionList[n].ending, ending,
                            sizeof(RestrictedPromotionList[n].ending));
                    RestrictedPromotionList[n++].promos = uprom;
                }
            }

            if (i == 0) {
                if (NumRestrictedPromotionEndings == 0)
                    break;

                rewind(fpromos);
                if (NumRestrictedPromotionEndings > 0) {
                    RestrictedPromotionList = (RESTRICTED_PROMOTION *)MyMalloc(
                        NumRestrictedPromotionEndings *
                        sizeof(RESTRICTED_PROMOTION));
                }
            }
        }

        fclose(fpromos);

        if (NumRestrictedPromotionEndings > 0) {
            qsort(RestrictedPromotionList, NumRestrictedPromotionEndings,
                  sizeof(RESTRICTED_PROMOTION), restricted_promo_compar);
            int n_total = 1;
            for (int i = 1; i < NumRestrictedPromotionEndings; i++) {
                if (restricted_promo_compar(
                        &RestrictedPromotionList[i],
                        &RestrictedPromotionList[n_total - 1]) != 0) {
                    memcpy(&RestrictedPromotionList[n_total],
                           &RestrictedPromotionList[i],
                           sizeof(RestrictedPromotionList[0]));
                    n_total++;
                }
            }
            NumRestrictedPromotionEndings = n_total;
        }

        if (Verbose > 0) {
            MyPrintf("Read %d promotion restricted endings from %s\n",
                     NumRestrictedPromotionEndings, RestrictedPromotionFile);
            MyFlush();
        }
    }

    if (InsertComments || AddEGTBComments || PrintBadlyPlayedPositions) {
        FILE *fdb;
        NumDBPositions = 0;
        if (ScoreFile == NULL) {
            MyPrintf("Must provide score file with -o option\n");
            Usage(argv[0], true);
            exit(1);
        }
        if ((fdb = fopen(ScoreFile, "r")) == NULL) {
            MyPrintf("Could not open %s for reading\n", ScoreFile);
            MyFlush();
            exit(1);
        }

        /* find number of scores (skipping unknowns) and read them into list */

        for (int i = 0; i < 2; i++) {
            int nscores = 0;
            while (fgets(line, MAX_LINE, fdb) != NULL) {
                POSITION_DATA pos_data;
                int nwords = ReadPositionData(line, &pos_data);
                char ending[MAX_LINE];
                int kk_index, score = UNKNOWN, zz_type = UNKNOWN;
                ZINDEX offset;

                if (nwords < 8)
                    continue;

                score = pos_data.score;

                if (score == UNKNOWN)
                    continue;

                if (nwords == 9) {
                    zz_type = pos_data.zz_type;
                }

                if (i == 0) {
                    if (score != UNKNOWN)
                        NumDBPositions++;
                    continue;
                }

                if (nscores < NumDBPositions) {
                    strncpy(PositionDB[nscores].ending, pos_data.ending,
                            sizeof(PositionDB[nscores].ending));
                    PositionDB[nscores].kk_index = pos_data.kk_index;
                    PositionDB[nscores].offset = pos_data.offset;
                    PositionDB[nscores].side = pos_data.side;
                    PositionDB[nscores].promos = pos_data.promos;
                    PositionDB[nscores].score = score;
                    PositionDB[nscores++].zz_type = zz_type;
                }
            }

            if (i == 0) {
                if (NumDBPositions == 0)
                    break;

                rewind(fdb);
                if (NumDBPositions > 0) {
                    PositionDB = (POSITION_DB *)MyMalloc(NumDBPositions *
                                                         sizeof(POSITION_DB));
                }
            }
        }
        fclose(fdb);

        if (Verbose > 1) {
            MyPrintf("Read %d scores from %s\n", NumDBPositions, ScoreFile);
            MyFlush();
        }

        if (NumDBPositions > 0) {
            qsort(PositionDB, NumDBPositions, sizeof(POSITION_DB),
                  db_pos_compar);
            int n_total = 1;
            for (int i = 1; i < NumDBPositions; i++) {
                if (db_pos_compar(&PositionDB[i], &PositionDB[n_total - 1]) !=
                    0) {
                    memcpy(&PositionDB[n_total], &PositionDB[i],
                           sizeof(PositionDB[0]));
                    n_total++;
                } else {
                    int best_score = PositionDB[n_total - 1].score;
                    if (best_score == NOT_WON) {
                        if (PositionDB[i].score == DRAW ||
                            PositionDB[i].score <= 0)
                            best_score = PositionDB[i].score;
                    } else if (best_score == NOT_LOST) {
                        if (PositionDB[i].score > 0 &&
                            PositionDB[i].score != UNKNOWN &&
                            PositionDB[i].score != NOT_WON)
                            best_score = PositionDB[i].score;
                    }
                    if (best_score != PositionDB[n_total - 1].score) {
                        PositionDB[n_total - 1].score = best_score;
                    }
                }
            }
            NumDBPositions = n_total;
        }

        if (Verbose > 0) {
            MyPrintf("Read %d unique scores from %s\n", NumDBPositions, DBFile);
            MyFlush();
        }
    }

    if (show_bad_games) {
        FILE *fscore;
        int RawScores = 0;
        NumZZPositions = 0;
        if (ScoreFile == NULL) {
            MyPrintf("Must provide results file with -o option\n");
            Usage(argv[0], true);
            exit(1);
        }
        if ((fscore = fopen(ScoreFile, "r")) == NULL) {
            MyPrintf("Could not open %s for reading\n", ScoreFile);
            MyFlush();
            exit(1);
        }

        /* find number of scores (skipping unknowns and variations) and read
         * them into list */

        for (int i = 0; i < 2; i++) {
            int nscores = 0, n_zz_scores = 0;
            while (fgets(line, MAX_LINE, fscore) != NULL) {
                POSITION_DATA pos_data;
                int nwords = ReadPositionData(line, &pos_data);

                if (nwords < 8 || pos_data.move_no < 0)
                    continue;

                int score = pos_data.score;
                int zz_type = UNKNOWN;
                char cz_type = '0';
                if (nwords >= 9) {
                    zz_type = pos_data.zz_type;
                    if (nwords >= 9)
                        cz_type = pos_data.cz_type;
                }

                if (score == UNKNOWN &&
                    (zz_type == UNKNOWN || zz_type == NO_MZUG) &&
                    cz_type == '0')
                    continue;

                if (i == 0) {
                    if (score != UNKNOWN)
                        RawScores++;
                    if ((zz_type != UNKNOWN && zz_type != NO_MZUG) ||
                        cz_type != '0')
                        NumZZPositions++;
                    continue;
                }

                if (nscores < RawScores) {
                    ScoreList[nscores].ending[0] = 0;
                    if (strlen(pos_data.ending) <= MAX_PIECES_MB) {
                        NormalizeEnding(pos_data.ending);
                        strcpy(ScoreList[nscores].ending, pos_data.ending);
                    }
                    ScoreList[nscores].game_num = pos_data.game_num;
                    ScoreList[nscores].move_no = pos_data.move_no;
                    ScoreList[nscores].side = pos_data.side;
                    ScoreList[nscores].result = pos_data.result;
                    ScoreList[nscores].zz_type = pos_data.zz_type;
                    ScoreList[nscores].cz_type = pos_data.cz_type;
                    ScoreList[nscores++].score = score;
                }

                if (n_zz_scores < NumZZPositions) {
                    if ((zz_type != UNKNOWN && zz_type != NO_MZUG) ||
                        cz_type != '0') {
                        ZZList[n_zz_scores].game_num = pos_data.game_num;
                        ZZList[n_zz_scores].move_no = pos_data.move_no;
                        ZZList[n_zz_scores].side = pos_data.side;
                        ZZList[n_zz_scores].zz_type = zz_type;
                        ZZList[n_zz_scores++].cz_type = cz_type;
                    }
                }
            }

            if (i == 0) {
                if (RawScores == 0 && NumZZPositions == 0)
                    break;

                rewind(fscore);
                if (RawScores > 0) {
                    ScoreList =
                        (SCORE *)MyMalloc((RawScores + 1) * sizeof(SCORE));
                    if (DepthDelta > 0)
                        ScoreListDelta =
                            (SCORE *)MyMalloc((RawScores + 1) * sizeof(SCORE));
                }
                if (NumZZPositions > 0) {
                    ZZList =
                        (SCORE *)MyMalloc((NumZZPositions + 1) * sizeof(SCORE));
                }
            }
        }
        fclose(fscore);

        if (Verbose > 0) {
            MyPrintf("Read %d scores and %d zz's from %s\n", RawScores,
                     NumZZPositions, ScoreFile);
            MyFlush();
        }

        ENDING_LIST *ending_list = NULL;
        int num_distinct_endings = 0;

        if (RawScores > 0) {
            if (extra_statistics) {
                qsort(ScoreList, RawScores, sizeof(SCORE), ending_compar);
                num_distinct_endings = 1;
                for (int i = 0; i < RawScores - 1; i++) {
                    if (strcmp(ScoreList[i].ending, ScoreList[i + 1].ending)) {
                        num_distinct_endings++;
                    }
                }
                if ((ending_list = (ENDING_LIST *)MyMalloc(
                         num_distinct_endings * sizeof(ENDING_LIST))) == NULL) {
                    fprintf(stderr,
                            "Could not allocate list of %d endings, statistics "
                            "ignored\n ",
                            num_distinct_endings);
                    extra_statistics = false;
                }
                if (extra_statistics) {
                    strcpy(ending_list[0].ending, ScoreList[0].ending);
                    int n_endings = 1;
                    for (int i = 1; i < RawScores; i++) {
                        if (strcmp(ScoreList[i - 1].ending,
                                   ScoreList[i].ending)) {
                            if (n_endings >= num_distinct_endings) {
                                fprintf(stderr,
                                        "Warning: more than the %d expected "
                                        "number of distinct endings\n",
                                        num_distinct_endings);
                                continue;
                            }
                            strcpy(ending_list[n_endings++].ending,
                                   ScoreList[i].ending);
                        }
                    }
                    if (n_endings < num_distinct_endings) {
                        fprintf(stderr,
                                "Warning: only found %d distinct endings, "
                                "expected %d\n",
                                n_endings, num_distinct_endings);
                        num_distinct_endings = n_endings;
                    }
                    for (int i = 0; i < num_distinct_endings; i++) {
                        ending_list[i].num_total = ending_list[i].num_bad = 0;
                    }
                }
            }
            qsort(ScoreList, RawScores, sizeof(SCORE), score_compar);
            ScoreList[RawScores].game_num = -1;
            if (DepthDelta > 0)
                memcpy(ScoreListDelta, ScoreList,
                       (RawScores + 1) * sizeof(SCORE));
        }

        if (NumZZPositions > 0) {
            qsort(ZZList, NumZZPositions, sizeof(SCORE), score_compar);
            ZZList[NumZZPositions].game_num = -1;
        }

        NumBadMoves = 0;
        NumBadMovesDelta = 0;

        for (int i = 0; i < RawScores; i++) {
            int si = ScoreList[i].score;
            int sip1 = UNKNOWN;
            int move;
            int side = ScoreList[i].side;
            bool phase_change = false;

            if (ScoreList[i].game_num == ScoreList[i + 1].game_num) {
                sip1 = ScoreList[i + 1].score;
                if (side == ScoreList[i + 1].side)
                    continue;
                if ((side == WHITE &&
                     (ScoreList[i + 1].move_no != ScoreList[i].move_no)) ||
                    (side == BLACK &&
                     (ScoreList[i + 1].move_no != ScoreList[i].move_no + 1)))
                    continue;
                if (strcmp(ScoreList[i + 1].ending, ScoreList[i].ending))
                    phase_change = true;
                move = ScoreList[i].move_no;
            } else {
                int result = ScoreList[i].result;

                if (side == WHITE)
                    side = BLACK;
                else if (side == BLACK)
                    side = WHITE;

                if (result == DRAW)
                    sip1 = DRAW;
                else if (result == WON) {
                    if (side == WHITE)
                        sip1 = WON;
                    else
                        sip1 = LOST;
                } else if (result == LOST) {
                    if (side == BLACK)
                        sip1 = WON;
                    else
                        sip1 = LOST;
                }

                move = 999;
            }

            strcpy(ScoreList[NumBadMoves].ending, ScoreList[i].ending);
            ScoreList[NumBadMoves].game_num = ScoreList[i].game_num;
            ScoreList[NumBadMoves].move_no = move;
            ScoreList[NumBadMoves].side = side;

            if (DepthDelta > 0) {
                strcpy(ScoreListDelta[NumBadMovesDelta].ending,
                       ScoreListDelta[i].ending);
                ScoreListDelta[NumBadMovesDelta].game_num =
                    ScoreListDelta[i].game_num;
                ScoreListDelta[NumBadMovesDelta].move_no = move;
                ScoreListDelta[NumBadMovesDelta].side = side;
            }

            int move_score = EvaluateMove(si, sip1, phase_change);

            ENDING_LIST ref, *ref_ptr = NULL;

            if (extra_statistics) {
                memcpy(ref.ending, ScoreList[i].ending, sizeof(ref.ending));
                ref_ptr = (ENDING_LIST *)bsearch(
                    &ref, ending_list, num_distinct_endings,
                    sizeof(ENDING_LIST), ending_list_compar);
                if (ref_ptr != NULL)
                    ref_ptr->num_total++;
            }

            if (IsResultChangingMove(move_score)) {
                if (ref_ptr != NULL)
                    ref_ptr->num_bad++;
                ScoreList[NumBadMoves++].score = move_score;
            } else if (DepthDelta > 0 && !phase_change) {
                if (abs(move_score) >= DepthDelta) {
                    ScoreListDelta[NumBadMovesDelta++].score = move_score;
                }
            }
        }

        if (Verbose > 0) {
            MyPrintf("Found %d bad moves in %s\n", NumBadMoves, ScoreFile);
            if (DepthDelta > 0)
                MyPrintf("Found %d large delta moves in %s\n", NumBadMovesDelta,
                         ScoreFile);
            MyFlush();
        }

        if (extra_statistics) {
            qsort(ending_list, num_distinct_endings, sizeof(ending_list[0]),
                  move_count_compar);
            MyPrintf("%12s  %10s  %10s  %s\n", "Ending", "Num-Moves",
                     "Bad-Moves", "% Bad");
            for (int i = 0; i < num_distinct_endings; i++) {
                if (ending_list[i].num_total > 0) {
                    MyPrintf("%12s  %10u  %10u  %5.2lf\n",
                             ending_list[i].ending, ending_list[i].num_total,
                             ending_list[i].num_bad,
                             100.0 * (double)ending_list[i].num_bad /
                                 (double)ending_list[i].num_total);
                }
            }
            MyPrintf("\n");
        }
        if (NumBadMoves > 0) {
            ScoreList[NumBadMoves].game_num = -1;
        }

        if (NumBadMovesDelta > 0) {
            ScoreListDelta[NumBadMovesDelta].game_num = -1;
        }

        if (show_bad_games) {
            for (int i = 0; i < NumBadMoves; i++) {
                char score_string[16];
                MoveScoreToString(ScoreList[i].score, score_string);
                MyPrintf("Ending %10s Game %8d move %3d %c %s\n",
                         ScoreList[i].ending, ScoreList[i].game_num,
                         ScoreList[i].move_no,
                         (ScoreList[i].side == WHITE ? 'w' : 'b'),
                         score_string);
            }
            exit(0);
        }
    }

    InitTransforms();
    InitMoves();
    InitParity();
    InitPieceStrengths();
    InitCaches();
    InitLexTables();
    InitTagList();

    if (comment_games) {
        BOARD current_pos, prev_pos;
        int legal = NEUTRAL;
        PGNToken symbol = NO_TOKEN;
        ParseType yylval;
        Move move_list[MAX_MOVES];

        if ((fin = fopen(pos_string, "rb")) == NULL) {
            fprintf(stderr, "Could not open %s\n", pos_string);
            exit(1);
        }

        if ((yylval.string_info =
                 (char *)malloc(MAX_PGN_LINE * sizeof(char))) == NULL) {
            fprintf(stderr, "Could not allocate  ParseType string length %d\n",
                    MAX_PGN_LINE);
            exit(1);
        }

        if (Verbose > 1)
            MyPrintf("Commenting on games in %s\n", pos_string);

        if (Verbose > 1)
            MyPrintf("Between %d and %d pieces\n", MinimumNumberOfPieces,
                     MaximumNumberOfPieces);

        legal = ReadPosition(
            "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
            &current_pos, NULL);

        current_pos.score = UNKNOWN;
        current_pos.zz_type = UNKNOWN;

        if (legal == NEUTRAL) {
            fprintf(stderr, "Could not set up starting position\n");
            exit(1);
        } else {
            if (Verbose > 5)
                DisplayBoard(&current_pos, "starting position");
        }

        if (CheckSyntaxOnly)
            InitEndingStats();

        num_games = 1;
        num_positions = 0;
        while (symbol != EOF_TOKEN) {
            skipping_game = false;
            symbol =
                ParseGame(symbol, fin, &yylval, &current_pos, best_capture);
            if (symbol != EOF_TOKEN) {
                num_games++;
                if (!CheckSyntaxOnly && !PositionDatabase &&
                    !PrintBadlyPlayedPositions)
                    MyPrintf("\n");
            }
        }

        if (CheckSyntaxOnly)
            CompileEndingStats();

        exit(0);
    }

    if (query) {
        PlayPosition(pos_string, SinglePos);
        fflush(stdout);
        if (!Dynamic) {
            if (SummaryStats)
                PrintSummary();
        }
        exit(0);
    }

    if (SinglePos) {
        for (;;) {
            if (Dynamic) {
                fgets(line, MAX_LINE, stdin);
                pos_string = &line[0];
            }
            if (bleicher_format) {
                EvaluateBleicher(pos_string, WHITE);
                EvaluateBleicher(pos_string, BLACK);
            } else {
                side = ReadPosition(pos_string, &Board, NULL);
                if (side != NEUTRAL) {
                    if (best_line)
                        FindBestLine(&Board, mark_mzugs, mark_best,
                                     mark_squeeze);
                    else
                        EvaluateEnding(&Board, other_side, best_move);
                }
            }
            fflush(stdout);
            if (!Dynamic) {
                if (SummaryStats)
                    PrintSummary();
                exit(0);
            }
        }
    }

    if ((sptr = strchr(pos_string, SEPARATOR[0])) != NULL) {
        single_pos = true;
        sscanf(sptr + 1, "%lu", &npos);
        if (npos <= 0)
            npos = 1;
        *sptr = '\0';
    }

    if (convert_from_yk_to_mb && convert_from_mb_to_yk) {
        MyPrintf(
            "Cannot convert both from MB to YK and from YK to MB, ignored\n");
        convert_from_mb_to_yk = false;
        convert_from_yk_to_mb = false;
    }

    if (Verbose > 1) {
        MyPrintf("Scanning file %s\n", pos_string);
        if (single_pos)
            MyPrintf("Only evaluate position number %d\n", npos);
        if (process_positions) {
            if (convert_from_yk_to_mb) {
                MyPrintf("Converting indices from YK to MB format\n");
            } else if (convert_from_mb_to_yk) {
                MyPrintf("Converting indices from MB to YK format\n");
            } else {
                if (restricted_promotions)
                    MyPrintf("Label restricted promotions\n");
                MyPrintf("Processing positions\n");
            }
        }
        MyFlush();
    }

    if ((fin = fopen(pos_string, "r")) == NULL) {
        MyPrintf("Could not open %s\n", pos_string);
        MyFlush();
        exit(1);
    }

    pos_count = 0;

    tm_start = time(NULL);

    if (restricted_promotions)
        FlagRestrictedPromotions = true;

    if (process_positions && (sort_list || mark_cz_zugs) &&
        !(convert_from_yk_to_mb || convert_from_mb_to_yk)) {
        POSITION *pos_list;
        while (fgets(line, MAX_LINE - 1, fin) != NULL) {
            POSITION_DATA pos_data;
            if (ReadPositionList(line, &Board, &pos_data) != NEUTRAL)
                pos_count++;
        }

        if (pos_count == 0) {
            fclose(fin);
            if (Verbose > 0) {
                MyPrintf("Found no positions\n");
                MyFlush();
            }
            exit(0);
        }

        pos_list = (POSITION *)MyMalloc(pos_count * sizeof(pos_list[0]));

        rewind(fin);

        pos_count = 0;

        while (fgets(line, MAX_LINE - 1, fin) != NULL) {
            POSITION_DATA pos_data;

            int side = ReadPositionList(line, &Board, &pos_data);

            if (side == NEUTRAL)
                continue;

            pos_list[pos_count].flipped = false;

            pos_list[pos_count].kk_index = pos_data.kk_index;
            pos_list[pos_count].offset = pos_data.offset;

            if (!mark_cz_zugs) {
                for (int i = KING - 1; i >= 0; i--) {
                    if (Board.piece_type_count[0][i] <
                        Board.piece_type_count[1][i]) {
                        pos_list[pos_count].flipped = true;
                        break;
                    } else if (Board.piece_type_count[0][i] >
                               Board.piece_type_count[1][i])
                        break;
                }

                if (pos_list[pos_count].flipped) {
                    MB_INFO mb_info;
                    if (Verbose > 4) {
                        MyPrintf("Ending %s is flipped\n", pos_data.ending);
                        MyFlush();
                    }
                    FlipBoard(&Board);
                    GetMBInfo(&Board, &mb_info);
                    pos_list[pos_count].kk_index = mb_info.kk_index;
                    pos_list[pos_count].offset = mb_info.parity_index[0].index;
                }
            }

            memcpy(pos_list[pos_count].piece_type_count, Board.piece_type_count,
                   sizeof(pos_list[pos_count].piece_type_count));

            pos_list[pos_count].side = Board.side;
            pos_list[pos_count].game_num = pos_data.game_num;
            pos_list[pos_count].move_no = pos_data.move_no;
            pos_list[pos_count].result = pos_data.result;
            pos_list[pos_count].score = pos_data.score;
            pos_list[pos_count].promos = pos_data.promos;
            pos_list[pos_count].zz_type = pos_data.zz_type;
            pos_list[pos_count].cz_type = '0';

            pos_count++;
        }

        fclose(fin);

        if (pos_count == 0) {
            if (Verbose > 0) {
                MyPrintf("Found no positions\n");
                MyFlush();
            }
            exit(0);
        }

        if (Verbose > 0) {
            MyPrintf("Found %d positions\n", pos_count);
            MyFlush();
        }

        if (mark_cz_zugs) {
            GINFO ginfo[MAX_PLY];
            qsort(pos_list, pos_count, sizeof(pos_list[0]), game_pos_compar);
            ginfo[0].kk_index = pos_list[0].kk_index;
            ginfo[0].offset = pos_list[0].offset;
            ginfo[0].result = pos_list[0].result;
            ginfo[0].pindex = 0;
            if (pos_list[0].side == WHITE)
                ginfo[0].ply = 2 * pos_list[0].move_no - 2;
            else
                ginfo[0].ply = 2 * pos_list[0].move_no - 1;
            int npos = 1;
            memcpy(ginfo[0].piece_type_count, pos_list[0].piece_type_count,
                   sizeof(ginfo[0].piece_type_count));
            int cz_index = 0;
            for (int i = 1; i < pos_count; i++) {
                if (i == (pos_count - 1) ||
                    pos_list[i].game_num != pos_list[i - 1].game_num) {
                    for (int j = 0; j < npos - 1; j++) {
                        int ply1 = ginfo[j].ply % 2;
                        int res1 = ginfo[j].result;
                        for (int k = j + 1; k < npos; k++) {
                            int ply2 = ginfo[k].ply % 2;
                            int res2 = ginfo[k].result;
                            if (ginfo[j].kk_index == ginfo[k].kk_index &&
                                ginfo[j].offset == ginfo[k].offset &&
                                ((res1 == WON && res2 == WON && ply1 == 0 &&
                                  ply2 == 1) ||
                                 (res1 == LOST && res2 == LOST && ply1 == 1 &&
                                  ply2 == 0)) &&
                                !memcmp(ginfo[j].piece_type_count,
                                        ginfo[k].piece_type_count,
                                        sizeof(ginfo[j].piece_type_count))) {
                                pos_list[ginfo[j].pindex].cz_type =
                                    'a' + cz_index;
                                pos_list[ginfo[k].pindex].cz_type =
                                    'A' + cz_index;
                                pos_list[ginfo[k].pindex].cz_depth =
                                    ginfo[k].pindex - ginfo[j].pindex;
                                cz_index++;
                                break;
                            }
                        }
                    }
                    npos = 0;
                    cz_index = 0;
                }
                if (npos >= MAX_PLY) {
                    fprintf(
                        stderr,
                        "Length of game > %d ply, need to increase MAX_PLY\n",
                        MAX_PLY);
                    exit(1);
                }
                ginfo[npos].kk_index = pos_list[i].kk_index;
                ginfo[npos].offset = pos_list[i].offset;
                ginfo[npos].result = isupper(pos_list[i].result)
                                         ? tolower(pos_list[i].result)
                                         : pos_list[i].result;
                ginfo[npos].pindex = i;
                if (pos_list[i].side == WHITE)
                    ginfo[npos].ply = 2 * pos_list[i].move_no - 2;
                else
                    ginfo[npos].ply = 2 * pos_list[i].move_no - 1;
                memcpy(ginfo[npos].piece_type_count,
                       pos_list[i].piece_type_count,
                       sizeof(ginfo[npos].piece_type_count));
                npos++;
            }

            for (int i = 0; i < pos_count; i++) {
                POSITION_DATA pos_data;

                char ending[MAX_LINE], score_string[MAX_LINE],
                    zz_string[MAX_LINE];
                GetEndingName(pos_list[i].piece_type_count, pos_data.ending);
                pos_data.kk_index = pos_list[i].kk_index;
                pos_data.offset = pos_list[i].offset;
                pos_data.side = pos_list[i].side;
                pos_data.promos = pos_list[i].promos;
                pos_data.game_num = pos_list[i].game_num;
                pos_data.move_no = pos_list[i].move_no;
                pos_data.result = pos_list[i].result;
                pos_data.score = pos_list[i].score;
                pos_data.zz_type = pos_list[i].zz_type;
                if (pos_list[i].cz_type == '0') {
                    WritePositionData(&pos_data, 9);
                } else {
                    pos_data.cz_type = pos_list[i].cz_type;
                    WritePositionData(&pos_data, 10);
                }
            }

            exit(0);
        }

        qsort(pos_list, pos_count, sizeof(pos_list[0]), pos_compar);

        for (int i = 0; i < pos_count; i++) {
            char ending[MAX_LINE];

            GetEndingName(pos_list[i].piece_type_count, ending);

            GetBoardFromMB(&Board, ending, pos_list[i].side,
                           pos_list[i].kk_index, pos_list[i].offset);

            if (pos_list[i].flipped) {
                FlipBoard(&Board);
            }

            Board.game_num = pos_list[i].game_num;
            Board.full_move = pos_list[i].move_no;

            Board.result = pos_list[i].result;

            Board.score = pos_list[i].score;
            Board.zz_type = pos_list[i].zz_type;

            EvaluatePosition(&Board, best_capture, mark_mzugs);
        }

        exit(0);
    }

    if (OutputPGNFormat)
        InitPGNOutput();

    while (fgets(line, MAX_LINE - 1, fin) != NULL) {
        POSITION_DATA pos_data;
        if (convert_from_mb_to_yk) {
            ConvertMB_to_YK(line);
        } else if (convert_from_yk_to_mb) {
            ConvertYK_to_MB(line);
        } else {
            if (process_positions)
                side = ReadPositionList(line, &Board, &pos_data);
            else
                side = ReadPosition(line, &Board, NULL);
            if (side != NEUTRAL) {
                pos_count++;
                if (!single_pos || pos_count == npos) {
                    if (best_line)
                        FindBestLine(&Board, mark_mzugs, mark_best,
                                     mark_squeeze);
                    else if (process_positions)
                        num_studies +=
                            EvaluatePosition(&Board, best_capture, mark_mzugs);
                    else
                        EvaluateEnding(&Board, other_side, best_move);
                    if (single_pos)
                        break;
                }
            }
        }
    }
    fclose(fin);

    tm_finish = time(NULL);

    if (!(convert_from_mb_to_yk || convert_from_yk_to_mb)) {
        if (process_positions)
            MyPrintf("%%Number of positions evaluated: %u\n", num_studies);
        else if (!comment_games && !best_line)
            MyPrintf("%%Number of positions evaluated: %u\n", pos_count);
        MyPrintf("%%Time taken: %lu seconds\n", tm_finish - tm_start);
    }

    if (SummaryStats)
        PrintSummary();
#endif // not UCI
    return 0;
}