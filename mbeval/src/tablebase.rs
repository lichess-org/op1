use std::{
    collections::HashMap,
    ffi::CString,
    io,
    os::unix::ffi::OsStrExt,
    path::{Path, PathBuf},
    sync::Once,
};

use mbeval_sys::{mbeval_add_path, mbeval_init};
use shakmaty::{ByColor, ByRole, Color, Role};

static INIT_MBEVAL: Once = Once::new();

pub struct Tablebase {
    tables: HashMap<
        (
            Material,
            PawnFileType,
            ByColor<BishopParity>,
            Color,
            KkIndex,
            TableType,
        ),
        PathBuf,
    >,
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
            tables: HashMap::new(),
        }
    }

    pub fn add_path(&mut self, path: &Path) -> io::Result<usize> {
        unsafe {
            mbeval_add_path(
                CString::new(path.to_owned().into_os_string().as_bytes())
                    .unwrap()
                    .as_c_str()
                    .as_ptr(),
            );
        }

        let mut num = 0;
        for directory in path.read_dir()? {
            let directory = directory?.path();
            if let Some((dir_material, pawn_file_type, bishop_parity)) = parse_dirname(&directory) {
                for file in directory.read_dir()? {
                    let file = file?.path();
                    if let Some((file_material, side, kk_index, table_type)) = parse_filename(&file)
                    {
                        if dir_material == file_material {
                            self.tables.insert(
                                (
                                    file_material,
                                    pawn_file_type,
                                    bishop_parity,
                                    side,
                                    kk_index,
                                    table_type,
                                ),
                                file,
                            );
                            num += 1;
                        }
                    }
                }
            }
        }
        Ok(num)
    }
}

type Material = ByColor<ByRole<u8>>;

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
enum BishopParity {
    None,
    Even,
    Odd,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
enum PawnFileType {
    Bp1,
    Op1,
    Op21,
    Op12,
    Dp2,
    Op22,
    Op31,
    Op13,
    Op41,
    Op14,
    Op32,
    Op23,
    Op33,
    Op42,
    Op24,
    Free,
}

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
                (name, PawnFileType::Bp1)
            } else if let Some(name) = name.strip_suffix("_op1") {
                (name, PawnFileType::Op1)
            } else if let Some(name) = name.strip_suffix("_op21") {
                (name, PawnFileType::Op21)
            } else if let Some(name) = name.strip_suffix("_op12") {
                (name, PawnFileType::Op12)
            } else if let Some(name) = name.strip_suffix("_dp2") {
                (name, PawnFileType::Dp2)
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
    let mut color = Color::White;
    for c in name.chars() {
        let role = Role::from_char(c)?;
        material[color][role] += 1;
        if role == Role::King {
            color = Color::Black;
        }
    }
    Some(material)
}
