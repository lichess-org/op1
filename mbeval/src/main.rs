use std::{
    collections::HashMap,
    ffi::{CString, c_int},
    net::SocketAddr,
    os::unix::ffi::OsStrExt,
    path::PathBuf,
    sync::Mutex,
};

use axum::{
    Json, Router,
    extract::{Query, State},
    http::StatusCode,
    response::{IntoResponse, Response},
    routing::get,
};
use clap::{ArgAction, CommandFactory as _, Parser, builder::PathBufValueParser};
use listenfd::ListenFd;
use mbeval_sys::{
    CONTEXT, mbeval_add_path, mbeval_context_create, mbeval_context_destroy, mbeval_context_probe,
    mbeval_init,
};
use serde::{Deserialize, Serialize};
use shakmaty::{
    CastlingMode, Chess, EnPassantMode, Position, PositionError, Role, fen::Fen, uci::UciMove,
};
use tokio::{
    net::{TcpListener, UnixListener},
    task,
};
use tower::ServiceBuilder;
use tower_http::trace::TraceLayer;

struct Context {
    ctx: *mut CONTEXT,
}

impl Context {
    pub unsafe fn new() -> Self {
        Context {
            ctx: unsafe { mbeval_context_create() },
        }
    }

    pub fn probe(&mut self, pos: &Chess) -> Option<i32> {
        if pos.castles().any() {
            return None;
        }

        let mut squares = [0; 64];
        for (sq, piece) in pos.board() {
            let role = match piece.role {
                Role::Pawn => mbeval_sys::PAWN,
                Role::Knight => mbeval_sys::KNIGHT,
                Role::Bishop => mbeval_sys::BISHOP,
                Role::Rook => mbeval_sys::ROOK,
                Role::Queen => mbeval_sys::QUEEN,
                Role::King => mbeval_sys::KING,
            } as c_int;
            squares[usize::from(sq)] = piece.color.fold_wb(role, -role);
        }
        let result = unsafe {
            mbeval_context_probe(
                self.ctx,
                squares.as_ptr(),
                pos.turn()
                    .fold_wb(mbeval_sys::WHITE as c_int, mbeval_sys::BLACK as c_int),
                pos.ep_square(EnPassantMode::Legal).map_or(0, c_int::from),
                0,
                0,
                1,
            )
        };

        Some(match result {
            0 => return None,
            v if v == mbeval_sys::DRAW as c_int => 0,
            v if v >= mbeval_sys::LOST as c_int => return None,
            v if v <= -(mbeval_sys::LOST as c_int) => return None,
            v => v,
        })
    }
}

impl Drop for Context {
    fn drop(&mut self) {
        unsafe {
            mbeval_context_destroy(self.ctx);
        }
    }
}

unsafe impl Send for Context {}
unsafe impl Sync for Context {}

#[derive(Parser, Debug)]
struct Opt {
    #[arg(long, default_value = "127.0.0.1:9999")]
    bind: SocketAddr,
    #[arg(long, action = ArgAction::Append, value_parser = PathBufValueParser::new())]
    path: Vec<PathBuf>,
}

struct AppState {
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
            children.insert(m.to_uci(CastlingMode::Chess960), ctx.probe(&after));
        }
        ProbeResponse {
            parent: ctx.probe(&pos),
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

    // Initialize mbeval
    unsafe {
        mbeval_init();
    }
    tracing::info!("mbeval initialized");

    // Add paths
    for path in opt.path {
        unsafe {
            mbeval_add_path(
                CString::new(path.into_os_string().as_bytes())
                    .unwrap()
                    .as_c_str()
                    .as_ptr(),
            );
        }
    }

    // Start server
    let state: &'static AppState = Box::leak(Box::new(AppState {
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
    use super::*;

    fn assert_score(ctx: &mut Context, fen: &str, expected: Option<c_int>) {
        let pos: Chess = fen
            .parse::<Fen>()
            .unwrap()
            .into_position(CastlingMode::Chess960)
            .unwrap();

        assert_eq!(ctx.probe(&pos), expected);
    }

    #[test]
    fn test_kbpkpppp() {
        unsafe {
            mbeval_init();
            mbeval_add_path(c"..".as_ptr());
        }

        let mut ctx = unsafe { Context::new() };
        assert_score(&mut ctx, "8/2b5/8/8/3P4/pPP5/P7/2k1K3 w - - 0 1", Some(-3));
        assert_score(&mut ctx, "8/2b5/8/8/3P4/pPP5/P7/1k2K3 w - - 0 1", Some(-1));
        assert_score(&mut ctx, "8/p1b5/8/8/3P4/1PP5/P7/1k2K3 w - - 0 1", Some(-2));
        assert_score(&mut ctx, "8/p1b5/8/2PP4/PP6/8/8/1k2K3 b - - 0 1", Some(-7));
        assert_score(&mut ctx, "8/p1b5/8/2PP4/PP6/8/8/1k2K3 w - - 0 1", Some(6));
        assert_score(&mut ctx, "8/2bp4/8/2PP4/PP6/8/8/1k2K3 w - - 0 1", Some(4));
        assert_score(&mut ctx, "8/1kbp4/8/2PP4/PP6/8/8/4K3 w - - 0 1", Some(0));
        assert_score(&mut ctx, "8/1kb1p3/8/2PP4/PP6/8/8/4K3 w - - 0 1", None);
        assert_score(&mut ctx, "8/4p3/8/6P1/4PP2/5b2/7P/5k1K w - - 1 3", None);
    }
}
