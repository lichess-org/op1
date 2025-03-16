use std::{env, path::PathBuf};

fn main() {
    println!("cargo:rerun-if-changed=mbeval/include/mbeval.h");
    println!("cargo:rerun-if-changed=mbeval/src/mbeval.c");

    let out_dir = PathBuf::from(env::var_os("OUT_DIR").unwrap());

    bindgen::builder()
        .layout_tests(false)
        .header("mbeval/include/mbeval.h")
        .generate()
        .unwrap()
        .write_to_file(out_dir.join("bindings.rs"))
        .unwrap();

    cc::Build::new()
        .include("mbeval/include")
        .file("mbeval/src/mbeval.c")
        .compile("mbeval");

    println!("cargo::rustc-link-lib=static=z");
    println!("cargo::rustc-link-lib=static=zstd");
}
