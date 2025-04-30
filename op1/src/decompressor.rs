use std::{
    ffi::{CStr, c_void},
    io,
};

use zerocopy::IntoBytes;
use zstd_sys::{
    ZSTD_DStream, ZSTD_createDStream, ZSTD_decompressStream, ZSTD_freeDStream, ZSTD_getErrorName,
    ZSTD_inBuffer_s, ZSTD_initDStream, ZSTD_isError, ZSTD_outBuffer_s,
};

pub struct Decompressor {
    ctx: *mut ZSTD_DStream,
}

impl Decompressor {
    pub fn new() -> Decompressor {
        let ctx = unsafe { ZSTD_createDStream() };
        assert!(!ctx.is_null());
        Decompressor { ctx }
    }

    pub fn decompress_prefix<T>(
        &mut self,
        compressed: &[u8],
        decompressed: &mut Vec<T>,
        items: usize,
    ) -> io::Result<()>
    where
        T: IntoBytes,
    {
        let mut in_buffer = ZSTD_inBuffer_s {
            src: compressed.as_ptr().cast::<c_void>(),
            size: compressed.len(),
            pos: 0,
        };

        decompressed.clear();
        decompressed.reserve(items);
        let mut out_buffer = ZSTD_outBuffer_s {
            dst: decompressed.as_mut_ptr().cast::<c_void>(),
            size: items * std::mem::size_of::<T>(),
            pos: 0,
        };

        unsafe {
            ZSTD_initDStream(self.ctx);
        }

        while in_buffer.pos < in_buffer.size && out_buffer.pos < out_buffer.size {
            let result =
                unsafe { ZSTD_decompressStream(self.ctx, &mut out_buffer, &mut in_buffer) };
            if unsafe { ZSTD_isError(result) } != 0 {
                return Err(io::Error::new(io::ErrorKind::InvalidData, unsafe {
                    CStr::from_ptr(ZSTD_getErrorName(result))
                        .to_str()
                        .expect("zstd error")
                }));
            }
        }

        unsafe {
            decompressed.set_len(out_buffer.pos / std::mem::size_of::<T>());
        }

        Ok(())
    }
}

impl Drop for Decompressor {
    fn drop(&mut self) {
        unsafe { ZSTD_freeDStream(self.ctx) };
    }
}
