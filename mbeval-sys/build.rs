use std::{env, path::PathBuf};

fn main() {
    println!("cargo:rerun-if-changed=mbeval/include/mbeval.h");
    println!("cargo:rerun-if-changed=mbeval/src/mbeval.c");

    let out_dir = PathBuf::from(env::var_os("OUT_DIR").unwrap());

    bindgen::builder()
        .layout_tests(false)
        .header("mbeval/include/mbeval.h")
        .allowlist_function("mbeval_init")
        .allowlist_function("mbeval_get_mb_info")
        .rustified_enum("PawnFileType")
        .rustified_enum("BishopParity")
        .rustified_enum("Side")
        .newtype_enum("Piece")
        .generate()
        .unwrap()
        .write_to_file(out_dir.join("bindings.rs"))
        .unwrap();

    cc::Build::new()
        .include("mbeval/include")
        .file("mbeval/src/mbeval.c")
        .compile("mbeval");
}
