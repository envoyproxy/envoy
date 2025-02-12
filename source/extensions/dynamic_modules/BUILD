load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

# Export callback symbols so that they can be found by the dynamic modules when loaded via dlopen.
#
# Note: with lld, we can use the glob matching to avoid listing all the symbols, but gold and ld are
# not able to do that. So we list all the symbols here.
SYMBOLS = [
    "envoy_dynamic_module_callback_http_get_request_header",
    "envoy_dynamic_module_callback_http_get_request_headers",
    "envoy_dynamic_module_callback_http_get_request_headers_count",
    "envoy_dynamic_module_callback_http_set_request_header",
    "envoy_dynamic_module_callback_http_get_request_trailer",
    "envoy_dynamic_module_callback_http_get_request_trailers",
    "envoy_dynamic_module_callback_http_get_request_trailers_count",
    "envoy_dynamic_module_callback_http_set_request_trailer",
    "envoy_dynamic_module_callback_http_get_response_header",
    "envoy_dynamic_module_callback_http_get_response_headers",
    "envoy_dynamic_module_callback_http_get_response_headers_count",
    "envoy_dynamic_module_callback_http_set_response_header",
    "envoy_dynamic_module_callback_http_get_response_trailer",
    "envoy_dynamic_module_callback_http_get_response_trailers",
    "envoy_dynamic_module_callback_http_get_response_trailers_count",
    "envoy_dynamic_module_callback_http_set_response_trailer",
    "envoy_dynamic_module_callback_http_send_response",
    "envoy_dynamic_module_callback_http_set_dynamic_metadata_number",
    "envoy_dynamic_module_callback_http_get_dynamic_metadata_number",
    "envoy_dynamic_module_callback_http_set_dynamic_metadata_string",
    "envoy_dynamic_module_callback_http_get_dynamic_metadata_string",
    "envoy_dynamic_module_callback_http_get_request_body_vector",
    "envoy_dynamic_module_callback_http_get_request_body_vector_size",
    "envoy_dynamic_module_callback_http_append_request_body",
    "envoy_dynamic_module_callback_http_drain_request_body",
    "envoy_dynamic_module_callback_http_get_response_body_vector",
    "envoy_dynamic_module_callback_http_get_response_body_vector_size",
    "envoy_dynamic_module_callback_http_append_response_body",
    "envoy_dynamic_module_callback_http_drain_response_body",
    "envoy_dynamic_module_callback_http_clear_route_cache",
]

envoy_cc_library(
    name = "dynamic_modules_lib",
    srcs = ["dynamic_modules.cc"],
    hdrs = [
        "abi.h",
        "dynamic_modules.h",
    ],
    linkopts = select({
        "//bazel:apple": ["-Wl,-exported_symbol -Wl,%s" % symbol for symbol in SYMBOLS],
        "//conditions:default": ["-Wl,--export-dynamic-symbol=%s" % symbol for symbol in SYMBOLS],
    }),
    deps = [
        ":abi_version_lib",
        "//envoy/common:exception_lib",
    ],
)

envoy_cc_library(
    name = "abi_version_lib",
    hdrs = [
        "abi_version.h",
    ],
)
