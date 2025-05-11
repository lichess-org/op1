use std::{fs::File, io, io::Read, mem, num::NonZeroU32, os::unix::fs::FileExt, path::Path};

use mbeval_sys::ZIndex;
use zerocopy::{
    FromBytes, FromZeros, Immutable, IntoBytes,
    little_endian::{I32, U32, U64},
};

use crate::decompressor::Decompressor;

pub(crate) struct Table {
    table_type: TableType,
    file: File,
    header: Header,
    offsets: Box<[U64]>,
    starting_indices: Box<[U64]>,
}

impl Table {
    pub(crate) fn open(path: &Path, table_type: TableType) -> io::Result<Table> {
        let mut file = File::open(path)?;

        let header = Header::try_from(RawHeader::read_from_io(&mut file)?)?;

        if header.list_element_size != table_type.list_element_size() {
            return Err(io::Error::new(
                io::ErrorKind::InvalidData,
                format!(
                    "unpexected list element size {} for {}",
                    header.list_element_size,
                    path.display(),
                ),
            ));
        }

        if u32::from(header.block_size) % u32::from(table_type.list_element_size()) != 0 {
            return Err(io::Error::new(
                io::ErrorKind::InvalidData,
                format!(
                    "block size {} not cleanly divisible by list element size",
                    header.block_size
                ),
            ));
        }

        let mut offsets = <[U64]>::new_box_zeroed_with_elems(header.num_blocks as usize + 1)
            .expect("allocate offsets vector");
        file.read_exact(offsets.as_mut_bytes())?;

        let starting_indices = match table_type {
            TableType::Mb => Box::default(),
            TableType::HighDtc => {
                let mut starting_indices =
                    <[U64]>::new_box_zeroed_with_elems(header.num_blocks as usize + 1)
                        .expect("allocate starting indices vector");
                file.read_exact(starting_indices.as_mut_bytes())?;
                starting_indices
            }
        };

        Ok(Table {
            table_type,
            file,
            header,
            offsets,
            starting_indices,
        })
    }

    fn block_offset(&self, block_index: u32) -> io::Result<u64> {
        self.offsets
            .get(block_index as usize)
            .copied()
            .map(u64::from)
            .ok_or_else(|| io::Error::new(io::ErrorKind::InvalidInput, "block index out of range"))
    }

    fn load_compressed_block(&self, block_index: u32, ctx: &mut ProbeContext) -> io::Result<()> {
        let compressed_block_start = self.block_offset(block_index)?;
        let compressed_block_end =
            self.block_offset(block_index.checked_add(1).ok_or_else(|| {
                io::Error::new(io::ErrorKind::InvalidInput, "index out of range")
            })?)?;
        let compressed_block_size = compressed_block_end
            .checked_sub(compressed_block_start)
            .ok_or_else(|| {
                io::Error::new(io::ErrorKind::InvalidData, "block offsets not monotonic")
            })?;

        ctx.compressed_block
            .resize(compressed_block_size as usize, 0);
        self.file
            .read_exact_at(&mut ctx.compressed_block[..], compressed_block_start)
    }

    pub(crate) fn read_mb(&self, index: ZIndex, ctx: &mut ProbeContext) -> io::Result<MbValue> {
        assert_eq!(self.table_type, TableType::Mb);

        let block_index = u32::try_from(index / u64::from(self.header.block_size.get()))
            .map_err(|_| io::Error::new(io::ErrorKind::InvalidInput, "index out of range"))?;
        let byte_index = index % u64::from(self.header.block_size.get());

        self.load_compressed_block(block_index, ctx)?;

        let block = match self.header.compression_method {
            CompressionMethod::None => &ctx.compressed_block,
            CompressionMethod::Zstd => {
                ctx.decompressor.decompress_prefix(
                    &ctx.compressed_block,
                    &mut ctx.decompressed_block,
                    byte_index as usize + 1,
                )?;
                &ctx.decompressed_block
            }
        };

        let value = block.get(byte_index as usize).copied().ok_or_else(|| {
            io::Error::new(
                io::ErrorKind::InvalidData,
                format!("index {byte_index} not found in decompressed block"),
            )
        })?;

        Ok(match value {
            254 if self.header.max_dtc > 254 => MbValue::MaybeHighDtc,
            255 => MbValue::Unresolved,
            dtc => MbValue::Dtc(dtc),
        })
    }

    pub(crate) fn read_high_dtc(
        &self,
        index: ZIndex,
        ctx: &mut ProbeContext,
    ) -> io::Result<SideValue> {
        assert_eq!(self.table_type, TableType::HighDtc);

        let block_index = match self.starting_indices.binary_search(&U64::new(index)) {
            Ok(block_index) => block_index,
            Err(0) => return Ok(SideValue::Dtc(254)),
            Err(block_index) => block_index - 1,
        } as u32;

        self.load_compressed_block(block_index, ctx)?;

        let num_per_block = self.header.block_size.get() as usize / mem::size_of::<HighDtc>();

        let mut decompressed_block = match self.header.compression_method {
            CompressionMethod::None => {
                let mut decompressed_block = HighDtc::new_vec_zeroed(num_per_block)
                    .expect("allocate memory for decompressed block");
                decompressed_block
                    .as_mut_bytes()
                    .copy_from_slice(&ctx.compressed_block);
                decompressed_block
            }
            CompressionMethod::Zstd => {
                let mut decompressed_block = Vec::<HighDtc>::new();
                ctx.decompressor.decompress_prefix(
                    &ctx.compressed_block,
                    &mut decompressed_block,
                    num_per_block,
                )?;
                decompressed_block
            }
        };

        if block_index == self.header.num_blocks - 1 {
            let last_block_entries = self.header.num_elements % num_per_block as u64;
            if last_block_entries != 0 {
                decompressed_block.truncate(last_block_entries as usize);
            }
        }

        Ok(SideValue::Dtc(
            if let Ok(ptr) =
                decompressed_block.binary_search_by_key(&U64::new(index), |entry| entry.index)
            {
                i32::from(decompressed_block[ptr].value)
            } else {
                254
            },
        ))
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub(crate) enum TableType {
    Mb,
    HighDtc,
}

impl TableType {
    fn list_element_size(self) -> u8 {
        match self {
            TableType::Mb => mem::size_of::<u8>() as u8,
            TableType::HighDtc => mem::size_of::<HighDtc>() as u8,
        }
    }
}

#[derive(FromBytes, Debug)]
#[repr(C)]
struct RawHeader {
    unused: [u8; 16],
    basename: [u8; 16],
    num_elements: U64,
    kk_index: U32,
    max_dtc: U32, // aka max_depth
    block_size: U32,
    num_blocks: U32,
    nrows: u8,
    ncols: u8,
    side: u8,
    metric: u8,
    compression_method: u8,
    index_size: u8,
    format_type: u8,
    list_element_size: u8,
}

struct Header {
    num_elements: u64,
    block_size: NonZeroU32,
    num_blocks: u32,
    max_dtc: u32,
    compression_method: CompressionMethod,
    list_element_size: u8,
}

impl TryFrom<RawHeader> for Header {
    type Error = io::Error;

    fn try_from(raw: RawHeader) -> Result<Self, Self::Error> {
        Ok(Header {
            num_elements: raw.num_elements.into(),
            block_size: NonZeroU32::new(raw.block_size.into())
                .ok_or_else(|| io::Error::new(io::ErrorKind::InvalidData, "zero block size"))?,
            num_blocks: raw.num_blocks.into(),
            max_dtc: raw.max_dtc.into(),
            compression_method: CompressionMethod::try_from(raw.compression_method)?,
            list_element_size: raw.list_element_size,
        })
    }
}

#[repr(C)]
#[derive(FromBytes, IntoBytes, Immutable)]
struct HighDtc {
    index: U64,
    value: I32,
    _padding: [u8; 4],
}

const _: () = const {
    assert!(mem::size_of::<HighDtc>() == 16);
};

enum CompressionMethod {
    None,
    Zstd,
}

impl TryFrom<u8> for CompressionMethod {
    type Error = io::Error;

    fn try_from(value: u8) -> Result<Self, Self::Error> {
        Ok(match value {
            0 => CompressionMethod::None,
            1 => {
                return Err(io::Error::new(
                    io::ErrorKind::InvalidData,
                    "zlib compression not supported",
                ));
            }
            2 => CompressionMethod::Zstd,
            _ => {
                return Err(io::Error::new(
                    io::ErrorKind::InvalidData,
                    format!("unknown compression method: {}", value),
                ));
            }
        })
    }
}

#[derive(Debug)]
pub(crate) enum MbValue {
    Dtc(u8),
    Unresolved,
    MaybeHighDtc,
}

#[derive(Debug)]
pub(crate) enum SideValue {
    Dtc(i32),
    Unresolved,
}

pub struct ProbeContext {
    compressed_block: Vec<u8>,
    decompressed_block: Vec<u8>,
    decompressor: Decompressor,
}

impl ProbeContext {
    pub fn new() -> io::Result<ProbeContext> {
        Ok(ProbeContext {
            compressed_block: Vec::new(),
            decompressed_block: Vec::new(),
            decompressor: Decompressor::new(),
        })
    }
}
