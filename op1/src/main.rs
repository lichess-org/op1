use std::{io, net::SocketAddr, path::PathBuf};

use axum::{
    Json, Router,
    extract::{Query, State},
    http::StatusCode,
    response::{IntoResponse, Response},
    routing::get,
};
use clap::{ArgAction, CommandFactory as _, Parser, builder::PathBufValueParser};
use listenfd::ListenFd;
use op1::Tablebase;
use rustc_hash::FxHashMap;
use serde::{Deserialize, Serialize};
use shakmaty::{CastlingMode, Chess, Position, PositionError, fen::Fen, uci::UciMove};
use tokio::{
    net::{TcpListener, UnixListener},
    task,
};
use tower::ServiceBuilder;
use tower_http::trace::TraceLayer;

#[derive(Parser, Debug)]
struct Opt {
    #[arg(long, default_value = "127.0.0.1:9999")]
    bind: SocketAddr,
    #[arg(long, action = ArgAction::Append, value_parser = PathBufValueParser::new())]
    path: Vec<PathBuf>,
}

struct AppState {
    tablebase: Tablebase,
}

#[derive(Deserialize)]
struct ProbeQuery {
    fen: Fen,
}

#[derive(Serialize)]
struct ProbeResponse {
    parent: Option<i32>,
    children: FxHashMap<UciMove, Option<i32>>,
}

enum ProbeError {
    Position(PositionError<Chess>),
    Io(io::Error),
}

impl IntoResponse for ProbeError {
    fn into_response(self) -> Response {
        (match self {
            ProbeError::Position(err) => (StatusCode::BAD_REQUEST, err.to_string()),
            ProbeError::Io(err) => (StatusCode::INTERNAL_SERVER_ERROR, err.to_string()),
        })
        .into_response()
    }
}

impl From<PositionError<Chess>> for ProbeError {
    fn from(err: PositionError<Chess>) -> Self {
        ProbeError::Position(err)
    }
}

impl From<io::Error> for ProbeError {
    fn from(err: io::Error) -> Self {
        ProbeError::Io(err)
    }
}

#[axum::debug_handler]
async fn handle_probe(
    State(app): State<&'static AppState>,
    Query(query): Query<ProbeQuery>,
) -> Result<Json<ProbeResponse>, ProbeError> {
    let pos = query.fen.into_position(CastlingMode::Chess960)?;

    let child_handles = pos
        .legal_moves()
        .into_iter()
        .map(|m| {
            let mut after = pos.clone();
            after.play_unchecked(&m);
            (
                m,
                task::spawn_blocking(move || {
                    app.tablebase
                        .probe(&after)
                        .map(|maybe_v| maybe_v.and_then(|v| v.zero_draw()))
                }),
            )
        })
        .collect::<Vec<_>>();

    let parent = task::spawn_blocking(move || {
        app.tablebase
            .probe(&pos)
            .map(|maybe_v| maybe_v.and_then(|v| v.zero_draw()))
    })
    .await
    .expect("blocking parent probe")
    .inspect(|_| tracing::trace!("parent success"))
    .inspect_err(|error| tracing::error!(%error, "parent fail"))?;

    let mut children = FxHashMap::with_capacity_and_hasher(child_handles.len(), Default::default());
    for (m, child) in child_handles {
        let uci = m.to_uci(CastlingMode::Chess960);
        children.insert(
            uci.clone(),
            child
                .await
                .expect("blocking child probe")
                .inspect(|_| tracing::trace!(%uci, "child success"))
                .inspect_err(|error| tracing::error!(%uci, %error, "child fail"))?,
        );
    }

    Ok(Json(ProbeResponse { parent, children }))
}

#[axum::debug_handler]
async fn handle_monitor(State(app): State<&'static AppState>) -> String {
    let stats = app.tablebase.stats();
    let metrics = &[
        format!("draws={}u", stats.draws()),
        format!("true_predictions={}u", stats.true_predictions()),
        format!("false_predictions={}u", stats.false_predictions()),
    ];
    format!("op1 {}", metrics.join(","))
}

#[tokio::main]
async fn main() {
    // Parse arguments
    let opt = Opt::parse();
    if opt.path.is_empty() {
        Opt::command().print_help().expect("usage");
        println!();
        return;
    }

    // Prepare tracing
    tracing_subscriber::fmt()
        .event_format(tracing_subscriber::fmt::format().compact())
        .without_time()
        .with_env_filter(tracing_subscriber::EnvFilter::from_default_env())
        .init();

    // Initialize tablebase
    let mut tablebase = Tablebase::new();
    for path in opt.path {
        let num = tablebase.add_path(&path).expect("add path");
        tracing::info!("loaded {} tables from {}", num, path.display());
    }

    // Start server
    let state: &'static AppState = Box::leak(Box::new(AppState { tablebase }));

    let app = Router::new()
        .route("/", get(handle_probe))
        .route("/monitor", get(handle_monitor))
        .with_state(state)
        .layer(ServiceBuilder::new().layer(TraceLayer::new_for_http()));

    let mut fds = ListenFd::from_env();
    if let Ok(Some(uds)) = fds.take_unix_listener(0) {
        uds.set_nonblocking(true).expect("set nonblocking");
        let listener = UnixListener::from_std(uds).expect("listener");
        axum::serve(listener, app).await.expect("serve");
    } else if let Ok(Some(tcp)) = fds.take_tcp_listener(0) {
        tcp.set_nonblocking(true).expect("set nonblocking");
        let listener = TcpListener::from_std(tcp).expect("listener");
        axum::serve(listener, app).await.expect("serve");
    } else {
        let listener = TcpListener::bind(&opt.bind).await.expect("bind");
        axum::serve(listener, app).await.expect("serve");
    }
}

#[cfg(test)]
mod tests {
    use op1::Value;

    use super::*;

    fn assert_score(tb: &Tablebase, fen: &str, expected: Option<Value>) {
        let pos: Chess = fen
            .parse::<Fen>()
            .unwrap()
            .into_position(CastlingMode::Chess960)
            .unwrap();

        assert_eq!(tb.probe(&pos).unwrap(), expected);
    }

    #[test]
    fn test_kbpkpppp() {
        let mut tb = Tablebase::new(); // Implies mveval_init
        tb.add_path("../tables").unwrap();

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
        let mut tb = Tablebase::new(); // Implies mveval_init
        tb.add_path("../tables").unwrap();

        assert_score(
            &tb,
            "R7/8/8/8/7q/2K1B2p/7P/2Bk4 w - - 0 1",
            Some(Value::Dtc(584)),
        );
    }

    #[test]
    fn test_kbnnpkqp() {
        let mut tb = Tablebase::new(); // Implies mveval_init
        tb.add_path("../tables").unwrap();

        assert_score(
            &tb,
            "8/8/6B1/1K3p2/N3k1N1/8/5P2/2q5 w - - 0 1",
            Some(Value::Dtc(304)),
        );
    }

    #[test]
    fn test_krrnkrr() {
        let mut tb = Tablebase::new(); // Implies mveval_init
        tb.add_path("../tables").unwrap();

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
}
