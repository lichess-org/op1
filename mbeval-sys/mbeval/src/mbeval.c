/* Based on mbeval.cpp 7.9 */

#include "mbeval.h"

#include <assert.h>
#include <ctype.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#if (NROWS == 8) && (NCOLS == 8)
#define Row(sq) ((sq) >> 3)
#define Column(sq) ((sq)&07)
#define SquareMake(row, col) (((row) << 3) | (col))
#else
#define Row(sq) ((sq) / (NCOLS))
#define Column(sq) ((sq) % (NCOLS))
#define SquareMake(row, col) ((NCOLS) * (row) + (col))
#endif

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
#define MAX_IDENT_PIECES 10

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

typedef uint64_t INDEX;

#define ZERO ((ZINDEX)0)
#define ONE ((ZINDEX)1)
#define ALL_ONES (~(ZERO))

static void *MyMalloc(size_t cb) {
    void *pv = malloc(cb);
    assert(pv != NULL);
    if (pv == NULL)
        abort();
    return pv;
}

enum { BASE = 0, EE, NE, NN, NW, WW, SW, SS, SE };

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
};

typedef ZINDEX (*pt2index)(int *);

typedef struct {
    PIECE board[NSQUARES];
    int ep_square;
    int num_pieces;
    int piece_type_count[2][KING];
    int piece_locations[2][KING][MAX_IDENT_PIECES];
    int wkpos, bkpos;
    SIDE side;
} BOARD;

static int ParityTable[NSQUARES];
static int WhiteSquare[NSQUARES / 2], BlackSquare[NSQUARES / 2];

// For 5 or more identical pieces, always compute index rather than creating
// lookup table

static ZINDEX k5_tab[NSQUARES + 1];

static ZINDEX N5_Index(int a, int b, int c, int d, int e) {
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

static ZINDEX N6_Index(int a, int b, int c, int d, int e, int f) {
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

    return k6_tab[a] + N5_Index(b, c, d, e, f);
}

static ZINDEX k7_tab[NSQUARES + 1];

static ZINDEX N7_Index(int a, int b, int c, int d, int e, int f, int g) {
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

    return k7_tab[a] + N6_Index(b, c, d, e, f, g);
}

static int *k2_tab = NULL, *k3_tab = NULL, *k4_tab = NULL;
static int *k2_even_tab = NULL;
static int *k2_odd_tab = NULL;
static int *k3_even_tab = NULL;
static int *k3_odd_tab = NULL;

static int *k2_opposing_tab = NULL;
static int *k2_1_opposing_tab = NULL;
static int *k1_2_opposing_tab = NULL;
static int *k4_opposing_tab = NULL;
static int *k2_2_opposing_tab = NULL;
static int *k3_1_opposing_tab = NULL;
static int *k1_3_opposing_tab = NULL;

static int N2_Index(int a, int b) { return (k2_tab[(a) | ((b) << 6)]); }
static int N3_Index(int a, int b, int c) {
    return (k3_tab[(a) | ((b) << 6) | ((c) << 12)]);
}
static int N4_Index(int a, int b, int c, int d) {
    return (k4_tab[(a) | ((b) << 6) | ((c) << 12) | ((d) << 18)]);
}
static int N2_Odd_Index(int a, int b) { return (k2_odd_tab[(a) | ((b) << 6)]); }
static int N2_Even_Index(int a, int b) {
    return (k2_even_tab[(a) | ((b) << 6)]);
}
static int N3_Odd_Index(int a, int b, int c) {
    return (k3_odd_tab[(a) | ((b) << 6) | ((c) << 12)]);
}
static int N3_Even_Index(int a, int b, int c) {
    return (k3_even_tab[(a) | ((b) << 6) | ((c) << 12)]);
}
static int N2_Opposing_Index(int a, int b) {
    return (k2_opposing_tab[(a) | ((b) << 6)]);
}
static int N4_Opposing_Index(int a, int b, int c, int d) {
    return (k4_opposing_tab[((a) >> 3) | ((b)&070) | ((c) << 6) | ((d) << 12)]);
}

static int N2_1_Opposing_Index(int a, int b, int c) {
    return (k2_1_opposing_tab[(a) | ((b) << 6) | ((c) << 12)]);
}
static int N1_2_Opposing_Index(int a, int b, int c) {
    return (k1_2_opposing_tab[(a) | ((b) << 6) | ((c) << 12)]);
}
static int N3_1_Opposing_Index(int a, int b, int c, int d) {
    return (k3_1_opposing_tab[(a) | ((b) << 6) | ((c) << 12) | ((d) << 18)]);
}
static int N1_3_Opposing_Index(int a, int b, int c, int d) {
    return (k1_3_opposing_tab[(a) | ((b) << 6) | ((c) << 12) | ((d) << 18)]);
}
static int N2_2_Opposing_Index(int a, int b, int c, int d) {
    return (k2_2_opposing_tab[(a) | ((b) << 6) | ((c) << 12) | ((d) << 18)]);
}

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

static void GetFlipFunction(__attribute__((unused)) int wk,
                            __attribute__((unused)) int bk, bool *flipped,
                            __attribute__((unused)) int **transform) {
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

static void InitN2Tables(int *tab) {
    int index = 0, score;

    for (int p1 = 0; p1 < NSQUARES; p1++) {
        for (int p2 = p1; p2 < NSQUARES; p2++) {
            if (p1 == p2)
                score = -1;
            else {
                score = index++;
            }
            tab[p1 + NSQUARES * p2] = score;
            tab[p2 + NSQUARES * p1] = score;
        }
    }

    assert(index == N2);
}

static void InitN2OddTables(int *tab) {
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
            score = index++;
            tab[p1 + NSQUARES * p2] = score;
            tab[p2 + NSQUARES * p1] = score;
        }
    }

    assert(index == N2_ODD_PARITY);
}

static void InitN2EvenTables(int *tab) {
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
            score = index++;
            tab[p1 + NSQUARES * p2] = score;
            tab[p2 + NSQUARES * p1] = score;
        }
    }

    assert(index == N2_EVEN_PARITY);
}

static void InitN3EvenTables(int *tab) {
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

static void InitN3OddTables(int *tab) {
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

static void InitN2OpposingTables(int *tab) {
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
            index++;
        }
    }

    assert(index == NCOLS * (NROWS - 2) * (NROWS - 3) / 2);
}

static void InitN2_1_OpposingTables(int *tab) {
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

static void InitN1_2_OpposingTables(int *tab) {
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

static void InitN2_2_OpposingTables(int *tab) {
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

static void InitN3_1_OpposingTables(int *tab) {
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

static void InitN1_3_OpposingTables(int *tab) {
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

static ZINDEX IndexDP22(const int *);

static void InitN4OpposingTables(int *tab) {
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

static void InitN3Tables(int *tab) {
    int index = 0, score;

    for (int p1 = 0; p1 < NSQUARES; p1++) {
        for (int p2 = p1; p2 < NSQUARES; p2++) {
            for (int p3 = p2; p3 < NSQUARES; p3++) {
                if (p1 == p2 || p1 == p3 || p2 == p3) {
                    score = -1;
                } else {
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

static void InitN4Tables(int *tab) {
    int index = 0, score;

    for (int p1 = 0; p1 < NSQUARES; p1++) {
        for (int p2 = p1; p2 < NSQUARES; p2++) {
            for (int p3 = p2; p3 < NSQUARES; p3++) {
                for (int p4 = p3; p4 < NSQUARES; p4++) {
                    if (p1 == p2 || p1 == p3 || p1 == p4 || p2 == p3 ||
                        p2 == p4 || p3 == p4) {
                        score = -1;
                    } else {
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

static void InitN5Tables() {
    for (unsigned int i = 0; i <= NSQUARES; i++)
        k5_tab[i] = i * (i - 1) * (i - 2) * (i - 3) * (i - 4) / 120;
}

static void InitN6Tables() {
    for (unsigned int i = 0; i <= NSQUARES; i++)
        k6_tab[i] =
            (i * (i - 1) * (i - 2) * (i - 3) * (i - 4) / 120) * (i - 5) / 6;
}

static void InitN7Tables() {
    for (unsigned int i = 0; i <= NSQUARES; i++) {
        ZINDEX itmp =
            (i * (i - 1) * (i - 2) * (i - 3) * (i - 4) / 120) * (i - 5) / 6;
        if (itmp % 7)
            k7_tab[i] = itmp * ((i - 6) / 7);
        else
            k7_tab[i] = (itmp / 7) * (i - 6);
    }
}

static void InitPermutationTables(void) {
    k2_opposing_tab = (int *)MyMalloc(NSQUARES * NSQUARES * sizeof(int));
    InitN2OpposingTables(k2_opposing_tab);

    k2_1_opposing_tab =
        (int *)MyMalloc(NSQUARES * NSQUARES * NSQUARES * sizeof(int));
    InitN2_1_OpposingTables(k2_1_opposing_tab);

    k1_2_opposing_tab =
        (int *)MyMalloc(NSQUARES * NSQUARES * NSQUARES * sizeof(int));
    InitN1_2_OpposingTables(k1_2_opposing_tab);

    k2_2_opposing_tab = (int *)MyMalloc(NSQUARES * NSQUARES * NSQUARES *
                                        NSQUARES * sizeof(int));
    InitN2_2_OpposingTables(k2_2_opposing_tab);

    k3_1_opposing_tab = (int *)MyMalloc(NSQUARES * NSQUARES * NSQUARES *
                                        NSQUARES * sizeof(int));
    InitN3_1_OpposingTables(k3_1_opposing_tab);

    k1_3_opposing_tab = (int *)MyMalloc(NSQUARES * NSQUARES * NSQUARES *
                                        NSQUARES * sizeof(int));
    InitN1_3_OpposingTables(k1_3_opposing_tab);

    k4_opposing_tab = (int *)MyMalloc(NSQUARES * NSQUARES * NSQUARES *
                                      NSQUARES * sizeof(int));
    InitN4OpposingTables(k4_opposing_tab);

    InitN5Tables();
    InitN6Tables();
    InitN7Tables();

    k4_tab = (int *)MyMalloc(NSQUARES * NSQUARES * NSQUARES * NSQUARES *
                             sizeof(int));
    InitN4Tables(k4_tab);

    k3_tab = (int *)MyMalloc(NSQUARES * NSQUARES * NSQUARES * sizeof(int));
    InitN3Tables(k3_tab);

    k3_even_tab = (int *)MyMalloc(NSQUARES * NSQUARES * NSQUARES * sizeof(int));
    InitN3EvenTables(k3_even_tab);

    k3_odd_tab = (int *)MyMalloc(NSQUARES * NSQUARES * NSQUARES * sizeof(int));
    InitN3OddTables(k3_odd_tab);

    k2_tab = (int *)MyMalloc(NSQUARES * NSQUARES * sizeof(int));
    InitN2Tables(k2_tab);

    k2_even_tab = (int *)MyMalloc(NSQUARES * NSQUARES * sizeof(int));
    InitN2EvenTables(k2_even_tab);

    k2_odd_tab = (int *)MyMalloc(NSQUARES * NSQUARES * sizeof(int));
    InitN2OddTables(k2_odd_tab);
}

static ZINDEX Index1(const int *pos) { return pos[2]; }

static ZINDEX Index11(const int *pos) { return pos[3] + NSQUARES * pos[2]; }

static ZINDEX IndexBP11(const int *pos) { return pos[2]; }

static ZINDEX IndexOP11(const int *pos) {
    int index = N2_Opposing_Index(pos[3], pos[2]);
    assert(index != -1);
    return index;
}

static ZINDEX Index111(const int *pos) {
    return pos[4] + NSQUARES * (pos[3] + NSQUARES * pos[2]);
}

static ZINDEX IndexBP111(const int *pos) { return pos[4] + NSQUARES * pos[2]; }

static ZINDEX IndexOP111(const int *pos) {
    int id2 = N2_Opposing_Index(pos[3], pos[2]);
    assert(id2 != -1);
    return pos[4] + NSQUARES * id2;
}

static ZINDEX Index1111(const int *pos) {
    return pos[5] +
           NSQUARES * (pos[4] + NSQUARES * (pos[3] + NSQUARES * pos[2]));
}

static ZINDEX IndexBP1111(const int *pos) {
    return pos[5] + NSQUARES * (pos[4] + NSQUARES * pos[2]);
}

static ZINDEX IndexOP1111(const int *pos) {
    int id2 = N2_Opposing_Index(pos[3], pos[2]);
    assert(id2 != -1);
    return pos[5] + NSQUARES * (pos[4] + NSQUARES * id2);
}

static ZINDEX Index11111(const int *pos) {
    return pos[6] +
           NSQUARES *
               (pos[5] +
                NSQUARES * (pos[4] + NSQUARES * (pos[3] + NSQUARES * pos[2])));
}

static ZINDEX IndexBP11111(const int *pos) {
    return pos[6] +
           NSQUARES * (pos[5] + NSQUARES * (pos[4] + NSQUARES * (pos[2])));
}

static ZINDEX IndexOP11111(const int *pos) {
    int id2 = N2_Opposing_Index(pos[3], pos[2]);
    assert(id2 != -1);
    return pos[6] +
           NSQUARES * (pos[5] + NSQUARES * (pos[4] + NSQUARES * (id2)));
}

static ZINDEX Index2(const int *pos) { return N2_Index(pos[3], pos[2]); }

static ZINDEX Index2_1100(const int *pos) {
    return N2_Odd_Index(pos[3], pos[2]);
}

static ZINDEX Index21(const int *pos) {
    return pos[4] + NSQUARES * N2_Index(pos[3], pos[2]);
}

static ZINDEX IndexOP21(const int *pos) {
    int index = N2_1_Opposing_Index(pos[4], pos[3], pos[2]);

    if (index == -1)
        return ALL_ONES;

    return (ZINDEX)index;
}

static ZINDEX Index12(const int *pos) {
    return pos[2] + NSQUARES * N2_Index(pos[4], pos[3]);
}

static ZINDEX IndexOP12(const int *pos) {
    int index = N1_2_Opposing_Index(pos[4], pos[3], pos[2]);

    if (index == -1)
        return ALL_ONES;

    return (ZINDEX)index;
}

static ZINDEX Index211(const int *pos) {
    return pos[5] + NSQUARES * (pos[4] + NSQUARES * N2_Index(pos[3], pos[2]));
}

static ZINDEX IndexOP211(const int *pos) {
    ZINDEX op21 = IndexOP21(pos);
    if (op21 == ALL_ONES)
        return ALL_ONES;
    return pos[5] + NSQUARES * op21;
}

static ZINDEX Index121(const int *pos) {
    return pos[5] + NSQUARES * (pos[2] + NSQUARES * N2_Index(pos[4], pos[3]));
}

static ZINDEX IndexOP121(const int *pos) {
    ZINDEX op12 = IndexOP12(pos);
    if (op12 == ALL_ONES)
        return ALL_ONES;
    return pos[5] + NSQUARES * op12;
}

static ZINDEX Index112(const int *pos) {
    return pos[3] + NSQUARES * (pos[2] + NSQUARES * N2_Index(pos[5], pos[4]));
}

static ZINDEX IndexBP112(const int *pos) {
    return N2_Offset * pos[2] + N2_Index(pos[5], pos[4]);
}

static ZINDEX IndexOP112(const int *pos) {
    int id2 = N2_Opposing_Index(pos[3], pos[2]);
    assert(id2 != -1);
    return N2_Offset * id2 + N2_Index(pos[5], pos[4]);
}

static ZINDEX Index2111(const int *pos) {
    return pos[6] +
           NSQUARES *
               (pos[5] +
                NSQUARES * (pos[4] + NSQUARES * N2_Index(pos[3], pos[2])));
}

static ZINDEX IndexOP2111(const int *pos) {
    ZINDEX op21 = IndexOP21(pos);
    if (op21 == ALL_ONES)
        return ALL_ONES;
    return pos[6] + NSQUARES * (ZINDEX)(pos[5] + NSQUARES * op21);
}

static ZINDEX Index1211(const int *pos) {
    return pos[6] +
           NSQUARES *
               (pos[5] +
                NSQUARES * (pos[2] + NSQUARES * N2_Index(pos[4], pos[3])));
}

static ZINDEX IndexOP1211(const int *pos) {
    ZINDEX op12 = IndexOP12(pos);
    if (op12 == ALL_ONES)
        return ALL_ONES;
    return pos[6] + NSQUARES * (ZINDEX)(pos[5] + NSQUARES * op12);
}

static ZINDEX Index1121(const int *pos) {
    return pos[6] +
           NSQUARES *
               (pos[3] +
                NSQUARES * (pos[2] + NSQUARES * N2_Index(pos[5], pos[4])));
}

static ZINDEX IndexBP1121(const int *pos) {
    return pos[6] + NSQUARES * (N2_Index(pos[5], pos[4]) + N2_Offset * pos[2]);
}

static ZINDEX IndexOP1121(const int *pos) {
    int id2 = N2_Opposing_Index(pos[3], pos[2]);
    assert(id2 != -1);
    return pos[6] + NSQUARES * (N2_Index(pos[5], pos[4]) + N2_Offset * id2);
}

static ZINDEX Index1112(const int *pos) {
    return pos[4] +
           NSQUARES *
               (pos[3] +
                NSQUARES * (pos[2] + NSQUARES * N2_Index(pos[6], pos[5])));
}

static ZINDEX IndexBP1112(const int *pos) {
    return pos[4] + NSQUARES * (N2_Index(pos[6], pos[5]) + N2_Offset * pos[2]);
}

static ZINDEX IndexOP1112(const int *pos) {
    int id2 = N2_Opposing_Index(pos[3], pos[2]);
    assert(id2 != -1);
    return pos[4] + NSQUARES * (N2_Index(pos[6], pos[5]) + N2_Offset * id2);
}

static ZINDEX Index22(const int *pos) {
    return N2_Index(pos[5], pos[4]) + N2_Offset * N2_Index(pos[3], pos[2]);
}

static ZINDEX IndexOP22(const int *pos) {
    int index = N2_2_Opposing_Index(pos[5], pos[4], pos[3], pos[2]);

    if (index == -1)
        return ALL_ONES;

    return (ZINDEX)index;
}

static ZINDEX IndexDP22(const int *pos) {
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

static ZINDEX Index221(const int *pos) {
    return pos[6] + NSQUARES * (N2_Index(pos[5], pos[4]) +
                                N2_Offset * N2_Index(pos[3], pos[2]));
}

static ZINDEX IndexOP221(const int *pos) {
    ZINDEX op22 = IndexOP22(pos);
    if (op22 == ALL_ONES)
        return ALL_ONES;
    return pos[6] + NSQUARES * op22;
}

static ZINDEX IndexDP221(const int *pos) {
    ZINDEX op22 = IndexDP22(pos);
    if (op22 == ALL_ONES)
        return ALL_ONES;
    return pos[6] + NSQUARES * op22;
}

static ZINDEX Index212(const int *pos) {
    return pos[4] + NSQUARES * (N2_Index(pos[6], pos[5]) +
                                N2_Offset * N2_Index(pos[3], pos[2]));
}

static ZINDEX IndexOP212(const int *pos) {
    ZINDEX op21 = IndexOP21(pos);
    if (op21 == ALL_ONES)
        return ALL_ONES;
    return N2_Index(pos[6], pos[5]) + N2_Offset * op21;
}

static ZINDEX Index122(const int *pos) {
    return pos[2] + NSQUARES * (N2_Index(pos[6], pos[5]) +
                                N2_Offset * N2_Index(pos[4], pos[3]));
}

static ZINDEX IndexOP122(const int *pos) {
    ZINDEX op12 = IndexOP12(pos);
    if (op12 == ALL_ONES)
        return ALL_ONES;
    return N2_Index(pos[6], pos[5]) + N2_Offset * op12;
}

static ZINDEX Index3(const int *pos) {
    return N3_Index(pos[4], pos[3], pos[2]);
}

static ZINDEX Index3_1100(const int *pos) {
    return N3_Odd_Index(pos[4], pos[3], pos[2]);
}

static ZINDEX Index31(const int *pos) {
    return pos[5] + NSQUARES * N3_Index(pos[4], pos[3], pos[2]);
}

static ZINDEX IndexOP31(const int *pos) {
    int index = N3_1_Opposing_Index(pos[5], pos[4], pos[3], pos[2]);

    if (index == -1)
        return ALL_ONES;

    return (ZINDEX)index;
}

static ZINDEX Index13(const int *pos) {
    return pos[2] + NSQUARES * N3_Index(pos[5], pos[4], pos[3]);
}

static ZINDEX IndexOP13(const int *pos) {
    int index = N1_3_Opposing_Index(pos[5], pos[4], pos[3], pos[2]);

    if (index == -1)
        return ALL_ONES;

    return (ZINDEX)index;
}

static ZINDEX Index311(const int *pos) {
    return pos[6] +
           NSQUARES * (pos[5] + NSQUARES * N3_Index(pos[4], pos[3], pos[2]));
}

static ZINDEX IndexOP311(const int *pos) {
    ZINDEX op31 = IndexOP31(pos);
    if (op31 == ALL_ONES)
        return ALL_ONES;
    return pos[6] + NSQUARES * op31;
}

static ZINDEX Index131(const int *pos) {
    return pos[6] +
           NSQUARES * (pos[2] + NSQUARES * N3_Index(pos[5], pos[4], pos[3]));
}

static ZINDEX IndexOP131(const int *pos) {
    ZINDEX op13 = IndexOP13(pos);
    if (op13 == ALL_ONES)
        return ALL_ONES;
    return pos[6] + NSQUARES * op13;
}

static ZINDEX Index113(const int *pos) {
    return pos[3] +
           NSQUARES * (pos[2] + NSQUARES * N3_Index(pos[6], pos[5], pos[4]));
}

static ZINDEX IndexBP113(const int *pos) {
    return N3_Index(pos[6], pos[5], pos[4]) + N3_Offset * pos[2];
}

static ZINDEX IndexOP113(const int *pos) {
    int id2 = N2_Opposing_Index(pos[3], pos[2]);
    assert(id2 != -1);
    return N3_Index(pos[6], pos[5], pos[4]) + N3_Offset * id2;
}

static ZINDEX Index32(const int *pos) {
    return N2_Index(pos[6], pos[5]) +
           N2_Offset * N3_Index(pos[4], pos[3], pos[2]);
}

static ZINDEX Index23(const int *pos) {
    return N2_Index(pos[3], pos[2]) +
           N2_Offset * N3_Index(pos[6], pos[5], pos[4]);
}

static ZINDEX Index4(const int *pos) {
    return N4_Index(pos[5], pos[4], pos[3], pos[2]);
}

static ZINDEX Index41(const int *pos) {
    return pos[6] + NSQUARES * N4_Index(pos[5], pos[4], pos[3], pos[2]);
}

static ZINDEX Index14(const int *pos) {
    return pos[2] + NSQUARES * N4_Index(pos[6], pos[5], pos[4], pos[3]);
}

static ZINDEX Index5(const int *pos) {
    return (N5 - 1) - N5_Index((NSQUARES - 1) - pos[2], (NSQUARES - 1) - pos[3],
                               (NSQUARES - 1) - pos[4], (NSQUARES - 1) - pos[5],
                               (NSQUARES - 1) - pos[6]);
}

static ZINDEX Index51(const int *pos) {
    return pos[7] + NSQUARES * Index5(pos);
}

static ZINDEX Index15(const int *pos) {
    return pos[2] + NSQUARES * Index5(pos + 1);
}

static ZINDEX Index6(const int *pos) {
    return (N6 - 1) - N6_Index((NSQUARES - 1) - pos[2], (NSQUARES - 1) - pos[3],
                               (NSQUARES - 1) - pos[4], (NSQUARES - 1) - pos[5],
                               (NSQUARES - 1) - pos[6],
                               (NSQUARES - 1) - pos[7]);
}

static ZINDEX Index7(const int *pos) {
    return (N7 - 1) - N7_Index((NSQUARES - 1) - pos[2], (NSQUARES - 1) - pos[3],
                               (NSQUARES - 1) - pos[4], (NSQUARES - 1) - pos[5],
                               (NSQUARES - 1) - pos[6], (NSQUARES - 1) - pos[7],
                               (NSQUARES - 1) - pos[8]);
}

/* index functions for 8-man endings require 64 bit zone sizes */

static ZINDEX Index111111(const int *pos) {
    return pos[7] +
           NSQUARES *
               (ZINDEX)(pos[6] +
                        NSQUARES *
                            (pos[5] +
                             NSQUARES *
                                 (pos[4] +
                                  NSQUARES * (pos[3] + NSQUARES * pos[2]))));
}

static ZINDEX IndexBP111111(const int *pos) {
    return pos[7] +
           NSQUARES *
               (ZINDEX)(pos[6] +
                        NSQUARES * (pos[5] +
                                    NSQUARES * (pos[4] + NSQUARES * (pos[2]))));
}

static ZINDEX IndexOP111111(const int *pos) {
    int id2 = N2_Opposing_Index(pos[3], pos[2]);
    assert(id2 != -1);
    return pos[7] +
           NSQUARES *
               (ZINDEX)(pos[6] +
                        NSQUARES *
                            (pos[5] + NSQUARES * (pos[4] + NSQUARES * (id2))));
}

static ZINDEX Index11112(const int *pos) {
    return pos[5] +
           NSQUARES *
               (ZINDEX)(pos[4] +
                        NSQUARES *
                            (pos[3] +
                             NSQUARES * (pos[2] +
                                         NSQUARES * N2_Index(pos[7], pos[6]))));
}

static ZINDEX IndexBP11112(const int *pos) {
    return pos[5] +
           NSQUARES * (ZINDEX)(pos[4] + NSQUARES * (N2_Index(pos[7], pos[6]) +
                                                    N2_Offset * pos[2]));
}

static ZINDEX IndexOP11112(const int *pos) {
    int id2 = N2_Opposing_Index(pos[3], pos[2]);
    assert(id2 != -1);
    return pos[5] +
           NSQUARES * (ZINDEX)(pos[4] + NSQUARES * (N2_Index(pos[7], pos[6]) +
                                                    N2_Offset * id2));
}

static ZINDEX Index11121(const int *pos) {
    return pos[7] +
           NSQUARES *
               (ZINDEX)(pos[4] +
                        NSQUARES *
                            (pos[3] +
                             NSQUARES * (pos[2] +
                                         NSQUARES * N2_Index(pos[6], pos[5]))));
}

static ZINDEX IndexBP11121(const int *pos) {
    return pos[7] +
           NSQUARES * (ZINDEX)(pos[4] + NSQUARES * (N2_Index(pos[6], pos[5]) +
                                                    N2_Offset * pos[2]));
}

static ZINDEX IndexOP11121(const int *pos) {
    int id2 = N2_Opposing_Index(pos[3], pos[2]);
    assert(id2 != -1);
    return pos[7] +
           NSQUARES * (ZINDEX)(pos[4] + NSQUARES * (N2_Index(pos[6], pos[5]) +
                                                    N2_Offset * id2));
}

static ZINDEX Index11211(const int *pos) {
    return pos[7] +
           NSQUARES *
               (ZINDEX)(pos[6] +
                        NSQUARES *
                            (pos[3] +
                             NSQUARES * (pos[2] +
                                         NSQUARES * N2_Index(pos[5], pos[4]))));
}

static ZINDEX IndexBP11211(const int *pos) {
    return pos[7] +
           NSQUARES * (ZINDEX)(pos[6] + NSQUARES * (N2_Index(pos[5], pos[4]) +
                                                    N2_Offset * pos[2]));
}

static ZINDEX IndexOP11211(const int *pos) {
    int id2 = N2_Opposing_Index(pos[3], pos[2]);
    assert(id2 != -1);
    return pos[7] +
           NSQUARES * (ZINDEX)(pos[6] + NSQUARES * (N2_Index(pos[5], pos[4]) +
                                                    N2_Offset * id2));
}

static ZINDEX Index12111(const int *pos) {
    return pos[7] +
           NSQUARES *
               (ZINDEX)(pos[6] +
                        NSQUARES *
                            (pos[5] +
                             NSQUARES * (pos[2] +
                                         NSQUARES * N2_Index(pos[4], pos[3]))));
}

static ZINDEX IndexOP12111(const int *pos) {
    ZINDEX op12 = IndexOP12(pos);
    if (op12 == ALL_ONES)
        return ALL_ONES;
    return pos[7] +
           NSQUARES * (ZINDEX)(pos[6] + NSQUARES * (pos[5] + NSQUARES * op12));
}

static ZINDEX Index21111(const int *pos) {
    return pos[7] +
           NSQUARES *
               (ZINDEX)(pos[6] +
                        NSQUARES *
                            (pos[5] +
                             NSQUARES * (pos[4] +
                                         NSQUARES * N2_Index(pos[3], pos[2]))));
}

static ZINDEX IndexOP21111(const int *pos) {
    ZINDEX op21 = IndexOP21(pos);
    if (op21 == ALL_ONES)
        return ALL_ONES;
    return pos[7] +
           NSQUARES * (ZINDEX)(pos[6] + NSQUARES * (pos[5] + NSQUARES * op21));
}

static ZINDEX Index2211(const int *pos) {
    return pos[7] +
           NSQUARES *
               (ZINDEX)(pos[6] +
                        NSQUARES * (N2_Index(pos[5], pos[4]) +
                                    N2_Offset * N2_Index(pos[3], pos[2])));
}

static ZINDEX IndexDP2211(const int *pos) {
    ZINDEX dp22 = IndexDP22(pos);
    if (dp22 == ALL_ONES)
        return ALL_ONES;
    return pos[7] + NSQUARES * (ZINDEX)(pos[6] + NSQUARES * dp22);
}

static ZINDEX IndexOP2211(const int *pos) {
    ZINDEX op22 = IndexOP22(pos);
    if (op22 == ALL_ONES)
        return ALL_ONES;
    return pos[7] + NSQUARES * (ZINDEX)(pos[6] + NSQUARES * op22);
}

static ZINDEX Index2211_1100(const int *pos) {
    return pos[7] +
           NSQUARES *
               (ZINDEX)(pos[6] + NSQUARES * (N2_Odd_Index(pos[3], pos[2]) +
                                             N2_ODD_PARITY_Offset *
                                                 N2_Index(pos[5], pos[4])));
}

static ZINDEX Index2211_1000(const int *pos) {
    return pos[7] +
           NSQUARES *
               (ZINDEX)(pos[6] + NSQUARES * (N2_Even_Index(pos[3], pos[2]) +
                                             N2_EVEN_PARITY_Offset *
                                                 N2_Index(pos[5], pos[4])));
}

static ZINDEX Index2121(const int *pos) {
    return pos[7] +
           NSQUARES *
               (ZINDEX)(pos[4] +
                        NSQUARES * (N2_Index(pos[6], pos[5]) +
                                    N2_Offset * N2_Index(pos[3], pos[2])));
}

static ZINDEX IndexOP2121(const int *pos) {
    ZINDEX op21 = IndexOP21(pos);
    if (op21 == ALL_ONES)
        return ALL_ONES;
    return pos[7] +
           NSQUARES * (ZINDEX)(N2_Index(pos[6], pos[5]) + N2_Offset * op21);
}

static ZINDEX Index2112(const int *pos) {
    return pos[5] +
           NSQUARES *
               (ZINDEX)(pos[4] +
                        NSQUARES * (N2_Index(pos[7], pos[6]) +
                                    N2_Offset * N2_Index(pos[3], pos[2])));
}

static ZINDEX IndexOP2112(const int *pos) {
    ZINDEX op21 = IndexOP21(pos);
    if (op21 == ALL_ONES)
        return ALL_ONES;
    return pos[5] +
           NSQUARES * (ZINDEX)(N2_Index(pos[7], pos[6]) + N2_Offset * op21);
}

static ZINDEX Index1221(const int *pos) {
    return pos[7] +
           NSQUARES *
               (ZINDEX)(pos[2] +
                        NSQUARES * (N2_Index(pos[6], pos[5]) +
                                    N2_Offset * N2_Index(pos[4], pos[3])));
}

static ZINDEX IndexOP1221(const int *pos) {
    ZINDEX op12 = IndexOP12(pos);
    if (op12 == ALL_ONES)
        return ALL_ONES;
    return pos[7] +
           NSQUARES * (ZINDEX)(N2_Index(pos[6], pos[5]) + N2_Offset * op12);
}

static ZINDEX Index1212(const int *pos) {
    return pos[5] +
           NSQUARES *
               (ZINDEX)(pos[2] +
                        NSQUARES * (N2_Index(pos[7], pos[6]) +
                                    N2_Offset * N2_Index(pos[4], pos[3])));
}

static ZINDEX IndexOP1212(const int *pos) {
    ZINDEX op12 = IndexOP12(pos);
    if (op12 == ALL_ONES)
        return ALL_ONES;
    return pos[5] +
           NSQUARES * (ZINDEX)(N2_Index(pos[7], pos[6]) + N2_Offset * op12);
}

static ZINDEX Index1122(const int *pos) {
    return pos[3] +
           NSQUARES *
               (ZINDEX)(pos[2] +
                        NSQUARES * (N2_Index(pos[7], pos[6]) +
                                    N2_Offset * N2_Index(pos[5], pos[4])));
}

static ZINDEX IndexBP1122(const int *pos) {
    return N2_Index(pos[7], pos[6]) +
           N2_Offset * (ZINDEX)(N2_Index(pos[5], pos[4]) + N2_Offset * pos[2]);
}

static ZINDEX IndexOP1122(const int *pos) {
    int id2 = N2_Opposing_Index(pos[3], pos[2]);
    assert(id2 != -1);
    return N2_Index(pos[7], pos[6]) +
           N2_Offset * (ZINDEX)(N2_Index(pos[5], pos[4]) + N2_Offset * id2);
}

static ZINDEX Index222(const int *pos) {
    return N2_Index(pos[7], pos[6]) +
           N2_Offset * (ZINDEX)(N2_Index(pos[5], pos[4]) +
                                N2_Offset * N2_Index(pos[3], pos[2]));
}

static ZINDEX IndexOP222(const int *pos) {
    ZINDEX op22 = IndexOP22(pos);
    if (op22 == ALL_ONES)
        return ALL_ONES;
    return N2_Index(pos[7], pos[6]) + N2_Offset * op22;
}
static ZINDEX IndexDP222(const int *pos) {
    ZINDEX dp22 = IndexDP22(pos);
    if (dp22 == ALL_ONES)
        return ALL_ONES;
    return N2_Index(pos[7], pos[6]) + N2_Offset * dp22;
}

static ZINDEX Index3111(const int *pos) {
    return pos[7] +
           NSQUARES *
               (ZINDEX)(pos[6] +
                        NSQUARES * (pos[5] + NSQUARES * N3_Index(pos[4], pos[3],
                                                                 pos[2])));
}

static ZINDEX IndexOP3111(const int *pos) {
    ZINDEX op31 = IndexOP31(pos);
    if (op31 == ALL_ONES)
        return ALL_ONES;
    return pos[7] + NSQUARES * (ZINDEX)(pos[6] + NSQUARES * op31);
}

static ZINDEX Index1311(const int *pos) {
    return pos[7] +
           NSQUARES *
               (ZINDEX)(pos[6] +
                        NSQUARES * (pos[2] + NSQUARES * N3_Index(pos[5], pos[4],
                                                                 pos[3])));
}

static ZINDEX IndexOP1311(const int *pos) {
    ZINDEX op13 = IndexOP13(pos);
    if (op13 == ALL_ONES)
        return ALL_ONES;
    return pos[7] + NSQUARES * (ZINDEX)(pos[6] + NSQUARES * op13);
}

static ZINDEX Index1131(const int *pos) {
    return pos[7] +
           NSQUARES *
               (ZINDEX)(pos[3] +
                        NSQUARES * (pos[2] + NSQUARES * N3_Index(pos[6], pos[5],
                                                                 pos[4])));
}

static ZINDEX IndexBP1131(const int *pos) {
    return pos[7] + NSQUARES * (ZINDEX)(N3_Index(pos[6], pos[5], pos[4]) +
                                        N3_Offset * pos[2]);
}

static ZINDEX IndexOP1131(const int *pos) {
    int id2 = N2_Opposing_Index(pos[3], pos[2]);
    assert(id2 != -1);
    return pos[7] + NSQUARES * (ZINDEX)(N3_Index(pos[6], pos[5], pos[4]) +
                                        N3_Offset * id2);
}

static ZINDEX Index1113(const int *pos) {
    return pos[4] +
           NSQUARES *
               (ZINDEX)(pos[3] +
                        NSQUARES * (pos[2] + NSQUARES * N3_Index(pos[7], pos[6],
                                                                 pos[5])));
}

static ZINDEX IndexBP1113(const int *pos) {
    return pos[4] + NSQUARES * (ZINDEX)(N3_Index(pos[7], pos[6], pos[5]) +
                                        N3_Offset * pos[2]);
}

static ZINDEX IndexOP1113(const int *pos) {
    int id2 = N2_Opposing_Index(pos[3], pos[2]);
    assert(id2 != -1);
    return pos[4] + NSQUARES * (ZINDEX)(N3_Index(pos[7], pos[6], pos[5]) +
                                        N3_Offset * id2);
}

static ZINDEX Index123(const int *pos) {
    return pos[2] +
           NSQUARES * (ZINDEX)(N2_Index(pos[4], pos[3]) +
                               N2_Offset * N3_Index(pos[7], pos[6], pos[5]));
}

static ZINDEX IndexOP123(const int *pos) {
    ZINDEX op12 = IndexOP12(pos);
    if (op12 == ALL_ONES)
        return ALL_ONES;
    return N3_Index(pos[7], pos[6], pos[5]) + N3_Offset * op12;
}

static ZINDEX Index132(const int *pos) {
    return pos[2] +
           NSQUARES * (ZINDEX)(N2_Index(pos[7], pos[6]) +
                               N2_Offset * N3_Index(pos[5], pos[4], pos[3]));
}

static ZINDEX IndexOP132(const int *pos) {
    ZINDEX op13 = IndexOP13(pos);
    if (op13 == ALL_ONES)
        return ALL_ONES;
    return N2_Index(pos[7], pos[6]) + N2_Offset * op13;
}

static ZINDEX Index213(const int *pos) {
    return pos[4] +
           NSQUARES * (ZINDEX)(N2_Index(pos[3], pos[2]) +
                               N2_Offset * N3_Index(pos[7], pos[6], pos[5]));
}

static ZINDEX IndexOP213(const int *pos) {
    ZINDEX op21 = IndexOP21(pos);
    if (op21 == ALL_ONES)
        return ALL_ONES;
    return N3_Index(pos[7], pos[6], pos[5]) + N3_Offset * op21;
}

static ZINDEX Index231(const int *pos) {
    return pos[7] +
           NSQUARES * (ZINDEX)(N2_Index(pos[3], pos[2]) +
                               N2_Offset * N3_Index(pos[6], pos[5], pos[4]));
}

static ZINDEX Index312(const int *pos) {
    return pos[5] +
           NSQUARES * (ZINDEX)(N2_Index(pos[7], pos[6]) +
                               N2_Offset * N3_Index(pos[4], pos[3], pos[2]));
}

static ZINDEX IndexOP312(const int *pos) {
    ZINDEX op31 = IndexOP31(pos);
    if (op31 == ALL_ONES)
        return ALL_ONES;
    return N2_Index(pos[7], pos[6]) + N2_Offset * op31;
}

static ZINDEX Index321(const int *pos) {
    return pos[7] +
           NSQUARES * (ZINDEX)(N2_Index(pos[6], pos[5]) +
                               N2_Offset * N3_Index(pos[4], pos[3], pos[2]));
}

static ZINDEX Index33(const int *pos) {
    return N3_Index(pos[7], pos[6], pos[5]) +
           N3_Offset * (ZINDEX)N3_Index(pos[4], pos[3], pos[2]);
}

static ZINDEX Index411(const int *pos) {
    return pos[7] +
           NSQUARES * (ZINDEX)(pos[6] + NSQUARES * N4_Index(pos[5], pos[4],
                                                            pos[3], pos[2]));
}

static ZINDEX Index141(const int *pos) {
    return pos[7] +
           NSQUARES * (ZINDEX)(pos[2] + NSQUARES * N4_Index(pos[6], pos[5],
                                                            pos[4], pos[3]));
}

static ZINDEX Index114(const int *pos) {
    return pos[3] +
           NSQUARES * (ZINDEX)(pos[2] + NSQUARES * N4_Index(pos[7], pos[6],
                                                            pos[5], pos[4]));
}

static ZINDEX IndexBP114(const int *pos) {
    return N4_Index(pos[7], pos[6], pos[5], pos[4]) +
           N4_Offset * (ZINDEX)pos[2];
}

static ZINDEX IndexOP114(const int *pos) {
    int id2 = N2_Opposing_Index(pos[3], pos[2]);
    assert(id2 != -1);
    return N4_Index(pos[7], pos[6], pos[5], pos[4]) + N4_Offset * (ZINDEX)id2;
}

static ZINDEX Index42(const int *pos) {
    return N2_Index(pos[7], pos[6]) +
           N2_Offset * (ZINDEX)N4_Index(pos[5], pos[4], pos[3], pos[2]);
}

static ZINDEX Index24(const int *pos) {
    return N2_Index(pos[3], pos[2]) +
           N2_Offset * (ZINDEX)N4_Index(pos[7], pos[6], pos[5], pos[4]);
}

// 9-pieces

static ZINDEX Index1111111(const int *pos) {
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

static ZINDEX Index211111(const int *pos) {
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

static ZINDEX Index121111(const int *pos) {
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

static ZINDEX Index112111(const int *pos) {
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

static ZINDEX Index111211(const int *pos) {
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

static ZINDEX Index111121(const int *pos) {
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

static ZINDEX Index111112(const int *pos) {
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

static ZINDEX Index22111(const int *pos) {
    return pos[8] +
           NSQUARES *
               (pos[7] +
                NSQUARES * (pos[6] +
                            NSQUARES * (ZINDEX)(N2_Index(pos[5], pos[4]) +
                                                N2_Offset *
                                                    N2_Index(pos[3], pos[2]))));
}

static ZINDEX IndexDP22111(const int *pos) {
    ZINDEX dp22 = IndexDP22(pos);
    if (dp22 == ALL_ONES)
        return ALL_ONES;
    return pos[8] + NSQUARES * (pos[7] + NSQUARES * (pos[6] + NSQUARES * dp22));
}

static ZINDEX Index21211(const int *pos) {
    return pos[8] +
           NSQUARES *
               (pos[7] +
                NSQUARES * (pos[4] +
                            NSQUARES * (ZINDEX)(N2_Index(pos[6], pos[5]) +
                                                N2_Offset *
                                                    N2_Index(pos[3], pos[2]))));
}

static ZINDEX Index21121(const int *pos) {
    return pos[8] +
           NSQUARES *
               (pos[5] +
                NSQUARES * (pos[4] +
                            NSQUARES * (ZINDEX)(N2_Index(pos[7], pos[6]) +
                                                N2_Offset *
                                                    N2_Index(pos[3], pos[2]))));
}

static ZINDEX Index21112(const int *pos) {
    return pos[6] +
           NSQUARES *
               (pos[5] +
                NSQUARES * (pos[4] +
                            NSQUARES * (ZINDEX)(N2_Index(pos[8], pos[7]) +
                                                N2_Offset *
                                                    N2_Index(pos[3], pos[2]))));
}

static ZINDEX Index12211(const int *pos) {
    return pos[8] +
           NSQUARES *
               (pos[7] +
                NSQUARES * (pos[2] +
                            NSQUARES * (ZINDEX)(N2_Index(pos[6], pos[5]) +
                                                N2_Offset *
                                                    N2_Index(pos[4], pos[3]))));
}

static ZINDEX Index12121(const int *pos) {
    return pos[8] +
           NSQUARES *
               (pos[5] +
                NSQUARES * (pos[2] +
                            NSQUARES * (ZINDEX)(N2_Index(pos[7], pos[6]) +
                                                N2_Offset *
                                                    N2_Index(pos[4], pos[3]))));
}

static ZINDEX Index12112(const int *pos) {
    return pos[6] +
           NSQUARES *
               (pos[5] +
                NSQUARES * (pos[2] +
                            NSQUARES * (ZINDEX)(N2_Index(pos[8], pos[7]) +
                                                N2_Offset *
                                                    N2_Index(pos[4], pos[3]))));
}

static ZINDEX Index11221(const int *pos) {
    return pos[8] +
           NSQUARES *
               (pos[3] +
                NSQUARES * (pos[2] +
                            NSQUARES * (ZINDEX)(N2_Index(pos[7], pos[6]) +
                                                N2_Offset *
                                                    N2_Index(pos[5], pos[4]))));
}

static ZINDEX Index11212(const int *pos) {
    return pos[6] +
           NSQUARES *
               (pos[3] +
                NSQUARES * (pos[2] +
                            NSQUARES * (ZINDEX)(N2_Index(pos[8], pos[7]) +
                                                N2_Offset *
                                                    N2_Index(pos[5], pos[4]))));
}

static ZINDEX Index11122(const int *pos) {
    return pos[4] +
           NSQUARES *
               (pos[3] +
                NSQUARES * (pos[2] +
                            NSQUARES * (ZINDEX)(N2_Index(pos[8], pos[7]) +
                                                N2_Offset *
                                                    N2_Index(pos[6], pos[5]))));
}

static ZINDEX Index2221(const int *pos) {
    return pos[8] +
           NSQUARES *
               (N2_Index(pos[7], pos[6]) +
                N2_Offset * (ZINDEX)(N2_Index(pos[5], pos[4]) +
                                     N2_Offset * N2_Index(pos[3], pos[2])));
}

static ZINDEX IndexDP2221(const int *pos) {
    ZINDEX dp22 = IndexDP22(pos);
    if (dp22 == ALL_ONES)
        return ALL_ONES;
    return pos[8] + NSQUARES * (N2_Index(pos[7], pos[6]) + N2_Offset * dp22);
}

static ZINDEX Index2221_1131(const int *pos) {
    return pos[8] + NSQUARES * (N2_Odd_Index(pos[7], pos[6]) +
                                N2_ODD_PARITY_Offset *
                                    (ZINDEX)(N2_Odd_Index(pos[3], pos[2]) +
                                             N2_ODD_PARITY_Offset *
                                                 N2_Index(pos[5], pos[4])));
}

static ZINDEX Index2221_1130(const int *pos) {
    return pos[8] + NSQUARES * (N2_Even_Index(pos[7], pos[6]) +
                                N2_EVEN_PARITY_Offset *
                                    (ZINDEX)(N2_Odd_Index(pos[3], pos[2]) +
                                             N2_ODD_PARITY_Offset *
                                                 N2_Index(pos[5], pos[4])));
}

static ZINDEX Index2221_1030(const int *pos) {
    return pos[8] + NSQUARES * (N2_Even_Index(pos[7], pos[6]) +
                                N2_EVEN_PARITY_Offset *
                                    (ZINDEX)(N2_Even_Index(pos[3], pos[2]) +
                                             N2_EVEN_PARITY_Offset *
                                                 N2_Index(pos[5], pos[4])));
}

static ZINDEX Index2212(const int *pos) {
    return pos[6] +
           NSQUARES *
               (N2_Index(pos[8], pos[7]) +
                N2_Offset * (ZINDEX)(N2_Index(pos[5], pos[4]) +
                                     N2_Offset * N2_Index(pos[3], pos[2])));
}

static ZINDEX IndexDP2212(const int *pos) {
    ZINDEX dp22 = IndexDP22(pos);
    if (dp22 == ALL_ONES)
        return ALL_ONES;
    return pos[6] + NSQUARES * (N2_Index(pos[8], pos[7]) + N2_Offset * dp22);
}

static ZINDEX Index2122(const int *pos) {
    return pos[4] +
           NSQUARES *
               (N2_Index(pos[8], pos[7]) +
                N2_Offset * (ZINDEX)(N2_Index(pos[6], pos[5]) +
                                     N2_Offset * N2_Index(pos[3], pos[2])));
}

static ZINDEX Index1222(const int *pos) {
    return pos[2] +
           NSQUARES *
               (N2_Index(pos[8], pos[7]) +
                N2_Offset * (ZINDEX)(N2_Index(pos[6], pos[5]) +
                                     N2_Offset * N2_Index(pos[4], pos[3])));
}

static ZINDEX Index31111(const int *pos) {
    return pos[8] +
           NSQUARES *
               (pos[7] +
                NSQUARES *
                    (ZINDEX)(pos[6] +
                             NSQUARES *
                                 (pos[5] + NSQUARES * N3_Index(pos[4], pos[3],
                                                               pos[2]))));
}

static ZINDEX Index13111(const int *pos) {
    return pos[8] +
           NSQUARES *
               (pos[7] +
                NSQUARES *
                    (ZINDEX)(pos[6] +
                             NSQUARES *
                                 (pos[2] + NSQUARES * N3_Index(pos[5], pos[4],
                                                               pos[3]))));
}

static ZINDEX Index11311(const int *pos) {
    return pos[8] +
           NSQUARES *
               (pos[7] +
                NSQUARES *
                    (ZINDEX)(pos[3] +
                             NSQUARES *
                                 (pos[2] + NSQUARES * N3_Index(pos[6], pos[5],
                                                               pos[4]))));
}

static ZINDEX Index11131(const int *pos) {
    return pos[8] +
           NSQUARES *
               (pos[4] +
                NSQUARES *
                    (ZINDEX)(pos[3] +
                             NSQUARES *
                                 (pos[2] + NSQUARES * N3_Index(pos[7], pos[6],
                                                               pos[5]))));
}

static ZINDEX Index11113(const int *pos) {
    return pos[5] +
           NSQUARES *
               (pos[4] +
                NSQUARES *
                    (ZINDEX)(pos[3] +
                             NSQUARES *
                                 (pos[2] + NSQUARES * N3_Index(pos[8], pos[7],
                                                               pos[6]))));
}

static ZINDEX Index3211(const int *pos) {
    return pos[8] +
           NSQUARES *
               (pos[7] +
                (NSQUARES) *
                    (ZINDEX)(N2_Index(pos[6], pos[5]) +
                             N2_Offset * N3_Index(pos[4], pos[3], pos[2])));
}

static ZINDEX Index3121(const int *pos) {
    return pos[8] +
           NSQUARES *
               (pos[5] +
                (NSQUARES) *
                    (ZINDEX)(N2_Index(pos[7], pos[6]) +
                             N2_Offset * N3_Index(pos[4], pos[3], pos[2])));
}

static ZINDEX Index3121_1100(const int *pos) {
    return pos[8] +
           NSQUARES *
               (pos[5] +
                (NSQUARES) *
                    (ZINDEX)(N2_Index(pos[7], pos[6]) +
                             N2_Offset * N3_Odd_Index(pos[4], pos[3], pos[2])));
}

static ZINDEX Index3121_1111(const int *pos) {
    return pos[8] +
           NSQUARES * (pos[5] +
                       (NSQUARES) *
                           (ZINDEX)(N2_Odd_Index(pos[7], pos[6]) +
                                    N2_ODD_PARITY_Offset *
                                        N3_Odd_Index(pos[4], pos[3], pos[2])));
}

static ZINDEX Index3121_1110(const int *pos) {
    return pos[8] +
           NSQUARES * (pos[5] +
                       (NSQUARES) *
                           (ZINDEX)(N2_Even_Index(pos[7], pos[6]) +
                                    N2_EVEN_PARITY_Offset *
                                        N3_Odd_Index(pos[4], pos[3], pos[2])));
}

static ZINDEX Index3112(const int *pos) {
    return pos[6] +
           NSQUARES *
               (pos[5] +
                (NSQUARES) *
                    (ZINDEX)(N2_Index(pos[8], pos[7]) +
                             N2_Offset * N3_Index(pos[4], pos[3], pos[2])));
}

static ZINDEX Index2311(const int *pos) {
    return pos[8] +
           NSQUARES *
               (pos[7] +
                (NSQUARES) *
                    (ZINDEX)(N2_Index(pos[3], pos[2]) +
                             N2_Offset * N3_Index(pos[6], pos[5], pos[4])));
}

static ZINDEX Index2131(const int *pos) {
    return pos[8] +
           NSQUARES *
               (pos[4] +
                (NSQUARES) *
                    (ZINDEX)(N2_Index(pos[3], pos[2]) +
                             N2_Offset * N3_Index(pos[7], pos[6], pos[5])));
}

static ZINDEX Index2113(const int *pos) {
    return pos[5] +
           NSQUARES *
               (pos[4] +
                (NSQUARES) *
                    (ZINDEX)(N2_Index(pos[3], pos[2]) +
                             N2_Offset * N3_Index(pos[8], pos[7], pos[6])));
}

static ZINDEX Index1321(const int *pos) {
    return pos[8] +
           NSQUARES *
               (pos[2] +
                (NSQUARES) *
                    (ZINDEX)(N2_Index(pos[7], pos[6]) +
                             N2_Offset * N3_Index(pos[5], pos[4], pos[3])));
}

static ZINDEX Index1312(const int *pos) {
    return pos[6] +
           NSQUARES *
               (pos[2] +
                (NSQUARES) *
                    (ZINDEX)(N2_Index(pos[8], pos[7]) +
                             N2_Offset * N3_Index(pos[5], pos[4], pos[3])));
}

static ZINDEX Index1312_0010(const int *pos) {
    return pos[6] +
           NSQUARES *
               (pos[2] +
                (NSQUARES) * (ZINDEX)(N2_Even_Index(pos[8], pos[7]) +
                                      N2_EVEN_PARITY_Offset *
                                          N3_Index(pos[5], pos[4], pos[3])));
}

static ZINDEX Index1312_0011(const int *pos) {
    return pos[6] +
           NSQUARES *
               (pos[2] +
                (NSQUARES) * (ZINDEX)(N2_Odd_Index(pos[8], pos[7]) +
                                      N2_ODD_PARITY_Offset *
                                          N3_Index(pos[5], pos[4], pos[3])));
}

static ZINDEX Index1231(const int *pos) {
    return pos[8] +
           NSQUARES *
               (pos[2] +
                (NSQUARES) *
                    (ZINDEX)(N2_Index(pos[4], pos[3]) +
                             N2_Offset * N3_Index(pos[7], pos[6], pos[5])));
}

static ZINDEX Index1213(const int *pos) {
    return pos[5] +
           NSQUARES *
               (pos[2] +
                (NSQUARES) *
                    (ZINDEX)(N2_Index(pos[4], pos[3]) +
                             N2_Offset * N3_Index(pos[8], pos[7], pos[6])));
}

static ZINDEX Index1132(const int *pos) {
    return pos[3] +
           NSQUARES *
               (pos[2] +
                (NSQUARES) *
                    (ZINDEX)(N2_Index(pos[8], pos[7]) +
                             N2_Offset * N3_Index(pos[6], pos[5], pos[4])));
}

static ZINDEX Index1123(const int *pos) {
    return pos[3] +
           NSQUARES *
               (pos[2] +
                (NSQUARES) *
                    (ZINDEX)(N2_Index(pos[5], pos[4]) +
                             N2_Offset * N3_Index(pos[8], pos[7], pos[6])));
}

static ZINDEX Index331(const int *pos) {
    return pos[8] +
           NSQUARES * (N3_Index(pos[7], pos[6], pos[5]) +
                       N3_Offset * (ZINDEX)N3_Index(pos[4], pos[3], pos[2]));
}

static ZINDEX Index331_0020(const int *pos) {
    return pos[8] + NSQUARES * (N3_Even_Index(pos[7], pos[6], pos[5]) +
                                N3_EVEN_PARITY_Offset *
                                    (ZINDEX)N3_Index(pos[4], pos[3], pos[2]));
}

static ZINDEX Index331_0021(const int *pos) {
    return pos[8] + NSQUARES * (N3_Odd_Index(pos[7], pos[6], pos[5]) +
                                N3_ODD_PARITY_Offset *
                                    (ZINDEX)N3_Index(pos[4], pos[3], pos[2]));
}

static ZINDEX Index313(const int *pos) {
    return pos[5] +
           NSQUARES * (N3_Index(pos[8], pos[7], pos[6]) +
                       N3_Offset * (ZINDEX)N3_Index(pos[4], pos[3], pos[2]));
}

static ZINDEX Index133(const int *pos) {
    return pos[2] +
           NSQUARES * (N3_Index(pos[8], pos[7], pos[6]) +
                       N3_Offset * (ZINDEX)N3_Index(pos[5], pos[4], pos[3]));
}

static ZINDEX Index322(const int *pos) {
    return N2_Index(pos[8], pos[7]) +
           (N2_Offset) * (N2_Index(pos[6], pos[5]) +
                          N2_Offset * (ZINDEX)N3_Index(pos[4], pos[3], pos[2]));
}

static ZINDEX Index322_0010(const int *pos) {
    return N2_Even_Index(pos[6], pos[5]) +
           (N2_EVEN_PARITY_Offset) *
               (N2_Index(pos[8], pos[7]) +
                N2_Offset * (ZINDEX)N3_Index(pos[4], pos[3], pos[2]));
}

static ZINDEX Index322_0011(const int *pos) {
    return N2_Odd_Index(pos[6], pos[5]) +
           (N2_ODD_PARITY_Offset) *
               (N2_Index(pos[8], pos[7]) +
                N2_Offset * (ZINDEX)N3_Index(pos[4], pos[3], pos[2]));
}

static ZINDEX Index232(const int *pos) {
    return N2_Index(pos[8], pos[7]) +
           (N2_Offset) * (N2_Index(pos[3], pos[2]) +
                          N2_Offset * (ZINDEX)N3_Index(pos[6], pos[5], pos[4]));
}

static ZINDEX Index223(const int *pos) {
    return N2_Index(pos[5], pos[4]) +
           (N2_Offset) *
               (N2_Index(pos[3], pos[2]) +
                (N2_Offset) * (ZINDEX)N3_Index(pos[8], pos[7], pos[6]));
}

static ZINDEX IndexDP223(const int *pos) {
    ZINDEX dp22 = IndexDP22(pos);
    if (dp22 == ALL_ONES)
        return ALL_ONES;
    return N3_Index(pos[8], pos[7], pos[6]) + (N3_Offset)*dp22;
}

static ZINDEX Index223_1100(const int *pos) {
    return N2_Odd_Index(pos[3], pos[2]) +
           (N2_ODD_PARITY_Offset) *
               (N2_Index(pos[5], pos[4]) +
                (N2_Offset) * (ZINDEX)N3_Index(pos[8], pos[7], pos[6]));
}

static ZINDEX Index223_1000(const int *pos) {
    return N2_Even_Index(pos[3], pos[2]) +
           (N2_EVEN_PARITY_Offset) *
               (N2_Index(pos[5], pos[4]) +
                (N2_Offset) * (ZINDEX)N3_Index(pos[8], pos[7], pos[6]));
}

static ZINDEX Index4111(const int *pos) {
    return pos[8] +
           (NSQUARES) *
               (pos[7] +
                (NSQUARES) *
                    (pos[6] + (NSQUARES) * (ZINDEX)N4_Index(pos[5], pos[4],
                                                            pos[3], pos[2])));
}

static ZINDEX Index1411(const int *pos) {
    return pos[8] +
           (NSQUARES) *
               (pos[7] +
                (NSQUARES) *
                    (pos[2] + (NSQUARES) * (ZINDEX)N4_Index(pos[6], pos[5],
                                                            pos[4], pos[3])));
}

static ZINDEX Index1141(const int *pos) {
    return pos[8] +
           (NSQUARES) *
               (pos[3] +
                (NSQUARES) *
                    (pos[2] + (NSQUARES) * (ZINDEX)N4_Index(pos[7], pos[6],
                                                            pos[5], pos[4])));
}

static ZINDEX Index1114(const int *pos) {
    return pos[4] +
           (NSQUARES) *
               (pos[3] +
                (NSQUARES) *
                    (pos[2] + (NSQUARES) * (ZINDEX)N4_Index(pos[8], pos[7],
                                                            pos[6], pos[5])));
}

static ZINDEX Index421(const int *pos) {
    return pos[8] + NSQUARES * (ZINDEX)(N2_Index(pos[7], pos[6]) +
                                        N2_Offset * N4_Index(pos[5], pos[4],
                                                             pos[3], pos[2]));
}

static ZINDEX Index421_0010(const int *pos) {
    return pos[8] +
           NSQUARES * (ZINDEX)(N2_Even_Index(pos[7], pos[6]) +
                               N2_EVEN_PARITY_Offset *
                                   N4_Index(pos[5], pos[4], pos[3], pos[2]));
}

static ZINDEX Index421_0011(const int *pos) {
    return pos[8] +
           NSQUARES * (ZINDEX)(N2_Odd_Index(pos[7], pos[6]) +
                               N2_ODD_PARITY_Offset *
                                   N4_Index(pos[5], pos[4], pos[3], pos[2]));
}

static ZINDEX Index412(const int *pos) {
    return pos[6] + NSQUARES * (ZINDEX)(N2_Index(pos[8], pos[7]) +
                                        N2_Offset * N4_Index(pos[5], pos[4],
                                                             pos[3], pos[2]));
}

static ZINDEX Index241(const int *pos) {
    return pos[8] + NSQUARES * (ZINDEX)(N2_Index(pos[3], pos[2]) +
                                        N2_Offset * N4_Index(pos[7], pos[6],
                                                             pos[5], pos[4]));
}

static ZINDEX Index214(const int *pos) {
    return pos[4] + NSQUARES * (ZINDEX)(N2_Index(pos[3], pos[2]) +
                                        N2_Offset * N4_Index(pos[8], pos[7],
                                                             pos[6], pos[5]));
}

static ZINDEX Index142(const int *pos) {
    return pos[2] + NSQUARES * (ZINDEX)(N2_Index(pos[8], pos[7]) +
                                        N2_Offset * N4_Index(pos[6], pos[5],
                                                             pos[4], pos[3]));
}

static ZINDEX Index124(const int *pos) {
    return pos[2] + NSQUARES * (ZINDEX)(N2_Index(pos[4], pos[3]) +
                                        N2_Offset * N4_Index(pos[8], pos[7],
                                                             pos[6], pos[5]));
}

static ZINDEX Index43(const int *pos) {
    return N3_Index(pos[8], pos[7], pos[6]) +
           N3_Offset * (ZINDEX)N4_Index(pos[5], pos[4], pos[3], pos[2]);
}

static ZINDEX Index34(const int *pos) {
    return N3_Index(pos[4], pos[3], pos[2]) +
           N3_Offset * (ZINDEX)N4_Index(pos[8], pos[7], pos[6], pos[5]);
}

static ZINDEX Index511(const int *pos) {
    return pos[8] + NSQUARES * (pos[7] + NSQUARES * (ZINDEX)Index5(pos));
}

static ZINDEX Index151(const int *pos) {
    return pos[8] + NSQUARES * (pos[2] + NSQUARES * (ZINDEX)Index5(pos + 1));
}

static ZINDEX Index115(const int *pos) {
    return pos[3] + NSQUARES * (pos[2] + NSQUARES * (ZINDEX)Index5(pos + 2));
}

static ZINDEX Index52(const int *pos) {
    return N2_Index(pos[8], pos[7]) + N2_Offset * ((ZINDEX)Index5(pos));
}

static ZINDEX Index25(const int *pos) {
    return N2_Index(pos[3], pos[2]) + N2_Offset * ((ZINDEX)Index5(pos + 2));
}

static ZINDEX Index61(const int *pos) {
    return pos[8] + NSQUARES * ((ZINDEX)Index6(pos));
}

static ZINDEX Index16(const int *pos) {
    return pos[2] + NSQUARES * ((ZINDEX)Index6(pos + 1));
}

static const IndexType IndexTable[] = {{111111, FREE_PAWNS, 0, Index111111},
                                       {111111, BP_11_PAWNS, 0, IndexBP111111},
                                       {111111, OP_11_PAWNS, 0, IndexOP111111},
                                       {21111, FREE_PAWNS, 0, Index21111},
                                       {21111, OP_21_PAWNS, 0, IndexOP21111},
                                       {12111, FREE_PAWNS, 0, Index12111},
                                       {12111, OP_12_PAWNS, 0, IndexOP12111},
                                       {11211, FREE_PAWNS, 0, Index11211},
                                       {11211, BP_11_PAWNS, 0, IndexBP11211},
                                       {11211, OP_11_PAWNS, 0, IndexOP11211},
                                       {11121, FREE_PAWNS, 0, Index11121},
                                       {11121, BP_11_PAWNS, 0, IndexBP11121},
                                       {11121, OP_11_PAWNS, 0, IndexOP11121},
                                       {11112, FREE_PAWNS, 0, Index11112},
                                       {11112, BP_11_PAWNS, 0, IndexBP11112},
                                       {11112, OP_11_PAWNS, 0, IndexOP11112},
                                       {2211, FREE_PAWNS, 0, Index2211},
                                       {2211, DP_22_PAWNS, 0, IndexDP2211},
                                       {2211, OP_22_PAWNS, 0, IndexOP2211},
                                       {2211, FREE_PAWNS, 1100, Index2211_1100},
                                       {2211, FREE_PAWNS, 1000, Index2211_1000},
                                       {2121, FREE_PAWNS, 0, Index2121},
                                       {2121, OP_21_PAWNS, 0, IndexOP2121},
                                       {1221, FREE_PAWNS, 0, Index1221},
                                       {1221, OP_12_PAWNS, 0, IndexOP1221},
                                       {2112, FREE_PAWNS, 0, Index2112},
                                       {2112, OP_21_PAWNS, 0, IndexOP2112},
                                       {1212, FREE_PAWNS, 0, Index1212},
                                       {1212, OP_12_PAWNS, 0, IndexOP1212},
                                       {1122, FREE_PAWNS, 0, Index1122},
                                       {1122, BP_11_PAWNS, 0, IndexBP1122},
                                       {1122, OP_11_PAWNS, 0, IndexOP1122},
                                       {222, FREE_PAWNS, 0, Index222},
                                       {222, DP_22_PAWNS, 0, IndexDP222},
                                       {222, OP_22_PAWNS, 0, IndexOP222},
                                       {3111, FREE_PAWNS, 0, Index3111},
                                       {3111, OP_31_PAWNS, 0, IndexOP3111},
                                       {1311, FREE_PAWNS, 0, Index1311},
                                       {1311, OP_13_PAWNS, 0, IndexOP1311},
                                       {1131, FREE_PAWNS, 0, Index1131},
                                       {1131, BP_11_PAWNS, 0, IndexBP1131},
                                       {1131, OP_11_PAWNS, 0, IndexOP1131},
                                       {1113, FREE_PAWNS, 0, Index1113},
                                       {1113, BP_11_PAWNS, 0, IndexBP1113},
                                       {1113, OP_11_PAWNS, 0, IndexOP1113},
                                       {123, FREE_PAWNS, 0, Index123},
                                       {123, OP_12_PAWNS, 0, IndexOP123},
                                       {213, FREE_PAWNS, 0, Index213},
                                       {213, OP_21_PAWNS, 0, IndexOP213},
                                       {132, FREE_PAWNS, 0, Index132},
                                       {132, OP_13_PAWNS, 0, IndexOP132},
                                       {231, FREE_PAWNS, 0, Index231},
                                       {312, FREE_PAWNS, 0, Index312},
                                       {312, OP_31_PAWNS, 0, IndexOP312},
                                       {321, FREE_PAWNS, 0, Index321},
                                       {33, FREE_PAWNS, 0, Index33},
                                       {411, FREE_PAWNS, 0, Index411},
                                       {141, FREE_PAWNS, 0, Index141},
                                       {114, FREE_PAWNS, 0, Index114},
                                       {114, BP_11_PAWNS, 0, IndexBP114},
                                       {114, OP_11_PAWNS, 0, IndexOP114},
                                       {42, FREE_PAWNS, 0, Index42},
                                       {24, FREE_PAWNS, 0, Index24},
                                       {1111111, FREE_PAWNS, 0, Index1111111},
                                       {211111, FREE_PAWNS, 0, Index211111},
                                       {121111, FREE_PAWNS, 0, Index121111},
                                       {112111, FREE_PAWNS, 0, Index112111},
                                       {111211, FREE_PAWNS, 0, Index111211},
                                       {111121, FREE_PAWNS, 0, Index111121},
                                       {111112, FREE_PAWNS, 0, Index111112},
                                       {22111, FREE_PAWNS, 0, Index22111},
                                       {22111, DP_22_PAWNS, 0, IndexDP22111},
                                       {21211, FREE_PAWNS, 0, Index21211},
                                       {21121, FREE_PAWNS, 0, Index21121},
                                       {21112, FREE_PAWNS, 0, Index21112},
                                       {12211, FREE_PAWNS, 0, Index12211},
                                       {12121, FREE_PAWNS, 0, Index12121},
                                       {12112, FREE_PAWNS, 0, Index12112},
                                       {11221, FREE_PAWNS, 0, Index11221},
                                       {11212, FREE_PAWNS, 0, Index11212},
                                       {11122, FREE_PAWNS, 0, Index11122},
                                       {2221, FREE_PAWNS, 0, Index2221},
                                       {2221, DP_22_PAWNS, 0, IndexDP2221},
                                       {2221, FREE_PAWNS, 1131, Index2221_1131},
                                       {2221, FREE_PAWNS, 1130, Index2221_1130},
                                       {2221, FREE_PAWNS, 1030, Index2221_1030},
                                       {2212, FREE_PAWNS, 0, Index2212},
                                       {2212, DP_22_PAWNS, 0, IndexDP2212},
                                       {2122, FREE_PAWNS, 0, Index2122},
                                       {1222, FREE_PAWNS, 0, Index1222},
                                       {31111, FREE_PAWNS, 0, Index31111},
                                       {13111, FREE_PAWNS, 0, Index13111},
                                       {11311, FREE_PAWNS, 0, Index11311},
                                       {11131, FREE_PAWNS, 0, Index11131},
                                       {11113, FREE_PAWNS, 0, Index11113},
                                       {3211, FREE_PAWNS, 0, Index3211},
                                       {3121, FREE_PAWNS, 0, Index3121},
                                       {3121, FREE_PAWNS, 1100, Index3121_1100},
                                       {3121, FREE_PAWNS, 1111, Index3121_1111},
                                       {3121, FREE_PAWNS, 1110, Index3121_1110},
                                       {3112, FREE_PAWNS, 0, Index3112},
                                       {2311, FREE_PAWNS, 0, Index2311},
                                       {2131, FREE_PAWNS, 0, Index2131},
                                       {2113, FREE_PAWNS, 0, Index2113},
                                       {1321, FREE_PAWNS, 0, Index1321},
                                       {1312, FREE_PAWNS, 0, Index1312},
                                       {1312, FREE_PAWNS, 10, Index1312_0010},
                                       {1312, FREE_PAWNS, 11, Index1312_0011},
                                       {1231, FREE_PAWNS, 0, Index1231},
                                       {1213, FREE_PAWNS, 0, Index1213},
                                       {1132, FREE_PAWNS, 0, Index1132},
                                       {1123, FREE_PAWNS, 0, Index1123},
                                       {322, FREE_PAWNS, 0, Index322},
                                       {322, FREE_PAWNS, 10, Index322_0010},
                                       {322, FREE_PAWNS, 11, Index322_0011},
                                       {232, FREE_PAWNS, 0, Index232},
                                       {223, FREE_PAWNS, 0, Index223},
                                       {223, DP_22_PAWNS, 0, IndexDP223},
                                       {223, FREE_PAWNS, 1100, Index223_1100},
                                       {223, FREE_PAWNS, 1000, Index223_1000},
                                       {331, FREE_PAWNS, 0, Index331},
                                       {331, FREE_PAWNS, 20, Index331_0020},
                                       {331, FREE_PAWNS, 21, Index331_0021},
                                       {313, FREE_PAWNS, 0, Index313},
                                       {133, FREE_PAWNS, 0, Index133},
                                       {4111, FREE_PAWNS, 0, Index4111},
                                       {1411, FREE_PAWNS, 0, Index1411},
                                       {1141, FREE_PAWNS, 0, Index1141},
                                       {1114, FREE_PAWNS, 0, Index1114},
                                       {421, FREE_PAWNS, 0, Index421},
                                       {421, FREE_PAWNS, 10, Index421_0010},
                                       {421, FREE_PAWNS, 11, Index421_0011},
                                       {412, FREE_PAWNS, 0, Index412},
                                       {241, FREE_PAWNS, 0, Index241},
                                       {214, FREE_PAWNS, 0, Index214},
                                       {142, FREE_PAWNS, 0, Index142},
                                       {124, FREE_PAWNS, 0, Index124},
                                       {43, FREE_PAWNS, 0, Index43},
                                       {34, FREE_PAWNS, 0, Index34},
                                       {511, FREE_PAWNS, 0, Index511},
                                       {151, FREE_PAWNS, 0, Index151},
                                       {115, FREE_PAWNS, 0, Index115},
                                       {52, FREE_PAWNS, 0, Index52},
                                       {25, FREE_PAWNS, 0, Index25},
                                       {61, FREE_PAWNS, 0, Index61},
                                       {16, FREE_PAWNS, 0, Index16},
                                       {1, FREE_PAWNS, 0, Index1},
                                       {11, FREE_PAWNS, 0, Index11},
                                       {11, BP_11_PAWNS, 0, IndexBP11},
                                       {11, OP_11_PAWNS, 0, IndexOP11},
                                       {111, FREE_PAWNS, 0, Index111},
                                       {111, BP_11_PAWNS, 0, IndexBP111},
                                       {111, OP_11_PAWNS, 0, IndexOP111},
                                       {1111, FREE_PAWNS, 0, Index1111},
                                       {1111, BP_11_PAWNS, 0, IndexBP1111},
                                       {1111, OP_11_PAWNS, 0, IndexOP1111},
                                       {11111, FREE_PAWNS, 0, Index11111},
                                       {11111, BP_11_PAWNS, 0, IndexBP11111},
                                       {11111, OP_11_PAWNS, 0, IndexOP11111},
                                       {2, FREE_PAWNS, 0, Index2},
                                       {2, FREE_PAWNS, 1100, Index2_1100},
                                       {21, FREE_PAWNS, 0, Index21},
                                       {21, OP_21_PAWNS, 0, IndexOP21},
                                       {12, FREE_PAWNS, 0, Index12},
                                       {12, OP_12_PAWNS, 0, IndexOP12},
                                       {211, FREE_PAWNS, 0, Index211},
                                       {211, OP_21_PAWNS, 0, IndexOP211},
                                       {121, FREE_PAWNS, 0, Index121},
                                       {121, OP_12_PAWNS, 0, IndexOP121},
                                       {112, FREE_PAWNS, 0, Index112},
                                       {112, BP_11_PAWNS, 0, IndexBP112},
                                       {112, OP_11_PAWNS, 0, IndexOP112},
                                       {2111, FREE_PAWNS, 0, Index2111},
                                       {2111, OP_21_PAWNS, 0, IndexOP2111},
                                       {1211, FREE_PAWNS, 0, Index1211},
                                       {1211, OP_12_PAWNS, 0, IndexOP1211},
                                       {1121, FREE_PAWNS, 0, Index1121},
                                       {1121, BP_11_PAWNS, 0, IndexBP1121},
                                       {1121, OP_11_PAWNS, 0, IndexOP1121},
                                       {1112, FREE_PAWNS, 0, Index1112},
                                       {1112, BP_11_PAWNS, 0, IndexBP1112},
                                       {1112, OP_11_PAWNS, 0, IndexOP1112},
                                       {22, FREE_PAWNS, 0, Index22},
                                       {22, DP_22_PAWNS, 0, IndexDP22},
                                       {22, OP_22_PAWNS, 0, IndexOP22},
                                       {221, FREE_PAWNS, 0, Index221},
                                       {221, DP_22_PAWNS, 0, IndexDP221},
                                       {221, OP_22_PAWNS, 0, IndexOP221},
                                       {212, FREE_PAWNS, 0, Index212},
                                       {212, OP_21_PAWNS, 0, IndexOP212},
                                       {122, FREE_PAWNS, 0, Index122},
                                       {122, OP_12_PAWNS, 0, IndexOP122},
                                       {3, FREE_PAWNS, 0, Index3},
                                       {3, FREE_PAWNS, 1100, Index3_1100},
                                       {31, FREE_PAWNS, 0, Index31},
                                       {31, OP_31_PAWNS, 0, IndexOP31},
                                       {13, FREE_PAWNS, 0, Index13},
                                       {13, OP_13_PAWNS, 0, IndexOP13},
                                       {311, FREE_PAWNS, 0, Index311},
                                       {311, OP_31_PAWNS, 0, IndexOP311},
                                       {131, FREE_PAWNS, 0, Index131},
                                       {131, OP_13_PAWNS, 0, IndexOP131},
                                       {113, FREE_PAWNS, 0, Index113},
                                       {113, BP_11_PAWNS, 0, IndexBP113},
                                       {113, OP_11_PAWNS, 0, IndexOP113},
                                       {32, FREE_PAWNS, 0, Index32},
                                       {23, FREE_PAWNS, 0, Index23},
                                       {4, FREE_PAWNS, 0, Index4},
                                       {41, FREE_PAWNS, 0, Index41},
                                       {14, FREE_PAWNS, 0, Index14},
                                       {5, FREE_PAWNS, 0, Index5},
                                       {51, FREE_PAWNS, 0, Index51},
                                       {15, FREE_PAWNS, 0, Index15},
                                       {6, FREE_PAWNS, 0, Index6},
                                       {7, FREE_PAWNS, 0, Index7}};

#define NumIndexTypes (sizeof(IndexTable) / sizeof(IndexTable[0]))

static int SetBoard(BOARD *Board, const PIECE board[NSQUARES], SIDE side,
                    int ep_square) {
    int npieces = 0, nwhite = 0, nblack = 0, i;

    memcpy(Board->board, board, sizeof(Board->board));
    Board->side = side;
    Board->ep_square = ep_square;
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
            }
            npieces++;
        }
    }

    Board->num_pieces = npieces;

    return npieces;
}

static int GetEndingType(const int count[2][KING], PIECE *piece_types,
                         PARITY bishop_parity[2],
                         PAWN_FILE_TYPE pawn_file_type) {
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

        for (size_t i = 0; i < NumIndexTypes; i++) {
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

        for (size_t i = 0; i < NumIndexTypes; i++) {
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
                         PAWN_FILE_TYPE *pawn_file_type) {
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

static int GetMBInfo(const BOARD *Board, MB_INFO *mb_info) {
    mb_info->num_parities = 0;
    mb_info->pawn_file_type = FREE_PAWNS;

    if (Board->num_pieces > MAX_PIECES_MB) {
        return TOO_MANY_PIECES;
    }

    memcpy(mb_info->piece_type_count, Board->piece_type_count,
           sizeof(Board->piece_type_count));

    PARITY bishop_parity[2] = {NONE, NONE};

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
        PARITY sub_bishop_parity[2];
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

void mbeval_init(void) {
    InitTransforms();
    InitParity();
    InitPermutationTables();
}

int mbeval_get_mb_info(const PIECE pieces[NSQUARES], SIDE side, int ep_square,
                       MB_INFO *info) {
    assert(pieces != NULL);
    assert(info != NULL);

    BOARD board;
    SetBoard(&board, pieces, side, ep_square);

    memset(info, 0, sizeof(MB_INFO));
    return GetMBInfo(&board, info);
}
