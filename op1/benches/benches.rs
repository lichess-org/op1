use std::hint::black_box;

use criterion::{Criterion, criterion_group, criterion_main};
use op1::{Tablebase, Value, guess_winner};
use shakmaty::{CastlingMode, Chess, fen::Fen};

fn kbpkpppp(c: &mut Criterion) {
    let pos: Chess = "8/2b5/8/8/3P4/pPP5/P7/1k2K3 w - - 0 1"
        .parse::<Fen>()
        .unwrap()
        .into_position(CastlingMode::Chess960)
        .unwrap();

    c.bench_function("probe_kbpkpppp", |b| {
        let mut tablebase = Tablebase::new();
        tablebase.add_path("../tables").unwrap();

        b.iter(|| {
            // Test position will cause two table probes. The block size is
            // 635392 bytes. Byte offsets into the block are 20981 and 334346.
            assert_eq!(
                tablebase.probe(black_box(&pos)).unwrap(),
                black_box(Some(Value::Dtc(-1)))
            );
        });
    });

    c.bench_function("guess_kbpkpppp", |b| {
        b.iter(|| guess_winner(black_box(&pos)));
    });
}

criterion_group!(benches, kbpkpppp);
criterion_main!(benches);
