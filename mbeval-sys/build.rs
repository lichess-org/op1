use std::env;
use std::path::PathBuf;

fn main() {
    let out_dir = PathBuf::from(env::var_os("OUT_DIR").unwrap());

    bindgen::builder()
        .layout_tests(false)
        .header("mbeval/include/mbeval.h")
        .generate()
        .unwrap()
        .write_to_file(out_dir.join("bindings.rs"))
        .unwrap();

    cc::Build::new()
        .file("mbeval/src/mbeval.c")
        .compile("mbeval");

    println!("cargo:root={}", out_dir.display());
}
