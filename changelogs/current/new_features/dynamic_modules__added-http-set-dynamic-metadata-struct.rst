Added ``envoy_dynamic_module_callback_http_set_dynamic_metadata_struct`` ABI callback that
sets an entire dynamic metadata namespace from a serialized ``google.protobuf.Struct`` in one
call, letting a module publish nested/structured metadata instead of only flat scalar keys. The
Rust SDK exposes this as ``EnvoyHttpFilter::set_dynamic_metadata_struct``.
