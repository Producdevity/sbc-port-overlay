// SPDX-License-Identifier: MIT
fn main() {
    println!("cargo:rerun-if-env-changed=SPO_LIB_DIR");
    println!("cargo:rerun-if-env-changed=SPO_SHARED");
    let path = std::env::var("SPO_LIB_DIR").expect("set SPO_LIB_DIR to the CMake build/install library directory");
    let path = std::path::Path::new(&path).canonicalize().expect("SPO_LIB_DIR must exist");
    println!("cargo:rerun-if-changed={}", path.display());
    if std::env::var_os("SPO_SHARED").is_some() {
        println!("cargo:rustc-link-search=native={}", path.display());
        println!("cargo:rustc-link-lib=dylib=spo");
        println!("cargo:rustc-link-arg=-Wl,-rpath,{}", path.display());
    } else {
        println!("cargo:rustc-link-arg={}/libspo.a", path.display());
    }
    println!("cargo:rustc-link-lib=m");
    println!("cargo:rustc-link-lib=pthread");
}
