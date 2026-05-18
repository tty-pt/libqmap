use std::env;
use std::path::PathBuf;

fn main() {
    let src_path = PathBuf::from(env::var("CARGO_MANIFEST_DIR").unwrap())
        .join("src")
        .join("bindings.rs");
    println!("cargo:rerun-if-changed={}", src_path.display());
}