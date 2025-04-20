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
use rustc_hash::FxHashMap;
use serde::{Deserialize, Serialize};
use shakmaty::{CastlingMode, Chess, Position, PositionError, fen::Fen, uci::UciMove};
use tokio::{
    net::{TcpListener, UnixListener},
    task,
};
use tower::ServiceBuilder;
use tower_http::trace::TraceLayer;

use crate::tablebase::Tablebase;

mod table;
mod tablebase;

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
    use super::*;
    use crate::tablebase::Value;

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
        tb.add_path("..").unwrap();

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
}
