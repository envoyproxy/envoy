load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

# Access log implementation that calls into a WASM VM.

envoy_cc_library(
    name = "wasm_access_log_lib",
    hdrs = ["wasm_access_log_impl.h"],
    deps = [
        "//envoy/access_log:access_log_interface",
        "//source/common/http:header_map_lib",
        "//source/extensions/common/wasm:wasm_lib",
    ],
    alwayslink = 1,
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":wasm_access_log_lib",
        "//envoy/access_log:access_log_config_interface",
        "//envoy/registry",
        "//source/common/config:datasource_lib",
        "//source/common/protobuf",
        "//source/extensions/common/wasm:remote_async_datasource_lib",
        "//source/extensions/common/wasm:wasm_lib",
        "@envoy_api//envoy/extensions/access_loggers/wasm/v3:pkg_cc_proto",
    ],
)
