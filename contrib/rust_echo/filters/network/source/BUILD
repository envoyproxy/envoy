load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_contrib_package",
    "envoy_rust_cxx_bridge",
    "envoy_rust_library",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

envoy_cc_library(
    name = "echo",
    hdrs = ["echo.h"],
    deps = [
        ":bridge",
        ":echo_rs",
        "//envoy/buffer:buffer_interface",
        "//envoy/event:dispatcher_interface",
        "//envoy/network:connection_interface",
        "//envoy/network:filter_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:minimal_logger_lib",
        "@cxx.rs//:core",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    deps = [
        ":echo",
        "//envoy/registry",
        "//envoy/server:filter_config_interface",
        "//source/extensions/filters/network:well_known_names",
        "//source/extensions/filters/network/common:factory_base_lib",
        "@envoy_api//contrib/envoy/extensions/filters/network/rust_echo/v3alpha:pkg_cc_proto",
    ],
)

envoy_rust_cxx_bridge(
    name = "executor_bridge",
    src = "executor.rs",
    deps = [
        ":rust_support_lib",
        "//envoy/network:filter_interface",
    ],
)

envoy_rust_library(
    name = "executor_rs",
    srcs = ["executor.rs"],
    deps = [
        ":bridge",
        "@cxx.rs//:cxx",
    ],
)

envoy_rust_library(
    name = "echo_rs",
    srcs = ["echo.rs"],
    deps = [
        ":bridge",
        ":executor_rs",
        "@cxx.rs//:cxx",
    ],
)

envoy_rust_cxx_bridge(
    name = "bridge",
    src = "echo.rs",
    deps = [
        ":executor_bridge",
    ],
)

envoy_cc_library(
    name = "rust_support_lib",
    srcs = ["rust_support.cc"],
    hdrs = ["rust_support.h"],
    deps = [
        "//envoy/buffer:buffer_interface",
        "//envoy/network:connection_interface",
        "//envoy/network:filter_interface",
        "@cxx.rs//:core",
    ],
)
