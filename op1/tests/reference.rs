use op1::{ProbeLog, Tablebase, Value};
use shakmaty::{CastlingMode, Chess, fen::Fen};
use test_log::test;

fn open_tablebase() -> Tablebase {
    let mut tb = Tablebase::new(); // Implies mveval_init
    assert!(tb.add_path("../tables").unwrap() > 0);
    tb
}

fn assert_score(tb: &Tablebase, fen: &str, expected: Option<Value>) {
    let pos: Chess = fen
        .parse::<Fen>()
        .unwrap()
        .into_position(CastlingMode::Chess960)
        .unwrap();

    assert_eq!(
        tb.probe(&pos, &mut ProbeLog::ignore()).unwrap(),
        expected,
        "{fen}"
    );
}

#[test]
fn test_kbpkpppp() {
    let tb = open_tablebase();

    assert_score(
        &tb,
        "8/1pp5/p1p5/8/B7/8/P6k/2K5 w - - 0 1",
        Some(Value::Dtc(53)),
    );
    assert_score(
        &tb,
        "8/7p/k7/8/8/5P2/P5PP/K2b4 w - - 0 1",
        Some(Value::Dtc(42)),
    );
    assert_score(
        &tb,
        "8/2b5/8/8/3P4/pPP5/P7/2k1K3 w - - 0 1",
        Some(Value::Dtc(-3)),
    );
    assert_score(
        &tb,
        "8/2b5/8/8/3P4/pPP5/P7/1k2K3 w - - 0 1",
        Some(Value::Dtc(-1)),
    );
    assert_score(
        &tb,
        "8/p1b5/8/8/3P4/1PP5/P7/1k2K3 w - - 0 1",
        Some(Value::Dtc(-2)),
    );
    assert_score(
        &tb,
        "8/p1b5/8/2PP4/PP6/8/8/1k2K3 b - - 0 1",
        Some(Value::Dtc(-7)),
    );
    assert_score(
        &tb,
        "8/p1b5/8/2PP4/PP6/8/8/1k2K3 w - - 0 1",
        Some(Value::Dtc(6)),
    );
    assert_score(
        &tb,
        "8/2bp4/8/2PP4/PP6/8/8/1k2K3 w - - 0 1",
        Some(Value::Dtc(4)),
    );
    assert_score(
        &tb,
        "8/1kbp4/8/2PP4/PP6/8/8/4K3 w - - 0 1",
        Some(Value::Draw),
    );
    assert_score(&tb, "8/1kb1p3/8/2PP4/PP6/8/8/4K3 w - - 0 1", None);
    assert_score(
        &tb,
        "8/4p3/8/6P1/4PP2/5b2/7P/5k1K w - - 1 3",
        Some(Value::Dtc(0)), // checkmate
    );
}

#[test]
fn test_krbbpkqp() {
    let tb = open_tablebase();

    assert_score(
        &tb,
        "R7/8/8/8/7q/2K1B2p/7P/2Bk4 w - - 0 1",
        Some(Value::Dtc(584)),
    );
}

#[test]
fn test_kbnnpkqp() {
    let tb = open_tablebase();

    assert_score(
        &tb,
        "8/8/6B1/1K3p2/N3k1N1/8/5P2/2q5 w - - 0 1",
        Some(Value::Dtc(304)),
    );
}

#[test]
fn test_krrnkrr() {
    let tb = open_tablebase();

    for fen in &[
        "r7/5r1N/8/8/8/6R1/6R1/3K1k2 w - - 0 1",
        "r7/5r1N/8/8/6R1/8/6R1/3K1k2 w - - 0 1",
        "r7/5r1N/8/6R1/8/8/6R1/3K1k2 w - - 0 1",
        "r7/5r1N/6R1/8/8/8/6R1/3K1k2 w - - 0 1",
        "r7/5rRN/8/8/8/8/6R1/3K1k2 w - - 0 1",
        "r5R1/5r1N/8/8/8/8/6R1/3K1k2 w - - 0 1",
        "r7/5r1N/8/8/6R1/6R1/8/3K1k2 w - - 0 1",
        "r7/5r1N/8/6R1/8/6R1/8/3K1k2 w - - 0 1",
        "r7/5r1N/6R1/8/8/6R1/8/3K1k2 w - - 0 1",
        "r7/5r1N/8/6R1/6R1/8/8/3K1k2 w - - 0 1",
        "r7/5r1N/6R1/8/6R1/8/8/3K1k2 w - - 0 1",
        "r7/5r1N/8/8/6k1/8/7R/3KR3 w - - 0 1",
        "r7/5r1N/8/8/6k1/8/7R/3K1R2 w - - 0 1",
        "r7/5r1N/8/8/6k1/8/7R/3K3R w - - 0 1",
    ] {
        assert_score(&tb, fen, Some(Value::Dtc(290)));
    }
}

#[test]
fn test_knnppkrpp_dp2() {
    let tb = open_tablebase();

    assert_score(
        &tb,
        "1k2N3/1p1r4/3p4/3P4/8/8/KP6/N7 w - - 0 1",
        Some(Value::Dtc(128)),
    );
    assert_score(
        &tb,
        "1k2N3/1p1r4/3p4/3P4/8/8/KP6/4N3 w - - 0 1",
        Some(Value::Dtc(128)),
    );
}

#[test]
fn test_krppknnpp_dp2() {
    let tb = open_tablebase();

    assert_score(
        &tb,
        "n6k/6p1/4n1P1/6p1/8/3K4/5RP1/8 w - - 0 1",
        Some(Value::Dtc(78)),
    );
}
