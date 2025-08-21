load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

# WASM service.

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = [
        "config.h",
    ],
    deps = [
        "//envoy/registry",
        "//envoy/server:bootstrap_extension_config_interface",
        "//envoy/server:factory_context_interface",
        "//envoy/server:instance_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:empty_string",
        "//source/common/config:datasource_lib",
        "//source/common/protobuf:utility_lib",
        "//source/extensions/common/wasm:remote_async_datasource_lib",
        "//source/extensions/common/wasm:wasm_lib",
        "@envoy_api//envoy/extensions/wasm/v3:pkg_cc_proto",
    ],
)
