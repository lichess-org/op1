use criterion::{Criterion, black_box, criterion_group, criterion_main};
use op1::{Tablebase, Value};
use shakmaty::{CastlingMode, Chess, fen::Fen};

fn kbpkpppp(c: &mut Criterion) {
    c.bench_function("kbpkpppp", |b| {
        let mut tablebase = Tablebase::new();
        tablebase.add_path("..").unwrap();

        b.iter(|| {
            // Test position will cause two table probes. The block size is
            // 635392 bytes. Byte offsets into the block are 20981 and 334346.

            let pos: Chess = black_box("8/2b5/8/8/3P4/pPP5/P7/1k2K3 w - - 0 1")
                .parse::<Fen>()
                .unwrap()
                .into_position(CastlingMode::Chess960)
                .unwrap();

            assert_eq!(
                tablebase.probe(&pos).unwrap(),
                black_box(Some(Value::Dtc(-1)))
            );
        });
    });
}

criterion_group!(benches, kbpkpppp);
criterion_main!(benches);
