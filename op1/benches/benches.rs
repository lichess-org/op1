use criterion::{Criterion, black_box, criterion_group, criterion_main};
use op1::{Tablebase, Value};
use shakmaty::{CastlingMode, Chess, fen::Fen};

fn kbpkpppp(c: &mut Criterion) {
    c.bench_function("kbpkpppp", |b| {
        let mut tablebase = Tablebase::new();
        tablebase.add_path("..").unwrap();

        b.iter(|| {
            let pos: Chess = black_box("8/p1b5/8/2PP4/PP6/8/8/1k2K3 b - - 0 1")
                .parse::<Fen>()
                .unwrap()
                .into_position(CastlingMode::Chess960)
                .unwrap();

            assert_eq!(
                tablebase.probe(&pos).unwrap(),
                black_box(Some(Value::Dtc(-7)))
            );

            let pos: Chess = black_box("8/1kbp4/8/2PP4/PP6/8/8/4K3 w - - 0 1")
                .parse::<Fen>()
                .unwrap()
                .into_position(CastlingMode::Chess960)
                .unwrap();

            assert_eq!(tablebase.probe(&pos).unwrap(), black_box(Some(Value::Draw)));
        });
    });
}

criterion_group!(benches, kbpkpppp);
criterion_main!(benches);
