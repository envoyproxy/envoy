load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        "//envoy/formatter:substitution_formatter_interface",
        "//envoy/registry",
        "//envoy/server:factory_context_interface",
        "//source/common/config:datasource_lib",
        "//source/common/protobuf:utility_lib",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/strings",
        "@envoy_api//envoy/extensions/formatter/datasource/v3:pkg_cc_proto",
    ],
)
