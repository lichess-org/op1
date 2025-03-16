/* Based on mbeval.cpp 7.9 */

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <zlib.h>
#include <zstd.h>

#if __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error "Need memcpy from file to struct"
#endif

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

enum { NO_COMPRESSION = 0, ZLIB, ZSTD, NUM_COMPRESSION_METHODS };
enum { ZLIB_YK = 0, BZIP_YK, LZMA_YK, ZSTD_YK, NO_COMPRESSION_YK };

typedef struct {
    ZSTD_DCtx *zstd_decompression;
    uint8_t *compressed_buffer;
    uint32_t compressed_buffer_size;
    uint8_t *block_buffer;
    uint32_t block_buffer_size;
} CONTEXT;

#define COMPRESS_OK Z_OK
#define COMPRESS_NOT_OK ((COMPRESS_OK) + 1)

#define ABS(a) ((a) < 0 ? -(a) : (a))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

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
#define MAX_IDENT_PIECES 10
#define MAX_FILES 64
#define MAX_FILES_YK 16
#define MAX_FILES_HIGH_DTZ 64

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

static char **TbPaths = NULL;
static int NumPaths = 0;

static const bool Chess960 = true;
static const bool Chess960Game = false;

typedef uint64_t INDEX;
#define DEC_INDEX_FORMAT "%" PRIu64
#define SEPARATOR ":"
#define DELIMITER "/"

typedef uint64_t ZINDEX;
#define DEC_ZINDEX_FORMAT "%" PRIu64
#define DEC_ZINDEX_FORMAT_W(n) "%" #n "I64u"
#define HEX_ZINDEX_FORMAT "%016I64X"

#define ZERO ((ZINDEX)0)
#define ONE ((ZINDEX)1)
#define ALL_ONES (~(ZERO))

#define YK_HEADER_SIZE (4096 - ((462) * 8) - 8)

typedef struct {
    uint8_t unused[16];
    char basename[16];
    INDEX n_elements;
    int kk_index;
    int max_depth;
    uint32_t block_size;
    uint32_t num_blocks;
    uint8_t nrows;
    uint8_t ncols;
    uint8_t side;
    uint8_t metric;
    uint8_t compression_method;
    uint8_t index_size;
    uint8_t format_type;
    uint8_t list_element_size;
} HEADER;

typedef struct {
    ZINDEX index;
    int score;
} HIGH_DTZ;

static int high_dtz_compar(const void *a, const void *b) {
    HIGH_DTZ *x = (HIGH_DTZ *)a;
    HIGH_DTZ *y = (HIGH_DTZ *)b;

    if (x->index < y->index)
        return -1;
    else if (x->index > y->index)
        return 1;
    return 0;
}

static void *MyMalloc(size_t cb) {
    void *pv = malloc(cb);
    assert(pv != NULL);
    if (pv == NULL)
        abort();
    return pv;
}

static void *MyRealloc(void *pv, size_t cb) {
    pv = realloc(pv, cb);
    assert(pv != NULL);
    if (pv == NULL)
        abort();
    return pv;
}

static void MyFree(void *pv) { free(pv); }

typedef struct {
    int fd;
} FD_WRAPPER;

typedef FD_WRAPPER *file;

static file f_open(const char *szFile) {
    int fd = open(szFile, O_RDONLY);
    if (fd < 0) {
        return NULL;
    }

    FD_WRAPPER *h = MyMalloc(sizeof(FD_WRAPPER));
    h->fd = fd;
    return h;
}

static void f_close(file f) {
    if (f == NULL)
        return;
    close(f->fd);
    MyFree(f);
}

static size_t f_read(void *pv, size_t cb, file fp, INDEX indStart) {
    size_t total = 0;
    while (total < cb) {
        ssize_t result = pread(fp->fd, pv, cb - total, indStart + total);
        if (result < 0 && errno == EINTR)
            continue;
        if (result < 0) {
            perror("f_read");
            abort();
        }
        total += result;
    }
    return total;
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
    OFFSET_READ_ERROR,
    ZONE_READ_ERROR,
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

static int ParityTable[NSQUARES];
static int WhiteSquare[NSQUARES / 2], BlackSquare[NSQUARES / 2];

typedef struct {
    int etype, op_type, sub_type;
    bool (*PosFromIndex)(ZINDEX index, int *pos);
    ZINDEX (*IndexFromPos)(int *pos);
} IndexType;

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
                pos[index] = p2 + NSQUARES * p1;
                int g_index = N2_Index_Function(p2, p1);
                assert(index == g_index);
                score = index++;
            }
            tab[p1 + NSQUARES * p2] = score;
            tab[p2 + NSQUARES * p1] = score;
        }
    }

    assert(index == N2);
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
            pos[index] = p2 + NSQUARES * p1;
            score = index++;
            tab[p1 + NSQUARES * p2] = score;
            tab[p2 + NSQUARES * p1] = score;
        }
    }

    assert(index == N2_ODD_PARITY);
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
            pos[index] = p2 + NSQUARES * p1;
            score = index++;
            tab[p1 + NSQUARES * p2] = score;
            tab[p2 + NSQUARES * p1] = score;
        }
    }

    assert(index == N2_EVEN_PARITY);
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
                pos[index] = p3 + NSQUARES * (p2 + NSQUARES * p1);
                score = index++;
                tab[p1 + NSQUARES * (p2 + NSQUARES * p3)] = score;
                tab[p1 + NSQUARES * (p3 + NSQUARES * p2)] = score;
                tab[p2 + NSQUARES * (p1 + NSQUARES * p3)] = score;
                tab[p2 + NSQUARES * (p3 + NSQUARES * p1)] = score;
                tab[p3 + NSQUARES * (p1 + NSQUARES * p2)] = score;
                tab[p3 + NSQUARES * (p2 + NSQUARES * p1)] = score;
            }
        }
    }

    assert(index == N3_EVEN_PARITY);
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
                pos[index] = p3 + NSQUARES * (p2 + NSQUARES * p1);
                score = index++;
                tab[p1 + NSQUARES * (p2 + NSQUARES * p3)] = score;
                tab[p1 + NSQUARES * (p3 + NSQUARES * p2)] = score;
                tab[p2 + NSQUARES * (p1 + NSQUARES * p3)] = score;
                tab[p2 + NSQUARES * (p3 + NSQUARES * p1)] = score;
                tab[p3 + NSQUARES * (p1 + NSQUARES * p2)] = score;
                tab[p3 + NSQUARES * (p2 + NSQUARES * p1)] = score;
            }
        }
    }

    assert(index == N3_ODD_PARITY);
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
            assert(index < NCOLS * (NROWS - 2) * (NROWS - 3) / 2);
            tab[sq2 + NSQUARES * sq1] = index;
            pos[index] = sq2 + NSQUARES * sq1;
            index++;
        }
    }

    assert(index == NCOLS * (NROWS - 2) * (NROWS - 3) / 2);
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
                    assert(index < N2_1_OPPOSING);
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

    assert(index == N2_1_OPPOSING);
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
                    assert(index < N1_2_OPPOSING);
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

    assert(index == N1_2_OPPOSING);
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

    assert(index == N2_2_OPPOSING);
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

    assert(index == N3_1_OPPOSING);
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

    assert(index == N1_3_OPPOSING);
}

enum { ONE_COLUMN = 0, ADJACENT, NON_ADJACENT, NO_DP22 };

static int IsValidDP22(int w1, int w2, int b1, int b2) {
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
            w1_row < MIN(b1_row, b2_row) && w2_row < MAX(b1_row, b2_row))
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

static ZINDEX IndexDP22(int *);

static void InitN4OpposingTables(int *tab, int *pos) {
    for (int w1 = 0; w1 < NSQUARES; w1++) {
        for (int w2 = 0; w2 < NSQUARES; w2++) {
            for (int b1_r = 0; b1_r < NROWS; b1_r++) {
                for (int b2_r = 0; b2_r < NROWS; b2_r++) {
                    tab[b2_r + NROWS * (b1_r + NROWS * (w2 + NSQUARES * w1))] =
                        -1;
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
                    assert(index < N4_OPPOSING);
                    int pos_array_00, pos_array_10, pos_array_01, pos_array_11;
                    int w1_col = Column(w1);
                    int b1_col = Column(b1);
                    int w2_col = Column(w2);
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
                    pos[index] = pos_array_00;
                    index++;
                }
            }
        }
    }

    assert(one_column == N4_ONE_COLUMN);
    assert(adjacent == N4_ADJACENT);
    assert(non_adjacent == N4_NON_ADJACENT);
    assert(index == N4_OPPOSING);

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
                    assert(index != ALL_ONES);
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
                    pos[index] = p3 + NSQUARES * (p2 + NSQUARES * p1);
                    int g_index = N3_Index_Function(p3, p2, p1);
                    assert(index == g_index);
                    score = index++;
                }
                tab[p1 + NSQUARES * (p2 + NSQUARES * p3)] = score;
                tab[p1 + NSQUARES * (p3 + NSQUARES * p2)] = score;

                tab[p2 + NSQUARES * (p1 + NSQUARES * p3)] = score;
                tab[p2 + NSQUARES * (p3 + NSQUARES * p1)] = score;

                tab[p3 + NSQUARES * (p2 + NSQUARES * p1)] = score;
                tab[p3 + NSQUARES * (p1 + NSQUARES * p2)] = score;
            }
        }
    }

    assert(index == N3);
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
                        pos[index] =
                            p4 +
                            NSQUARES * (p3 + NSQUARES * (p2 + NSQUARES * p1));
                        int g_index = N4_Index_Function(p4, p3, p2, p1);
                        assert(index == g_index);
                        score = index++;
                    }
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

    assert(index == N4);
}

static void InitN4TablesMB(int *pos) {
    int index = 0;

    for (int p4 = 3; p4 < NSQUARES; p4++) {
        for (int p3 = 2; p3 <= p4; p3++) {
            for (int p2 = 1; p2 <= p3; p2++) {
                for (int p1 = 0; p1 <= p2; p1++) {
                    if (p1 == p2 || p1 == p3 || p1 == p4 || p2 == p3 ||
                        p2 == p4 || p3 == p4) {
                        // skip
                    } else {
                        pos[index] =
                            p4 +
                            NSQUARES * (p3 + NSQUARES * (p2 + NSQUARES * p1));
                        int g_index = p4 * (p4 - 1) * (p4 - 2) * (p4 - 3) / 24 +
                                      p3 * (p3 - 1) * (p3 - 2) / 6 +
                                      p2 * (p2 - 1) / 2 + p1;
                        assert(index == g_index);
                        index++;
                    }
                }
            }
        }
    }

    assert(index == N4);
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
                        assert(index == g_index);
                        index++;
                    }
                }
            }
        }
    }

    assert(index == N5);
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
                            assert(index == g_index);
                            index++;
                        }
                    }
                }
            }
        }
    }

    assert(index == N6);
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
                                assert(index == g_index);
                                index++;
                            }
                        }
                    }
                }
            }
        }
    }

    assert(index == N7);
}

static void InitPermutationTables(void) {
    k2_opposing_tab = (int *)MyMalloc(NSQUARES * NSQUARES * sizeof(int));
    p2_opposing_tab =
        (int *)MyMalloc(NCOLS * (NROWS - 2) * (NROWS - 3) / 2 * sizeof(int));
    InitN2OpposingTables(k2_opposing_tab, p2_opposing_tab);

    k2_1_opposing_tab =
        (int *)MyMalloc(NSQUARES * NSQUARES * NSQUARES * sizeof(int));
    p2_1_opposing_tab = (int *)MyMalloc(N2_1_OPPOSING * sizeof(int));
    InitN2_1_OpposingTables(k2_1_opposing_tab, p2_1_opposing_tab);

    k1_2_opposing_tab =
        (int *)MyMalloc(NSQUARES * NSQUARES * NSQUARES * sizeof(int));
    p1_2_opposing_tab = (int *)MyMalloc(N1_2_OPPOSING * sizeof(int));
    InitN1_2_OpposingTables(k1_2_opposing_tab, p1_2_opposing_tab);

    k2_2_opposing_tab = (int *)MyMalloc(NSQUARES * NSQUARES * NSQUARES *
                                        NSQUARES * sizeof(int));
    p2_2_opposing_tab = (int *)MyMalloc(N2_2_OPPOSING * sizeof(int));
    InitN2_2_OpposingTables(k2_2_opposing_tab, p2_2_opposing_tab);

    k3_1_opposing_tab = (int *)MyMalloc(NSQUARES * NSQUARES * NSQUARES *
                                        NSQUARES * sizeof(int));
    p3_1_opposing_tab = (int *)MyMalloc(N3_1_OPPOSING * sizeof(int));
    InitN3_1_OpposingTables(k3_1_opposing_tab, p3_1_opposing_tab);

    k1_3_opposing_tab = (int *)MyMalloc(NSQUARES * NSQUARES * NSQUARES *
                                        NSQUARES * sizeof(int));
    p1_3_opposing_tab = (int *)MyMalloc(N1_3_OPPOSING * sizeof(int));
    InitN1_3_OpposingTables(k1_3_opposing_tab, p1_3_opposing_tab);

    k4_opposing_tab = (int *)MyMalloc(NSQUARES * NSQUARES * NSQUARES *
                                      NSQUARES * sizeof(int));
    p4_opposing_tab = (int *)MyMalloc(N4_OPPOSING * sizeof(int));
    InitN4OpposingTables(k4_opposing_tab, p4_opposing_tab);

    p4_tab_mb = (int *)MyMalloc(N4 * sizeof(int));
    InitN4TablesMB(p4_tab_mb);
    InitN5Tables();
    InitN6Tables();
    InitN7Tables();

#if defined(USE_PERMUTATION_FUNCTIONS)
    InitN4Tables(NULL, NULL);
#else
    k4_tab = (int *)MyMalloc(NSQUARES * NSQUARES * NSQUARES * NSQUARES *
                             sizeof(int));
    p4_tab = (int *)MyMalloc(N4 * sizeof(int));
    InitN4Tables(k4_tab, p4_tab);
#endif

#if defined(USE_PERMUTATION_FUNCTIONS)
    InitN3Tables(NULL, NULL);
#else
    k3_tab = (int *)MyMalloc(NSQUARES * NSQUARES * NSQUARES * sizeof(int));
    p3_tab = (int *)MyMalloc(N3 * sizeof(int));
    InitN3Tables(k3_tab, p3_tab);
#endif

#if (NUM_WHITE_SQUARES) == (NUM_BLACK_SQUARES)
    k3_even_tab = (int *)MyMalloc(NSQUARES * NSQUARES * NSQUARES * sizeof(int));
    p3_even_tab = (int *)MyMalloc(N3_EVEN_PARITY * sizeof(int));
    InitN3EvenTables(k3_even_tab, p3_even_tab);

    k3_odd_tab = (int *)MyMalloc(NSQUARES * NSQUARES * NSQUARES * sizeof(int));
    p3_odd_tab = (int *)MyMalloc(N3_ODD_PARITY * sizeof(int));
    InitN3OddTables(k3_odd_tab, p3_odd_tab);
#endif

#if defined(USE_PERMUTATION_FUNCTIONS)
    InitN2Tables(NULL, NULL);
#else
    k2_tab = (int *)MyMalloc(NSQUARES * NSQUARES * sizeof(int));
    p2_tab = (int *)MyMalloc(N2 * sizeof(int));
    InitN2Tables(k2_tab, p2_tab);
#endif

#if (NUM_WHITE_SQUARES) == (NUM_BLACK_SQUARES)
    k2_even_tab = (int *)MyMalloc(NSQUARES * NSQUARES * sizeof(int));
    p2_even_tab = (int *)MyMalloc(N2_EVEN_PARITY * sizeof(int));
    InitN2EvenTables(k2_even_tab, p2_even_tab);
#endif

    k2_odd_tab = (int *)MyMalloc(NSQUARES * NSQUARES * sizeof(int));
    p2_odd_tab = (int *)MyMalloc(N2_ODD_PARITY * sizeof(int));
    InitN2OddTables(k2_odd_tab, p2_odd_tab);
}

static int BinarySearchLeftmost(ZINDEX *arr, int n, ZINDEX x) {
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

static int LargestSquareInSeptuplet(ZINDEX *index) {
    if (*index == 0)
        return 6;

    int m = BinarySearchLeftmost(k7_tab, NSQUARES, *index);
    if (k7_tab[m] > *index)
        m--;

    *index -= k7_tab[m];
    return m;
}

static int LargestSquareInSextuplet(ZINDEX *index) {
    if (*index == 0)
        return 5;

    int m = BinarySearchLeftmost(k6_tab, NSQUARES, *index);
    if (k6_tab[m] > *index)
        m--;

    *index -= k6_tab[m];
    return m;
}

static int LargestSquareInQuintuplet(ZINDEX *index) {
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
    int p2 = p2_tab[index];
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
    int p2 = p2_tab[index];
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
    int p2 = p2_tab[index];
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
    int p2 = p2_tab[index];
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
    int p2 = p2_tab[index];
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
    int p2 = p2_tab[index];
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

static const IndexType IndexTable[] = {
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
    {7, FREE_PAWNS, 0, Pos7, Index7}};

#define NumIndexTypes (sizeof(IndexTable) / sizeof(IndexTable[0]))

typedef struct {
    ZINDEX index;
    const IndexType *eptr;
    int bishop_parity[2];
} PARITY_INDEX;

typedef struct {
    int kk_index;
    ZINDEX index;
    uint8_t metric;
} INDEX_DATA;

typedef struct {
    PARITY_INDEX parity_index[4];
    int num_parities;
    int mb_position[MAX_PIECES_MB], mb_piece_types[MAX_PIECES_MB];
    int piece_type_count[2][KING];
    int parity;
    int pawn_file_type;
    const IndexType *eptr_bp_11, *eptr_op_11, *eptr_op_21, *eptr_op_12,
        *eptr_dp_22, *eptr_op_22, *eptr_op_31, *eptr_op_13, *eptr_op_41,
        *eptr_op_14, *eptr_op_32, *eptr_op_23, *eptr_op_33, *eptr_op_42,
        *eptr_op_24;
    ZINDEX index_bp_11, index_op_11, index_op_21, index_op_12, index_dp_22,
        index_op_22, index_op_31, index_op_13, index_op_41, index_op_14,
        index_op_32, index_op_23, index_op_33, index_op_42, index_op_24;
    int num_pieces;
    int kk_index;
} MB_INFO;

#ifdef USE_YK
typedef struct {
    int yk_position[MAX_PIECES_YK], yk_piece_types[MAX_PIECES_YK];
    int piece_type_count[2][KING];
    const IndexType *eptr;
    ZINDEX index;
    int num_pieces;
    int kk_index;
} YK_INFO;

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
#endif // USE_YK

static int PieceStrengths[KING];

static char PieceChar(int type) {
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

static int GetEndingName(const int type_count[2][KING], char *ending) {
    int len = 0, piece;

    ending[len++] = 'k';
    for (piece = KING - 1; piece >= PAWN; piece--) {
        for (int k = len; k < len + type_count[WHITE][piece]; k++)
            ending[k] = PieceChar(piece);
        len += type_count[WHITE][piece];
    }
    ending[len++] = 'k';
    for (piece = KING - 1; piece >= PAWN; piece--) {
        for (int k = len; k < len + type_count[BLACK][piece]; k++)
            ending[k] = PieceChar(piece);
        len += type_count[BLACK][piece];
    }
    ending[len] = '\0';
    return len;
}

static int SetBoard(BOARD *Board, const int *board, int side, int ep_square,
                    int castle, int half_move, int full_move) {
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

typedef struct {
    int piece_type_count[2][KING];
    int kk_index;
    int pawn_file_type;
    int bishop_parity[2];
    uint32_t max_num_blocks;
    file fp;
    HEADER header;
    INDEX *offsets;
} FILE_CACHE;

static FILE_CACHE FileCache[MAX_FILES][2];
static int num_cached_files[2] = {0, 0};
static int cached_file_lru[MAX_FILES][2];

#ifdef USE_YK
typedef struct {
    int piece_type_count[2][KING];
    uint32_t max_num_blocks, num_blocks;
    int compression_method;
    int max_depth;
    INDEX num_high_dtc;
    file fp, fp_high;
    HEADER header;
    INDEX *offsets;
    uint8_t *block;
    HDATA *block_high;
    uint32_t max_block_size, block_size;
    int block_index;
} FILE_CACHE_YK;

static FILE_CACHE_YK FileCacheYK[MAX_FILES_YK][2];

static int num_cached_files_yk[2] = {0, 0};
static int cached_file_lru_yk[MAX_FILES_YK][2];
#endif // USE_YK

typedef struct {
    int piece_type_count[2][KING];
    int kk_index;
    int pawn_file_type;
    int bishop_parity[2];
    uint32_t max_num_blocks;
    file fp;
    HEADER header;
    INDEX *offsets;
    ZINDEX *starting_index;
} FILE_CACHE_HIGH_DTZ;

static FILE_CACHE_HIGH_DTZ FileCacheHighDTZ[MAX_FILES_HIGH_DTZ][2];

static int num_cached_files_high_dtz[2] = {0, 0};
static int cached_file_high_dtz_lru[MAX_FILES_HIGH_DTZ][2];

static void InitCaches() {

    memset(FileCache, 0, sizeof(FileCache));
    for (int i = 0; i < MAX_FILES; i++) {
        FileCache[i][0].fp = FileCache[i][1].fp = NULL;
        FileCache[i][0].offsets = FileCache[i][1].offsets = NULL;
    }

#ifdef USE_YK
    memset(FileCacheYK, 0, sizeof(FileCacheYK));
    for (int i = 0; i < MAX_FILES_YK; i++) {
        FileCacheYK[i][0].fp = FileCacheYK[i][1].fp = NULL;
        FileCacheYK[i][0].fp_high = FileCacheYK[i][1].fp_high = NULL;
        FileCacheYK[i][0].offsets = FileCacheYK[i][1].offsets = NULL;
        FileCacheYK[i][0].block = FileCacheYK[i][1].block = NULL;
        FileCacheYK[i][0].block_high = FileCacheYK[i][1].block_high = NULL;
    }
#endif

    memset(FileCacheHighDTZ, 0, sizeof(FileCacheHighDTZ));
    for (int i = 0; i < MAX_FILES_HIGH_DTZ; i++) {
        FileCacheHighDTZ[i][0].fp = FileCacheHighDTZ[i][1].fp = NULL;
        FileCacheHighDTZ[i][0].offsets = FileCacheHighDTZ[i][1].offsets = NULL;
        FileCacheHighDTZ[i][0].starting_index =
            FileCacheHighDTZ[i][1].starting_index = NULL;
    }
}

static int MyUncompress(CONTEXT *ctx, uint8_t *dest, uint32_t *dest_size,
                        const uint8_t *source, uint32_t source_size,
                        int method) {
    if (method == NO_COMPRESSION) {
        *dest_size = source_size;
        memcpy(dest, source, source_size);
    } else if (method == ZSTD) {
        size_t destCapacity = *dest_size;
        destCapacity = ZSTD_decompressDCtx(ctx->zstd_decompression, dest,
                                           destCapacity, source, source_size);
        if (ZSTD_isError(destCapacity)) {
            return COMPRESS_NOT_OK;
        }
        *dest_size = destCapacity;
    } else if (method == ZLIB) {
        uLongf dest_size_value = 0;
        int err_code = uncompress(dest, &dest_size_value, source, source_size);
        *dest_size = dest_size_value;

        if (err_code != Z_OK) {
            return COMPRESS_NOT_OK;
        }
    } else {
        fprintf(stderr, "MyUncompress: unknown de-compression method\n");
        return COMPRESS_NOT_OK;
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

static int GetEndingType(const int count[2][KING], int *piece_types,
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
                if (bishop_parity[WHITE] == EVEN) {
                    sub_type = 10 * triplet_index + 0;
                } else if (bishop_parity[WHITE] == ODD) {
                    sub_type = 10 * triplet_index + 1;
                }
            } else {
                assert(false);
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
                if (bishop_parity[BLACK] == EVEN) {
                    sub_type_black = 10 * triplet_index + 0;
                } else if (bishop_parity[BLACK] == ODD) {
                    sub_type_black = 10 * triplet_index + 1;
                }
            } else {
                assert(false);
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

#ifdef USE_YK
static int GetEndingTypeYK(const int count[2][KING], int *piece_types) {
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
#endif // USE_YK

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

static int CastleRights(int *board, int proposed_castle) {
    int king_orig_col = KING_ORIG_COL_TRADITIONAL;
    int crook_orig_col = CROOK_ORIG_COL_TRADITIONAL;
    int grook_orig_col = GROOK_ORIG_COL_TRADITIONAL;

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

static int ScanPosition(char *pos_string, int *board, int *ep_square,
                        int *castle, char *title) {
    int i = 0, side = NEUTRAL, row, col, pi, sq, n, wk = -1, bk = -1;
    char ccol, ptype, *pptr;

    memset(board, 0, NSQUARES * sizeof(board[0]));
    *ep_square = 0;
    *castle = 0;

    char castle_str[8];

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
                return NEUTRAL;
            }
            sq = SquareMake(row, col);
            pi = GetPiece(ptype);
            if (pi == -1) {
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
                return NEUTRAL;
            }
            col = (int)(ccol - 'a');
            row--;
            if (col < 0 || col >= NCOLS || row < 0 || row >= NROWS) {
                return NEUTRAL;
            }
            *ep_square = SquareMake(row, col);
            i += (3 + (row >= 9));
            break;
        case 'K':
        case 'Q':
        case 'k':
        case 'q':
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
            return NEUTRAL;
        }
    }

    for (i = 0; i < NSQUARES; i++) {
        if (board[i] == KING) {
            if (wk != -1) {
                return NEUTRAL;
            }
            wk = i;
        } else if (board[i] == -KING) {
            if (bk != -1) {
                return NEUTRAL;
            }
            bk = i;
        }
    }

    int row_wk = Row(wk);
    int row_bk = Row(bk);
    int col_wk = Column(wk);
    int col_bk = Column(bk);

    if (wk == bk || (ABS(row_wk - row_bk) <= 1 && ABS(col_wk - col_bk) <= 1)) {
        return NEUTRAL;
    }

    if (*ep_square != 0) {
        row = Row(*ep_square);
        col = Column(*ep_square);
        for (;;) {
            if (side == WHITE) {
                if (row != NROWS - 3) {
                    *ep_square = 0;
                    break;
                }
                if (board[SquareMake(row, col)] != 0) {
                    *ep_square = 0;
                    break;
                }
                if (board[SquareMake(row + 1, col)] != 0) {
                    *ep_square = 0;
                    break;
                }
                if (board[SquareMake(row - 1, col)] != -PAWN) {
                    *ep_square = 0;
                    break;
                }
                bool ok = false;
                if ((col > 0 && board[SquareMake(row - 1, col - 1)] == PAWN) ||
                    (col < (NCOLS - 1) &&
                     board[SquareMake(row - 1, col + 1)] == PAWN))
                    ok = true;
                if (!ok) {
                    *ep_square = 0;
                    break;
                }
                break;
            } else if (side == BLACK) {
                if (row != 2) {
                    break;
                }
                if (board[SquareMake(row, col)] != 0) {
                    break;
                }
                if (board[SquareMake(row - 1, col)] != 0) {
                    break;
                }
                if (board[SquareMake(row + 1, col)] != PAWN) {
                    break;
                }
                bool ok = false;
                if ((col > 0 && board[SquareMake(row + 1, col - 1)] == -PAWN) ||
                    (col < (NCOLS - 1) &&
                     board[SquareMake(row + 1, col + 1)] == -PAWN))
                    ok = true;
                if (!ok) {
                    break;
                }
                break;
            }
        }
    }

    if (*castle != 0) {
        int avail_castle = CastleRights(board, *castle);
        if ((*castle) != avail_castle) {
            *castle = avail_castle;
        }
    }

    return side;
}

static int ScanFEN(char *fen_string, int *board, int *ep_square, int *castle,
                   int *half_move, int *full_move, char *title) {
    char pos_str[128], side_str[128], castle_str[128], ep_str[128];
    char half_move_str[128], full_move_str[128];
    char *pptr, save_char;
    int kings[2], n, row, col, side, wk = -1, bk = -1;

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
                goto error;
            }
            if (islower(c))
                color = BLACK;
            if (piece == KING)
                (color == WHITE) ? kings[0]++ : kings[1]++;

            if (row < 0 || col > (NCOLS - 1)) {
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
        goto error;
    }

    int row_wk = Row(wk);
    int row_bk = Row(bk);
    int col_wk = Column(wk);
    int col_bk = Column(bk);

    if (ABS(row_wk - row_bk) <= 1 && ABS(col_wk - col_bk) <= 1) {
        goto error;
    }

    side = WHITE;
    if (n > 1) {
        if (side_str[0] == 'w' || side_str[0] == 'W') {
            side = WHITE;
        } else if (side_str[0] == 'b' || side_str[0] == 'B') {
            side = BLACK;
        } else {
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
            if (col < 0 || col >= NCOLS || row < 0 || row >= NROWS) {
                break;
            }

            if (side == WHITE) {
                if (row != NROWS - 3) {
                    break;
                }
                if (board[SquareMake(row, col)] != 0) {
                    break;
                }
                if (board[SquareMake(row + 1, col)] != 0) {
                    break;
                }
                if (board[SquareMake(row - 1, col)] != -PAWN) {
                    break;
                }
                bool ok = false;
                if ((col > 0 && board[SquareMake(row - 1, col - 1)] == PAWN) ||
                    (col < (NCOLS - 1) &&
                     board[SquareMake(row - 1, col + 1)] == PAWN))
                    ok = true;
                if (!ok) {
                    break;
                }
                *ep_square = SquareMake(row, col);
                break;
            } else if (side == BLACK) {
                if (row != 2) {
                    break;
                }
                if (board[SquareMake(row, col)] != 0) {
                    break;
                }
                if (board[SquareMake(row - 1, col)] != 0) {
                    break;
                }
                if (board[SquareMake(row + 1, col)] != PAWN) {
                    break;
                }
                bool ok = false;
                if ((col > 0 && board[SquareMake(row + 1, col - 1)] == -PAWN) ||
                    (col < (NCOLS - 1) &&
                     board[SquareMake(row + 1, col + 1)] == -PAWN))
                    ok = true;
                if (!ok) {
                    break;
                }
                *ep_square = SquareMake(row, col);
                break;
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

static int ReadPosition(char *pos, BOARD *Board, char *title) {
    int legal, board[NSQUARES], ep_square = 0, castle = 0, half_move = 0,
                                full_move = 1;

    if (strchr(pos, '/') != NULL) {
        legal = ScanFEN(pos, board, &ep_square, &castle, &half_move, &full_move,
                        title);
    } else {
        legal = ScanPosition(pos, board, &ep_square, &castle, title);
    }
    if (legal != NEUTRAL) {
        SetBoard(Board, board, legal, ep_square, castle, half_move, full_move);
    }
    return legal;
}

/*
 * KK_Canonical computes the symmetry operation to transform wk_in and bk_in to
 * a "canonical" configuration when pawns are present.  The symmetry operation
 * index and transformed king positions are stored in sym, wk_in, and bk_in
 * respectively.
 *
 * The routine returns true if the position is legal, false otherwise
 */
static bool KK_Canonical(int *wk_in, int *bk_in, int *sym) {
    int wk = *wk_in;
    int bk = *bk_in;
    int wk_row = Row(wk);
    int wk_col = Column(wk);
    int bk_row = Row(bk);
    int bk_col = Column(bk);

    // positions where kings are adjacent are illegal
    if (ABS(wk_row - bk_row) <= 1 && ABS(wk_col - bk_col) <= 1)
        return false;

    // try all symmetry operations on the two kings to find the canonical one
    for (int isym = 0; isym < NSYMMETRIES; isym++) {
        if (isym != IDENTITY && isym != REFLECT_V)
            continue;
        int *transform = Transforms[isym];
        int wk_trans = transform[wk];
        int bk_trans = transform[bk];
#if (NCOLS % 2)
        int wk_trans_row = Row(wk_trans);
#endif
        int wk_trans_col = Column(wk_trans);
#if (NCOLS % 2)
        int bk_trans_row = Row(bk_trans);
        int bk_trans_col = Column(bk_trans);
#endif
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
static bool KK_Canonical_NoPawns(int *wk_in, int *bk_in, int *sym) {
    int wk = *wk_in;
    int bk = *bk_in;
    int wk_row = Row(wk);
    int wk_col = Column(wk);
    int bk_row = Row(bk);
    int bk_col = Column(bk);

    // positions where kings are adjacent are illegal
    if (ABS(wk_row - bk_row) <= 1 && ABS(wk_col - bk_col) <= 1)
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
            assert(inverse_trans[trans[sq]] == sq);
        }
    }

#if (NSQUARES > KK_TABLE_LIMIT)
    KK_Transform_Table = (int *)MyMalloc(NSQUARES * NSQUARES * sizeof(int));
    KK_Index_Table = (int *)MyMalloc(NSQUARES * NSQUARES * sizeof(int));

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

    assert(nw == NUM_WHITE_SQUARES);
    assert(nb == NUM_BLACK_SQUARES);

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

static void InitParity() {
    int sq;

    for (sq = 0; sq < NSQUARES; sq++) {
        ParityTable[sq] = (Row(sq) & 1) ^ (Column(sq) & 1);
        if (ParityTable[sq])
            WhiteSquare[sq / 2] = sq;
        else
            BlackSquare[sq / 2] = sq;
    }
}

static int GetMBPosition(const BOARD *Board, int *mb_position, int *parity,
                         int *pawn_file_type) {
    int loc = 0, color, type, i;
    int bishops_on_white_squares[2] = {0, 0};
    int bishops_on_black_squares[2] = {0, 0};

    mb_position[loc++] = Board->wkpos;
    mb_position[loc++] = Board->bkpos;

    for (color = WHITE; color <= BLACK; color++) {
        const int *pos = Board->piece_locations[color][PAWN];
        for (int i = 0; i < Board->piece_type_count[color][PAWN]; i++) {
            mb_position[loc] = pos[i];
            if (Board->ep_square > 0) {
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
            const int *pos = Board->piece_locations[color][type];
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

    assert(loc == Board->num_pieces);
    return loc;
}

#ifdef USE_YK
static int GetYKPosition(const BOARD *Board, int *yk_position) {
    int loc = 0;

    yk_position[loc++] = Board->wkpos;
    yk_position[loc++] = Board->bkpos;

    for (int color = WHITE; color <= BLACK; color++) {
        for (int type = KING - 1; type >= PAWN; type--) {
            const int *pos = Board->piece_locations[color][type];
            for (int i = 0; i < Board->piece_type_count[color][type]; i++) {
                yk_position[loc] = pos[i];
                if (type == PAWN && Board->ep_square > 0) {
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

    assert(loc == Board->num_pieces);
    return loc;
}
#endif // USE_YK

static ZINDEX GetMBIndex(int *mb_pos, int npieces, bool pawns_present,
                         const IndexType *eptr, int *kindex, ZINDEX *offset) {
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

    int *transform = Transforms[sym];

    for (int i = 0; i < npieces; i++) {
        mb_pos[i] = transform[mb_pos[i]];
    }

    wk = mb_pos[0];
    bk = mb_pos[1];

    *offset = (eptr->IndexFromPos)(mb_pos);

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

#ifdef USE_YK
static int GetYKIndex(int *yk_pos, int npieces, bool pawns_present,
                      const IndexType *eptr, int *kindex, ZINDEX *offset) {
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

    int *transform = Transforms[sym];

    for (int i = 0; i < npieces; i++) {
        yk_pos[i] = transform[yk_pos[i]];
    }

    wk = yk_pos[0];
    bk = yk_pos[1];

    *offset = (eptr->IndexFromPos)(yk_pos);

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

    if (pawns_present)
        *kindex = KK_Index(wk, bk);
    else
        *kindex = KK_Index_NoPawns(wk, bk);

    return 0;
}

static void GetYKBaseFileName(const int count[2][KING], int side, char *fname) {
    fname[0] = 'k';
    int len = 1;
    for (int piece = KING - 1; piece >= PAWN; piece--) {
        for (int i = len; i < len + count[WHITE][piece]; i++)
            fname[i] = PieceChar(piece);
        len += count[WHITE][piece];
    }
    fname[len++] = 'k';
    for (int piece = KING - 1; piece >= PAWN; piece--) {
        for (int i = len; i < len + count[BLACK][piece]; i++)
            fname[i] = PieceChar(piece);
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
#endif // USE_YK

static file OpenMBFile(char *ending, int kk_index, int bishop_parity[2],
                       int pawn_file_type, int side, bool high_dtz) {
    char path[1024];

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
        file fptr = f_open(path);
        if (fptr != NULL) {
            return fptr;
        }
    }
    return NULL;
}

#ifdef USE_YK
static file OpenYKFile(char *base_name) {
    char path[1024];

    for (int i = 0; i < NumPaths; i++) {
        snprintf(path, sizeof(path) - 1, "%s%c%s", TbPaths[i], DELIMITER[0],
                 base_name);
        file fptr = f_open(path);
        if (fptr != NULL) {
            return fptr;
        }
    }
    return NULL;
}
#endif // USE_YK

static int GetMBInfo(const BOARD *Board, MB_INFO *mb_info) {
    mb_info->num_parities = 0;
    mb_info->pawn_file_type = FREE_PAWNS;

    if (Board->num_pieces > MAX_PIECES_MB) {
        return TOO_MANY_PIECES;
    }

    memcpy(mb_info->piece_type_count, Board->piece_type_count,
           sizeof(Board->piece_type_count));

    int bishop_parity[2] = {NONE, NONE};

    mb_info->num_pieces = Board->num_pieces;

    GetMBPosition(Board, mb_info->mb_position, &mb_info->parity,
                  &mb_info->pawn_file_type);

    memset(mb_info->mb_piece_types, 0, sizeof(mb_info->mb_piece_types));

    int eindex = GetEndingType(Board->piece_type_count, mb_info->mb_piece_types,
                               bishop_parity, FREE_PAWNS);

    int kk_index_blocked = -1;
    if (eindex >= 0) {
        memcpy(mb_info->parity_index[0].bishop_parity, bishop_parity,
               sizeof(bishop_parity));
        mb_info->parity_index[0].eptr = &IndexTable[eindex];
        mb_info->num_parities++;
        // check whether we can also probe blocked/opposing pawn data

        if (mb_info->pawn_file_type == OP_11_PAWNS ||
            mb_info->pawn_file_type == BP_11_PAWNS) {
            eindex = GetEndingType(Board->piece_type_count, NULL, bishop_parity,
                                   OP_11_PAWNS);
            if (eindex >= 0) {
                mb_info->eptr_op_11 = &IndexTable[eindex];
                GetMBIndex(mb_info->mb_position, mb_info->num_pieces, true,
                           mb_info->eptr_op_11, &kk_index_blocked,
                           &mb_info->index_op_11);
            } else {
                mb_info->eptr_op_11 = NULL;
                mb_info->index_op_11 = ALL_ONES;
            }
        }

        if (mb_info->pawn_file_type == BP_11_PAWNS) {
            eindex = GetEndingType(Board->piece_type_count, NULL, bishop_parity,
                                   BP_11_PAWNS);
            if (eindex >= 0) {
                mb_info->eptr_bp_11 = &IndexTable[eindex];
                GetMBIndex(mb_info->mb_position, mb_info->num_pieces, true,
                           mb_info->eptr_bp_11, &kk_index_blocked,
                           &mb_info->index_bp_11);
            } else {
                mb_info->eptr_bp_11 = NULL;
                mb_info->index_bp_11 = ALL_ONES;
            }
        }

        if (mb_info->pawn_file_type == OP_21_PAWNS) {
            eindex = GetEndingType(Board->piece_type_count, NULL, bishop_parity,
                                   OP_21_PAWNS);
            if (eindex >= 0) {
                mb_info->eptr_op_21 = &IndexTable[eindex];
                GetMBIndex(mb_info->mb_position, mb_info->num_pieces, true,
                           mb_info->eptr_op_21, &kk_index_blocked,
                           &mb_info->index_op_21);
            } else {
                mb_info->eptr_op_21 = NULL;
                mb_info->index_op_21 = ALL_ONES;
            }
        }

        if (mb_info->pawn_file_type == OP_12_PAWNS) {
            eindex = GetEndingType(Board->piece_type_count, NULL, bishop_parity,
                                   OP_12_PAWNS);
            if (eindex >= 0) {
                mb_info->eptr_op_12 = &IndexTable[eindex];
                GetMBIndex(mb_info->mb_position, mb_info->num_pieces, true,
                           mb_info->eptr_op_12, &kk_index_blocked,
                           &mb_info->index_op_12);
            } else {
                mb_info->eptr_op_12 = NULL;
                mb_info->index_op_12 = ALL_ONES;
            }
        }

        if (mb_info->pawn_file_type == OP_22_PAWNS ||
            mb_info->pawn_file_type == DP_22_PAWNS) {
            eindex = GetEndingType(Board->piece_type_count, NULL, bishop_parity,
                                   OP_22_PAWNS);
            if (eindex >= 0) {
                mb_info->eptr_op_22 = &IndexTable[eindex];
                GetMBIndex(mb_info->mb_position, mb_info->num_pieces, true,
                           mb_info->eptr_op_22, &kk_index_blocked,
                           &mb_info->index_op_22);
            } else {
                mb_info->eptr_op_22 = NULL;
                mb_info->index_op_22 = ALL_ONES;
            }
        }

        if (mb_info->pawn_file_type == DP_22_PAWNS) {
            eindex = GetEndingType(Board->piece_type_count, NULL, bishop_parity,
                                   DP_22_PAWNS);
            if (eindex >= 0) {
                mb_info->eptr_dp_22 = &IndexTable[eindex];
                GetMBIndex(mb_info->mb_position, mb_info->num_pieces, true,
                           mb_info->eptr_dp_22, &kk_index_blocked,
                           &mb_info->index_dp_22);
            } else {
                mb_info->eptr_dp_22 = NULL;
                mb_info->index_dp_22 = ALL_ONES;
            }
        }

        if (mb_info->pawn_file_type == OP_31_PAWNS) {
            eindex = GetEndingType(Board->piece_type_count, NULL, bishop_parity,
                                   OP_31_PAWNS);
            if (eindex >= 0) {
                mb_info->eptr_op_31 = &IndexTable[eindex];
                GetMBIndex(mb_info->mb_position, mb_info->num_pieces, true,
                           mb_info->eptr_op_31, &kk_index_blocked,
                           &mb_info->index_op_31);
            } else {
                mb_info->eptr_op_31 = NULL;
                mb_info->index_op_31 = ALL_ONES;
            }
        }

        if (mb_info->pawn_file_type == OP_13_PAWNS) {
            eindex = GetEndingType(Board->piece_type_count, NULL, bishop_parity,
                                   OP_13_PAWNS);
            if (eindex >= 0) {
                mb_info->eptr_op_13 = &IndexTable[eindex];
                GetMBIndex(mb_info->mb_position, mb_info->num_pieces, true,
                           mb_info->eptr_op_13, &kk_index_blocked,
                           &mb_info->index_op_13);
            } else {
                mb_info->eptr_op_13 = NULL;
                mb_info->index_op_13 = ALL_ONES;
            }
        }

        if (mb_info->pawn_file_type == OP_41_PAWNS) {
            eindex = GetEndingType(Board->piece_type_count, NULL, bishop_parity,
                                   OP_41_PAWNS);
            if (eindex >= 0) {
                mb_info->eptr_op_41 = &IndexTable[eindex];
                GetMBIndex(mb_info->mb_position, mb_info->num_pieces, true,
                           mb_info->eptr_op_41, &kk_index_blocked,
                           &mb_info->index_op_41);
            } else {
                mb_info->eptr_op_41 = NULL;
                mb_info->index_op_41 = ALL_ONES;
            }
        }

        if (mb_info->pawn_file_type == OP_14_PAWNS) {
            eindex = GetEndingType(Board->piece_type_count, NULL, bishop_parity,
                                   OP_14_PAWNS);
            if (eindex >= 0) {
                mb_info->eptr_op_14 = &IndexTable[eindex];
                GetMBIndex(mb_info->mb_position, mb_info->num_pieces, true,
                           mb_info->eptr_op_14, &kk_index_blocked,
                           &mb_info->index_op_14);
            } else {
                mb_info->eptr_op_14 = NULL;
                mb_info->index_op_14 = ALL_ONES;
            }
        }

        if (mb_info->pawn_file_type == OP_32_PAWNS) {
            eindex = GetEndingType(Board->piece_type_count, NULL, bishop_parity,
                                   OP_32_PAWNS);
            if (eindex >= 0) {
                mb_info->eptr_op_32 = &IndexTable[eindex];
                GetMBIndex(mb_info->mb_position, mb_info->num_pieces, true,
                           mb_info->eptr_op_32, &kk_index_blocked,
                           &mb_info->index_op_32);
            } else {
                mb_info->eptr_op_32 = NULL;
                mb_info->index_op_32 = ALL_ONES;
            }
        }

        if (mb_info->pawn_file_type == OP_23_PAWNS) {
            eindex = GetEndingType(Board->piece_type_count, NULL, bishop_parity,
                                   OP_23_PAWNS);
            if (eindex >= 0) {
                mb_info->eptr_op_23 = &IndexTable[eindex];
                GetMBIndex(mb_info->mb_position, mb_info->num_pieces, true,
                           mb_info->eptr_op_23, &kk_index_blocked,
                           &mb_info->index_op_23);
            } else {
                mb_info->eptr_op_23 = NULL;
                mb_info->index_op_23 = ALL_ONES;
            }
        }

        if (mb_info->pawn_file_type == OP_33_PAWNS) {
            eindex = GetEndingType(Board->piece_type_count, NULL, bishop_parity,
                                   OP_33_PAWNS);
            if (eindex >= 0) {
                mb_info->eptr_op_33 = &IndexTable[eindex];
                GetMBIndex(mb_info->mb_position, mb_info->num_pieces, true,
                           mb_info->eptr_op_33, &kk_index_blocked,
                           &mb_info->index_op_33);
            } else {
                mb_info->eptr_op_33 = NULL;
                mb_info->index_op_33 = ALL_ONES;
            }
        }

        if (mb_info->pawn_file_type == OP_42_PAWNS) {
            eindex = GetEndingType(Board->piece_type_count, NULL, bishop_parity,
                                   OP_42_PAWNS);
            if (eindex >= 0) {
                mb_info->eptr_op_42 = &IndexTable[eindex];
                GetMBIndex(mb_info->mb_position, mb_info->num_pieces, true,
                           mb_info->eptr_op_42, &kk_index_blocked,
                           &mb_info->index_op_42);
            } else {
                mb_info->eptr_op_42 = NULL;
                mb_info->index_op_42 = ALL_ONES;
            }
        }

        if (mb_info->pawn_file_type == OP_24_PAWNS) {
            eindex = GetEndingType(Board->piece_type_count, NULL, bishop_parity,
                                   OP_24_PAWNS);
            if (eindex >= 0) {
                mb_info->eptr_op_24 = &IndexTable[eindex];
                GetMBIndex(mb_info->mb_position, mb_info->num_pieces, true,
                           mb_info->eptr_op_24, &kk_index_blocked,
                           &mb_info->index_op_24);
            } else {
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
    }

    // now gather index for specific bishop parity

    eindex =
        GetEndingType(Board->piece_type_count, NULL, bishop_parity, FREE_PAWNS);

    if (eindex >= 0) {
        memcpy(mb_info->parity_index[mb_info->num_parities].bishop_parity,
               bishop_parity, sizeof(bishop_parity));
        mb_info->parity_index[mb_info->num_parities].eptr = &IndexTable[eindex];
        mb_info->num_parities++;
    }

    // if both white and black have parity constraints, can add cases where only
    // one side is constrained

    if (bishop_parity[WHITE] != NONE && bishop_parity[BLACK] != NONE) {
        int sub_bishop_parity[2];
        sub_bishop_parity[WHITE] = bishop_parity[WHITE];
        sub_bishop_parity[BLACK] = NONE;

        eindex = GetEndingType(Board->piece_type_count, NULL, sub_bishop_parity,
                               FREE_PAWNS);

        if (eindex >= 0) {
            memcpy(mb_info->parity_index[mb_info->num_parities].bishop_parity,
                   sub_bishop_parity, sizeof(sub_bishop_parity));
            mb_info->parity_index[mb_info->num_parities].eptr =
                &IndexTable[eindex];
            mb_info->num_parities++;
        }

        sub_bishop_parity[WHITE] = NONE;
        sub_bishop_parity[BLACK] = bishop_parity[BLACK];

        eindex = GetEndingType(Board->piece_type_count, NULL, sub_bishop_parity,
                               FREE_PAWNS);

        if (eindex >= 0) {
            memcpy(mb_info->parity_index[mb_info->num_parities].bishop_parity,
                   sub_bishop_parity, sizeof(sub_bishop_parity));
            mb_info->parity_index[mb_info->num_parities].eptr =
                &IndexTable[eindex];
            mb_info->num_parities++;
        }
    }

    if (mb_info->num_parities == 0) {
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

#ifdef USE_YK
static int GetYKInfo(const BOARD *Board, YK_INFO *yk_info) {
    if (Board->num_pieces > MAX_PIECES_YK) {
        return TOO_MANY_PIECES;
    }

    memcpy(yk_info->piece_type_count, Board->piece_type_count,
           sizeof(Board->piece_type_count));

    yk_info->num_pieces = Board->num_pieces;

    GetYKPosition(Board, yk_info->yk_position);

    memset(yk_info->yk_piece_types, 0, sizeof(yk_info->yk_piece_types));

    int eindex =
        GetEndingTypeYK(Board->piece_type_count, yk_info->yk_piece_types);

    if (eindex < 0)
        return ETYPE_NOT_MAPPED;

    bool pawns_present = yk_info->piece_type_count[WHITE][PAWN] ||
                         yk_info->piece_type_count[BLACK][PAWN];

    yk_info->eptr = &IndexTable[eindex];

    GetYKIndex(yk_info->yk_position, yk_info->num_pieces, pawns_present,
               yk_info->eptr, &yk_info->kk_index, &yk_info->index);

    return 0;
}
#endif // USE_YK

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
#ifdef USE_YK
static int GetYKResult(CONTEXT *ctx, const BOARD *Board, INDEX_DATA *ind) {
    YK_INFO yk_info;
    FILE_CACHE_YK *fcache = NULL;

    if (Board->num_pieces > MAX_PIECES_YK) {
        return TOO_MANY_PIECES;
    }

    int result = GetYKInfo(Board, &yk_info);

    if (yk_info.index > 0xefffffff) {
        return BAD_ZONE_SIZE;
    }

    ind->kk_index = yk_info.kk_index;
    ind->index = yk_info.index;

    if (result < 0)
        return result;

    int side = Board->side;

    // check whether file for ending is already opened

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

    // if file pointer is not cached, need to open new file

    if (file_index == -1) {
        char ending[64];
        GetEndingName(yk_info.piece_type_count, ending);

        char base_name[64];
        GetYKBaseFileName(yk_info.piece_type_count, side, base_name);
        strcat(base_name, ".yk");

        file file_yk = OpenYKFile(base_name);

        if (file_yk == NULL) {
            return YK_FILE_MISSING;
        }

        if (num_cached_files_yk[side] < MAX_FILES_YK) {
            file_index = num_cached_files_yk[side];
            num_cached_files_yk[side]++;
        } else
            file_index = cached_file_lru_yk[MAX_FILES_YK - 1][side];

        fcache = &FileCacheYK[file_index][side];

        if (fcache->fp != NULL) {
            f_close(fcache->fp);
        }

        fcache->fp = file_yk;

        uint8_t header[YK_HEADER_SIZE];
        if (f_read(&header, YK_HEADER_SIZE, file_yk, 0) != YK_HEADER_SIZE) {
            f_close(file_yk);
            return HEADER_READ_ERROR;
        }

        uint32_t block_size, num_blocks;

        memcpy(&block_size, &header[0], 4);
        memcpy(&num_blocks, &header[4], 4);

        if (num_blocks > fcache->max_num_blocks) {
            if (fcache->max_num_blocks > 0) {
                MyFree(fcache->offsets);
            }
            fcache->max_num_blocks = num_blocks;
            fcache->offsets =
                (INDEX *)MyMalloc((fcache->max_num_blocks + 1) * sizeof(INDEX));
        }

        fcache->num_blocks = num_blocks;

        if (f_read(fcache->offsets, (num_blocks + 1) * sizeof(INDEX), file_yk,
                   YK_HEADER_SIZE) != (num_blocks + 1) * sizeof(INDEX)) {
            f_close(file_yk);
            return OFFSET_READ_ERROR;
        }

        fcache->block_size = block_size;

        uint8_t archive_id;

        memcpy(&archive_id, &header[23], 1);

        if (archive_id == NO_COMPRESSION_YK)
            fcache->compression_method = NO_COMPRESSION;
        else if (archive_id == ZLIB_YK)
            fcache->compression_method = ZLIB;
        else if (archive_id == ZSTD_YK)
            fcache->compression_method = ZSTD;
        else if (archive_id == LZMA_YK) {
            fprintf(stderr, "LZMA decompression not supported\n");
            abort();
        } else if (archive_id == BZIP_YK) {
            fprintf(stderr, "BZIP decompression not supported\n");
            abort();
        } else {
            fprintf(stderr, "Unknown decompression method %d in YK file %s\n",
                    archive_id, base_name);
            abort();
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
        uint32_t length =
            fcache->offsets[b_index + 1] - fcache->offsets[b_index];
        if (length > ctx->compressed_buffer_size) {
            ctx->compressed_buffer =
                (uint8_t *)MyRealloc(ctx->compressed_buffer, length);
            ctx->compressed_buffer_size = length;
        }
        f_read(ctx->compressed_buffer, length, fcache->fp,
               fcache->offsets[b_index]);
        uint32_t tmp_zone_size = fcache->block_size;
        if (tmp_zone_size > fcache->max_block_size) {
            if (fcache->block != NULL) {
                MyFree(fcache->block);
            }
            fcache->max_block_size = tmp_zone_size;
            fcache->block = (uint8_t *)MyMalloc(tmp_zone_size);
        }
        MyUncompress(ctx, fcache->block, &tmp_zone_size, ctx->compressed_buffer,
                     length, fcache->compression_method);
        assert(tmp_zone_size == fcache->block_size);
        fcache->block_index = b_index;
    }

    result = fcache->block[ind->index % fcache->block_size];

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
                    return HIGH_DTZ_MISSING;
                }

                fcache->fp_high = file_yk;
            }
            if (fcache->num_high_dtc > 0) {
                fcache->block_high =
                    (HDATA *)MyMalloc(fcache->num_high_dtc * sizeof(HDATA));
                INDEX nread = f_read(fcache->block_high,
                                     fcache->num_high_dtc * sizeof(HDATA),
                                     fcache->fp_high, 0);
                if (nread != fcache->num_high_dtc * sizeof(HDATA)) {
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
            return tptr->dtc;
        }
    } else if (result == 255) {
        result = UNRESOLVED;
    }

    return result;
}
#endif // USE_YK

static int GetMBResult(CONTEXT *ctx, const BOARD *Board, INDEX_DATA *ind) {
    MB_INFO mb_info = {0};
    FILE_CACHE *fcache;

    int result = GetMBInfo(Board, &mb_info);

    ind->kk_index = mb_info.kk_index;
    ind->index = mb_info.parity_index[0].index;

    if (result < 0) {
        return result;
    }

    int side = Board->side;

    // check whether file for ending is already opened

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
                    found_pawn_file = true;
                }
            }

            if (!found_pawn_file && mb_info.pawn_file_type == BP_11_PAWNS &&
                fcache->pawn_file_type == BP_11_PAWNS) {
                if (mb_info.index_bp_11 != ALL_ONES) {
                    ind->index = mb_info.index_bp_11;
                    found_pawn_file = true;
                }
            }

            if (!found_pawn_file && mb_info.pawn_file_type == OP_21_PAWNS &&
                fcache->pawn_file_type == OP_21_PAWNS) {
                if (mb_info.index_op_21 != ALL_ONES) {
                    ind->index = mb_info.index_op_21;
                    found_pawn_file = true;
                }
            }

            if (!found_pawn_file && mb_info.pawn_file_type == OP_12_PAWNS &&
                fcache->pawn_file_type == OP_12_PAWNS) {
                if (mb_info.index_op_12 != ALL_ONES) {
                    ind->index = mb_info.index_op_12;
                    found_pawn_file = true;
                }
            }

            if (!found_pawn_file &&
                (mb_info.pawn_file_type == OP_22_PAWNS ||
                 mb_info.pawn_file_type == DP_22_PAWNS) &&
                fcache->pawn_file_type == OP_22_PAWNS) {
                if (mb_info.index_op_22 != ALL_ONES) {
                    ind->index = mb_info.index_op_22;
                    found_pawn_file = true;
                }
            }

            if (!found_pawn_file && mb_info.pawn_file_type == DP_22_PAWNS &&
                fcache->pawn_file_type == DP_22_PAWNS) {
                if (mb_info.index_dp_22 != ALL_ONES) {
                    ind->index = mb_info.index_dp_22;
                    found_pawn_file = true;
                }
            }

            if (!found_pawn_file && mb_info.pawn_file_type == OP_31_PAWNS &&
                fcache->pawn_file_type == OP_31_PAWNS) {
                if (mb_info.index_op_31 != ALL_ONES) {
                    ind->index = mb_info.index_op_31;
                    found_pawn_file = true;
                }
            }

            if (!found_pawn_file && mb_info.pawn_file_type == OP_13_PAWNS &&
                fcache->pawn_file_type == OP_13_PAWNS) {
                if (mb_info.index_op_13 != ALL_ONES) {
                    ind->index = mb_info.index_op_13;
                    found_pawn_file = true;
                }
            }

            if (!found_pawn_file && mb_info.pawn_file_type == OP_41_PAWNS &&
                fcache->pawn_file_type == OP_41_PAWNS) {
                if (mb_info.index_op_41 != ALL_ONES) {
                    ind->index = mb_info.index_op_41;
                    found_pawn_file = true;
                }
            }

            if (!found_pawn_file && mb_info.pawn_file_type == OP_14_PAWNS &&
                fcache->pawn_file_type == OP_14_PAWNS) {
                if (mb_info.index_op_14 != ALL_ONES) {
                    ind->index = mb_info.index_op_14;
                    found_pawn_file = true;
                }
            }

            if (!found_pawn_file && mb_info.pawn_file_type == OP_32_PAWNS &&
                fcache->pawn_file_type == OP_32_PAWNS) {
                if (mb_info.index_op_32 != ALL_ONES) {
                    ind->index = mb_info.index_op_32;
                    found_pawn_file = true;
                }
            }

            if (!found_pawn_file && mb_info.pawn_file_type == OP_23_PAWNS &&
                fcache->pawn_file_type == OP_23_PAWNS) {
                if (mb_info.index_op_23 != ALL_ONES) {
                    ind->index = mb_info.index_op_23;
                    found_pawn_file = true;
                }
            }

            if (!found_pawn_file && mb_info.pawn_file_type == OP_33_PAWNS &&
                fcache->pawn_file_type == OP_33_PAWNS) {
                if (mb_info.index_op_33 != ALL_ONES) {
                    ind->index = mb_info.index_op_33;
                    found_pawn_file = true;
                }
            }

            if (!found_pawn_file && mb_info.pawn_file_type == OP_42_PAWNS &&
                fcache->pawn_file_type == OP_42_PAWNS) {
                if (mb_info.index_op_42 != ALL_ONES) {
                    ind->index = mb_info.index_op_42;
                    found_pawn_file = true;
                }
            }

            if (!found_pawn_file && mb_info.pawn_file_type == OP_24_PAWNS &&
                fcache->pawn_file_type == OP_24_PAWNS) {
                if (mb_info.index_op_24 != ALL_ONES) {
                    ind->index = mb_info.index_op_24;
                    found_pawn_file = true;
                }
            }
        }

        if (!found_pawn_file) {
            continue;
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

    if (file_index == -1) {
        char ending[64];
        GetEndingName(mb_info.piece_type_count, ending);

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

        int pawn_file_type = FREE_PAWNS;

        if (file_mb == NULL) {
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
#ifdef USE_YK
                INDEX_DATA ind_yk;
                return GetYKResult(ctx, Board, &ind_yk);
#else
                return UNKNOWN;
#endif
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
                MyFree(fcache->offsets);
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

        // move file index to front of queue
        if (num_cached_files[side] > 1) {
            for (int i = num_cached_files[side] - 1; i > 0; i--) {
                cached_file_lru[i][side] = cached_file_lru[i - 1][side];
            }
        }
        cached_file_lru[0][side] = file_index;
    }

    int b_index = ind->index / fcache->header.block_size;

    // load and decompress block

    uint32_t length = fcache->offsets[b_index + 1] - fcache->offsets[b_index];
    if (length > ctx->compressed_buffer_size) {
        ctx->compressed_buffer =
            (uint8_t *)MyRealloc(ctx->compressed_buffer, length);
        ctx->compressed_buffer_size = length;
    }
    f_read(ctx->compressed_buffer, length, fcache->fp,
           fcache->offsets[b_index]);
    uint32_t tmp_zone_size = fcache->header.block_size;
    if (tmp_zone_size > ctx->block_buffer_size) {
        ctx->block_buffer =
            (uint8_t *)MyRealloc(ctx->block_buffer, tmp_zone_size);
        ctx->block_buffer_size = tmp_zone_size;
    }
    MyUncompress(ctx, ctx->block_buffer, &tmp_zone_size, ctx->compressed_buffer,
                 length, fcache->header.compression_method);
    assert(tmp_zone_size == fcache->header.block_size);

    result = ctx->block_buffer[ind->index % fcache->header.block_size];

    // check for results with > 254 moves if possible

    if (result == 254 && fcache->header.max_depth > 254) {
        file_index = -1;
        FILE_CACHE_HIGH_DTZ *fcache_high_dtz = NULL;
        uint32_t n_per_block = 0;

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

            if (!found_parity) {
                bool found_pawn_file = false;
                if ((mb_info.pawn_file_type == OP_11_PAWNS ||
                     mb_info.pawn_file_type == BP_11_PAWNS) &&
                    fcache_high_dtz->pawn_file_type == OP_11_PAWNS) {
                    if (mb_info.index_op_11 != ALL_ONES) {
                        ind->index = mb_info.index_op_11;
                        found_pawn_file = true;
                    }
                }

                if (!found_pawn_file && mb_info.pawn_file_type == BP_11_PAWNS &&
                    fcache_high_dtz->pawn_file_type == BP_11_PAWNS) {
                    if (mb_info.index_bp_11 != ALL_ONES) {
                        ind->index = mb_info.index_bp_11;
                        found_pawn_file = true;
                    }
                }

                if (!found_pawn_file && mb_info.pawn_file_type == OP_21_PAWNS &&
                    fcache_high_dtz->pawn_file_type == OP_21_PAWNS) {
                    if (mb_info.index_op_21 != ALL_ONES) {
                        ind->index = mb_info.index_op_21;
                        found_pawn_file = true;
                    }
                }

                if (!found_pawn_file && mb_info.pawn_file_type == OP_12_PAWNS &&
                    fcache_high_dtz->pawn_file_type == OP_12_PAWNS) {
                    if (mb_info.index_op_12 != ALL_ONES) {
                        ind->index = mb_info.index_op_12;
                        found_pawn_file = true;
                    }
                }

                if (!found_pawn_file &&
                    (mb_info.pawn_file_type == OP_22_PAWNS ||
                     mb_info.pawn_file_type == DP_22_PAWNS) &&
                    fcache_high_dtz->pawn_file_type == OP_22_PAWNS) {
                    if (mb_info.index_op_22 != ALL_ONES) {
                        ind->index = mb_info.index_op_22;
                        found_pawn_file = true;
                    }
                }

                if (!found_pawn_file && mb_info.pawn_file_type == DP_22_PAWNS &&
                    fcache_high_dtz->pawn_file_type == DP_22_PAWNS) {
                    if (mb_info.index_dp_22 != ALL_ONES) {
                        ind->index = mb_info.index_dp_22;
                        found_pawn_file = true;
                    }
                }

                if (!found_pawn_file && mb_info.pawn_file_type == OP_31_PAWNS &&
                    fcache_high_dtz->pawn_file_type == OP_31_PAWNS) {
                    if (mb_info.index_op_31 != ALL_ONES) {
                        ind->index = mb_info.index_op_31;
                        found_pawn_file = true;
                    }
                }

                if (!found_pawn_file && mb_info.pawn_file_type == OP_13_PAWNS &&
                    fcache_high_dtz->pawn_file_type == OP_13_PAWNS) {
                    if (mb_info.index_op_13 != ALL_ONES) {
                        ind->index = mb_info.index_op_13;
                        found_pawn_file = true;
                    }
                }

                if (!found_pawn_file && mb_info.pawn_file_type == OP_41_PAWNS &&
                    fcache_high_dtz->pawn_file_type == OP_41_PAWNS) {
                    if (mb_info.index_op_41 != ALL_ONES) {
                        ind->index = mb_info.index_op_41;
                        found_pawn_file = true;
                    }
                }

                if (!found_pawn_file && mb_info.pawn_file_type == OP_14_PAWNS &&
                    fcache_high_dtz->pawn_file_type == OP_14_PAWNS) {
                    if (mb_info.index_op_14 != ALL_ONES) {
                        ind->index = mb_info.index_op_14;
                        found_pawn_file = true;
                    }
                }

                if (!found_pawn_file && mb_info.pawn_file_type == OP_32_PAWNS &&
                    fcache_high_dtz->pawn_file_type == OP_32_PAWNS) {
                    if (mb_info.index_op_32 != ALL_ONES) {
                        ind->index = mb_info.index_op_32;
                        found_pawn_file = true;
                    }
                }

                if (!found_pawn_file && mb_info.pawn_file_type == OP_23_PAWNS &&
                    fcache_high_dtz->pawn_file_type == OP_23_PAWNS) {
                    if (mb_info.index_op_23 != ALL_ONES) {
                        ind->index = mb_info.index_op_23;
                        found_pawn_file = true;
                    }
                }

                if (!found_pawn_file && mb_info.pawn_file_type == OP_33_PAWNS &&
                    fcache_high_dtz->pawn_file_type == OP_33_PAWNS) {
                    if (mb_info.index_op_33 != ALL_ONES) {
                        ind->index = mb_info.index_op_33;
                        found_pawn_file = true;
                    }
                }

                if (!found_pawn_file && mb_info.pawn_file_type == OP_42_PAWNS &&
                    fcache_high_dtz->pawn_file_type == OP_42_PAWNS) {
                    if (mb_info.index_op_42 != ALL_ONES) {
                        ind->index = mb_info.index_op_42;
                        found_pawn_file = true;
                    }
                }

                if (!found_pawn_file && mb_info.pawn_file_type == OP_24_PAWNS &&
                    fcache_high_dtz->pawn_file_type == OP_24_PAWNS) {
                    if (mb_info.index_op_24 != ALL_ONES) {
                        ind->index = mb_info.index_op_24;
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

        // if file pointer is not cached, need to open new file

        if (file_index == -1) {
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
                return HIGH_DTZ_MISSING;
            }

            if (fcache_high_dtz->header.num_blocks >
                fcache_high_dtz->max_num_blocks) {
                if (fcache_high_dtz->max_num_blocks > 0) {
                    MyFree(fcache_high_dtz->offsets);
                    MyFree(fcache_high_dtz->starting_index);
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
            uint32_t block_size = fcache_high_dtz->header.block_size;
            if (block_size > ctx->block_buffer_size) {
                ctx->block_buffer =
                    (uint8_t *)MyRealloc(ctx->block_buffer, block_size);
                ctx->block_buffer_size = block_size;
            }
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
            return 254;
        }

        n_per_block = fcache_high_dtz->header.block_size /
                      fcache_high_dtz->header.list_element_size;

        // read block from file.
        // do binary search to find which block of indices the depth would
        // be
        int block_index = 0, r = fcache_high_dtz->header.num_blocks;
        while (block_index < r) {
            int m = (block_index + r) / 2;
            if (fcache_high_dtz->starting_index[m] < ind->index)
                block_index = m + 1;
            else
                r = m;
        }
        if ((block_index == fcache_high_dtz->header.num_blocks) ||
            (fcache_high_dtz->starting_index[block_index] > ind->index))
            block_index--;
        // now read block from file
        uint32_t length = fcache_high_dtz->offsets[block_index + 1] -
                          fcache_high_dtz->offsets[block_index];
        if (length > ctx->compressed_buffer_size) {
            ctx->compressed_buffer =
                (uint8_t *)MyRealloc(ctx->compressed_buffer, length);
            ctx->compressed_buffer_size = length;
        }
        f_read(ctx->compressed_buffer, length, fcache_high_dtz->fp,
               fcache_high_dtz->offsets[block_index]);
        uint32_t n_per_block_cached = n_per_block;
        if (block_index == fcache_high_dtz->header.num_blocks - 1) {
            uint32_t rem = fcache_high_dtz->header.n_elements % n_per_block;
            if (rem != 0)
                n_per_block_cached = rem;
        }
        uint32_t tmp_zone_size = n_per_block_cached * sizeof(HIGH_DTZ);
        MyUncompress(ctx, (uint8_t *)ctx->block_buffer, &tmp_zone_size,
                     ctx->compressed_buffer, length,
                     fcache_high_dtz->header.compression_method);
        assert(tmp_zone_size == n_per_block_cached * sizeof(HIGH_DTZ));

        // now perform binary search on block that may contain score
        // corresponding to index
        HIGH_DTZ key;
        key.index = ind->index;

        HIGH_DTZ *kptr =
            (HIGH_DTZ *)bsearch(&key, ctx->block_buffer, n_per_block_cached,
                                sizeof(HIGH_DTZ), high_dtz_compar);

        if (kptr == NULL)
            result = 254;
        else
            result = kptr->score;
    } else {
        if (result == 255)
            result = UNRESOLVED;
    }

    if (result == UNKNOWN) {
#ifdef USE_YK
        INDEX_DATA ind_yk;
        result = GetYKResult(ctx, Board, &ind_yk);
#endif
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
static int ScorePosition(CONTEXT *ctx, const BOARD *BoardIn,
                         INDEX_DATA *index) {
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

    if (BoardIn->num_pieces > MAX_PIECES_MB || BoardIn->castle)
        return UNKNOWN;

    BOARD Board;
    memcpy(&Board, BoardIn, sizeof(Board));

    // make the stronger side white to reduce chance of having
    // to probe "flipped" position
    if (Board.strength_w < Board.strength_b) {
        FlipBoard(&Board);
    }

    int result = GetMBResult(ctx, &Board, index);

    // if we have definite result, can return right away
    if (!(result < 0 || result == UNRESOLVED)) {
        if ((Board.side == WHITE) || result == LOST || result == WON ||
            result == HIGH_DTZ_MISSING) {
            return result;
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
        }
    }

    FlipBoard(&Board);

    INDEX_DATA index2;
    int result_flipped = GetMBResult(ctx, &Board, &index2);

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
    }

    return UNKNOWN;
}

static void AssertScore(CONTEXT *ctx, const char *fen, int expected_score) {
    char mutable_fen[256] = {0};
    strncpy(mutable_fen, fen, sizeof(mutable_fen) - 1);

    BOARD Board;
    int side = ReadPosition(mutable_fen, &Board, NULL);
    assert(side != NEUTRAL);

    INDEX_DATA index;
    int score = ScorePosition(ctx, &Board, &index);
    if (score != expected_score) {
        printf("FEN: %s - expected score: %d, actual score: %d\n", fen,
               expected_score, score);
        abort();
    }
}

void ykmb_init(void) {
    InitTransforms();
    InitParity();
    InitPieceStrengths();
    InitCaches();
    InitPermutationTables();
}

void ykmb_add_path(const char *path) {
    TbPaths = MyRealloc(TbPaths, (NumPaths + 1) * sizeof(char *));
    TbPaths[NumPaths] = strdup(path);
    assert(TbPaths[NumPaths] != NULL);
    if (TbPaths[NumPaths] == NULL)
        abort();
    NumPaths++;
}

CONTEXT *ykmb_context_create() {
    CONTEXT *context = (CONTEXT *)MyMalloc(sizeof(CONTEXT));
    memset(context, 0, sizeof(CONTEXT));

    context->zstd_decompression = ZSTD_createDCtx();
    assert(context->zstd_decompression != NULL);

    return context;
}

void ykmb_context_destroy(CONTEXT *context) {
    assert(context != NULL);

    MyFree(context->compressed_buffer);
    MyFree(context->block_buffer);

    ZSTD_freeDCtx(context->zstd_decompression);
    MyFree(context);
}

BOARD *ykmb_board_create() {
    BOARD *board = (BOARD *)MyMalloc(sizeof(BOARD));
    memset(board, 0, sizeof(BOARD));
    return board;
}

void ykmb_board_set(BOARD *board, const int pieces[NSQUARES], int side,
                    int ep_square, int castle, int half_move, int full_move) {
    assert(board != NULL);
    SetBoard(board, pieces, side, ep_square, castle, half_move, full_move);
}

void ykmb_board_destroy(BOARD *board) {
    assert(board != NULL);
    MyFree(board);
}

int ykmb_probe(CONTEXT *ctx, const BOARD *board) {
    assert(ctx != NULL);
    assert(board != NULL);

    INDEX_DATA index = {0};
    return ScorePosition(ctx, board, &index);
}

int main(int argc, char *argv[]) {
    ykmb_init();
    ykmb_add_path(".");

    assert(IsWinningScore(1));
    assert(IsLosingScore(-1));
    assert(ScoreCompare(1, 2) < 0);

    CONTEXT *ctx = ykmb_context_create();

    AssertScore(ctx, "8/2b5/8/8/3P4/pPP5/P7/2k1K3 w - - 0 1", -3);
    AssertScore(ctx, "8/2b5/8/8/3P4/pPP5/P7/1k2K3 w - - 0 1", -1);
    AssertScore(ctx, "8/p1b5/8/8/3P4/1PP5/P7/1k2K3 w - - 0 1", -2);
    AssertScore(ctx, "8/p1b5/8/2PP4/PP6/8/8/1k2K3 b - - 0 1", -7);
    AssertScore(ctx, "8/p1b5/8/2PP4/PP6/8/8/1k2K3 w - - 0 1", 6);
    AssertScore(ctx, "8/2bp4/8/2PP4/PP6/8/8/1k2K3 w - - 0 1", 4);
    AssertScore(ctx, "8/1kbp4/8/2PP4/PP6/8/8/4K3 w - - 0 1", DRAW);
    AssertScore(ctx, "8/1kb1p3/8/2PP4/PP6/8/8/4K3 w - - 0 1", UNKNOWN);

    ykmb_context_destroy(ctx);

    return 0;
}
