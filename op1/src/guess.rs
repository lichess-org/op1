use shakmaty::{Board, Chess, Color, Position, Role};

fn role_value(role: Role) -> i32 {
    match role {
        Role::Pawn => 1,
        Role::Knight => 3,
        Role::Bishop => 3,
        Role::Rook => 5,
        Role::Queen => 9,
        Role::King => 0,
    }
}

fn strength(board: &Board, color: Color) -> i32 {
    let side = board.by_color(color);
    (side & board.pawns()).count() as i32
        + (side & board.knights()).count() as i32 * role_value(Role::Knight)
        + (side & board.bishops()).count() as i32 * role_value(Role::Bishop)
        + (side & board.rooks()).count() as i32 * role_value(Role::Rook)
        + (side & board.queens()).count() as i32 * role_value(Role::Queen)
}

fn material_balance(pos: &Chess) -> i32 {
    let current = strength(pos.board(), pos.turn()) - strength(pos.board(), !pos.turn());

    pos.legal_moves()
        .into_iter()
        .map(|m| {
            current
                + m.promotion()
                    .map_or(0, |r| role_value(r) - role_value(Role::Pawn))
                + m.capture().map_or(0, |r| role_value(r))
        })
        .max()
        .unwrap_or(i32::MIN)
}

pub fn guess_winner(pos: &Chess) -> Color {
    pos.turn() ^ (material_balance(pos) < 0)
}

#[cfg(test)]
mod tests {
    use shakmaty::{CastlingMode, fen::Fen};

    use super::*;

    #[test]
    fn test_guess_winner() {
        let mut true_prediction = 0;
        let mut total = 0;

        for (fen, dtc) in [
            ("8/3k4/6R1/2r3p1/4P3/5P2/6PK/8 w - -", 2),
            ("8/8/8/5p2/p3k2p/P6P/3K1P2/8 w - -", -7),
            ("8/3k4/6R1/2r3p1/4P3/5PK1/6P1/8 b - -", -2),
            ("6R1/8/4pp2/1P6/k7/8/5PP1/6K1 w - -", 2),
            ("8/8/8/p7/Pp5k/1P3Kp1/8/7B b - -", 0),
            ("5r2/6kp/7B/5Q2/q6P/8/6K1/8 b - -", -1),
            ("6R1/8/1P2pp2/1k6/8/8/5PP1/6K1 w - -", 2),
            ("8/3k4/6R1/6p1/4P3/5PK1/2r3P1/8 w - -", 1),
            ("6R1/1P6/4pp2/1k6/8/8/5PP1/6K1 b - -", -1),
            ("8/8/8/p7/Pp6/1P3Kpk/8/7B w - -", 0),
            ("8/4k3/6R1/2r3p1/4P3/5PK1/6P1/8 w - -", 2),
            ("8/4k3/6R1/2r3p1/4P1K1/5P2/6P1/8 b - -", -1),
            ("8/8/8/5p2/p3k2p/P6P/4KP2/8 b - -", 7),
            ("8/4k3/6R1/6p1/4P1K1/5P2/2r3P1/8 w - -", 1),
            ("8/8/8/5p2/p4k1p/P6P/4KP2/8 w - -", -6),
            ("8/4k3/6R1/6p1/4P1K1/5PP1/2r5/8 b - -", -1),
            ("8/8/8/5p2/p4k1p/P6P/5P2/4K3 b - -", 6),
            ("8/4k3/6R1/6p1/2r1P1K1/5PP1/8/8 w - -", 1),
            ("8/Q7/7k/6p1/P7/5K1P/6P1/1r6 b - -", -4),
            ("8/8/8/8/p4p1p/P4k1P/5P2/5K2 w - -", -4),
            ("8/p7/2K5/3p4/PP1P4/4k3/4p3/8 b - -", 1),
            ("8/4k3/6R1/2r3p1/4P1K1/5PP1/8/8 w - -", 1),
            ("8/p7/2K5/3p4/PP1P4/4k3/8/4q3 w - -", -1),
            ("8/7k/8/8/1R1Pp3/2r1P3/6P1/6K1 b - -", 0),
            ("4k3/8/8/8/8/8/8/3K4 b - -", 0),    // Immediate draw
            ("1R2k3/8/4K3/8/8/8/8/8 b - -", -1), // Checkmated
            ("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq -", 0), // Start pos
        ] {
            let pos = fen
                .parse::<Fen>()
                .expect("valid fen")
                .into_position::<Chess>(CastlingMode::Chess960)
                .expect("legal position");

            let winner = if dtc >= 0 {
                pos.turn()
            } else {
                pos.turn().other()
            };

            true_prediction += u32::from(guess_winner(&pos) == winner);
            total += 1;
        }

        assert!(
            true_prediction * 100 / total >= 75,
            "{} %",
            true_prediction * 100 / total
        );
    }
}
