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
        "//include/envoy/access_log:access_log_interface",
        "//source/common/http:header_map_lib",
        "//source/extensions/access_loggers:well_known_names",
        "//source/extensions/common/wasm:wasm_lib",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    security_posture = "unknown",
    status = "alpha",
    deps = [
        ":wasm_access_log_lib",
        "//include/envoy/registry",
        "//include/envoy/server:access_log_config_interface",
        "//source/common/config:datasource_lib",
        "//source/common/protobuf",
        "//source/extensions/access_loggers:well_known_names",
        "//source/extensions/common/wasm:wasm_lib",
        "@envoy_api//envoy/extensions/access_loggers/wasm/v3:pkg_cc_proto",
    ],
)
