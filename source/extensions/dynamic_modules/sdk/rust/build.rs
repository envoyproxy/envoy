fn main() {
    // TODO: this is just to ensure that bindgen dependency is available in build.rs.
    let _ = bindgen::Builder::default()
        .header("abi.h")
        .parse_callbacks(Box::new(bindgen::CargoCallbacks))
        .generate();
}
