use std::ops;

include!(concat!(env!("OUT_DIR"), "/bindings.rs"));

impl ops::Neg for Piece {
    type Output = Piece;

    fn neg(self) -> Piece {
        Piece(-self.0)
    }
}

impl PawnFileType {
    pub const ALL: [PawnFileType; 16] = [
        PawnFileType::Free,
        PawnFileType::Bp11,
        PawnFileType::Op11,
        PawnFileType::Op21,
        PawnFileType::Op12,
        PawnFileType::Op22,
        PawnFileType::Dp22,
        PawnFileType::Op31,
        PawnFileType::Op13,
        PawnFileType::Op14,
        PawnFileType::Op41,
        PawnFileType::Op32,
        PawnFileType::Op23,
        PawnFileType::Op33,
        PawnFileType::Op42,
        PawnFileType::Op24,
    ];

    pub fn as_filename_part(self) -> Option<&'static str> {
        Some(match self {
            PawnFileType::Free => return None,
            PawnFileType::Bp11 => "bp1",
            PawnFileType::Op11 => "op1",
            PawnFileType::Op21 => "op21",
            PawnFileType::Op12 => "op12",
            PawnFileType::Op22 => "op22",
            PawnFileType::Dp22 => "dp2",
            PawnFileType::Op31 => "op31",
            PawnFileType::Op13 => "op13",
            PawnFileType::Op14 => "op14",
            PawnFileType::Op41 => "op41",
            PawnFileType::Op32 => "op32",
            PawnFileType::Op23 => "op23",
            PawnFileType::Op33 => "op33",
            PawnFileType::Op42 => "op42",
            PawnFileType::Op24 => "op24",
        })
    }

    pub fn from_filename_part(s: &str) -> Option<PawnFileType> {
        Self::ALL
            .into_iter()
            .find(|pawn_file_type| pawn_file_type.as_filename_part() == Some(s))
    }
}

impl BishopParity {
    pub const ALL: [BishopParity; 3] = [BishopParity::None, BishopParity::Even, BishopParity::Odd];

    pub fn as_filename_part_black(self) -> Option<&'static str> {
        Some(match self {
            BishopParity::None => return None,
            BishopParity::Even => "bbe",
            BishopParity::Odd => "bbo",
        })
    }

    pub fn as_filename_part_white(self) -> Option<&'static str> {
        Some(match self {
            BishopParity::None => return None,
            BishopParity::Even => "wbe",
            BishopParity::Odd => "wbo",
        })
    }

    pub fn from_filename_part_black(s: &str) -> Option<BishopParity> {
        Self::ALL
            .into_iter()
            .find(|bishop_parity| bishop_parity.as_filename_part_black() == Some(s))
    }

    pub fn from_filename_part_white(s: &str) -> Option<BishopParity> {
        Self::ALL
            .into_iter()
            .find(|bishop_parity| bishop_parity.as_filename_part_white() == Some(s))
    }
}
