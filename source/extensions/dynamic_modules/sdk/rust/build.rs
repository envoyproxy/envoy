use std::env;
use std::path::PathBuf;

fn main() {
  // Print all environment variables for debugging!
  for (key, value) in env::vars() {
    eprintln!("{}: {}", key, value);
  }

  // Check if it has CC environment variable, and if that matches clang,
  // then set the CLANG_PATH to the value of CC to instruct bindgen to use
  // clang as the compiler.
  // https://github.com/KyleMayes/clang-sys?tab=readme-ov-file#environment-variables
  if let Some(cc) = env::var("CC").ok() {
    if cc.contains("clang") {
      env::set_var("CLANG_PATH", cc);
    }
  }

  println!("cargo:rerun-if-changed=abi.h");
  let bindings = bindgen::Builder::default()
    .header("../../abi.h")
    .header("../../abi_version.h")
    .clang_arg("-v")
    .parse_callbacks(Box::new(bindgen::CargoCallbacks::new()))
    .generate()
    .expect("Unable to generate bindings");

  let out_path = PathBuf::from(env::var("OUT_DIR").unwrap());
  bindings
    .write_to_file(out_path.join("bindings.rs"))
    .expect("Couldn't write bindings");
}
