use std::{fs::File, io, io::Read, num::NonZeroU32, os::unix::fs::FileExt, path::Path};

use zerocopy::{
    FromBytes,
    little_endian::{U32, U64},
};

pub(crate) struct Table {
    file: File,
    header: Header,
    offsets: Vec<u8>,
}

impl Table {
    pub(crate) fn open(path: &Path) -> io::Result<Table> {
        let mut file = File::open(path)?;

        let header = Header::try_from(RawHeader::read_from_io(&mut file)?)?;

        let mut offsets = vec![0; 8 * (header.num_blocks as usize + 1)];
        file.read_exact(&mut offsets[..])?;

        Ok(Table {
            file,
            header,
            offsets,
        })
    }

    fn block_offset(&self, block_index: u32) -> io::Result<u64> {
        let block_index = block_index as usize;
        let encoded = self
            .offsets
            .get(block_index * 8..block_index * 8 + 8)
            .ok_or_else(|| io::Error::new(io::ErrorKind::InvalidInput, "block index out of range"))?
            .try_into()
            .expect("8 bytes");
        Ok(u64::from_le_bytes(encoded))
    }

    pub(crate) fn read_mb(&self, index: u64, ctx: &mut ProbeContext) -> io::Result<MbValue> {
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
            254 => MbValue::MaybeHighDtc,
            255 => MbValue::Unresolved,
            dtc => MbValue::Dtc(dtc),
        })
    }
}

#[derive(FromBytes)]
#[repr(C)]
struct RawHeader {
    unused: [u8; 16],
    basename: [u8; 16],
    n_elements: U64,
    kk_index: U32,
    max_depth: U32,
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
    compression_method: CompressionMethod,
    block_size: NonZeroU32,
    num_blocks: u32,
}

impl TryFrom<RawHeader> for Header {
    type Error = io::Error;

    fn try_from(raw: RawHeader) -> Result<Self, Self::Error> {
        Ok(Header {
            block_size: NonZeroU32::try_from(u32::from(raw.block_size))
                .map_err(|err| io::Error::new(io::ErrorKind::InvalidData, err))?,
            num_blocks: u32::from(raw.num_blocks),
            compression_method: CompressionMethod::try_from(raw.compression_method)?,
        })
    }
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
    MaybeHighDtc,
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
