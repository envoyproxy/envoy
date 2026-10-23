dynamic modules: ``envoy_dynamic_module_callback_http_set_header`` and
``envoy_dynamic_module_callback_http_add_header`` now accept an empty header value. Setting a header
to a non-null zero-length value sets it to an empty value instead of removing it, and adding one adds
an empty value instead of failing. Only a null value pointer removes the header, as documented. The Go
and C++ SDKs now pass a non-null pointer for empty values. Modules that set a header to an empty value
to remove it must use the SDK's remove method instead.
