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
    root: Option<i32>,
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

    let root = task::spawn_blocking(move || {
        app.tablebase
            .probe(&pos)
            .map(|maybe_v| maybe_v.and_then(|v| v.zero_draw()))
    })
    .await
    .expect("blocking root probe")
    .inspect(|_| tracing::trace!("root success"))
    .inspect_err(|error| tracing::error!(%error, "root fail"))?;

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

    Ok(Json(ProbeResponse { root, children }))
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
