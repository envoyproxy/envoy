Added HTTP filter callbacks that read nested metadata through a path of literal struct keys and
list indices. ``envoy_dynamic_module_callback_http_get_metadata_nested_string``,
``envoy_dynamic_module_callback_http_get_metadata_nested_number``,
``envoy_dynamic_module_callback_http_get_metadata_nested_bool`` and
``envoy_dynamic_module_callback_http_get_metadata_nested_list_size`` return a string, a number,
a bool or a list size, and a dot in a key is part of the key. The Rust SDK exposes them as
``get_metadata_nested_string``, ``get_metadata_nested_number``, ``get_metadata_nested_bool`` and
``get_metadata_nested_list_size`` on ``EnvoyHttpFilter``, with paths built from
``MetadataPathComponent``.
