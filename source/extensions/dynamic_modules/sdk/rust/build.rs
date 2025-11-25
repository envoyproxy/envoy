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
    .default_enum_style(bindgen::EnumVariation::Rust {
      non_exhaustive: false,
    })
    .parse_callbacks(Box::new(bindgen::CargoCallbacks::new()))
    .parse_callbacks(Box::new(TrimEnumNameFromVariantName))
    .generate()
    .expect("Unable to generate bindings");

  let out_path = PathBuf::from(env::var("OUT_DIR").unwrap());
  bindings
    .write_to_file(out_path.join("bindings.rs"))
    .expect("Couldn't write bindings");
}

#[derive(Debug)]
// This allows us to simplify the enum variant names.
// Otherwise, the generated enum result would be `EnumName::EnumName_VariantName`. E.g.
// `envoy_dynamic_module_type_on_http_filter_response_trailers_status::envoy_dynamic_module_type_on_http_filter_response_trailers_status_Continue`
// instead of `envoy_dynamic_module_type_on_http_filter_response_trailers_status::Continue`.
//
// See https://github.com/rust-lang/rust-bindgen/issues/777
struct TrimEnumNameFromVariantName;

impl bindgen::callbacks::ParseCallbacks for TrimEnumNameFromVariantName {
  fn enum_variant_name(
    &self,
    enum_name: Option<&str>,
    original_variant_name: &str,
    _variant_value: bindgen::callbacks::EnumVariantValue,
  ) -> Option<String> {
    let variant_name = match enum_name {
      Some(enum_name) => original_variant_name
        .trim_start_matches(enum_name.trim_start_matches("enum "))
        .trim_start_matches('_'),
      None => original_variant_name,
    };
    Some(variant_name.to_string())
  }
}
