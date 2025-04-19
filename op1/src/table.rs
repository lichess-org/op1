use std::{fs::File, io, io::Read, os::unix::fs::FileExt, path::Path};

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

        let header = Header::read_from_io(&mut file)?;

        if header.block_size == 0 {
            return Err(io::Error::new(
                io::ErrorKind::InvalidData,
                "block size cannot be zero",
            ));
        }

        let mut offsets = vec![0; 8 * (header.num_blocks.get() as usize + 1)];
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
        let block_index = u32::try_from(index / u64::from(self.header.block_size))
            .map_err(|_| io::Error::new(io::ErrorKind::InvalidInput, "index out of range"))?;
        let byte_index = index % u64::from(self.header.block_size);

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
            m if m as u32 == mbeval_sys::NO_COMPRESSION => &ctx.compressed_block,
            m if m as u32 == mbeval_sys::ZSTD => {
                let required_capacity = zstd::bulk::Decompressor::upper_bound(
                    &ctx.compressed_block,
                )
                .ok_or_else(|| {
                    io::Error::new(
                        io::ErrorKind::InvalidData,
                        "failed to determine upper bound for decompressed block size",
                    )
                })?;
                if let Some(additional_capacity) =
                    required_capacity.checked_sub(ctx.decompressed_block.len())
                {
                    ctx.decompressed_block.reserve_exact(additional_capacity);
                }

                ctx.decompressor
                    .decompress_to_buffer(&ctx.compressed_block, &mut ctx.decompressed_block)?;
                &ctx.decompressed_block
            }
            m => {
                return Err(io::Error::new(
                    io::ErrorKind::InvalidData,
                    format!("unexpected format: {m}"),
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
struct Header {
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
