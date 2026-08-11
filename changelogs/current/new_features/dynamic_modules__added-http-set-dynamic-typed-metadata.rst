Added ``envoy_dynamic_module_callback_http_set_dynamic_typed_metadata`` ABI callback that sets a
typed dynamic metadata namespace from a serialized ``google.protobuf.Any``. Unlike
``envoy_dynamic_module_callback_http_set_dynamic_metadata_struct`` it preserves the exact message
type (via the Any ``type_url``) in ``typed_filter_metadata``, so consumers such as ext_authz
(``typed_metadata_context_namespaces``) receive the original message rather than a lossy Struct. The
Rust SDK exposes this as ``EnvoyHttpFilter::set_dynamic_typed_metadata``, the C++ SDK as
``HttpFilterHandle::setTypedMetadata`` and the Go SDK as ``HttpFilterHandle.SetTypedMetadata``.
