use std::env;
use std::path::Path;
use std::path::PathBuf;

// Resolves a `$(location ...)` value produced by Bazel to an absolute path.
//
// `$(location ...)` yields a path relative to the execroot, but the current working directory of
// a Bazel-invoked build script isn't guaranteed to *be* the execroot. So we canonicalize against
// the current working directory, and if that doesn't exist, we walk up parent directories of the
// current working directory until the relative path resolves.
fn resolve_bazel_location(location: &str) -> Option<PathBuf> {
  let mut dir = env::current_dir().ok()?;
  loop {
    let candidate = dir.join(location);
    if candidate.exists() {
      return candidate.canonicalize().ok();
    }
    if !dir.pop() {
      return None;
    }
  }
}

// Derives the `BINDGEN_EXTRA_CLANG_ARGS`-equivalent clang arguments from the anchor environment
// variables set by the `@platforms//os:linux` branch of `build_script_env` in the BUILD file
// (see there for details). These are only set when building under Bazel on Linux; a plain
// `cargo build` (or a macOS/Bazel build) leaves them unset, in which case this returns an empty
// vector and behavior is unchanged.
fn bazel_clang_args() -> Vec<String> {
  let mut args = Vec::new();

  if let Ok(llvm_anchor) = env::var("ENVOY_LLVM_ANCHOR") {
    if let Some(anchor) = resolve_bazel_location(&llvm_anchor) {
      // The anchor is `<llvm repo root>/lib/libclang.so`, so its grandparent is the repo root.
      if let Some(root) = anchor.parent().and_then(Path::parent) {
        let llvm_major = env::var("ENVOY_LLVM_MAJOR").unwrap_or_default();
        args.push("-resource-dir".to_string());
        args.push(format!("{}/lib/clang/{}", root.display(), llvm_major));
        args.push("-isystem".to_string());
        args.push(format!(
          "{}/lib_legacy/clang/{}/include",
          root.display(),
          llvm_major
        ));
        args.push("-isystem".to_string());
        args.push(format!(
          "{}/include/x86_64-unknown-linux-gnu/c++/v1",
          root.display()
        ));
        args.push("-isystem".to_string());
        args.push(format!(
          "{}/include/aarch64-unknown-linux-gnu/c++/v1",
          root.display()
        ));
      }
    }
  }

  // This allows the cross-compilation of the SDK to succeed by providing the necessary system
  // headers for the target platform.
  for (env_var, triple) in [
    ("ENVOY_SYSROOT_AMD64_ANCHOR", "x86_64-linux-gnu"),
    ("ENVOY_SYSROOT_ARM64_ANCHOR", "aarch64-linux-gnu"),
  ] {
    if let Ok(sysroot_anchor) = env::var(env_var) {
      if let Some(anchor) = resolve_bazel_location(&sysroot_anchor) {
        // The anchor is `<sysroot>/usr/include/stdio.h`, so its parent is `usr/include`.
        if let Some(include_dir) = anchor.parent() {
          args.push(format!("-isystem {}", include_dir.display()));
          args.push(format!("-isystem {}/{}", include_dir.display(), triple));
        }
      }
    }
  }

  args
}

fn main() {
  // This is Envoy CI specific: Check if "/opt/llvm/bin/clang" exists, and if it does, set the
  // CLANG_PATH environment variable. CLANG_PATH is for clang-sys used by bindgen:
  // https://github.com/KyleMayes/clang-sys?tab=readme-ov-file#environment-variables
  //
  // "/opt/llvm/bin/clang" exists in Envoy CI containers. If the clang doesn't exist there, bindgen
  // will try to use the system clang from PATH. So, this doesn't affect the local builds.
  // In any case, clang must be found to build the bindings.

  let abi_header = PathBuf::from(env::var("CARGO_MANIFEST_DIR").unwrap()).join("abi/abi.h");

  println!("cargo:rerun-if-changed={}", abi_header.display());

  let out_path = PathBuf::from(env::var("OUT_DIR").unwrap());

  let bindings = bindgen::Builder::default()
    .header(abi_header.to_str().unwrap())
    .clang_args(bazel_clang_args())
    .clang_arg("-v")
    .default_enum_style(bindgen::EnumVariation::Rust {
      non_exhaustive: false,
    })
    .derive_partialeq(true)
    .parse_callbacks(Box::new(bindgen::CargoCallbacks::new()))
    .parse_callbacks(Box::new(TrimEnumNameFromVariantName))
    .generate()
    .expect("Unable to generate bindings");

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
