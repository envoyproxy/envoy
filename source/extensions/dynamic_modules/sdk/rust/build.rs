use std::env;
use std::path::PathBuf;

fn main() {
  // Print all environment variables for debugging!
  for (key, value) in env::vars() {
    eprintln!("{}: {}", key, value);
  }

  // Check if '/opt/llvm/bin/clang' exists, and if it does, set the CLANG_PATH environment variable.
  // This matches how our CI containers are set up. If the clang doesn't exist there, bindgen will
  // try to use the system clang. In any case, clang must be found to build the bindings.
  if std::fs::metadata("/opt/llvm/bin/clang").is_ok() {
    env::set_var("CLANG_PATH", "/opt/llvm/bin/clang");
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
