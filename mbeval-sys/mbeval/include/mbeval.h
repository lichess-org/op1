#pragma once

#define NROWS 8
#define NCOLS 8

#define NSQUARES ((NROWS) * (NCOLS))

struct _CONTEXT;

typedef struct _CONTEXT CONTEXT;

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

void mbeval_init(void);

void mbeval_add_path(const char *path);

CONTEXT *mbeval_context_create(void);
void mbeval_context_destroy(CONTEXT *ctx);
int mbeval_context_probe(CONTEXT *ctx, const int pieces[NSQUARES], int side,
                         int ep_square, int castle, int half_move,
                         int full_move);
