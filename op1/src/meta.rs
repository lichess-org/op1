use rustc_hash::FxHashMap;
use serde::{Deserialize, Serialize};

#[derive(Debug, Serialize, Deserialize, Clone)]
pub struct Meta {
    files: FxHashMap<String, FileMeta>,
    histograms: Histograms,
    max_positions: Vec<MaxPosition>,
    wtm_max_win: u32,
    btm_max_loss: u32,
    wtm_wins: u64,
    btm_loses: u64,
    wtm_draws: u64,
    btm_draws: u64,
    btm_wins: u64,
    wtm_legal: u64,
    btm_legal: u64,
    btm_stalemated: u64,
    wtm_winning_captures: u64,
    btm_saving_captures: u64,
}

#[derive(Debug, Serialize, Deserialize, Clone)]
pub struct FileMeta {
    bytes: u64,
    md5: Option<String>,
    sha256: Option<String>,
}

#[derive(Debug, Serialize, Deserialize, Clone)]
pub struct Histograms {
    wtm_wins: Vec<u64>,
    wtm_lost: Vec<u64>,
    btm_wins: Vec<u64>,
    btm_lost: Vec<u64>,
}

#[derive(Debug, Serialize, Deserialize, Clone)]
pub struct MaxPosition {
    dtc: u32,
    fen: String,
}
