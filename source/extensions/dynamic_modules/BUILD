load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "dynamic_modules_lib",
    srcs = ["dynamic_modules.cc"],
    hdrs = [
        "abi.h",
        "dynamic_modules.h",
    ],
    linkopts = [
        "-Wl,--export-dynamic-symbol=envoy_dynamic_module_callback_http_get_request_header_value",
        "-Wl,--export-dynamic-symbol=envoy_dynamic_module_callback_http_get_request_trailer_value",
        "-Wl,--export-dynamic-symbol=envoy_dynamic_module_callback_http_get_response_header_value",
        "-Wl,--export-dynamic-symbol=envoy_dynamic_module_callback_http_get_response_trailer_value",
    ],
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
