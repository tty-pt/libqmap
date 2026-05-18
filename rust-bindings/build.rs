fn main() {
    // Link against libqmap — the C library must be installed separately.
    // Use QMAP_DIR to point to a custom installation (e.g. QMAP_DIR=/usr/local).
    if let Ok(dir) = std::env::var("QMAP_DIR") {
        println!("cargo:rustc-link-search={}/lib", dir);
    }
    println!("cargo:rustc-link-lib=qmap");

    // Rebuild when this script or QMAP_DIR changes.
    println!("cargo:rerun-if-changed=build.rs");
    println!("cargo:rerun-if-env-changed=QMAP_DIR");
}
