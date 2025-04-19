#pragma once

#include <stdint.h>

#define MAX_PIECES_MB 9

#define NROWS 8
#define NCOLS 8

#define NSQUARES ((NROWS) * (NCOLS))

struct _CONTEXT;

typedef struct _CONTEXT CONTEXT;

enum { NO_COMPRESSION = 0, ZLIB, ZSTD, NUM_COMPRESSION_METHODS };

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

enum { WHITE = 0, BLACK, NEUTRAL };

enum {
    LOST = 65000,
    DRAW,
    STALE_MATE,
    NOT_LOST,
    NOT_WON,
    HIGH_DTC_MISSING,
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

void mbeval_add_path(const char *path);

CONTEXT *mbeval_context_create(void);
void mbeval_context_destroy(CONTEXT *ctx);
int mbeval_context_probe(CONTEXT *ctx, const int pieces[NSQUARES], int side,
                         int ep_square, int castle, int half_move,
                         int full_move);
int mbeval_context_get_mb_result(CONTEXT *ctx, const int pieces[NSQUARES],
                                 int side, int ep_square, int castle,
                                 int half_move, int full_move);
int mbeval_get_mb_info(const int pieces[NSQUARES], int side, int ep_square,
                       int castle, int half_move, int full_move, MB_INFO *info);
