use std::{fs::File, io, io::Read, mem, num::NonZeroU32, os::unix::fs::FileExt, path::Path};

use mbeval_sys::ZIndex;
use zerocopy::{
    FromBytes, IntoBytes,
    little_endian::{I32, U32, U64},
};

pub(crate) struct Table {
    table_type: TableType,
    file: File,
    header: Header,
    offsets: Vec<U64>,
    starting_indices: Vec<U64>,
}

impl Table {
    pub(crate) fn open(path: &Path, table_type: TableType) -> io::Result<Table> {
        let mut file = File::open(path)?;

        let header = Header::try_from(dbg!(RawHeader::read_from_io(&mut file)?))?;

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

        let mut offsets = vec![U64::ZERO; header.num_blocks as usize + 1];
        file.read_exact(offsets.as_mut_bytes())?;

        let starting_indices = match table_type {
            TableType::Mb => Vec::new(),
            TableType::HighDtc => {
                let mut starting_indices = vec![U64::ZERO; header.num_blocks as usize + 1];
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

    pub(crate) fn read_mb(&self, index: ZIndex, ctx: &mut ProbeContext) -> io::Result<MbValue> {
        assert_eq!(self.table_type, TableType::Mb);

        let block_index = u32::try_from(index / u64::from(self.header.block_size.get()))
            .map_err(|_| io::Error::new(io::ErrorKind::InvalidInput, "index out of range"))?;
        let byte_index = index % u64::from(self.header.block_size.get());

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
            .read_exact_at(&mut ctx.compressed_block[..], compressed_block_start)?;

        let block = match self.header.compression_method {
            CompressionMethod::None => &ctx.compressed_block,
            CompressionMethod::Zstd => {
                if let Some(additional_capacity) = (self.header.block_size.get() as usize)
                    .checked_sub(ctx.decompressed_block.capacity())
                {
                    ctx.decompressed_block.reserve(additional_capacity);
                }
                ctx.decompressor
                    .decompress_to_buffer(&ctx.compressed_block, &mut ctx.decompressed_block)?;
                &ctx.decompressed_block
            }
            CompressionMethod::Zlib => {
                return Err(io::Error::new(
                    io::ErrorKind::InvalidData,
                    "zlib compression not supported",
                ));
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
        _index: ZIndex,
        _ctx: &mut ProbeContext,
    ) -> io::Result<SideValue> {
        assert_eq!(self.table_type, TableType::HighDtc);

        todo!()
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
    n_elements: U64,
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
            block_size: NonZeroU32::try_from(u32::from(raw.block_size))
                .map_err(|err| io::Error::new(io::ErrorKind::InvalidData, err))?,
            num_blocks: u32::from(raw.num_blocks),
            max_dtc: u32::from(raw.max_dtc),
            compression_method: CompressionMethod::try_from(raw.compression_method)?,
            list_element_size: raw.list_element_size,
        })
    }
}

#[derive(FromBytes)]
struct HighDtc {
    index: U64,
    score: I32,
}

enum CompressionMethod {
    None,
    Zlib,
    Zstd,
}

impl TryFrom<u8> for CompressionMethod {
    type Error = io::Error;

    fn try_from(value: u8) -> Result<Self, Self::Error> {
        Ok(match value {
            0 => CompressionMethod::None,
            1 => CompressionMethod::Zlib,
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
    Dtc(u8),
    Unresolved,
}

pub struct ProbeContext {
    compressed_block: Vec<u8>,
    decompressed_block: Vec<u8>,
    decompressor: zstd::bulk::Decompressor<'static>,
}

impl ProbeContext {
    pub fn new() -> io::Result<ProbeContext> {
        Ok(ProbeContext {
            compressed_block: Vec::new(),
            decompressed_block: Vec::new(),
            decompressor: zstd::bulk::Decompressor::new()?,
        })
    }
}
