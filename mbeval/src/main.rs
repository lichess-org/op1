use std::ffi::c_int;

use mbeval_sys::{
    CONTEXT, mbeval_add_path, mbeval_context_create, mbeval_context_destroy, mbeval_context_probe,
    mbeval_init,
};
use shakmaty::{CastlingMode, Chess, EnPassantMode, Position, Role, fen::Fen};

struct Context {
    ctx: *mut CONTEXT,
}

impl Context {
    pub unsafe fn new() -> Self {
        Context {
            ctx: unsafe { mbeval_context_create() },
        }
    }

    pub fn probe(&mut self, pos: &Chess) -> Option<i32> {
        if pos.castles().any() {
            return None;
        }

        let mut squares = [0; 64];
        for (sq, piece) in pos.board() {
            let role = match piece.role {
                Role::Pawn => mbeval_sys::PAWN,
                Role::Knight => mbeval_sys::KNIGHT,
                Role::Bishop => mbeval_sys::BISHOP,
                Role::Rook => mbeval_sys::ROOK,
                Role::Queen => mbeval_sys::QUEEN,
                Role::King => mbeval_sys::KING,
            } as c_int;
            squares[usize::from(sq)] = piece.color.fold_wb(role, -role);
        }
        let result = unsafe {
            mbeval_context_probe(
                self.ctx,
                squares.as_ptr(),
                pos.turn()
                    .fold_wb(mbeval_sys::WHITE as c_int, mbeval_sys::BLACK as c_int),
                pos.ep_square(EnPassantMode::Legal).map_or(0, c_int::from),
                0,
                0,
                1,
            )
        };

        Some(match result {
            v if v == mbeval_sys::DRAW as c_int => 0,
            v if v >= mbeval_sys::LOST as c_int => return None,
            v => v,
        })
    }
}

impl Drop for Context {
    fn drop(&mut self) {
        unsafe {
            mbeval_context_destroy(self.ctx);
        }
    }
}

fn assert_score(ctx: &mut Context, fen: &str, expected: Option<c_int>) {
    let pos: Chess = fen
        .parse::<Fen>()
        .unwrap()
        .into_position(CastlingMode::Chess960)
        .unwrap();

    assert_eq!(ctx.probe(&pos), expected);
}

fn main() {
    unsafe {
        mbeval_init();
        mbeval_add_path(c".".as_ptr());
    }

    let mut ctx = unsafe { Context::new() };
    assert_score(&mut ctx, "8/2b5/8/8/3P4/pPP5/P7/2k1K3 w - - 0 1", Some(-3));
    assert_score(&mut ctx, "8/2b5/8/8/3P4/pPP5/P7/1k2K3 w - - 0 1", Some(-1));
    assert_score(&mut ctx, "8/p1b5/8/8/3P4/1PP5/P7/1k2K3 w - - 0 1", Some(-2));
    assert_score(&mut ctx, "8/p1b5/8/2PP4/PP6/8/8/1k2K3 b - - 0 1", Some(-7));
    assert_score(&mut ctx, "8/p1b5/8/2PP4/PP6/8/8/1k2K3 w - - 0 1", Some(6));
    assert_score(&mut ctx, "8/2bp4/8/2PP4/PP6/8/8/1k2K3 w - - 0 1", Some(4));
    assert_score(&mut ctx, "8/1kbp4/8/2PP4/PP6/8/8/4K3 w - - 0 1", Some(0));
    assert_score(&mut ctx, "8/1kb1p3/8/2PP4/PP6/8/8/4K3 w - - 0 1", None);
}
