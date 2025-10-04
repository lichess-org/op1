use std::ops;

include!(concat!(env!("OUT_DIR"), "/bindings.rs"));

impl ops::Neg for Piece {
    type Output = Piece;

    fn neg(self) -> Piece {
        Piece(-self.0)
    }
}
