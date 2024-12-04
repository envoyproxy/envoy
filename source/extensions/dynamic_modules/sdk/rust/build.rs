use std::env;
use std::path::PathBuf;

fn main() {
  // This is Envoy CI specific: Check if "/opt/llvm/bin/clang" exists, and if it does, set the
  // CLANG_PATH environment variable. CLANG_PATH is for clang-sys used by bindgen:
  // https://github.com/KyleMayes/clang-sys?tab=readme-ov-file#environment-variables
  //
  // "/opt/llvm/bin/clang" exists in Envoy CI containers. If the clang doesn't exist there, bindgen
  // will try to use the system clang from PATH. So, this doesn't affect the local builds.
  // In any case, clang must be found to build the bindings.
  //
  // TODO: add /opt/llvm/bin to PATH in the CI containers. That would be a better solution.
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
