use std::{
    ffi::c_int,
    io,
    mem::MaybeUninit,
    path::{Path, PathBuf},
    sync::Once,
};

use mbeval_sys::{BishopParity, MB_INFO, PawnFileType, mbeval_get_mb_info, mbeval_init};
use once_cell::sync::OnceCell;
use rustc_hash::FxHashMap;
use shakmaty::{
    Board, ByColor, ByRole, CastlingMode, Chess, Color, EnPassantMode, Position as _, Role,
};

use crate::table::{MbValue, ProbeContext, Table};

const ALL_ONES: u64 = !0;

static INIT_MBEVAL: Once = Once::new();

pub struct Tablebase {
    tables: FxHashMap<TableKey, (PathBuf, OnceCell<Table>)>,
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
            tables: FxHashMap::default(),
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
        Ok(num)
    }

    fn open_table(&self, key: &TableKey) -> io::Result<Option<&Table>> {
        self.tables
            .get(key)
            .map(|(path, table)| table.get_or_try_init(|| Table::open(path)))
            .transpose()
    }

    fn select_table(
        &self,
        pos: &Chess,
        mb_info: &MB_INFO,
        table_type: TableType,
    ) -> io::Result<Option<(&Table, u64)>> {
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
                    white: bishop_parity.bishop_parity[mbeval_sys::SIDE_WHITE as usize],
                    black: bishop_parity.bishop_parity[mbeval_sys::SIDE_BLACK as usize],
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
        let mut squares = [0; 64];
        for (sq, piece) in pos.board() {
            let role = match piece.role {
                Role::Pawn => mbeval_sys::PIECE_PAWN,
                Role::Knight => mbeval_sys::PIECE_KNIGHT,
                Role::Bishop => mbeval_sys::PIECE_BISHOP,
                Role::Rook => mbeval_sys::PIECE_ROOK,
                Role::Queen => mbeval_sys::PIECE_QUEEN,
                Role::King => mbeval_sys::PIECE_KING,
            };
            squares[usize::from(sq)] = piece.color.fold_wb(role, -role);
        }
        let mut mb_info: MaybeUninit<MB_INFO> = MaybeUninit::zeroed();
        let result = unsafe {
            mbeval_get_mb_info(
                squares.as_ptr(),
                pos.turn()
                    .fold_wb(mbeval_sys::SIDE_WHITE, mbeval_sys::SIDE_BLACK),
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

        Ok(Some(match table.read_mb(index, ctx)? {
            MbValue::Dtc(dtc) => SideValue::Dtc(dtc),
            MbValue::MaybeHighDtc => return Ok(None), // TODO
            MbValue::Unresolved => SideValue::Unresolved,
        }))
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
            None => return Ok(None),
            Some(SideValue::Dtc(n)) => {
                return Ok(Some(Value::Dtc(i32::from(n) * pos.turn().fold_wb(1, -1))));
            }
            Some(SideValue::Unresolved) => (),
        }

        let pos = flip_position(pos);

        Ok(match self.probe_side(&pos, &mut ctx)? {
            None => None,
            Some(SideValue::Dtc(n)) => Some(Value::Dtc(i32::from(n) * pos.turn().fold_wb(1, -1))),
            Some(SideValue::Unresolved) => Some(Value::Draw),
        })
    }
}

#[derive(Debug)]
enum SideValue {
    Dtc(u8),
    Unresolved,
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

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
enum TableType {
    Mb,
    HighDtc,
}

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
    let mut color = Color::Black;
    for c in name.chars() {
        let role = Role::from_char(c)?;
        if role == Role::King {
            color = !color;
        }
        material[color][role] += 1;
    }
    Some(material)
}

fn strength(board: &Board, color: Color) -> usize {
    (board.by_color(color) & board.pawns()).count()
        + (board.by_color(color) & board.knights()).count() * 3
        + (board.by_color(color) & board.bishops()).count() * 3
        + (board.by_color(color) & board.rooks()).count() * 5
        + (board.by_color(color) & board.queens()).count() * 9
}

#[must_use]
fn flip_position(pos: Chess) -> Chess {
    pos.into_setup(EnPassantMode::Legal)
        .into_mirrored()
        .position(CastlingMode::Chess960)
        .expect("equivalent position")
}
