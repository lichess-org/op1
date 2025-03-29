use std::{ffi::c_int, ops::Neg};

use mbeval_sys::{
    CONTEXT, mbeval_context_create, mbeval_context_destroy, mbeval_context_get_mb_result,
};
use shakmaty::{Board, CastlingMode, Chess, Color, EnPassantMode, Position as _, Role};

#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub enum Score {
    Dtc(i32),
    Draw,
    Unknown,
    Unresolved,
    Lost,
    Won,
    NotLost,
    NotWon,
    HighDtcMissing,
}

impl Score {
    pub fn dtc(self) -> Option<i32> {
        match self {
            Score::Dtc(dtc) => Some(dtc),
            _ => None,
        }
    }
}

impl Neg for Score {
    type Output = Score;

    fn neg(self) -> Self::Output {
        match self {
            Score::Dtc(dtc) => Score::Dtc(-dtc),
            Score::Draw => Score::Draw,
            Score::Unknown => Score::Unknown,
            Score::Unresolved => Score::Unresolved,
            Score::Lost => Score::Won,
            Score::Won => Score::Lost,
            Score::NotLost => Score::NotWon,
            Score::NotWon => Score::NotLost,
            Score::HighDtcMissing => Score::HighDtcMissing,
        }
    }
}

#[derive(Debug)]
pub struct Error {}

pub struct Context {
    ctx: *mut CONTEXT,
}

impl Context {
    pub unsafe fn new() -> Self {
        Context {
            ctx: unsafe { mbeval_context_create() },
        }
    }

    fn get_mb_result(&mut self, pos: &Chess) -> Result<Score, Error> {
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
            mbeval_context_get_mb_result(
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

        Ok(match result {
            v if v == mbeval_sys::LOST as i32 => Score::Lost,
            v if v == mbeval_sys::DRAW as i32 => Score::Draw,
            v if v == mbeval_sys::NOT_LOST as i32 => Score::NotLost,
            v if v == mbeval_sys::NOT_WON as i32 => Score::NotWon,
            v if v == mbeval_sys::HIGH_DTC_MISSING as i32 => Score::HighDtcMissing,
            v if v == mbeval_sys::WON as i32 => Score::Won,
            v if v == mbeval_sys::UNRESOLVED as i32 => Score::Unresolved,
            v if v == mbeval_sys::UNKNOWN as i32 => Score::Unknown,
            v => Score::Dtc(v),
        })
    }

    pub fn score_position(&mut self, mut pos: Chess) -> Result<Score, Error> {
        if pos.is_insufficient_material() {
            return Ok(Score::Draw);
        }

        if pos.board().occupied().count() > 9 || pos.castles().any() {
            return Ok(Score::Unknown);
        }

        // Make the stronger side white to reduce the chance of having to probe the
        // flipped position.
        if strength(pos.board(), Color::White) < strength(pos.board(), Color::Black) {
            pos = flip_position(pos)
        }

        let result = self.get_mb_result(&pos)?;

        // If we have a definite result, we can return right away.
        if !(result.dtc().is_some_and(|dtc| dtc < 0) || result == Score::Unresolved) {
            if pos.turn() == Color::White
                || result == Score::Lost
                || result == Score::Won
                || result == Score::HighDtcMissing
            {
                return Ok(result);
            } else {
                return Ok(-result);
            }
        }

        // If one side has no pieces, there is no flipped table. So unresolved
        // becomes a draw.
        if !pos.board().black().more_than_one() {
            if result.dtc().is_some_and(|dtc| dtc < 0) {
                return Ok(Score::Unknown);
            } else if result == Score::Unresolved {
                return Ok(Score::Draw);
            }
        }

        pos = flip_position(pos);

        let result_flipped = self.get_mb_result(&pos)?;

        Ok(
            if result_flipped == Score::Won
                || result_flipped == Score::Lost
                || result_flipped == Score::HighDtcMissing
            {
                result_flipped
            } else if result_flipped.dtc().is_some_and(|dtc| dtc >= 0) {
                pos.turn().fold_wb(result_flipped, -result_flipped)
            } else if result_flipped.dtc().is_some_and(|dtc| dtc < 0)
                && result.dtc().is_some_and(|dtc| dtc < 0)
            {
                Score::Unknown
            } else if result_flipped == Score::Unresolved && result == Score::Unresolved {
                Score::Draw
            } else if result == Score::Unresolved && result_flipped.dtc().is_some_and(|dtc| dtc < 0)
            {
                pos.turn().fold_wb(Score::NotLost, Score::NotWon)
            } else if result.dtc().is_some_and(|dtc| dtc < 0) && result_flipped == Score::Unresolved
            {
                pos.turn().fold_wb(Score::NotWon, Score::NotLost)
            } else {
                Score::Unknown
            },
        )
    }
}

impl Drop for Context {
    fn drop(&mut self) {
        unsafe {
            mbeval_context_destroy(self.ctx);
        }
    }
}

unsafe impl Send for Context {}
unsafe impl Sync for Context {}

fn strength(board: &Board, color: Color) -> usize {
    (board.by_color(color) & board.pawns()).count()
        + (board.by_color(color) & board.knights()).count() * 3
        + (board.by_color(color) & board.bishops()).count() * 3
        + (board.by_color(color) & board.rooks()).count() * 5
        + (board.by_color(color) & board.queens()).count() * 9
}

#[must_use]
fn flip_position(pos: Chess) -> Chess {
    pos.into_setup(EnPassantMode::Legal)
        .into_mirrored()
        .position(CastlingMode::Chess960)
        .expect("equivalent position")
}
