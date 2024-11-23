use std::env;
use std::path::PathBuf;

fn main() {
  let bindings = bindgen::Builder::default()
    .header("../../abi.h")
    .header("../../abi_version.h")
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
        .trim_start_matches(enum_name)
        .trim_start_matches('_'),
      None => original_variant_name,
    };
    Some(variant_name.to_string())
  }
}
