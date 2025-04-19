#pragma once

#include <stdint.h>

#define MAX_PIECES_MB 9

#define NROWS 8
#define NCOLS 8

#define NSQUARES ((NROWS) * (NCOLS))

enum { NO_COMPRESSION = 0, ZLIB, ZSTD, NUM_COMPRESSION_METHODS };

enum {
    NO_PIECE = 0,
    PAWN = 1,
    KNIGHT = 2,
    BISHOP = 4,
    ROOK = 8,
    QUEEN = (BISHOP | ROOK),
    KING = 16
};

enum { WHITE = 0, BLACK, NEUTRAL };

typedef uint64_t ZINDEX;

typedef struct {
    int etype, op_type, sub_type;
    ZINDEX (*IndexFromPos)(const int *pos);
} IndexType;

enum { NONE = 0, EVEN, ODD };

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
    OP_24_PAWNS,
};

typedef struct {
    ZINDEX index;
    const IndexType *eptr;
    int bishop_parity[2];
} PARITY_INDEX;

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

void mbeval_init(void);

int mbeval_get_mb_info(const int pieces[NSQUARES], int side, int ep_square,
                       int castle, int half_move, int full_move, MB_INFO *info);
