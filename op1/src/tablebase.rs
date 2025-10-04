use std::{
    error::Error,
    ffi::c_int,
    fmt,
    fs::File,
    io,
    mem::MaybeUninit,
    path::{Path, PathBuf},
    str::FromStr,
    sync::{
        Once,
        atomic::{AtomicU64, Ordering},
    },
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

use crate::{
    meta::Meta,
    table::{MbValue, ProbeContext, SideValue, Table, TableType},
};

const ALL_ONES: ZIndex = !0;

static INIT_MBEVAL: Once = Once::new();

pub struct Tablebase {
    meta: FxHashMap<DirectoryKey, Option<Meta>>,
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
            if let Some(directory_key) = parse_dirname(&directory) {
                self.meta.insert(
                    directory_key.clone(),
                    match read_meta_file(path.as_ref(), &directory_key) {
                        Ok(meta) => Some(meta),
                        Err(err) => {
                            tracing::error!("meta file for {directory_key}: {err}");
                            None
                        }
                    },
                );

                for file in directory.read_dir()? {
                    let file = file?.path();
                    if let Some((file_material, side, kk_index, table_type)) = parse_filename(&file)
                        && directory_key.material == file_material
                    {
                        self.tables.insert(
                            TableKey {
                                material: file_material,
                                pawn_file_type: directory_key.pawn_file_type,
                                bishop_parity: directory_key.bishop_parity,
                                side,
                                kk_index,
                                table_type,
                            },
                            (file, OnceCell::new()),
                        );
                        num += 1;
                    } else if is_meta_file(&file, &directory_key) {
                        self.meta.insert(
                            directory_key.clone(),
                            serde_json::from_reader(File::open(&file)?)
                                .map_err(io::Error::other)?,
                        );
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
    ) -> io::Result<Option<(TableKey, &Table, ZIndex)>> {
        let table_key = TableKey {
            material: pos.board().material(),
            pawn_file_type: PawnFileType::Free,
            bishop_parity: ByColor::new_with(|_| BishopParity::None),
            side: pos.turn(),
            kk_index: KkIndex(mb_info.kk_index as u32),
            table_type,
        };

        for bishop_parity in &mb_info.parity_index[..mb_info.num_parities as usize] {
            let with_bishop_parity = TableKey {
                bishop_parity: ByColor {
                    white: bishop_parity.bishop_parity[Side::White as usize],
                    black: bishop_parity.bishop_parity[Side::Black as usize],
                },
                ..table_key
            };
            if let Some(table) = self.open_table(&with_bishop_parity)? {
                return Ok(Some((with_bishop_parity, table, bishop_parity.index)));
            }
        }

        let index = match mb_info.pawn_file_type {
            PawnFileType::Free => ALL_ONES,
            PawnFileType::Bp11 => {
                let with_op11 = TableKey {
                    pawn_file_type: PawnFileType::Op11,
                    ..table_key
                };
                if mb_info.index_op_11 != ALL_ONES
                    && let Some(table) = self.open_table(&with_op11)?
                {
                    return Ok(Some((with_op11, table, mb_info.index_op_11)));
                }
                mb_info.index_bp_11
            }
            PawnFileType::Op11 => mb_info.index_op_11,
            PawnFileType::Op21 => mb_info.index_op_21,
            PawnFileType::Op12 => mb_info.index_op_12,
            PawnFileType::Op22 => mb_info.index_op_22,
            PawnFileType::Dp22 => {
                let with_op22 = TableKey {
                    pawn_file_type: PawnFileType::Op22,
                    ..table_key
                };
                if mb_info.index_op_22 != ALL_ONES
                    && let Some(table) = self.open_table(&with_op22)?
                {
                    return Ok(Some((with_op22, table, mb_info.index_op_22)));
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

        let with_pawn_file_type = TableKey {
            pawn_file_type: mb_info.pawn_file_type,
            ..table_key
        };
        Ok(self
            .open_table(&with_pawn_file_type)?
            .map(|table| (with_pawn_file_type, table, index)))
    }

    fn probe_side(
        &self,
        pos: &Chess,
        ctx: &mut ProbeContext,
        log: &mut ProbeLog,
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

        let Some((table_key, table, index)) = self.select_table(pos, &mb_info, TableType::Mb)?
        else {
            return Ok(None);
        };

        log.push(table_key);
        Ok(match table.read_mb(index, ctx)? {
            MbValue::Dtc(dtc) => Some(SideValue::Dtc(i32::from(dtc))),
            MbValue::Unresolved => Some(SideValue::Unresolved),
            MbValue::MaybeHighDtc => self
                .select_table(pos, &mb_info, TableType::HighDtc)?
                .map(|(table_key, table, index)| {
                    log.push(table_key);
                    table.read_high_dtc(index, ctx)
                })
                .transpose()?,
        })
    }

    pub fn probe(&self, pos: &Chess, log: &mut ProbeLog) -> Result<Option<Value>, io::Error> {
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

        match self.probe_side(&pos, &mut ctx, log)? {
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

        Ok(match self.probe_side(&pos, &mut ctx, log)? {
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

    pub fn meta_keys(&self) -> impl Iterator<Item = &DirectoryKey> {
        self.meta.keys()
    }

    pub fn meta(&self, key: &DirectoryKey) -> Option<Meta> {
        self.meta.get(key).cloned().unwrap_or_default()
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

#[derive(Debug, Clone, Eq, Hash, PartialEq)]
pub struct DirectoryKey {
    material: Material,
    pawn_file_type: PawnFileType,
    bishop_parity: ByColor<BishopParity>,
}

#[derive(Debug)]
pub struct InvalidDirectoryKey;

impl fmt::Display for InvalidDirectoryKey {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str("invalid directory key")
    }
}

impl Error for InvalidDirectoryKey {}

impl FromStr for DirectoryKey {
    type Err = InvalidDirectoryKey;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        let mut parts = s.split('_').peekable();

        let key = DirectoryKey {
            material: parts
                .next()
                .and_then(parse_material)
                .ok_or(InvalidDirectoryKey)?,
            pawn_file_type: parts
                .peek()
                .copied()
                .and_then(PawnFileType::from_filename_part)
                .inspect(|_| {
                    parts.next();
                })
                .unwrap_or(PawnFileType::Free),
            bishop_parity: ByColor {
                white: parts
                    .peek()
                    .copied()
                    .and_then(BishopParity::from_filename_part_white)
                    .inspect(|_| {
                        parts.next();
                    })
                    .unwrap_or(BishopParity::None),
                black: parts
                    .peek()
                    .copied()
                    .and_then(BishopParity::from_filename_part_black)
                    .inspect(|_| {
                        parts.next();
                    })
                    .unwrap_or(BishopParity::None),
            },
        };

        if parts.next().is_some() {
            return Err(InvalidDirectoryKey);
        }

        Ok(key)
    }
}

impl fmt::Display for DirectoryKey {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str(&material_to_string(&self.material))?;
        if let Some(part) = self.pawn_file_type.as_filename_part() {
            write!(f, "_{}", part)?;
        }
        if let Some(part) = self.bishop_parity.white.as_filename_part_white() {
            write!(f, "_{}", part)?;
        }
        if let Some(part) = self.bishop_parity.black.as_filename_part_black() {
            write!(f, "_{}", part)?;
        }
        Ok(())
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

impl fmt::Display for TableKey {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str(&material_to_string(&self.material))?;
        if let Some(part) = self.pawn_file_type.as_filename_part() {
            write!(f, "_{}", part)?;
        }
        if let Some(part) = self.bishop_parity.white.as_filename_part_white() {
            write!(f, "_{}", part)?;
        }
        if let Some(part) = self.bishop_parity.black.as_filename_part_black() {
            write!(f, "_{}", part)?;
        }
        write!(
            f,
            "_{}_{}.{}",
            self.side.char(),
            self.kk_index.0,
            self.table_type.filename_extension()
        )
    }
}

type Material = ByColor<ByRole<u8>>;

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
struct KkIndex(u32);

fn parse_dirname(path: &Path) -> Option<DirectoryKey> {
    path.file_name()?
        .to_str()?
        .strip_suffix("_out")?
        .parse()
        .ok()
}

fn read_meta_file(directory: &Path, directory_key: &DirectoryKey) -> Result<Meta, io::Error> {
    let file = File::open(directory.join(format!("{directory_key}.json")))?;
    serde_json::from_reader(file).map_err(io::Error::other)
}

fn is_meta_file(path: &Path, directory_key: &DirectoryKey) -> bool {
    path.file_name()
        .and_then(|name| name.to_str())
        .and_then(|name| name.strip_suffix(".json"))
        .is_some_and(|stem| stem == directory_key.to_string())
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

fn material_to_string(material: &Material) -> String {
    let mut s = String::new();
    for side in material.iter().rev() {
        for (role, count) in side.zip_role().into_iter().rev() {
            s.push_str(&role.char().to_string().repeat(usize::from(count)));
        }
    }
    s
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

pub struct ProbeLog {
    steps: Option<Vec<TableKey>>,
}

impl Default for ProbeLog {
    fn default() -> Self {
        ProbeLog::new()
    }
}

impl ProbeLog {
    pub fn new() -> ProbeLog {
        ProbeLog {
            steps: Some(Vec::new()),
        }
    }

    pub fn ignore() -> ProbeLog {
        ProbeLog { steps: None }
    }

    pub fn push(&mut self, table: TableKey) {
        if let Some(steps) = &mut self.steps {
            steps.push(table);
        }
    }

    pub fn into_steps(self) -> Vec<TableKey> {
        self.steps.unwrap_or_default()
    }
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
