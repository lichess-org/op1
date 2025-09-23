use std::{
    ffi::c_int,
    io,
    mem::MaybeUninit,
    path::{Path, PathBuf},
    sync::{
        Once,
        atomic::{AtomicU64, Ordering},
    },
    fmt,
};

use mbeval_sys::{
    BishopParity, MbInfo, PawnFileType, Side, ZIndex, mbeval_get_mb_info, mbeval_init,
};
use once_cell::sync::OnceCell;
use rustc_hash::FxHashMap;
use shakmaty::{
    Board, ByColor, ByRole, CastlingMode, Chess, Color, EnPassantMode, Position as _, Role,
    fen::Fen,
};

use crate::meta::Meta;
use crate::table::{MbValue, ProbeContext, SideValue, Table, TableType};

const ALL_ONES: ZIndex = !0;

static INIT_MBEVAL: Once = Once::new();

pub struct Tablebase {
    meta: FxHashMap<DirectoryKey, Meta>,
    tables: FxHashMap<TableKey, (PathBuf, OnceCell<Table>)>,
    stats: Stats,
}

impl Default for Tablebase {
    fn default() -> Tablebase {
        Tablebase::new()
    }
}

impl Tablebase {
    pub fn new() -> Tablebase {
        INIT_MBEVAL.call_once(|| {
            unsafe {
                mbeval_init();
            }
            tracing::info!("mbeval initialized");
        });

        Tablebase {
            meta: FxHashMap::default(),
            tables: FxHashMap::default(),
            stats: Stats::default(),
        }
    }

    pub fn add_path(&mut self, path: impl AsRef<Path>) -> io::Result<usize> {
        let mut num = 0;
        for directory in path.as_ref().read_dir()? {
            let directory = directory?.path();
            if let Some((dir_material, pawn_file_type, bishop_parity)) = parse_dirname(&directory) {
                for file in directory.read_dir()? {
                    let file = file?.path();
                    if let Some((file_material, side, kk_index, table_type)) = parse_filename(&file)
                    {
                        if dir_material == file_material {
                            self.tables.insert(
                                TableKey {
                                    material: file_material,
                                    pawn_file_type,
                                    bishop_parity,
                                    side,
                                    kk_index,
                                    table_type,
                                },
                                (file, OnceCell::new()),
                            );
                            num += 1;
                        }
                    }
                }
            }
        }
        tracing::info!("added {num} table files");
        Ok(num)
    }

    fn open_table(&self, key: &TableKey) -> io::Result<Option<&Table>> {
        self.tables
            .get(key)
            .map(|(path, table)| table.get_or_try_init(|| Table::open(path, key.table_type)))
            .transpose()
    }

    fn select_table(
        &self,
        pos: &Chess,
        mb_info: &MbInfo,
        table_type: TableType,
    ) -> io::Result<Option<(&Table, ZIndex)>> {
        let table_key = TableKey {
            material: pos.board().material(),
            pawn_file_type: PawnFileType::Free,
            bishop_parity: ByColor::new_with(|_| BishopParity::None),
            side: pos.turn(),
            kk_index: KkIndex(mb_info.kk_index as u32),
            table_type,
        };

        for bishop_parity in &mb_info.parity_index[..mb_info.num_parities as usize] {
            if let Some(table) = self.open_table(&TableKey {
                bishop_parity: ByColor {
                    white: bishop_parity.bishop_parity[Side::White as usize],
                    black: bishop_parity.bishop_parity[Side::Black as usize],
                },
                ..table_key
            })? {
                return Ok(Some((table, bishop_parity.index)));
            }
        }

        let index = match mb_info.pawn_file_type {
            PawnFileType::Free => ALL_ONES,
            PawnFileType::Bp11 => {
                if mb_info.index_op_11 != ALL_ONES {
                    if let Some(table) = self.open_table(&TableKey {
                        pawn_file_type: PawnFileType::Op11,
                        ..table_key
                    })? {
                        return Ok(Some((table, mb_info.index_op_11)));
                    }
                }
                mb_info.index_bp_11
            }
            PawnFileType::Op11 => mb_info.index_op_11,
            PawnFileType::Op21 => mb_info.index_op_21,
            PawnFileType::Op12 => mb_info.index_op_12,
            PawnFileType::Op22 => mb_info.index_op_22,
            PawnFileType::Dp22 => {
                if mb_info.index_op_22 != ALL_ONES {
                    if let Some(table) = self.open_table(&TableKey {
                        pawn_file_type: PawnFileType::Op22,
                        ..table_key
                    })? {
                        return Ok(Some((table, mb_info.index_op_22)));
                    }
                }
                mb_info.index_dp_22
            }
            PawnFileType::Op31 => mb_info.index_op_31,
            PawnFileType::Op13 => mb_info.index_op_13,
            PawnFileType::Op41 => mb_info.index_op_41,
            PawnFileType::Op14 => mb_info.index_op_14,
            PawnFileType::Op32 => mb_info.index_op_32,
            PawnFileType::Op23 => mb_info.index_op_23,
            PawnFileType::Op33 => mb_info.index_op_33,
            PawnFileType::Op42 => mb_info.index_op_42,
            PawnFileType::Op24 => mb_info.index_op_24,
        };

        if index == ALL_ONES {
            return Ok(None);
        }

        Ok(self
            .open_table(&TableKey {
                pawn_file_type: mb_info.pawn_file_type,
                ..table_key
            })?
            .map(|table| (table, index)))
    }

    fn probe_side(
        &self,
        pos: &Chess,
        ctx: &mut ProbeContext,
    ) -> Result<Option<SideValue>, io::Error> {
        // If one side has no pieces, only the other side can potentially win.
        if !pos.board().white().more_than_one() {
            return Ok(Some(SideValue::Unresolved));
        }

        // Retrieve MB_INFO struct.
        let mut squares = [mbeval_sys::Piece::NO_PIECE; 64];
        for (sq, piece) in pos.board() {
            let role = match piece.role {
                Role::Pawn => mbeval_sys::Piece::PAWN,
                Role::Knight => mbeval_sys::Piece::KNIGHT,
                Role::Bishop => mbeval_sys::Piece::BISHOP,
                Role::Rook => mbeval_sys::Piece::ROOK,
                Role::Queen => mbeval_sys::Piece::QUEEN,
                Role::King => mbeval_sys::Piece::KING,
            };
            squares[usize::from(sq)] = piece.color.fold_wb(role, -role);
        }
        let mut mb_info: MaybeUninit<MbInfo> = MaybeUninit::zeroed();
        let result = unsafe {
            mbeval_get_mb_info(
                squares.as_ptr(),
                pos.turn().fold_wb(Side::White, Side::Black),
                pos.ep_square(EnPassantMode::Legal).map_or(0, c_int::from),
                mb_info.as_mut_ptr(),
            )
        };
        if result != 0 {
            return Ok(None);
        }
        let mb_info = unsafe { mb_info.assume_init() };

        let Some((table, index)) = self.select_table(pos, &mb_info, TableType::Mb)? else {
            return Ok(None);
        };

        Ok(match table.read_mb(index, ctx)? {
            MbValue::Dtc(dtc) => Some(SideValue::Dtc(i32::from(dtc))),
            MbValue::Unresolved => Some(SideValue::Unresolved),
            MbValue::MaybeHighDtc => self
                .select_table(pos, &mb_info, TableType::HighDtc)?
                .map(|(table, index)| table.read_high_dtc(index, ctx))
                .transpose()?,
        })
    }

    pub fn probe(&self, pos: &Chess) -> Result<Option<Value>, io::Error> {
        if pos.is_insufficient_material() {
            return Ok(Some(Value::Draw));
        }

        if pos.board().occupied().count() > 9 || pos.castles().any() {
            return Ok(None);
        }

        // Make the stronger side white to reduce the chance of having to probe the
        // flipped position.
        let pos = if strength(pos.board(), Color::White) < strength(pos.board(), Color::Black) {
            flip_position(pos.clone())
        } else {
            pos.clone()
        };

        let mut ctx = ProbeContext::new()?;

        match self.probe_side(&pos, &mut ctx)? {
            None => {
                tracing::warn!(
                    "no table for {}",
                    Fen(pos.clone().into_setup(EnPassantMode::Legal))
                );
                return Ok(None);
            }
            Some(SideValue::Dtc(n)) => {
                self.stats.true_predictions.fetch_add(1, Ordering::Relaxed);
                return Ok(Some(Value::Dtc(pos.turn().fold_wb(n, n.saturating_neg()))));
            }
            Some(SideValue::Unresolved) => (),
        }

        let pos = flip_position(pos);

        Ok(match self.probe_side(&pos, &mut ctx)? {
            None => {
                tracing::warn!(
                    "no table for {} (flipped)",
                    Fen(pos.clone().into_setup(EnPassantMode::Legal))
                );
                None
            }
            Some(SideValue::Dtc(n)) => {
                self.stats.false_predictions.fetch_add(1, Ordering::Relaxed);
                Some(Value::Dtc(pos.turn().fold_wb(n, n.saturating_neg())))
            }
            Some(SideValue::Unresolved) => {
                self.stats.draws.fetch_add(1, Ordering::Relaxed);
                Some(Value::Draw)
            }
        })
    }

    pub fn stats(&self) -> &Stats {
        &self.stats
    }
}

#[derive(Debug, Eq, PartialEq, Copy, Clone)]
pub enum Value {
    Draw,
    Dtc(i32),
}

impl Value {
    pub fn zero_draw(self) -> Option<i32> {
        match self {
            Value::Draw => Some(0),
            Value::Dtc(0) => None,
            Value::Dtc(dtc) => Some(dtc),
        }
    }
}

#[derive(Debug, Eq, Hash, PartialEq)]
pub struct DirectoryKey {
    material: Material,
    pawn_file_type: PawnFileType,
    bishop_parity: ByColor<BishopParity>,
}

impl fmt::Display for DirectoryKey{
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        todo!()
    }
}

#[derive(Debug, Eq, Hash, PartialEq)]
pub struct TableKey {
    material: Material,
    pawn_file_type: PawnFileType,
    bishop_parity: ByColor<BishopParity>,
    side: Color,
    kk_index: KkIndex,
    table_type: TableType,
}

type Material = ByColor<ByRole<u8>>;

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
struct KkIndex(u32);

fn parse_dirname(path: &Path) -> Option<(Material, PawnFileType, ByColor<BishopParity>)> {
    let name = path.file_name()?.to_str()?.strip_suffix("_out")?;

    let (name, black_bishop_parity) = if let Some(name) = name.strip_suffix("_bbo") {
        (name, BishopParity::Odd)
    } else if let Some(name) = name.strip_suffix("_bbe") {
        (name, BishopParity::Even)
    } else {
        (name, BishopParity::None)
    };

    let (name, white_bishop_parity) = if let Some(name) = name.strip_suffix("_wbo") {
        (name, BishopParity::Odd)
    } else if let Some(name) = name.strip_suffix("_wbe") {
        (name, BishopParity::Even)
    } else {
        (name, BishopParity::None)
    };

    let (name, pawn_file_type) =
        if black_bishop_parity == BishopParity::None && white_bishop_parity == BishopParity::None {
            if let Some(name) = name.strip_suffix("_bp1") {
                (name, PawnFileType::Bp11)
            } else if let Some(name) = name.strip_suffix("_op1") {
                (name, PawnFileType::Op11)
            } else if let Some(name) = name.strip_suffix("_op21") {
                (name, PawnFileType::Op21)
            } else if let Some(name) = name.strip_suffix("_op12") {
                (name, PawnFileType::Op12)
            } else if let Some(name) = name.strip_suffix("_dp2") {
                (name, PawnFileType::Dp22)
            } else if let Some(name) = name.strip_suffix("_op22") {
                (name, PawnFileType::Op22)
            } else if let Some(name) = name.strip_suffix("_op31") {
                (name, PawnFileType::Op31)
            } else if let Some(name) = name.strip_suffix("_op13") {
                (name, PawnFileType::Op13)
            } else if let Some(name) = name.strip_suffix("_op41") {
                (name, PawnFileType::Op41)
            } else if let Some(name) = name.strip_suffix("_op14") {
                (name, PawnFileType::Op14)
            } else if let Some(name) = name.strip_suffix("_op32") {
                (name, PawnFileType::Op32)
            } else if let Some(name) = name.strip_suffix("_op23") {
                (name, PawnFileType::Op23)
            } else if let Some(name) = name.strip_suffix("_op33") {
                (name, PawnFileType::Op33)
            } else if let Some(name) = name.strip_suffix("_op42") {
                (name, PawnFileType::Op42)
            } else if let Some(name) = name.strip_suffix("_op24") {
                (name, PawnFileType::Op24)
            } else {
                (name, PawnFileType::Free)
            }
        } else {
            (name, PawnFileType::Free)
        };

    Some((
        parse_material(name)?,
        pawn_file_type,
        ByColor {
            white: white_bishop_parity,
            black: black_bishop_parity,
        },
    ))
}

fn parse_filename(path: &Path) -> Option<(Material, Color, KkIndex, TableType)> {
    let name = path.file_name()?.to_str()?;

    let (name, table_type) = if let Some(name) = name.strip_suffix(".mb") {
        (name, TableType::Mb)
    } else if let Some(name) = name.strip_suffix(".hi") {
        (name, TableType::HighDtc)
    } else {
        return None;
    };

    let (name, side, kk_index) = if let Some((name, kk_index)) = name.split_once("_b_") {
        (name, Color::Black, kk_index)
    } else if let Some((name, kk_index)) = name.split_once("_w_") {
        (name, Color::White, kk_index)
    } else {
        return None;
    };

    Some((
        parse_material(name)?,
        side,
        KkIndex(kk_index.parse().ok()?),
        table_type,
    ))
}

fn parse_material(name: &str) -> Option<Material> {
    if name.len() > 9 {
        return None;
    }

    let mut material = Material::default();
    let mut color = None;
    for c in name.chars() {
        let role = Role::from_char(c)?;
        if role == Role::King {
            color = match color {
                None => Some(Color::White),
                Some(Color::White) => Some(Color::Black),
                Some(Color::Black) => return None,
            };
        };
        material[color?][role] += 1;
    }

    Some(material)
}

fn strength(board: &Board, color: Color) -> usize {
    let side = board.by_color(color);
    (side & board.pawns()).count()
        + (side & board.knights()).count() * 3
        + (side & board.bishops()).count() * 3
        + (side & board.rooks()).count() * 5
        + (side & board.queens()).count() * 9
}

#[must_use]
fn flip_position(pos: Chess) -> Chess {
    pos.into_setup(EnPassantMode::Legal)
        .into_mirrored()
        .position(CastlingMode::Chess960)
        .expect("equivalent position")
}

#[derive(Default)]
pub struct Stats {
    draws: AtomicU64,
    true_predictions: AtomicU64,
    false_predictions: AtomicU64,
}

impl Stats {
    pub fn new() -> Stats {
        Self::default()
    }

    pub fn draws(&self) -> u64 {
        self.draws.load(Ordering::Relaxed)
    }

    pub fn true_predictions(&self) -> u64 {
        self.true_predictions.load(Ordering::Relaxed)
    }

    pub fn false_predictions(&self) -> u64 {
        self.false_predictions.load(Ordering::Relaxed)
    }
}
