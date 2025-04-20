#pragma once

#include <stdint.h>

#define MAX_PIECES_MB 9

#define NROWS 8
#define NCOLS 8

#define NSQUARES ((NROWS) * (NCOLS))

typedef enum {
    NO_PIECE = 0,
    PAWN = 1,
    KNIGHT = 2,
    BISHOP = 4,
    ROOK = 8,
    QUEEN = (BISHOP | ROOK),
    KING = 16,
    BLACK_PAWN = -PAWN,
    BLACK_KNIGHT = -KNIGHT,
    BLACK_BISHOP = -BISHOP,
    BLACK_ROOK = -ROOK,
    BLACK_QUEEN = -QUEEN,
    BLACK_KING = -KING,
} PIECE;

typedef enum { WHITE = 0, BLACK } SIDE;

typedef uint64_t ZINDEX;

typedef struct {
    int etype, op_type, sub_type;
    ZINDEX (*IndexFromPos)(const int *pos);
} IndexType;

typedef enum { None = 0, Even, Odd } BishopParity;

typedef enum {
    Free = 0,
    Bp11,
    Op11,
    Op21,
    Op12,
    Op22,
    Dp22,
    Op31,
    Op13,
    Op41,
    Op14,
    Op32,
    Op23,
    Op33,
    Op42,
    Op24,
} PawnFileType;

typedef struct {
    ZINDEX index;
    const IndexType *eptr;
    BishopParity bishop_parity[2];
} PARITY_INDEX;

typedef struct {
    PARITY_INDEX parity_index[4];
    int num_parities;
    int mb_position[MAX_PIECES_MB];
    PIECE mb_piece_types[MAX_PIECES_MB];
    int piece_type_count[2][KING];
    int parity;
    PawnFileType pawn_file_type;
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

void mbeval_init(void);

int mbeval_get_mb_info(const PIECE pieces[NSQUARES], SIDE side, int ep_square,
                       MB_INFO *info);
