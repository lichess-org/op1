use std::{collections::HashMap, ffi::c_int, net::SocketAddr, path::PathBuf, sync::Mutex};

use axum::{
    Json, Router,
    extract::{Query, State},
    http::StatusCode,
    response::{IntoResponse, Response},
    routing::get,
};
use clap::{ArgAction, CommandFactory as _, Parser, builder::PathBufValueParser};
use listenfd::ListenFd;
use serde::{Deserialize, Serialize};
use shakmaty::{CastlingMode, Chess, Position, PositionError, fen::Fen, uci::UciMove};
use tokio::{
    net::{TcpListener, UnixListener},
    task,
};
use tower::ServiceBuilder;
use tower_http::trace::TraceLayer;

use crate::{probe::Context, tablebase::Tablebase};

mod probe;
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
    _tablebase: Tablebase,
    ctx: Mutex<Context>,
}

#[derive(Deserialize)]
struct ProbeQuery {
    fen: Fen,
}

#[derive(Serialize)]
struct ProbeResponse {
    parent: Option<c_int>,
    children: HashMap<UciMove, Option<c_int>>,
}

enum ProbeError {
    Position(PositionError<Chess>),
}

impl IntoResponse for ProbeError {
    fn into_response(self) -> Response {
        match self {
            ProbeError::Position(err) => (StatusCode::BAD_REQUEST, err.to_string()).into_response(),
        }
    }
}

impl From<PositionError<Chess>> for ProbeError {
    fn from(err: PositionError<Chess>) -> Self {
        ProbeError::Position(err)
    }
}

#[axum::debug_handler]
async fn handle_probe(
    State(app): State<&'static AppState>,
    Query(query): Query<ProbeQuery>,
) -> Result<Json<ProbeResponse>, ProbeError> {
    let pos = query.fen.into_position(CastlingMode::Chess960)?;

    let response = task::spawn_blocking(move || {
        let legals = pos.legal_moves();
        let mut children = HashMap::with_capacity(legals.len());
        let mut ctx = app.ctx.lock().expect("lock");
        for m in legals {
            let mut after = pos.clone();
            after.play_unchecked(&m);
            children.insert(
                m.to_uci(CastlingMode::Chess960),
                ctx.score_position(after).unwrap().dtc(),
            );
        }
        ProbeResponse {
            parent: ctx.score_position(pos).unwrap().dtc(),
            children,
        }
    })
    .await
    .expect("blocking probe");

    Ok(Json(response))
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
    let state: &'static AppState = Box::leak(Box::new(AppState {
        _tablebase: tablebase,
        ctx: Mutex::new(unsafe { Context::new() }),
    }));

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
    use mbeval_sys::mbeval_add_path;

    use super::*;
    use crate::probe::Score;

    fn assert_score(tb: &Tablebase, ctx: &mut Context, fen: &str, expected: Score) {
        let pos: Chess = fen
            .parse::<Fen>()
            .unwrap()
            .into_position(CastlingMode::Chess960)
            .unwrap();

        let mb_info = ctx.get_mb_info(&pos).unwrap();
        dbg!(tb.probe(&pos, &mb_info).unwrap());

        assert_eq!(ctx.score_position(pos).unwrap(), expected);
    }

    #[test]
    fn test_kbpkpppp() {
        let mut tb = Tablebase::new(); // Implies mveval_init

        dbg!(tb.add_path("..").unwrap());
        unsafe {
            mbeval_add_path(c"..".as_ptr());
        }

        let mut ctx = unsafe { Context::new() };
        assert_score(
            &tb,
            &mut ctx,
            "8/2b5/8/8/3P4/pPP5/P7/2k1K3 w - - 0 1",
            Score::Dtc(-3),
        );
        assert_score(
            &tb,
            &mut ctx,
            "8/2b5/8/8/3P4/pPP5/P7/1k2K3 w - - 0 1",
            Score::Dtc(-1),
        );
        assert_score(
            &tb,
            &mut ctx,
            "8/p1b5/8/8/3P4/1PP5/P7/1k2K3 w - - 0 1",
            Score::Dtc(-2),
        );
        assert_score(
            &tb,
            &mut ctx,
            "8/p1b5/8/2PP4/PP6/8/8/1k2K3 b - - 0 1",
            Score::Dtc(-7),
        );
        assert_score(
            &tb,
            &mut ctx,
            "8/p1b5/8/2PP4/PP6/8/8/1k2K3 w - - 0 1",
            Score::Dtc(6),
        );
        assert_score(
            &tb,
            &mut ctx,
            "8/2bp4/8/2PP4/PP6/8/8/1k2K3 w - - 0 1",
            Score::Dtc(4),
        );
        assert_score(
            &tb,
            &mut ctx,
            "8/1kbp4/8/2PP4/PP6/8/8/4K3 w - - 0 1",
            Score::Draw,
        );
        assert_score(
            &tb,
            &mut ctx,
            "8/1kb1p3/8/2PP4/PP6/8/8/4K3 w - - 0 1",
            Score::Unknown,
        );
        assert_score(
            &tb,
            &mut ctx,
            "8/4p3/8/6P1/4PP2/5b2/7P/5k1K w - - 1 3",
            Score::Dtc(0), // checkmate
        );
    }
}
